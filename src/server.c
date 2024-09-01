#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dyn_arr.h"

#include "networking.h"

#include "server.h"

struct ServerState {
	mtx_t mutex;
	int sfd_receiver;
	bool shutdown;
	struct DynamicArray connections;
};

static void acceptLoop(struct ServerState* state) {
	unsigned int next_id = 0;
	while(true) {
		int socket = accept(state->sfd_receiver, NULL, NULL);
		if (state->shutdown) break;
		if (socket == -1) {
			printf("Error accepting connection: %s\n", strerror(errno));
			continue;
		}

		fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK);

		struct Connection new_connection = newConnection(socket);
		printf("Connection %u accepted\n", new_connection.socket);

		mtx_lock(&state->mutex);
		DynamicArray_push(&state->connections, &new_connection);
		mtx_unlock(&state->mutex);
	}
}

static void broadcastMessage(struct ServerState* state, char* sender, char* message) {
	struct Connection* connections = state->connections.data;
	for (size_t i = 0; i < state->connections.num_elements; i++) {
		struct Connection* cur_connection = &connections[i];
		sendSegment_Message(cur_connection, sender, message);
	}
}

static void broadcastStatus(struct ServerState* state, char* status) {
	struct Connection* connections = state->connections.data;
	for (size_t i = 0; i < state->connections.num_elements; i++) {
		struct Connection* cur_connection = &connections[i];
		sendSegment_Status(cur_connection, status);
	}
}

static void handleSegment(struct ServerState* state, struct Connection* connection) {
	switch (connection->segment_type) {
		case SEGMENT_STATUS: {
			printf("Status message received by the server..?\n");
			break;
		}
		case SEGMENT_MESSAGE: {
			struct Segment_Message* segment = connection->segment;
			printf("Connection %u message: <%s> %s\n", connection->socket, segment->sender, segment->contents);
			broadcastMessage(state, segment->sender, segment->contents);
			if (strcmp(segment->contents, "close") == 0) {
				state->shutdown = true;
				printf("Shutting down server\n");
			}
			break;
		}
		default:
			printf("Default segment type?\n");
			break;
	}

	markHandled(connection);
}

static void pollLoop(struct ServerState* state) {
	while(true) {
		if (state->shutdown) break;
 
		mtx_lock(&state->mutex);

		struct Connection* connections = state->connections.data;
		for (size_t i = 0; i < state->connections.num_elements; i++) {
			struct Connection* cur_connection = &connections[i];
			updateConnection(cur_connection);
			if (cur_connection->reader.closed) {
				printf("Connection %u closed, removing\n", cur_connection->socket);
				cleanupConnection(cur_connection);
				DynamicArray_remove(&state->connections, i);
				i--;
			}

			if (cur_connection->segment_ready)
				handleSegment(state, cur_connection);
		}

		mtx_unlock(&state->mutex);
		thrd_yield();
	}
	mtx_lock(&state->mutex);
	broadcastStatus(state, "Server has shut down.");
	mtx_unlock(&state->mutex);
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
	state.connections = DynamicArray_new(sizeof(struct Connection), 1);
	
	thrd_t accept_thread;
	if (thrd_create(&accept_thread, (thrd_start_t)acceptLoop, &state) != thrd_success) {
		printf("Failed to create acception thread.\n");
		return 1;
	}
	pollLoop(&state);
	printf("Exited poll loop\n");
	shutdown(state.sfd_receiver, SHUT_RD);

	mtx_destroy(&state.mutex);

	struct Connection* connections = state.connections.data;
	for (size_t i = 0; i < state.connections.num_elements; i++) {
		struct Connection* cur_connection = &connections[i];
		cleanupConnection(cur_connection);
	}
	DynamicArray_free(&state.connections);

	printf("Joining...\n");
	thrd_join(accept_thread, NULL);
	printf("Closing.\n");
	close(state.sfd_receiver);

	return 0;
}
