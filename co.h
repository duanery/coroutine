#ifndef __CO_H__
#define __CO_H__

//sched.c
typedef void (*co_routine)(void *);
int schedule();
#define yield schedule
unsigned long cocreate(int stack_size, co_routine f, void *d);
int coid();
void *coself();
void cokill(int coid);
int cowait();
void __cowakeup(void *c);
void cowakeup(int coid);
int co_key_create(void (*destructor)(void*));
int co_key_delete(int key);
void *co_getspecific(int key);
int co_setspecific(int key, const void *value);
#define AUTOSTACK 0
#if defined(__x86_64__) || defined(__aarch64__)
    #define CO_STACK_BOTTOM 0x20000000000
#elif defined(__i386__)
    #define CO_STACK_BOTTOM 0x80000000
#endif
extern unsigned long COPY_STACK;
#define MMAP_STACK (4*1024*1024)
#define DEFAULT_STACK (128*1024)


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
int coloop();


//event.c
typedef void (*event_handler_t)(int fd, int events, void *data);
typedef void (*coevent_handler_t)(int fd, void *data);
int register_event(int fd, event_handler_t h, void *data);
void unregister_event(int fd);
int modify_event(int fd, unsigned int new_events);
int register_coevent(int fd, coevent_handler_t h, void *data);



#endif