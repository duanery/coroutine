#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "compiler.h"
#include "rbtree.h"
#include "co.h"


static int efd;
static struct rb_root events_tree = RB_ROOT;
static struct epoll_event *events;
static int nr_events;

static void __init init_event()
{
    nr_events = 1024;
    events = malloc(nr_events * sizeof(struct epoll_event));
    memset(events, 0, nr_events * sizeof(struct epoll_event));
    
    efd = epoll_create(nr_events);
    if (efd < 0) {
        //TODO
    }
}

struct event_info {
    int fd;
    event_handler_t handle;
    void *data;
    int events;
    struct rb_node rb;
};

static int event_cmp(const struct event_info *e1, const struct event_info *e2)
{
    return intcmp(e1->fd, e2->fd);
}

static struct event_info *lookup_event(int fd)
{
    struct event_info key = { .fd = fd };
    return rb_search(&events_tree, &key, rb, event_cmp);
}

int register_event(int fd, event_handler_t h, void *data)
{
    int ret;
    struct epoll_event ev;
    struct event_info *ei;
    
    ei = malloc(sizeof(struct event_info));
    ei->fd = fd;
    ei->handle = h;
    ei->data = data;
    ei->events = EPOLLIN | EPOLLET;
    rb_init_node(&ei->rb);
    
    memset(&ev, 0, sizeof(ev));
    ev.events = ei->events;
    ev.data.ptr = ei;
    
    ret = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
    if (ret) {
        free(ei);
    } else
        rb_insert(&events_tree, ei, rb, event_cmp);
    return ret;
}

void unregister_event(int fd)
{
    struct event_info *ei;
    
    ei = lookup_event(fd);
    if (!ei)
        return;
    
    epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
    rb_erase(&ei->rb, &events_tree);
    free(ei);
}

int modify_event(int fd, unsigned int new_events)
{
    int ret;
    struct epoll_event ev;
    struct event_info *ei;

    ei = lookup_event(fd);
    if (!ei) {
        return 1;
    }
    if(ei->events == new_events | EPOLLET) return 0;
    
    memset(&ev, 0, sizeof(ev));
    ev.events = ei->events = new_events | EPOLLET;
    ev.data.ptr = ei;

    ret = epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev);
    if (ret) {
        return 1;
    }
    return 0;
}
//timeout：单位是微妙
void event_loop(int timeout)
{
    int i, nr;
    
    if (timeout >= 0 && timeout < 1000) {
        usleep(timeout);
        return;
    }
    nr = epoll_wait(efd, events, nr_events, timeout<0 ? -1 : timeout/1000);
    if (nr < 0) {
        if (errno == EINTR)
            return;
        exit(1);
    } else if (nr) {
        for (i = 0; i < nr; i++) {
            struct event_info *ei;
            ei = (struct event_info *)events[i].data.ptr;
            ei->handle(ei->fd, events[i].events, ei->data);
        }
    }
}


struct coevent_info {
    void *co;
    int fd;
    coevent_handler_t handle;
    void *data;
    int events;
};

static void event_wakeup(int fd, int events, void *data)
{
    struct coevent_info *coevent = data;
    coevent->events = events;
    __cowakeup(coevent->co);
}

static void event_routine(void *data)
{
    struct coevent_info *coevent = data;
    
    coevent->co = coself();
    
    register_event(coevent->fd, event_wakeup, coevent);
    
    coevent->handle(coevent->fd, coevent->data);
    
    unregister_event(coevent->fd);
    
    close(coevent->fd);
    
    free(coevent);
}

int register_coevent(int fd, coevent_handler_t h, void *data)
{
    struct coevent_info *coevent;
    
    coevent = malloc(sizeof(struct coevent_info));
    if(unlikely(coevent == NULL))
        return -1;
    cocreate(DEFAULT_STACK, event_routine, coevent);
    coevent->fd = fd;
    coevent->handle = h;
    coevent->data = data;
    return 0;
}
