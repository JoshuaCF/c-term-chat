#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include "networking.h"

#define isConnectionClosed(bytes_read) (bytes_read == 0 || (bytes_read == -1 && errno != EWOULDBLOCK))

static bool fragmentedRead(struct SocketReader* read_args) {
	void* destination = read_args->dest + read_args->bytes_read;
	ssize_t bytes_remaining = read_args->target_bytes - read_args->bytes_read;

	if (bytes_remaining == 0) return false;

	ssize_t bytes_received = recv(read_args->socket, destination, bytes_remaining, 0b0);
	read_args->closed = isConnectionClosed(bytes_received);
	if (read_args->closed) return false;

	if (bytes_received > 0) read_args->bytes_read += bytes_received;

	return read_args->bytes_read == read_args->target_bytes;
}

struct Connection newConnection(int socket) {
	struct Connection new = {0};

	new.socket = socket;
	new.bfr = malloc(SEGMENT_MAX_LENGTH);
	new.reader = (struct SocketReader) {0};
	new.reader.socket = socket;
	new.reader.target_bytes = 3;
	new.reader.dest = new.bfr;

	return new;
}

void markHandled(struct Connection* connection) {
	switch (connection->segment_type) {
		case SEGMENT_STATUS: {
			struct Segment_Status* segment = connection->segment;
			free(segment->status);
			break;
		}
		case SEGMENT_MESSAGE: {
			struct Segment_Message* segment = connection->segment;
			free(segment->sender);
			free(segment->contents);
			break;
		}
	}

	free(connection->segment);
	connection->segment = NULL;
	connection->segment_ready = false;
	connection->segment_type = SEGMENT_NONE;
	connection->reader.bytes_read = 0;
	connection->reader.target_bytes = 3;
}

// TODO: There lacks data sanitation and sanity checks
void updateConnection(struct Connection* connection) {
	if (connection->segment_ready) return;

	while (fragmentedRead(&connection->reader)) {
		switch(connection->segment_type) {
			case SEGMENT_NONE:
				connection->segment_type = connection->bfr[0];
				connection->reader.bytes_read = 0;
				connection->reader.target_bytes = ntohs(*(uint16_t*)&(connection->bfr[1]));
				break;
			case SEGMENT_STATUS: {
				connection->segment = calloc(1, sizeof(struct Segment_Status));
				struct Segment_Status* segment = connection->segment;
				void* read_loc = connection->bfr;

				segment->status_len = ntohs(*(uint16_t*)read_loc);
				read_loc += sizeof(uint16_t);

				segment->status = malloc(segment->status_len+1);
				char* status = segment->status;
				status[segment->status_len] = '\0';
				memcpy(status, read_loc, segment->status_len);

				connection->segment_ready = true;
				break;
			}
			case SEGMENT_MESSAGE: {
				connection->segment = calloc(1, sizeof(struct Segment_Message));
				struct Segment_Message* segment = connection->segment;
				void* read_loc = connection->bfr;

				segment->sender_len = ntohs(*(uint16_t*)read_loc);
				read_loc += sizeof(uint16_t);

				segment->sender = malloc(segment->sender_len+1);
				char* sender = segment->sender;
				sender[segment->sender_len] = '\0';
				memcpy(sender, read_loc, segment->sender_len);
				read_loc += segment->sender_len;

				segment->contents_len = ntohs(*(uint16_t*)read_loc);
				read_loc += sizeof(uint16_t);

				segment->contents = malloc(segment->contents_len+1);
				char* contents = segment->contents;
				contents[segment->contents_len] = '\0';
				memcpy(contents, read_loc, segment->contents_len);

				connection->segment_ready = true;
				break;
			}
			default:
				break;
		}
	}
}

void cleanupConnection(struct Connection* connection) {
	if (connection->segment_ready) markHandled(connection);
	free(connection->bfr);
	close(connection->socket);
}

static bool sendBulk(struct Connection* connection, void* data, size_t bytes) {
	size_t total_sent = 0;
	while (total_sent < bytes) {
		ssize_t bytes_sent = send(connection->socket, data+total_sent, bytes-total_sent, 0b0);
		if (bytes_sent == -1) return false;
		total_sent += bytes_sent;
	}

	return true;
}

void sendSegment_Status(struct Connection* connection, char* status) {
	char bfr[SEGMENT_MAX_LENGTH + sizeof(unsigned char) + sizeof(uint16_t)];
	void* write_pos = bfr;
	*(unsigned char*)write_pos = (unsigned char)SEGMENT_STATUS;
	write_pos += 1;

	uint16_t status_len = strlen(status);
	uint16_t segment_size =
		sizeof(uint16_t) // Component size indicators
		+ status_len // Status data
	;

	*(uint16_t*)write_pos = htons(segment_size);
	write_pos += sizeof(uint16_t);

	*(uint16_t*)write_pos = htons(status_len);
	write_pos += sizeof(uint16_t);

	memcpy(write_pos, status, status_len);
	write_pos += status_len;

	size_t total_bytes = write_pos - (void*)bfr;

	sendBulk(connection, bfr, total_bytes);
}

void sendSegment_Message(struct Connection* connection, char* sender, char* contents) {
	char bfr[SEGMENT_MAX_LENGTH + sizeof(unsigned char) + sizeof(uint16_t)];
	void* write_pos = bfr;
	*(unsigned char*)write_pos = (unsigned char)SEGMENT_MESSAGE;
	write_pos += 1;

	uint16_t sender_len = strlen(sender);
	uint16_t contents_len = strlen(contents);
	uint16_t segment_size =
		sizeof(uint16_t) * 2 // Component size indicators
		+ sender_len // Sender name data
		+ contents_len // Contents data
	;

	*(uint16_t*)write_pos = htons(segment_size);
	write_pos += sizeof(uint16_t);

	*(uint16_t*)write_pos = htons(sender_len);
	write_pos += sizeof(uint16_t);

	memcpy(write_pos, sender, sender_len);
	write_pos += sender_len;

	*(uint16_t*)write_pos = htons(contents_len);
	write_pos += sizeof(uint16_t);

	memcpy(write_pos, contents, contents_len);
	write_pos += contents_len;

	size_t total_bytes = write_pos - (void*)bfr;

	sendBulk(connection, bfr, total_bytes);
}
