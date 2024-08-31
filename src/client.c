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

#include "networking.h"

#include "client.h"


struct ClientState {
	struct Connection connection;
	char input_bfr[256];
	bool input_ready;
	bool shutdown;
};

static void inputLoop(struct ClientState* state) {
	while (true) {
		while (state->input_ready) { usleep(10000); }
		if (state->shutdown) break;

		fgets(state->input_bfr, 256, stdin);
		unsigned long msg_len = strlen(state->input_bfr);
		if (state->input_bfr[msg_len-1] == '\n') state->input_bfr[msg_len-1] = '\0';
		state->input_ready = true;
	}
}

static void displayMessage(struct ClientState* state, char* msg) {
	cursorSavePosition();

	cursorMoveToOrigin();
	printf("%s", msg);
	displayEraseFromCursor();

	cursorRestorePosition();
	fflush(stdout);
}

static void handleSegment(struct ClientState* state) {
	switch (state->connection.segment_type) {
		case SEGMENT_MESSAGE: {
			char bfr[SEGMENT_MAX_LENGTH + 40];
			struct Segment_Message* segment = state->connection.segment;
			sprintf(bfr, "<%s> %s", segment->sender, segment->contents);
			displayMessage(state, bfr);
			break;
		}
		case SEGMENT_STATUS: {
			char bfr[SEGMENT_MAX_LENGTH + 40];
			struct Segment_Status* segment = state->connection.segment;
			sprintf(bfr, "<SERVER> %s", segment->status);
			displayMessage(state, bfr);
			break;
		}
		default:
			printf("Default segment type?\n");
			break;
	}
	markHandled(&state->connection);
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

	struct Connection server_connection = newConnection(sfd_server);

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

		if (state.connection.segment_ready)
			handleSegment(&state);
		
		if (state.input_ready) {
			if (strcmp(state.input_bfr, "exit") == 0) {
				state.shutdown = true;
				state.input_ready = false;
				break;
			}

			sendSegment_Message(&state.connection, "client", state.input_bfr);
			cursorMoveTo(5, 1);
			displayEraseLine();
			state.input_ready = false;
			fflush(stdout);
		}
	}
	cleanupConnection(&state.connection);
	displayLeaveAltBuffer();
	fflush(stdout);

	thrd_join(input_thread, NULL);
	
	close(sfd_server);
	return 0;
}
