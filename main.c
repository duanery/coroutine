#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "co.h"

void f1(void *unused)
{
    struct timeval tv, tv1;
    printf("myid = %d\n", coid());
    gettimeofday(&tv, NULL);
    cosleep(coid()*100000);
    gettimeofday(&tv1, NULL);
    printf("cosleep %d us\n", (tv1.tv_sec - tv.tv_sec)*1000000+tv1.tv_usec - tv.tv_usec);
}

void f(void *unused)
{
    int i=100;
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(16*1024, f1, NULL);
        i--;
        schedule();
    }
}


void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        perror("set nonblocking fail.");
        exit(-1);
    }
}

void fd_handler(int fd, void *data)
{
    char buff[512];
    int i;
    while(1) {
        int len = coread(fd, buff, sizeof(buff));
        if(len == 0) break;
        cowrite(fd, buff, len);
    }
    printf("handle return %d\n", coid());
}

void listenfd_handler(int listen_fd, void *data)
{
    struct sockaddr_storage from;
    socklen_t namesize;
    int fd;
    while(1) {
        namesize = sizeof(from);
        fd = coaccept(listen_fd, (struct sockaddr *)&from, &namesize);
        set_nonblocking(fd);
        printf("accept fd %d \n", fd);
        register_coevent(fd , fd_handler, NULL);
    }
    printf("listen return\n");
}
void bind_listen(unsigned short port) {
    int listen_fd           = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family         = AF_INET;
    addr.sin_port           = htons(port);
    addr.sin_addr.s_addr    = inet_addr("192.168.31.128");
    int ret = bind( listen_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr) );
    if (ret < 0) {
        perror("bind fail.");
        exit(-1);
    }
    ret = listen( listen_fd, 20 );
    if (ret < 0) {
        perror("listen fail.");
        exit(-1);
    }
    printf("routine[%d] listen bind at port: %u\n", coid(), port);
    set_nonblocking( listen_fd );
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    register_coevent( listen_fd, listenfd_handler, NULL );
}


void main()
{
    cocreate(16*1024, f, NULL);
    printf("coid = %d\n", coid());
    signal(SIGPIPE, SIG_IGN);
    bind_listen(55667);
    while(1) {
        coloop();
    }
}
