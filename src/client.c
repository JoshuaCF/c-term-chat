#include <stdbool.h>
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

#include "term_ctrl.h"

#include "defs.h"

#include "client.h"

static void sendMessage(char* msg, int socket) {
	char bfr[SEGMENT_MAX_SIZE];
	void* write_pos = bfr;
	unsigned long msg_len = strlen(msg);

	*(uint32_t*)write_pos = htonl(msg_len);
	write_pos += sizeof(uint32_t);
	memcpy(write_pos, msg, msg_len);
	write_pos += msg_len;
	send(socket, bfr, write_pos-(void*)bfr, 0b0);
}

enum ConnectionReadState {
	CONNECTION_WAITING,
	CONNECTION_READING,
	CONNECTION_MSG_COMPLETE,
	CONNECTION_CLOSED,
};
struct Connection {
	int socket;
	char bfr[SEGMENT_MAX_SIZE];
	uint32_t msg_length;
	uint32_t bytes_read;
	enum ConnectionReadState state;
};

static void updateConnection(struct Connection* connection) {
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
			if (connection->msg_length > SEGMENT_MAX_SIZE-1) connection->msg_length = SEGMENT_MAX_SIZE-1;
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

struct ClientState {
	struct Connection connection;
	char input_bfr[256];
	bool input_ready;
};

static void inputLoop(struct ClientState* state) {
	while (true) {
		while (state->input_ready) { usleep(10000); }

		fgets(state->input_bfr, 256, stdin);
		unsigned long msg_len = strlen(state->input_bfr);
		if (state->input_bfr[msg_len-1] == '\n') state->input_bfr[msg_len-1] = '\0';
		state->input_ready = true;
	}
}

static void displayMessage(struct ClientState* state) {
	cursorSavePosition();
	fflush(stdout);

	cursorMoveToOrigin();
	printf("%s", state->connection.bfr);

	cursorRestorePosition();
	fflush(stdout);
}

int client(uint32_t ip, uint16_t port) {
	struct sockaddr_in server_address = {0};
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr = (struct in_addr) { htonl(ip) };

	int sfd_server = socket(AF_INET, SOCK_STREAM, 0);

	if (connect(sfd_server, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in)) != 0) {
		printf("Failed to connect.\n");
		return 1;
	}

	fcntl(sfd_server, F_SETFL, fcntl(sfd_server, F_GETFL) | O_NONBLOCK);

	struct Connection server_connection = {0};
	server_connection.state = CONNECTION_WAITING;
	server_connection.socket = sfd_server;

	struct ClientState state = {0};
	state.connection = server_connection;

	thrd_t input_thread;
	if (thrd_create(&input_thread, (thrd_start_t)inputLoop, &state) != thrd_success) {
		printf("Failed to create thread.\n");
		return 1;
	}

	printf("Entering alt buffer\n");
	displayEnterAltBuffer();
	cursorMoveTo(5, 1);
	fflush(stdout);
	while (true) {
		updateConnection(&state.connection);

		if (state.connection.state == CONNECTION_MSG_COMPLETE) {
			displayMessage(&state);
		}
		
		if (state.input_ready) {
			if (strcmp(state.input_bfr, "exit") == 0) break;

			sendMessage(state.input_bfr, state.connection.socket);
			cursorMoveTo(5, 1);
			displayEraseLine();
			state.input_ready = false;
			fflush(stdout);
		}
	}
	displayLeaveAltBuffer();
	fflush(stdout);

	thrd_join(input_thread, NULL);
	
	close(sfd_server);
	return 0;
}
