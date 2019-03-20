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

#include "co.h"

void f1(void *unused)
{
    struct timeval tv, tv1;
    printf("myid = %d\n", coid());
    gettimeofday(&tv, NULL);
    cousleep(coid()*100000);
    gettimeofday(&tv1, NULL);
    printf("cosleep %d us\n", (tv1.tv_sec - tv.tv_sec)*1000000+tv1.tv_usec - tv.tv_usec);
}

void f(void *unused)
{
    int i=5;
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(16*1024, f1, NULL);
        i--;
        schedule();
    }
}

#define handle_error(msg) \
           do { perror(msg); } while (0)
void signal_routine(int sfd, void *d)
{
    sigset_t mask; 
    struct signalfd_siginfo fdsi; 
    ssize_t s; 

    sigemptyset(&mask); 
    sigaddset(&mask, SIGINT); 
    sigaddset(&mask, SIGQUIT); 
    sigaddset(&mask, SIGTSTP); 

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) 
        handle_error("sigprocmask");
    
    sfd = signalfd(sfd, &mask, 0);
    for (;;) { 
        s = coread(sfd, &fdsi, sizeof(struct signalfd_siginfo)); 
        if (s != sizeof(struct signalfd_siginfo)) 
            handle_error("read"); 

        extern struct co_info {
            uint64_t max_stack_consumption;
            uint64_t co_num;
        } co_info;
        printf("max_stack_consumption %u co_num %u\n", co_info.max_stack_consumption, co_info.co_num);
        
        if (fdsi.ssi_signo == SIGINT) { 
            printf("Got SIGINT\n"); 
            exit(1);
        } else if (fdsi.ssi_signo == SIGQUIT) { 
            printf("Got SIGQUIT\n"); 
            exit(0); 
        } else if (fdsi.ssi_signo == SIGTSTP) { 
            ;
        } else {
            printf("Read unexpected signal\n"); 
        } 
    }
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) 
        handle_error("sigprocmask");    
}

void signalfd_init() 
{ 
    sigset_t mask; 
    int sfd;  

    sigemptyset(&mask);  

    sfd = signalfd(-1, &mask, SFD_NONBLOCK); 
    if (sfd == -1) 
        handle_error("signalfd"); 

    register_coevent(sfd, signal_routine, NULL);
}


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
        /*
        struct timeval tv, tv1;
        gettimeofday(&tv, NULL);
        struct timespec ts = {.tv_sec = 1};
        while(conanosleep(&ts, &ts) < 0 && errno == EINTR);
        gettimeofday(&tv1, NULL);
        printf("%d cosleep %d us\n", coid(), (tv1.tv_sec - tv.tv_sec)*1000000+tv1.tv_usec - tv.tv_usec);
        */
        ret = cowrite(fd, buff, len);
        if(ret < 0)
            break;
    }
    printf("handle return %d\n", coid());
}
#define min(x,y) ((x)<(y)?(x):(y))
void response_handle(int fd, void *data)
{
    uint8_t buff[128];
    uint32_t size;
    int ret;
    while(1) {
        int len = coread1(fd, &size, sizeof(size));
        if(len == 0) break;
        //printf("size = %d\n", size);
        while(size) {
            len = min(size, sizeof(buff));
            ret = cowrite(fd, buff, len);
            if(ret < 0) break;
            size -= ret;
        }
    }
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


int stack_loop = 0;
void buf()
{
    char buff[512];
    int i = 0;
    for(;i<256;i++)
        buff[i] = (unsigned char)stack_loop;
    if(++stack_loop >= 60 ) return;
    buf();
    printf("coid %d, buff %d\n", coid(), buff[0]);
}
void pagefault(void *d)
{
    buf();
}

void main()
{
    cocreate(16*1024, pagefault, NULL);
    while(schedule());
    
    signalfd_init();
    signal(SIGPIPE, SIG_IGN);
    printf("pid:%d\n", getpid());
    printf("coid = %d\n", coid());
    
    cocreate(16*1024, f, NULL);
    bind_listen(55667, echo_handler);
    bind_listen(55668, response_handle);
    while(1) {
        coloop();
    }
}
