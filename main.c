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

void f1(void *unused)
{
    struct timeval tv, tv1;
    printf("myid = %d\n", coid());
    gettimeofday(&tv, NULL);
    cousleep(coid()*10000);
    gettimeofday(&tv1, NULL);
    printf("cosleep %d us\n", (tv1.tv_sec - tv.tv_sec)*1000000+tv1.tv_usec - tv.tv_usec);
}

void f(void *unused)
{
    int i=5;
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(AUTOSTACK, f1, NULL);
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
            unsigned long max_stack_consumption;
            unsigned long co_num;
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

void ab_handle(int fd, void *data)
{
    const char *response_str = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html\r\nContent-Length: 11\r\n\r\nhello world\r\n";
    char buf[256];
    int len = coread(fd, buf, sizeof(buf));
    cowrite(fd, response_str, strlen(response_str));
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


#define MAXRAND 400
void buf(int loop)
{
    uint8_t buff[512];
    int i = 0;
    int k = 0;
    uint8_t c = (uint8_t)loop;
    if(c == 0) c = 1;
    for(;i<512;i++)
        buff[i] = c;
    if(loop == 0) {
        schedule();
        schedule();
        return;
    }
    buf(loop-1);
    for(i=0;i<512;i++) {
        if(buff[i] != c) k++;
    }
    if(k)
        printf("coid %d, buff %d, %d\n", coid(), buff[0], k);
}
void pagefault(void *d)
{
    int loop = rand() % MAXRAND;
    buf(loop);
    buf(loop);
}
#include "compiler.h"
#include "rbtree.h"
#include "list.h"
#include "co.h"
#include "co_inner.h"
int key_test;
void cocall_test1(void *d)
{
    long a = 500000000;
    long i=0;
    co_t *cur = coself();
    printf("OK1, %p, %p, %d\n", &a, (void *)cur->rsp, cur->stack_size);
    schedule();   //实际不会切出去
    while(a--)i+=a;
    printf("i=%ld\n", i);
    printf("key_test %d\n", co_getspecific(key_test));
    co_setspecific(key_test, (void *)2);
}
void cocall_test(void *d)
{
    int a = 5;
    co_t *cur = coself();
    key_test = co_key_create(NULL);
    printf("OK, %p, %p, %d\n", &a, (void *)cur->rsp, cur->stack_size);
    co_setspecific(key_test, (void *)1);
    cocall(0, cocall_test1, NULL);
}

void main()
{
    int i=0;
    srand(time(0));
    
    for(; i<MAXRAND; i++)
        cocreate(AUTOSTACK, pagefault, NULL);
    cocall(DEFAULT_STACK, cocall_test, NULL);
    printf("key_test %d\n", co_getspecific(key_test));
    while(coloop());
    
    signalfd_init();
    signal(SIGPIPE, SIG_IGN);
    printf("pid:%d\n", getpid());
    printf("coid = %d\n", coid());
    
    cocreate(16*1024, f, NULL);
    bind_listen(55667, echo_handler);
    bind_listen(55668, response_handle);
    bind_listen(8080, ab_handle);
    while(coloop());
}
