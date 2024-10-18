#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "server.h"
#include "client.h"

int parseIPv4(char* string, uint32_t* out) {
	char extra;

	unsigned char byte1, byte2, byte3, byte4;
	if (sscanf(string, "%hhu.%hhu.%hhu.%hhu%c", &byte1, &byte2, &byte3, &byte4, &extra) != 4)
		return 1;

	*out =
		(byte1 << 24) &
		(byte2 << 16) &
		(byte3 << 8) &
		byte4;
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc < 2) goto invalid;

	if (strcmp(argv[1], "connect") == 0) {
		if (argc != 4) goto invalid;

		char extra;

		uint32_t ip;
		if (parseIPv4(argv[2], &ip) != 0) goto invalid;
		uint16_t port;
		if (sscanf(argv[3], "%hu%c", &port, &extra) != 1) goto invalid;

		return client(ip, port);
	}

	if (strcmp(argv[1], "host") == 0) {
		if (argc != 3) goto invalid;

		char extra;

		uint16_t port;
		if (sscanf(argv[2], "%hu%c", &port, &extra) != 1) goto invalid;

		return server(port);
	}

invalid:
	printf("Invalid usage. Correct usages as follows:\n");
	printf("\t%s connect IP PORT\n", argv[0]);
	printf("\t%s host PORT\n", argv[0]);
	printf("Where:\n");
	printf("\tIP is an IPv4 address formatted as X.X.X.X, where each X is a value in the range 0-255\n");
	printf("\tPORT is a number in the range of 0-65535 to host on or connect to\n");
	return 1;
}
