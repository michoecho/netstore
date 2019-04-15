#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT "6543"

#define REQUEST_LIST 1
#define REQUEST_FILE 2

#define RESPONSE_LIST 1
#define RESPONSE_REJECT 2
#define RESPONSE_ACCEPT 3
#define ERROR_BAD_FILENAME 1
#define ERROR_BAD_OFFSET 2
#define ERROR_ZERO_LEN 3

#define FILELIST_MAX (1<<24)

ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, void const *buf, size_t n);
void read_exact(int fd, void *buf, size_t n);
void write_exact(int fd, void const *buf, size_t n);
void htons_write(int fd, unsigned short val);
unsigned short ntohs_read(int fd);
void htonl_write(int fd, unsigned val);
unsigned ntohl_read(int fd);
int ntohs_read_optional(int fd, unsigned short *val);

#define PERROR(message) do perror(message); while (0)

#define SYSERR(message)\
do {\
	PERROR(message);\
	exit(EXIT_FAILURE);\
} while (0)

#define FATAL(message)\
do {\
	fprintf(stderr, "%s\n", message);\
	exit(EXIT_FAILURE);\
} while (0)


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#endif
