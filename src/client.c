#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "defs.h"

#include "client.h"

void sendMessage(char* msg, int socket) {
	char bfr[SEGMENT_MAX_SIZE];
	void* write_pos = bfr;
	unsigned long msg_len = strlen(msg);

	*(uint32_t*)write_pos = htonl(msg_len);
	write_pos += sizeof(uint32_t);
	memcpy(write_pos, msg, msg_len);
	write_pos += msg_len;
	send(socket, bfr, write_pos-(void*)bfr, 0b0);
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

	char msg[256];
	while(1) {
		fgets(msg, 256, stdin);

		unsigned long len = strlen(msg);
		if (msg[len-1] == '\n') msg[len-1] = '\0';

		if (strcmp(msg, "exit") == 0) break;
		sendMessage(msg, sfd_server);
	}
	
	close(sfd_server);
	return 0;
}
