#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define QUEUE_LENGTH 128

int ignore_sigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	int err;
	err = sigaction(SIGPIPE, &action, NULL);
	if (err < 0) {
		PERROR("sigaction");
	}
	return err;
}

int socket_and_bind(struct addrinfo const *list) {
	int sock;
	int err;
	struct addrinfo const *p;

	for (p = list; p != NULL; p = p->ai_next) {
		sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);	
		if (sock < 0) {
			continue;
		}

		err = bind(sock, p->ai_addr, p->ai_addrlen);
		if (err >= 0) {
			return sock;
		}
	}

	return -1;
}

int tcp_bind(char const *port) {
	struct addrinfo hints;
	struct addrinfo *list;
	int gaierr;
	int sock;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	gaierr = getaddrinfo(NULL, port, &hints, &list);
	if (gaierr != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gaierr));
		return -1;
	}

	sock = socket_and_bind(list);
	freeaddrinfo(list);

	return sock;
}

void send_listing(int sock) {
	int file_count = 0;
	unsigned filelist_len = 0;
	char sep = '|';
	int err;
	char *buffer;
	DIR *dir;
	struct dirent *entry;

	buffer = malloc(FILELIST_MAX);
	if (buffer == NULL) {
		SYSERR("malloc");
	}

	dir = opendir(".");
	if (dir == NULL) {
		SYSERR("opendir");
	}

	while ((entry = readdir(dir)) != NULL) {
		struct stat st;
		size_t name_len;

		err = stat(entry->d_name, &st);
		if (err < 0) {
			FATAL("stat");
		}

		if (!S_ISREG(st.st_mode))
			continue;

		name_len = strlen(entry->d_name);
		if (filelist_len + name_len + sizeof(sep) > FILELIST_MAX) {
			FATAL("File list size limit exceeded");
		}

		if (file_count++ > 0) {
			buffer[filelist_len++] = sep;
		}

		strcpy(buffer + filelist_len, entry->d_name);
		filelist_len += name_len;
	}

	htons_write(sock, RESPONSE_LIST);
	htonl_write(sock, filelist_len);
	write_exact(sock, buffer, filelist_len);

	free(buffer);
}

int open_regular(char const *filename) {
	int err;
	int fd;
	struct stat st;

	if (strchr(filename, '/') != NULL) {
		return -1;
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		if (errno == EACCES || errno == ENAMETOOLONG || errno == ENOENT) {
			return -1;
		} else {
			SYSERR("open");
		}
	}

	err = stat(filename, &st);
	if (err < 0) {
		close(fd);
		return -1;
	}

	if (!S_ISREG(st.st_mode)) {
		close(fd);
		return -1;
	}

	return fd;
}

void serve_file(int sock) {
	char filename[65536];
	char buffer[512*1024];
	unsigned offset;
	unsigned length;
	unsigned short fnamelen;
	int fd;
	off_t file_end;
	off_t file_pos;
	unsigned left;

	offset = ntohl_read(sock);
	length = ntohl_read(sock);
	fnamelen = ntohs_read(sock);

	read_exact(sock, filename, fnamelen);
	filename[fnamelen] = '\0';

	fd = open_regular(filename);
	if (fd < 0) {
		htons_write(sock, RESPONSE_REJECT);
		htonl_write(sock, ERROR_BAD_FILENAME);
		fprintf(stderr, "Bad filename requested\n");
		return;
	}

	file_end = lseek(fd, 0, SEEK_END);
	if (file_end < 0) {
		SYSERR("lseek");
	}

	file_pos = lseek(fd, offset, SEEK_SET);
	if (file_pos >= file_end) {
		htons_write(sock, RESPONSE_REJECT);
		htonl_write(sock, ERROR_BAD_OFFSET);
		fprintf(stderr, "Bad offset\n");
		return;
	}
	
	if (length == 0) {
		htons_write(sock, RESPONSE_REJECT);
		htonl_write(sock, ERROR_ZERO_LEN);
		fprintf(stderr, "Zero length fragment requested\n");
		return;
	}
	
	htons_write(sock, RESPONSE_ACCEPT);
	left = MIN(length, file_end - file_pos);
	htonl_write(sock, left);

	while (left > 0) {
		unsigned chunk;

	        chunk = MIN(left, sizeof(buffer));
		read_exact(fd, buffer, chunk);
		write_exact(sock, buffer, chunk);
		left -= chunk;
	}
}

void serve_client(int sock) {
	for (;;) {
		unsigned short request_type;
		if (ntohs_read_optional(sock, &request_type) == 0) {
			return;
		} else if (request_type == REQUEST_LIST) {
			send_listing(sock);
		} else if (request_type == REQUEST_FILE) {
			serve_file(sock);
		} else {
			FATAL("Unexpected request type");
		}
	}
}

int netstore_server(char const *directory, char const *port) {
	int err;
	int main_sock;

	ignore_sigpipe();

       	err = chdir(directory);
	if (err < 0) {
		SYSERR("chdir");
	}

	main_sock = tcp_bind(port);
	if (main_sock < 0) {
		SYSERR("bind");
	}

	err = listen(main_sock, QUEUE_LENGTH);
	if (err < 0) {
		SYSERR("listen");
	}

	for (;;) {
		int child_sock;

	       	child_sock = accept(main_sock, NULL, NULL);
		if (child_sock < 0) {
			PERROR("accept");
		} else {
			switch (fork()) {
			case -1:
				PERROR("fork");
				close(child_sock);
				continue;
			case 0:
				close(main_sock);
				serve_client(child_sock);
				close(child_sock);
				return 0;
			default:
				close(child_sock);
				// The spec requires the server to act sequentially.
				wait(NULL);
			};
		}
	}

	return 0;
}

int main (int argc, char *argv[]) {
	char const usage[] = "Usage: netstore-server DIRECTORY [PORT]\n";
	char *port = DEFAULT_PORT;
	char *directory;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, usage);
		exit(EXIT_FAILURE);
	}
	
	if (strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, usage);
		exit(EXIT_SUCCESS);
	}

	directory = argv[1];

	if (argc == 3) {
		port = argv[2];
	}

	return netstore_server(directory, port);
}
