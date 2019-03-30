#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ucontext.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#include <map>
#include <list>

#define MAX_ROUTINE         1024
#define MAX_STACK_SIZE_KB   128
#define MAX_EVENT_SIZE      10240
enum { UNUSED = 0, IDLE = 1, RUNNING = 2 };

typedef void (*STD_ROUTINE_FUNC)(void);

typedef struct {
    ucontext_t          ctx;
    char                stack[ MAX_STACK_SIZE_KB * 1024 ];
    STD_ROUTINE_FUNC    func;
    int                 status;
    int                 wait_fd;
    int                 events;
} RoutineContext;

typedef std::map<unsigned int, std::list<int> > TimeoutMap;

typedef struct {
    struct epoll_event events[MAX_ROUTINE];
    RoutineContext  routines[MAX_ROUTINE];
    TimeoutMap      timeout_map;
    ucontext_t      main;
    int             epoll_fd;
    int             running_id;
    int             routine_cnt;
} RoutineCenter; 

RoutineCenter routinecenter;

void init() {
    srand(time(NULL));
    routinecenter.running_id = -1;
}

void routine_wrap() {
    int running_id = routinecenter.running_id;
    if ( running_id < 0 ) {
        puts("current context don't attach to any routine except main.");
        return;
    }
    routinecenter.routines[running_id].func();

    routinecenter.routines[running_id].status = UNUSED;
    routinecenter.routine_cnt--;
}

int create( STD_ROUTINE_FUNC routine_proc) {
    int new_id = -1;
    for (int i = 0; i < MAX_ROUTINE; i++) {
        if ( routinecenter.routines[i].status == UNUSED ) {
            new_id = i;
            break;
        }
    }

    if ( new_id < 0 ) {
        puts("max routine number reached. no more routine.");
        return -1;
    }

    ucontext_t* pctx = &(routinecenter.routines[new_id].ctx);
    getcontext(pctx);

    pctx->uc_stack.ss_sp    = routinecenter.routines[new_id].stack;
    pctx->uc_stack.ss_size  = MAX_STACK_SIZE_KB * 1024;
    pctx->uc_stack.ss_flags = 0;
    pctx->uc_link           = &(routinecenter.main);

    makecontext(pctx, routine_wrap, 0);

    routinecenter.routines[new_id].status   = IDLE;
    routinecenter.routines[new_id].func     = routine_proc;
    routinecenter.routine_cnt++;
    return new_id;
}

int yield() {
    if ( routinecenter.running_id < 0 ) {
        puts("no routine running except main.");
        return 0;
    }
    int running_id          = routinecenter.running_id;
    RoutineContext* info    = &(routinecenter.routines[running_id]);
    info->status            = IDLE;
    info->events            = 0;
    swapcontext( &(info->ctx), &(routinecenter.main) );
    return 0;   
}

int resume(int id, int events = 0) {
    if ( id < 0 || id >= MAX_ROUTINE ) {
        puts("routine id out of bound.");
        return -1;
    }
    int running_id          = routinecenter.running_id;
    if (id == running_id) {
        puts("current routine is running already.");
        return 0;
    }
    if (routinecenter.routines[id].status != IDLE) {
        puts("target routine is not in idel status. can't resume");
        return -1;
    }

    routinecenter.running_id            = id;
    routinecenter.routines[id].status   = RUNNING;
    routinecenter.routines[id].events   = events;
    if (running_id < 0) {
        // in main
        swapcontext( &(routinecenter.main), &(routinecenter.routines[id].ctx));
        routinecenter.running_id = -1;
    } else {
        // in other routine
        routinecenter.routines[running_id].status = IDLE;
        swapcontext( &(routinecenter.routines[running_id].ctx), &(routinecenter.routines[id].ctx) );
        routinecenter.running_id = running_id;
    }
    return 0;
}

int routine_id() { return routinecenter.running_id; }

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        perror("set nonblocking fail.");
        exit(-1);
    }
}

