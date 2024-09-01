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


#define LOG_LENGTH 50
struct MessageLog {
	char* history[LOG_LENGTH];
	size_t write_index;
	size_t msg_count;
};
char* getNthNewestMessage(struct MessageLog* log, size_t n) {
	if (n > log->write_index) {
		return log->history[log->write_index+LOG_LENGTH-n];
	} else {
		return log->history[log->write_index-n];
	}
}
void appendMessage(struct MessageLog* log, char* message) {
	if (log->msg_count == LOG_LENGTH)
		free(log->history[log->write_index]);

	log->history[log->write_index] = malloc(strlen(message));
	strcpy(log->history[log->write_index], message);
	log->write_index = (log->write_index+1) % LOG_LENGTH;

	if (log->msg_count < LOG_LENGTH)
		log->msg_count++;
}
void emptyLog(struct MessageLog* log) {
	size_t erase_index = log->write_index;
	for (size_t i = 0; i < log->msg_count; i++) {
		erase_index--;
		if (erase_index >= LOG_LENGTH) erase_index = LOG_LENGTH-1;
		free(log->history[erase_index]);
	}
	log->write_index = 0;
	log->msg_count = 0;
}

struct ClientState {
	struct Connection connection;
	char input_bfr[256];
	bool input_ready;
	bool shutdown;
	size_t width, height;
	struct MessageLog log;
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

static void displayMessages(struct ClientState* state) {
	cursorSavePosition();

	size_t cur_row = state->height-2;
	size_t end_row = 1;
	cursorMoveTo(cur_row, 1);
	for (size_t i = 0; i < state->log.msg_count; i++) {
		if (cur_row < end_row) break;

		// TODO: This probably breaks if the message is longer than one line
		printf("%s", getNthNewestMessage(&state->log, i+1));
		displayEraseLineFromCursor();
		cursorMoveUpToLeft(1);

		cur_row--;
	}

	cursorRestorePosition();
	fflush(stdout);
}

static void handleSegment(struct ClientState* state) {
	switch (state->connection.segment_type) {
		case SEGMENT_MESSAGE: {
			char bfr[SEGMENT_MAX_LENGTH + 40];
			struct Segment_Message* segment = state->connection.segment;
			sprintf(bfr, "<%s> %s", segment->sender, segment->contents);
			appendMessage(&state->log, bfr);
			break;
		}
		case SEGMENT_STATUS: {
			char bfr[SEGMENT_MAX_LENGTH + 40];
			struct Segment_Status* segment = state->connection.segment;
			sprintf(bfr, "<SERVER> %s", segment->status);
			appendMessage(&state->log, bfr);
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
	fflush(stdout);

	state.width = 80;
	state.height = 15;

	cursorMoveTo(state.height, 1);
	fflush(stdout);

	while (true) {
		updateConnection(&state.connection);

		if (state.connection.segment_ready) {
			handleSegment(&state);
			displayMessages(&state);
		}
		
		if (state.input_ready) {
			if (strcmp(state.input_bfr, "exit") == 0) {
				state.shutdown = true;
				state.input_ready = false;
				break;
			}

			sendSegment_Message(&state.connection, "client", state.input_bfr);
			cursorMoveTo(state.height, 1);
			displayEraseLine();
			state.input_ready = false;
			fflush(stdout);
		}
	}
	cleanupConnection(&state.connection);
	emptyLog(&state.log);
	displayLeaveAltBuffer();
	fflush(stdout);

	thrd_join(input_thread, NULL);
	
	close(sfd_server);
	return 0;
}
