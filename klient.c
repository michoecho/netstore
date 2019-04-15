#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>

#define QUEUE_LENGTH 128

int socket_connect(struct addrinfo const *list) {
	int sock;
	int err;
	struct addrinfo const *p;

	for (p = list; p != NULL; p = p->ai_next) {
		sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sock < 0)
			continue;

		err = connect(sock, p->ai_addr, p->ai_addrlen);
		if (err >= 0) 
			return sock;
	}

	return -1;
}

int tcp_connect(char const *host, char const *port) {
	struct addrinfo hints;
	struct addrinfo *list;
	int gaierr;
	int sock;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	gaierr = getaddrinfo(host, port, &hints, &list);
	if (gaierr != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gaierr));
		exit(EXIT_FAILURE);
	}

	sock = socket_connect(list);
	if (sock < 0) {
		FATAL("Could not connect");
	}

	freeaddrinfo(list);

	return sock;
}

void request_listing(int sock) {
	htons_write(sock, REQUEST_LIST);
	unsigned short response = ntohs_read(sock);

	if (response != RESPONSE_LIST) {
		FATAL("Invalid server response");
	}
}

unsigned prompt_for_unsigned(char const *prompt) {
	unsigned ans;
	for (;;) {
		printf(prompt);
		int nread = scanf("%u", &ans);
		if (nread == 1) {
			// Check the remainder of the line.
			for (;;) {
				int c = getchar();
				if (c == '\n') {
					return ans;
				} if (!isspace(c)) {
					break;
				}
			}
		}

		puts("Please input a single unsigned integer.");

		// Skip to end of line.
		for (;;) {
			int c = getchar();
			if (c == EOF)
				FATAL("Invalid user input");
			if (c == '\n')
				break;
		}
	}
}

void choose_file(int sock, char *filename) {
	const char sep = '|';
	unsigned length = ntohl_read(sock);

	if (length == 0) {
		fprintf(stderr, "No files currently available\n");
		exit(EXIT_SUCCESS);
	}

	char *buffer = malloc(length);
	if (buffer == NULL) {
		SYSERR("Could not allocate memory");
	}

	read_exact(sock, buffer, length);

	unsigned nfiles = 0;
	unsigned i = 0;
	while (i < length) {
		printf("%d. ", ++nfiles);
		while (i < length && buffer[i] != sep) {
			putchar(buffer[i]);
			++i;
		}
		++i;
		putchar('\n');
	}

	unsigned choice = prompt_for_unsigned("\nChoose file number:\n> ");

	if (choice < 1 || choice > nfiles) {
		FATAL("Bad choice");
	}

	unsigned fileno = 1;

	char *fname = buffer;
	while (fileno != choice) {
		if (*fname == sep) {
			++fileno;
		}
		++fname;
	}

	int fnamelen = 0;
	while (fname + fnamelen < buffer + length && fname[fnamelen] != sep)
		++fnamelen;

	memcpy(filename, fname, fnamelen);
	filename[fnamelen] = '\0';

	free(buffer);
}

void check_response(int sock) {
	unsigned short response = ntohs_read(sock);

	if (response == RESPONSE_REJECT) {
		fprintf(stderr, "Request rejected. reason: ");
		unsigned reason = ntohl_read(sock);

		if (reason == ERROR_BAD_FILENAME) FATAL("bad filename");
		if (reason == ERROR_BAD_OFFSET) FATAL("bad offset");
		if (reason == ERROR_ZERO_LEN) FATAL("zero length");
		FATAL("unknown");
	}

	if (response != RESPONSE_ACCEPT) {
		FATAL("Invalid server response");
	}
}

void request_file(int sock, char const *filename, int beg, int end) {
	htons_write(sock, REQUEST_FILE);
	htonl_write(sock, beg);
	htonl_write(sock, end - beg);
	unsigned short len = strlen(filename);
	htons_write(sock, len);
	write_exact(sock, filename, len);
	check_response(sock);
}

int open_tmp() {
	struct stat st = {0};
	int err = stat("tmp", &st);

	if (err < 0) {
		if (errno == ENOENT) {
			int err = mkdir("tmp", 0755);
			if (err)
				SYSERR("Failed to create ./tmp/");
		} else {
			SYSERR("Failed to access ./tmp/");
		}
	}
	
	int dir = open("tmp", O_DIRECTORY | O_RDONLY);
	if (dir < 0) {
		SYSERR("Failed to access ./tmp/");
	}

	return dir;
}

void accept_file(int sock, char const *filename, int offset) {
	unsigned length = ntohl_read(sock);
	unsigned left = length;

	int dir = open_tmp();
	int file = openat(dir, filename, O_WRONLY | O_CREAT, 0644);
	if (file < 0) {
		SYSERR("Could not open the file for writing");
	}

	if (lseek(file, offset, SEEK_SET) < 0) {
		SYSERR("Could not seek to the beginning position for writing");
	}

	char buffer[512 * 1024];

	while (left > 0) {
		int batch = MIN(sizeof(buffer), left);
		ssize_t nread = readn(sock, buffer, batch);
		if (nread < batch) {
			fprintf(stderr, "Error transmitting the file after %d bytes written\n", length - left);
			SYSERR("read failed");
		}

		ssize_t nwritten = writen(file, buffer, nread);
		if (nwritten < batch) {
			fprintf(stderr, "Error transmitting the file after %d bytes written\n", length - left);
			SYSERR("write failed");
		}

		left -= batch;
	}
}

void netstore_client(int sock) {
	char filename[256];
	unsigned beg;
	unsigned end;
	request_listing(sock);

	choose_file(sock, filename);

	beg = prompt_for_unsigned("\nChoose the offset of the file fragment\n> ");
	end = prompt_for_unsigned("\nChoose the end of the file fragment\n> ");

	if (end < beg) {
		FATAL("Invalid address combination");
	}

	request_file(sock, filename, beg, end);
	accept_file(sock, filename, beg);
}

int main(int argc, char *argv[]) {
	char const usage[] = "Usage: netstore-client SERVER [PORT]\n";
	char *host;
	char *port;
	int sock;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, usage);
		exit(EXIT_FAILURE);
	}
	
	if (strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, usage);
		exit(EXIT_SUCCESS);
	}

	host = argv[1];
	port = DEFAULT_PORT;

	if (argc == 3) {
		port = argv[2];
	}

	sock = tcp_connect(host, port);

	netstore_client(sock);

	return 0;
}
