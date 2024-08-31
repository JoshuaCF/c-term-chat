#pragma once


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/socket.h>

#define SEGMENT_MAX_LENGTH 1024

/* SEGMENT STRUCTURE
 * 1 byte: segment type, one of the SegmentType enumerations
 * 2 bytes: segment remaining data, # of bytes the rest of the segment has
 * n bytes: a number of bytes corresponding to the count specified by the
 *	previous two bytes. This data is interpeted on a per-segment basis
 */
enum SegmentType {
	SEGMENT_NONE,
	SEGMENT_MESSAGE,
	SEGMENT_STATUS,
};

/* SEGMENT_MESSAGE STRUCTURE
 * 2 bytes: length of the following sender text
 * n bytes: sender name
 * 2 bytes: length of the following message text
 * n bytes: message text
 */
struct Segment_Message {
	uint16_t sender_len;
	char* sender;
	uint16_t contents_len;
	char* contents;
};
/* SEGMENT_STATUS STRUCTURE
 * 2 bytes: length of the status text
 * n bytes: status text
 */
struct Segment_Status {
	uint16_t status_len;
	char* status;
};


struct SocketReader {
	int socket;
	void* dest;
	bool closed;
	ssize_t target_bytes;
	ssize_t bytes_read;
};


struct Connection {
	unsigned char segment_type;
	void* segment;
	bool segment_ready;
	int socket;
	char* bfr;
	struct SocketReader reader;
};
struct Connection newConnection(int socket);
void markHandled(struct Connection* connection);
void updateConnection(struct Connection* connection);
void cleanupConnection(struct Connection* connection);

void sendSegment_Message(struct Connection* connection, char* sender, char* contents);
void sendSegment_Status(struct Connection* connection, char* status);
