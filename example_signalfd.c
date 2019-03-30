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

void main()
{    
    signalfd_init();

    while(coloop());
}
