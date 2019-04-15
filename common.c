#include "common.h"

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

ssize_t readn(int fd, void *buf, size_t n) {
	size_t nleft = n;

	while (nleft > 0) {
		ssize_t nread = read(fd, buf, nleft);
		if (nread > 0) {
			nleft -= nread;
			buf += nread;
		} else if (nread == 0) {
			break;
		} else {
			if (errno != EINTR)
				return -1;
		}
	}

	return n - nleft;
}

ssize_t writen(int fd, void const *buf, size_t n) {
	size_t nleft = n;

	while (nleft > 0) {
		ssize_t nwritten = write(fd, buf, nleft);
		if (nwritten > 0) {
			nleft -= nwritten;
			buf += nwritten;
		} else if (nwritten == 0) {
			break;
		} else {
			if (errno != EINTR)
				return -1;
		}
	}

	return n - nleft;
}

void read_exact(int fd, void *buf, size_t n) {
	ssize_t nread = readn(fd, buf, n);
	if (nread < 0) {
		SYSERR("read");
	} else if (nread < (ssize_t)n) {
		FATAL("Incomplete data transfer");
	}
}

void write_exact(int fd, void const *buf, size_t n) {
	ssize_t nwritten = writen(fd, buf, n);
	if (nwritten < 0) {
		SYSERR("read");
	} else if (nwritten < (ssize_t)n) {
		FATAL("Incomplete data transfer");
	}
}

void htons_write(int fd, unsigned short val) {
	val = htons(val);
	write_exact(fd, &val, sizeof(val));
}

unsigned short ntohs_read(int fd) {
	unsigned short val;
	read_exact(fd, &val, sizeof(val));
	return ntohs(val);
}

int ntohs_read_optional(int fd, unsigned short *val) {
	int nread;

	nread = readn(fd, val, sizeof(*val));
	if (nread < 0) {
		SYSERR("read");
	} else if (nread == 0) {
		return 0;
	} else if (nread != sizeof(*val)) {
		FATAL("Incomplete data transfer");
	}

	*val = ntohs(*val);

	return sizeof(*val);
}

void htonl_write(int fd, unsigned val) {
	val = htonl(val);
	write_exact(fd, &val, sizeof(val));
}

unsigned ntohl_read(int fd) {
	unsigned val;
	read_exact(fd, &val, sizeof(val));
	return ntohl(val);
}
