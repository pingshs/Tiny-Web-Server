#define NDEBUG

#include<sys/types.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<sys/wait.h>
#include<netdb.h>
#include<unistd.h>
#include<fcntl.h>

#include<memory>

#include<errno.h>
#include<string.h>
#include<stdlib.h>
#include<signal.h>

#include"threadpool.h"
#include"rio.h"

const int LISTENQ = 100;
const int FD_SIZE = 2048;
const int EVENTSIZE = 1024;

const int HOSTLENGTH = 100;
const int PORTLENGTH = 15;

//多线程数量
const int THREAD_NUM = 4;

const int MAXLINESIZE = 1024;
const int MAXSIZE = 50;
//获取监听套接字描述符
int open_listenfd(char *port);
//epoll的添加、删除
void add_event(int epollfd, int fd, int state);
void delete_event(int epollfd, int fd, int state);
//接受连接，并将其注册为读事件
void handle_accept(int epollfd, int listenfd);
//智能指针删除器
void Close(int *p);

//处理客户端的连接，
//参数为连接描述符的智能指针
void handle_request(std::shared_ptr<int>&);
//连接时的错误处理
void connectionerror(std::shared_ptr<int>& pconnfd, const char *cause, const char *errnum, const char *shortmsg, const char *longmsg);
//读取请求头部（简单的忽略，不处理）
void read_requestheader(rio_t *rp);
//void read_requestheader(std::shared_ptr<int>& pconnfd);
//分析uri
int parse_uri(char *uri, char *filename, char *cgiargs);
//处理静态内容
void get_filetype(char *filename, char *filetype);
void serve_static(std::shared_ptr<int>& pconnfd, char* filename, int filesize);
//处理动态内容
void serve_dynamic(std::shared_ptr<int>& pconnfd, char* filename, char *cgiargs);

//处理SIGPIPE信号
void handler_sigpipe(int sig){
	fprintf(stderr, "SIGPIPE: %s\n", strerror(errno));
}

int main(int argc, char *argv[]){
	if(argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		return -1;
	}

	if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		fprintf(stderr, "signal error");

	int listenfd = open_listenfd(argv[1]);
	if(listenfd < 0){
		fprintf(stderr, "open_listenfd: %s\n", strerror(errno));
		return -1;
	}

	int epollfd = epoll_create(FD_SIZE);
	add_event(epollfd, listenfd, EPOLLIN);

	struct epoll_event events[EVENTSIZE];
	int current_events;

	ThreadPool pool;
	pool.start(THREAD_NUM);

	while(1){
		current_events = epoll_wait(epollfd, events, EVENTSIZE, -1);

		for(int i = 0; i < current_events; i++){
			if(listenfd == events[i].data.fd && (events[i].events & EPOLLIN)){
				handle_accept(epollfd, listenfd);
			}else if(events[i].events & EPOLLIN){
				delete_event(epollfd, events[i].data.fd, EPOLLIN);
				std::shared_ptr<int> pconnfd(new int(events[i].data.fd), Close);
						
				pool.put(std::bind(handle_request, std::move(pconnfd)));	
			}
		}
	}
}

//获取监听套接字描述符
int open_listenfd(char *port){
	struct addrinfo hints, *listp, *p;	

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;

	int ret;
	if((ret = getaddrinfo(NULL, port, &hints, &listp)) != 0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}

	int sockfd;
	for(p = listp; p; p = p->ai_next){
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);	
		if(sockfd < 0)
			continue;

		int optval = 1;
		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
				(const void*)&optval, sizeof(int)) < 0)
			continue;

		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == 0)
			break;
		if(close(sockfd) < 0){
			fprintf(stderr, "close error 130\n");
			return -1;
		}
	}

	if(!p)
		return -1;

	if(listen(sockfd, LISTENQ) < 0)
		return -1;
	return sockfd;
}

//epoll的添加、删除
void add_event(int epollfd, int fd, int state){
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = state;
	ev.data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}
void delete_event(int epollfd, int fd, int state){
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = state;
	ev.data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
}

//接受连接，并将其注册为读事件
void handle_accept(int epollfd, int listenfd){
	struct sockaddr_storage clientaddr;
	socklen_t clientaddrlen = sizeof(clientaddr);

	int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clientaddrlen);
	if(connfd < 0){
		fprintf(stderr, "accept error \n");
		return ;
	}

	char host[HOSTLENGTH], port[PORTLENGTH];
	getnameinfo((struct sockaddr*)&clientaddr, clientaddrlen, host, sizeof(host), port, sizeof(port), 0);
//TODO
//	printf("Accept a Connection<%s : %s>\n", host, port);

	add_event(epollfd, connfd, EPOLLIN);
}

