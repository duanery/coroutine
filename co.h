#ifndef __CO_H__
#define __CO_H__

//sched.c
typedef void (*co_routine)(void *);
int schedule();
int cocreate(int stack_size, co_routine f, void *d);
int coid();
void cokill(int coid);
int cowait();
void cowakeup(int coid);
#define AUTOSTACK 0
#define STACK_GROW 2 /* pages */


//syscall.c
#include <unistd.h>
int cousleep(useconds_t us);
unsigned int cosleep(unsigned int seconds);
#include <time.h>
int conanosleep(const struct timespec *req, struct timespec *rem);
int coread(int fd, void *buf, size_t count);
int coread1(int fd, void *buf, size_t count);
int cowrite(int fd, const void *buf, size_t count);
#include <sys/socket.h>
int coaccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
void coloop();


//event.c
typedef void (*event_handler_t)(int fd, int events, void *data);
typedef void (*coevent_handler_t)(int fd, void *data);
int register_event(int fd, event_handler_t h, void *data);
void unregister_event(int fd);
int modify_event(int fd, unsigned int new_events);
int register_coevent(int fd, coevent_handler_t h, void *data);



#endif