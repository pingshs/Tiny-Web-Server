#include<string.h>
#include<stdio.h>

#include"rio.h"

ssize_t rio_writen(int fd, void *buf, size_t count){
	size_t nleft = count;
	ssize_t n_this_write;
	char *bufptr = (char*)(buf);

	while(nleft > 0){
		if((n_this_write = write(fd, bufptr, nleft)) <= 0){
			if(errno == EINTR)
				n_this_write = 0;
			else
				return -1;	
		}
		nleft -= n_this_write;
		bufptr += n_this_write;
	}
	return count;
}

void rio_readinitb(rio_t* rp, int fd){
	rp->rio_fd = fd;
	rp->rio_count = 0;
	rp->rio_bufptr = rp->rio_buf;
}
static ssize_t rio_read(rio_t *rp, char *usrbuf, int n){

	while(rp->rio_count <= 0){
		rp->rio_count = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
		if(rp->rio_count < 0){
			if(errno != EINTR)
				return -1;
		}else if(rp->rio_count == 0)
			return 0;
		else
			rp->rio_bufptr = rp->rio_buf;
	}
	
	int count = n;
	if(rp->rio_count < count)
		count = rp->rio_count;
	
	memcpy(usrbuf, rp->rio_bufptr, count);

	rp->rio_count -= count;
	rp->rio_bufptr += count;
	return count;
}
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, int maxlen){
	char* bufptr = static_cast<char*>(usrbuf);
	char c;

	int n, ret;
	for(n = 1; n < maxlen; n++){
		if((ret = rio_read(rp, &c, 1)) == 1){
			*bufptr++ = c;
			if(c == '\n'){
				n++;
				break;
			}
		}else if(ret == 0){
			if(n == 1)
				return 0;
			else 
				break;
		}else
			return -1;
	}	
	*bufptr = 0;

	return n-1;
}

/*
int main(){
	rio_t rio;
	rio_readinitb(&rio, STDOUT_FILENO);
	char buf[1024];	
	printf("%d\n", sizeof(buf));
	rio_readlineb(&rio, buf, sizeof(buf));
	printf("%s\n", buf);
}
*/
