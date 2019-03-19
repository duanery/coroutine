#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <errno.h>

#include "rbtree.h"
#include "co.h"

struct sleep_info {
    int coid;
    struct timeval t;
    struct rb_node rb;
};

static struct rb_root sleep_info_root = RB_ROOT;

static inline int sleep_info_cmp(struct sleep_info *s1, struct sleep_info *s2)
{
    int r = intcmp(s1->t.tv_sec, s2->t.tv_sec);
    return r != 0 ? r : intcmp(s1->t.tv_usec, s2->t.tv_usec);
}

//对usleep的封装
int cousleep(useconds_t us)
{
    if(unlikely(coid() == 0)) {
        return usleep(us);
    }
    int ret = 0;
    struct sleep_info si;
    struct sleep_info cur;
    time_t tv_sec = us / 1000000;
    long   tv_usec = us % 1000000;
    si.coid = coid();
    gettimeofday(&si.t, NULL);
    si.t.tv_sec += tv_sec;
    si.t.tv_usec += tv_usec;
    if(si.t.tv_usec > 1000000) {
        si.t.tv_sec++;
        si.t.tv_usec -= 1000000;
    }
    rb_init_node(&si.rb);
    rb_insert(&sleep_info_root, &si, rb, sleep_info_cmp);
    cowait();
    rb_erase(&si.rb, &sleep_info_root);
    gettimeofday(&cur.t, NULL);
    if(sleep_info_cmp(&si, &cur) > 0) {
        errno = EINTR;
        ret = -1;
    }
    return ret;
}

//对sleep的封装
unsigned int cosleep(unsigned int seconds)
{
    if(unlikely(coid() == 0)) {
        return sleep(seconds);
    }
    struct sleep_info si;
    struct sleep_info cur;
    si.coid = coid();
    gettimeofday(&si.t, NULL);
    si.t.tv_sec += seconds;
    rb_init_node(&si.rb);
    rb_insert(&sleep_info_root, &si, rb, sleep_info_cmp);
    cowait();
    rb_erase(&si.rb, &sleep_info_root);
    gettimeofday(&cur.t, NULL);
    if(sleep_info_cmp(&si, &cur) > 0) {
        long d = si.t.tv_usec - cur.t.tv_usec;
        int t = 0;
        if(d < -500000) t = -1;
        else if(d > 500000) t = 1;
        return si.t.tv_sec - cur.t.tv_sec - t;
    }
    return 0;
}

//对nanosleep的封装
int conanosleep(const struct timespec *req, struct timespec *rem)
{
    if(unlikely(coid() == 0)) {
        return nanosleep(req, rem);
    }
    int ret = 0;
    struct sleep_info si;
    struct sleep_info cur;
    time_t tv_sec = req->tv_sec;
    long   tv_usec = req->tv_nsec / 1000;
    si.coid = coid();
    gettimeofday(&si.t, NULL);
    si.t.tv_sec += tv_sec;
    si.t.tv_usec += tv_usec;
    if(si.t.tv_usec > 1000000) {
        si.t.tv_sec++;
        si.t.tv_usec -= 1000000;
    }
    rb_init_node(&si.rb);
    rb_insert(&sleep_info_root, &si, rb, sleep_info_cmp);
    cowait();
    rb_erase(&si.rb, &sleep_info_root);
    gettimeofday(&cur.t, NULL);
    if(sleep_info_cmp(&si, &cur) > 0) {
        errno = EINTR;
        ret = -1;
        if(rem) {
            long us = si.t.tv_usec - cur.t.tv_usec;
            rem->tv_sec = si.t.tv_sec - cur.t.tv_sec;
            if(us < 0) {
                rem->tv_sec--;
                us += 1000000;
            }
            rem->tv_nsec = us * 1000 + req->tv_nsec % 1000;
        }
    }
    return ret;
}

//返回值是微妙
static long co_timeout()
{
    struct rb_node *rb_node;
    rb_node = rb_first(&sleep_info_root);
    if(rb_node) {
        struct timeval tv;
        struct sleep_info *si;
        long us;
        si = rb_entry(rb_node, struct sleep_info, rb);
        gettimeofday(&tv, NULL);
        us = (si->t.tv_sec - tv.tv_sec) * 1000000 + (si->t.tv_usec - tv.tv_usec);
        return us < 0 ? 0 : us;
    }
    return -1;
}

//唤醒sleep到期的协程
static void co_wakeup()
{
    if(rb_first(&sleep_info_root)) {
        struct sleep_info *si;
        struct sleep_info cur;
        gettimeofday(&cur.t, NULL);
        rb_for_each_entry(si, &sleep_info_root, rb) {
            if(sleep_info_cmp(si, &cur) <= 0) {
                cowakeup(si->coid);
            }
            else
                break;
        }
    }
}


//对read的封装
int coread(int fd, void *buf, size_t count)
{
    int ret, len = 0;
reread:
    ret = read(fd, buf+len, count-len);
    if(ret < 0) {
        if(errno == EINTR)
            goto reread;
        if(errno == EAGAIN) {
            modify_event(fd, EPOLLIN);
            cowait();
            goto reread;
        }
    }
    return ret;
}

//对read的封装，读满count个字节
int coread1(int fd, void *buf, size_t count)
{
    int ret, len = 0;
reread:
    ret = read(fd, buf+len, count-len);
    if(ret < 0) {
        if(errno == EINTR)
            goto reread;
        if(errno == EAGAIN) {
            modify_event(fd, EPOLLIN);
            cowait();
            goto reread;
        }
        return ret;
    }

    len += ret;
    if(ret == 0 || len == count)
        return len;
    goto reread;
}

//对write的封装
int cowrite(int fd, const void *buf, size_t count)
{
    int ret, len = 0;
rewrite:
    ret = write(fd, buf+len, count-len);
    if(ret < 0) {
        if(errno == EINTR)
            goto rewrite;
        if(errno == EAGAIN) {
            modify_event(fd, EPOLLOUT);
            cowait();
            goto rewrite;
        }
        return ret;
    }
    len += ret;
    if(len == count)
        return len;
    goto rewrite;
}

//对accept的封装
int coaccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int ret;
reaccept:
    ret = accept(sockfd, addr, addrlen);
    if(ret < 0) {
        if(errno == EINTR)
            goto reaccept;
        if(errno == EAGAIN) {
            modify_event(sockfd, EPOLLIN);
            cowait();
            goto reaccept;
        }
    }
    return ret;
}

extern void event_loop(int timeout);
void coloop()
{
    while(schedule());
    event_loop(co_timeout());
    co_wakeup();
}