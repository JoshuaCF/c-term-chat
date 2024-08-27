#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dyn_arr.h"

#include "server.h"

enum ConnectionReadState {
	CONNECTION_WAITING,
	CONNECTION_READING,
	CONNECTION_MSG_COMPLETE,
	CONNECTION_CLOSED,
};
#define CONNECTION_BFR_SIZE 512
struct Connection {
	unsigned int id;
	int socket;
	char bfr[CONNECTION_BFR_SIZE];
	uint32_t msg_length;
	uint32_t bytes_read;
	enum ConnectionReadState state;
};

void updateConnection(struct Connection* connection) {
	switch (connection->state) {
		case CONNECTION_WAITING: {
			void* destination = connection->bfr + connection->bytes_read;
			size_t bytes_remaining = sizeof(uint32_t) - connection->bytes_read;

			ssize_t bytes = recv(connection->socket, destination, bytes_remaining, 0b0);
			if (bytes > 0) {
				connection->bytes_read += bytes;
			} else if ((bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK) || bytes == 0) {
				connection->state = CONNECTION_CLOSED;
				return;
			}

			if (connection->bytes_read != sizeof(uint32_t)) return;
			connection->msg_length = ntohl(*(uint32_t*)connection->bfr);
			if (connection->msg_length > CONNECTION_BFR_SIZE-1) connection->msg_length = CONNECTION_BFR_SIZE-1;
			connection->bytes_read = 0;
			connection->state = CONNECTION_READING;
		}
		case CONNECTION_READING: {
			void* destination = connection->bfr + connection->bytes_read;
			size_t bytes_remaining = connection->msg_length - connection->bytes_read;

			ssize_t bytes = recv(connection->socket, destination, bytes_remaining, 0b0);
			if (bytes >= 0) {
				connection->bytes_read += bytes;
			} else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
				connection->state = CONNECTION_CLOSED;
				return;
			}

			if (connection->bytes_read != connection->msg_length) return;
			connection->bfr[connection->msg_length] = '\0';
			connection->bytes_read = 0;
			connection->state = CONNECTION_MSG_COMPLETE;
		}
		case CONNECTION_MSG_COMPLETE:
		break;
		case CONNECTION_CLOSED:
		break;
	}
}

void cleanupConnection(struct Connection* connection) {
	close(connection->socket);
}

struct ServerState {
	mtx_t mutex;
	int sfd_receiver;
	bool shutdown;
	struct DynamicArray connections;
	struct DynamicArray pollfds;
};

void rebuildPollFDs(struct ServerState* state) {
	DynamicArray_clear(&state->pollfds);

	struct Connection* connections = state->connections.data;
	for (size_t i = 0; i < state->connections.num_elements; i++) {
		struct Connection* cur_connection = &connections[i];
		struct pollfd pfd = {0};
		pfd.fd = cur_connection->socket;
		pfd.events = POLLIN | POLLPRI;
		DynamicArray_push(&state->pollfds, &pfd);
	}
}

void acceptLoop(struct ServerState* state) {
	unsigned int next_id = 0;
	while(true) {
		int socket = accept(state->sfd_receiver, NULL, NULL);
		if (state->shutdown) break;
		if (socket == -1) {
			printf("Error accepting connection: %s\n", strerror(errno));
			continue;
		}

		struct Connection new_connection = {0};
		new_connection.state = CONNECTION_WAITING;
		new_connection.socket = socket;
		new_connection.id = next_id;
		next_id++;
		printf("Connection %u accepted\n", new_connection.id);

		mtx_lock(&state->mutex);
		DynamicArray_push(&state->connections, &new_connection);
		rebuildPollFDs(state);
		mtx_unlock(&state->mutex);
		thrd_yield();
	}
}

#define POLL_TIMEOUT_MS 100
void pollLoop(struct ServerState* state) {
	// It would be good to make this work with select()
	struct DynamicArray removal_list = DynamicArray_new(sizeof(size_t), 1);
	while(true) {
		if (state->shutdown) break;

		mtx_lock(&state->mutex);
		int num_ready = poll(state->pollfds.data, state->pollfds.num_elements, POLL_TIMEOUT_MS);
		if (num_ready == 0) goto unlock;

		struct Connection* connections = state->connections.data;
		struct pollfd* pollfds = state->pollfds.data;
		for (size_t i = 0; i < state->connections.num_elements; i++) {
			struct pollfd* cur_pollfd = &pollfds[i];
			if (!(cur_pollfd->revents & (POLLIN | POLLPRI))) continue;

			struct Connection* cur_connection = &connections[i];

			updateConnection(cur_connection);
			if (cur_connection->state == CONNECTION_CLOSED) {
				printf("Connection %u closed, removing\n", cur_connection->id);
				cleanupConnection(cur_connection);
				DynamicArray_push(&removal_list, &i);
			}

			if (cur_connection->state == CONNECTION_MSG_COMPLETE) {
				printf("Connection %u message: %s\n", cur_connection->id, cur_connection->bfr);
				cur_connection->state = CONNECTION_WAITING;
				if (strcmp(cur_connection->bfr, "close") == 0) {
					state->shutdown = true;
					printf("Shutting down server\n");
				}
			}
		}

		if (removal_list.num_elements == 0) goto unlock;

		printf("To remove: %u\n", removal_list.num_elements);
		// Removing from back to front will not cause reordering of data before removal
		size_t* removal_indices = removal_list.data;
		for (unsigned int i = removal_list.num_elements; i > 0; i--) {
			size_t removal_index = removal_indices[i-1];
			DynamicArray_remove(&state->connections, removal_index);
		}
		rebuildPollFDs(state);
		DynamicArray_clear(&removal_list);

		unlock:
		mtx_unlock(&state->mutex);
		
		fflush(stdout);
		thrd_yield();
	}
	DynamicArray_free(&removal_list);
}

int server(uint16_t port) {
	printf("Hosting on port %hu\n", port);

	struct sockaddr_in bind_addr = {0};
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(port);
	bind_addr.sin_addr = (struct in_addr) { INADDR_ANY };

	int sfd_receiver = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd_receiver == -1) {
		printf("Unable to create socket.\n");
		return 1;
	}

	if (bind(sfd_receiver, (struct sockaddr*) &bind_addr, sizeof(struct sockaddr_in)) != 0) {
		printf("Unable to bind socket.\n");
		return 1;
	}

	if (listen(sfd_receiver, 0) != 0) {
		printf("Unable to mark socket as listening.\n");
		return 1;
	}

	struct ServerState state = {0};
	if (mtx_init(&state.mutex, mtx_plain) != thrd_success) {
		printf("Unable to create mutex.\n");
		return 1;
	}
	state.sfd_receiver = sfd_receiver;
	state.shutdown = false;
	// This is set to 1 so I can find issues with it as soon as possible
	// My dynamic array is relatively untested
	state.connections = DynamicArray_new(sizeof(struct Connection), 1);
	state.pollfds = DynamicArray_new(sizeof(struct pollfd), 1);
	
	thrd_t accept_thread;
	if (thrd_create(&accept_thread, (thrd_start_t)acceptLoop, &state) != thrd_success) {
		printf("Failed to create acception thread.\n");
		return 1;
	}
	pollLoop(&state);
	printf("Exited poll loop\n");
	shutdown(state.sfd_receiver, SHUT_RD);

	mtx_destroy(&state.mutex);
	DynamicArray_free(&state.connections);
	DynamicArray_free(&state.pollfds);

	printf("Joining...\n");
	thrd_join(accept_thread, NULL);
	printf("Closing.\n");
	close(state.sfd_receiver);

	return 0;
}
