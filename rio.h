#include<unistd.h>
#include<errno.h>

ssize_t rio_writen(int fd, void *buf, size_t count);

static const int RIO_BUFSIZE = 8192;
typedef struct {
	int rio_fd;
	int rio_count;
	char* rio_bufptr;
	char rio_buf[RIO_BUFSIZE];
} rio_t;

void rio_readinitb(rio_t* rp, int fd);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, int count);

