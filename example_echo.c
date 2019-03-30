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
#include <stdlib.h>
#include <sys/signalfd.h>
#include <string.h>

#include "co.h"

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        perror("set nonblocking fail.");
        exit(-1);
    }
}

void echo_handler(int fd, void *data)
{
    char buff[512];
    int i, ret;
    while(1) {
        int len = coread(fd, buff, sizeof(buff));
        if(len == 0) break;
        ret = cowrite(fd, buff, len);
        if(ret < 0)
            break;
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
        register_coevent(fd , (coevent_handler_t)data, NULL);
    }
    printf("listen return\n");
}


void bind_listen(unsigned short port, void *handle) {
    int listen_fd           = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family         = AF_INET;
    addr.sin_port           = htons(port);
    addr.sin_addr.s_addr    = INADDR_ANY;
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
    register_coevent( listen_fd, listenfd_handler, handle );
}

void main()
{
    signal(SIGPIPE, SIG_IGN);
    printf("pid:%d\n", getpid());
    printf("coid = %d\n", coid());

    bind_listen(55667, echo_handler);
    while(coloop());
}