void mod_event(int fd, int events, int op, int routine_id) {
    struct epoll_event ev = {0};
    if ( EPOLL_CTL_DEL != op ) {
        ev.data.fd= routine_id;
        routinecenter.routines[routine_id].wait_fd = fd;
    }
    ev.events = events;

    int ret = epoll_ctl(routinecenter.epoll_fd, op, fd, &ev);
    if (ret < 0) {
        if ( errno == EEXIST && op != EPOLL_CTL_DEL) {
            epoll_ctl(routinecenter.epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        }
    }
}

int routine_read(int fd, char* buff, int size) {
    mod_event(fd, EPOLLIN, EPOLL_CTL_ADD, routine_id());
    while (!(routinecenter.routines[routine_id()].events & EPOLLIN)) {
        yield();    
    }
    while (routinecenter.routines[routine_id()].events & EPOLLIN) {
        int need    = size;
        int readin  = 0;
        while (need > 0) {
            int ret = read(fd, buff + readin, need);
            if (ret <= 0) {
                break;
            } else {
                readin += ret;
                need -= ret;
            }
        }
        if (readin == 0 && size != 0) {
            yield();
            continue;
        } else {
            mod_event(fd, EPOLLIN, EPOLL_CTL_DEL, routine_id());
        }
        return readin;
    } 
    printf("routine[%d][%s]routine system ran out of order.\n", routine_id(), __func__);
    return 0;
}

int routine_write(int fd, char* buff, int size) {
    mod_event(fd, EPOLLOUT, EPOLL_CTL_ADD, routine_id());
    while (!(routinecenter.routines[routine_id()].events & EPOLLOUT)) {
        yield();    
    }
    while (routinecenter.routines[routine_id()].events & EPOLLOUT) {
        int need    = size;
        int wrout   = 0;
        while (need > 0) {
            int ret = write(fd, buff + wrout, need);
            if (ret <= 0) {
                break;
            } else {
                wrout += ret;
                need -= ret;
            }
        }
        if ( wrout == 0 && size != 0 ) {
            yield();    
            continue;
        } else {
            mod_event(fd, EPOLLOUT, EPOLL_CTL_DEL, routine_id());
        }
        return wrout;
    }
    printf("routine[%d][%s]routine system ran out of order.\n", routine_id(), __func__);
    return 0;
}

void routine_delay_resume(int rid, int delay_sec) {
    if (delay_sec <= 0) {
        resume(rid);
        return;
    }
    routinecenter.timeout_map[time(NULL) + delay_sec].push_back(rid);
}

void routine_sleep(int time_sec) {
    routine_delay_resume(routine_id(), time_sec);
    yield();
}

int routine_nearest_timeout() {
    if (routinecenter.timeout_map.empty()) {
        return 60 * 1000; // default epoll timeout
    }
    unsigned int now    = time(NULL);
    int diff            = routinecenter.timeout_map.begin()->first - now;
    return diff < 0 ? 0 : diff * 1000;
}

void routine_resume_timeout() {
    // printf("[epoll] process timeout\n");
    if ( routinecenter.timeout_map.empty() ) {
        return;
    }
    unsigned int timestamp      = routinecenter.timeout_map.begin()->first;

    if (timestamp > time(NULL)) {
        return;
    }

    std::list<int>& routine_ids = routinecenter.timeout_map.begin()->second;

    for (int i : routine_ids) {
        resume(i);
    }
    routinecenter.timeout_map.erase(timestamp);
}

void routine_resume_event(int n) {
    // printf("[epoll] process event\n");
    for (int i = 0; i < n; i++) {
        int rid = routinecenter.events[i].data.fd;
        resume(rid, routinecenter.events[i].events);
     }
}

void create_routine_poll() {
    routinecenter.epoll_fd = epoll_create1 (0); 

    if (routinecenter.epoll_fd == -1) {
        perror ("epoll_create");
        exit(-1);
    }
}

void routine_poll() {

    for (;;) {
        int n = epoll_wait (routinecenter.epoll_fd, routinecenter.events, MAX_EVENT_SIZE, routine_nearest_timeout());
        // printf("[epoll] event_num:%d\n", n);
        routine_resume_timeout();
        routine_resume_event(n);
    }
}

void echo_server_routine() {
    int conn_fd = routinecenter.routines[routine_id()].wait_fd;
    
    printf("routine[%d][%s] server start. conn_fd: %d\n", routine_id(), __func__, conn_fd);

    for (;;) {
        //printf("routine[%d][%s] loop start. conn_fd: %d\n", routine_id(), __func__, conn_fd);
        char buf[512] = {0};
        int n       = 0;
        
        n = routine_read( conn_fd, buf, sizeof (buf) );
        if (n < 0) {
            perror("server read error.");
            break;
        }

        n = routine_write(conn_fd, buf, n);
        if (n < 0) {
            perror("server write error.");
            break;
        }
    }
    printf("routine[%d][%s] server start. conn_fd: %d\n", routine_id(), __func__, conn_fd);
}

void request_accept() {
    for (;;) {
        struct sockaddr_in addr     = {0};
        socklen_t           slen    = sizeof(addr);
        int fd = accept(routinecenter.routines[routine_id()].wait_fd, (struct sockaddr*)&addr, &slen);
        struct sockaddr_in peer = {0};
        int ret = getpeername(fd, (struct sockaddr*)&peer, &slen);
        if (ret < 0) {
            perror("getpeername error.");
            exit(-1);
        }
        printf("routine[%d][%s] accept from %s conn_fd:%d\n", routine_id(), __func__, inet_ntoa(peer.sin_addr), fd);
        set_nonblocking(fd);
        int rid = create( echo_server_routine );
        routinecenter.routines[rid].wait_fd = fd;

        mod_event(fd, EPOLLIN, EPOLL_CTL_ADD, rid);

        resume(rid);

        yield();
    }
}

void bind_listen(unsigned short port) {
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
    printf("routine[%d] listen bind at port: %u\n", routine_id(), port);
    set_nonblocking( listen_fd );
    int rid = create( request_accept );
    mod_event( listen_fd, EPOLLIN, EPOLL_CTL_ADD, rid );
}

int main() {
    init();

    create_routine_poll();

    bind_listen(55667);
    //bind_listen(55668);
    //bind_listen(55669);

    //routine_delay_resume(create( [](){ printf("routine[%d] alarm fired\n", routine_id()); } ), 3);

    routine_poll();

    puts("all routine exit");

    return 0;
}