//智能指针删除器
void Close(int *p){
	if(close(*p) < 0)
		fprintf(stderr, "close error 178%s\n", strerror(errno));
}

//处理客户端的连接，
//参数为连接描述符的智能指针
void handle_request(std::shared_ptr<int>& pconnfd){
	rio_t rio;		
	rio_readinitb(&rio, *pconnfd);

	char buf[MAXLINESIZE];
	rio_readlineb(&rio, buf, sizeof(buf));

	char method[MAXSIZE], uri[MAXSIZE], version[MAXSIZE];
	sscanf(buf, "%s %s %s", method, uri, version);

	if(strcasecmp(method, "GET") != 0){
		connectionerror(pconnfd, method, "501", "Not implemented",
				"Tiny does not implement this method");
		return;
	}
	read_requestheader(&rio);

	char filename[MAXLINESIZE], cgiargs[MAXLINESIZE];
	int is_static = parse_uri(uri, filename, cgiargs);	

	struct stat sbuf;
	memset(&sbuf, 0, sizeof(sbuf));

	if(stat(filename, &sbuf) < 0){
		connectionerror(pconnfd, filename, "404", "Not found",
				"Tiny couldn't find this file: ");
		return;
	}

	if(is_static){
		if(!S_ISREG(sbuf.st_mode) || !(S_IRUSR & sbuf.st_mode)){
			connectionerror(pconnfd, filename, "403", "Forbided",
					"Tiny couldn't read this file: ");
			return;
		}		
		serve_static(pconnfd, filename, sbuf.st_size);
	}else{
		if(!S_ISREG(sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)){
			connectionerror(pconnfd, filename, "403", "Forbided",
					"Tiny couldn't run this file: ");
			return;
		}		
		serve_dynamic(pconnfd, filename, cgiargs);
	}
}
void connectionerror(std::shared_ptr<int>& pconnfd, const char *cause, const char *errnum, const char *shortmsg, const char *longmsg){
	char body[MAXLINESIZE], buf[MAXLINESIZE];

	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bfcolor = ""fffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);	
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sContent-type:text/html\r\n", buf);
	sprintf(buf, "%sContent-length:%lu\r\n\r\n", buf, strlen(body));
	rio_writen(*pconnfd, buf, strlen(buf));
	rio_writen(*pconnfd, body, strlen(body));
}
void read_requestheader(rio_t *rp){
	char buf[MAXLINESIZE];

	rio_readlineb(rp, buf, sizeof(buf));
//	printf("%s", buf); //TOBEDELETE
	while(strstr(buf, "\r\n") && strlen(buf) > 4){
		rio_readlineb(rp, buf, sizeof(buf));
//		printf("%s", buf); //TOBEDELETE
	}	
}
int parse_uri(char *uri, char *filename, char *cgiargs){

	if(!strstr(uri, "cgi-bin")){
		strcpy(cgiargs, "");
		strcpy(filename, ".");	
		strcat(filename, uri);
		if(uri[strlen(uri) - 1] == '/')
			strcat(filename, "index.html");
		return 1;
	}else{
		char *p;
		if((p = strstr(uri, "?")) != NULL){
			strcpy(cgiargs, p+1);
			*p = '\0';
		}else{
			strcpy(cgiargs, "");
		}
		strcpy(filename, ".");
		strcat(filename, uri);
		return 0;
	}
}
void get_filetype(char *filename, char *filetype){
	if(strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if(strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if(strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if(strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else
		strcpy(filetype, "text/plain");
}
void serve_static(std::shared_ptr<int>& pconnfd, char* filename, int filesize){
	char buf[MAXLINESIZE];

	char filetype[MAXLINESIZE];
	get_filetype(filename, filetype);

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sContent-type:%s\r\n", buf, filetype);
	sprintf(buf, "%sContent-length:%d\r\n\r\n", buf, filesize);

	rio_writen(*pconnfd, buf, strlen(buf));

	int fd = open(filename, O_RDONLY, 0);
	if(fd < 0){
		fprintf(stderr, "open error %s\n", strerror(errno));
		return;
	}
	char *begin_address = static_cast<char*>(mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0));
	Close(&fd); //TODO

	rio_writen(*pconnfd, begin_address, filesize);

	munmap(begin_address, filesize);
}
void serve_dynamic(std::shared_ptr<int>& pconnfd, char* filename, char *cgiargs){
	char buf[MAXLINESIZE];
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);

	rio_writen(*pconnfd, buf, strlen(buf));

	char *emptylist[] = {NULL};
	if(fork() == 0){
		setenv("QUERY_STRING", cgiargs, 1);
		dup2(*pconnfd, STDOUT_FILENO);
		execve(filename, emptylist, environ);
	}
	wait(NULL);
}
