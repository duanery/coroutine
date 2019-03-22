#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <inttypes.h>
#include <execinfo.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h>

#include "compiler.h"
#include "rbtree.h"
#include "list.h"
#include "co.h"

typedef struct stack_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t ret;
}frame_t;

typedef struct co_struct {
    uint64_t rsp;
    void *stack;
    int stack_size;
    int id, shmfd;
    uint32_t exit : 1;
    uint32_t autostack : 1;
    uint32_t mmapstack : 1; 
    co_routine func;
    void *data;
    struct list_head rq_node;
    struct rb_node rb;
}co_t;

struct co_info {
    uint64_t max_stack_consumption;
    uint64_t co_num;
}co_info = {0, 0};

// 初始协程, 标识主线程
static co_t init = {0, 0, 0, 0, 0, 0, .rq_node = LIST_HEAD_INIT(init.rq_node)};

//current 标识当前协程co_t
co_t *current=&init;

// 协程红黑树的根
static struct rb_root co_root = RB_ROOT;

//公共协程栈以及在栈上的协程
static void *co_stack_bottom = NULL;
static co_t *co_onstack = NULL;

static inline int co_cmp(co_t *co1, co_t *co2)
{
    return intcmp(co1->id, co2->id);
}

/*
 * 非AUTOSTACK协程 :
 *  使用malloc分配的栈大小作为协程栈
 *
 * AUTOSTACK协程 :
 *  协程栈的栈底在CO_STACK_BOTTOM位置，所有协程共享这个线性地址。
 *  |____________|_____________________|
 *  0           128K                   4M
 *  |            |                     |
 *  |            |                     `MMAP_STACK - 大于128K小于4M的栈称为`mmap栈`
 *  |            `COPY_STACK - 小于128K的栈称为`copy栈`
 *  `CO_STACK_BOTTOM - 栈底
 *
 *  协程栈的消耗小于COPY_STACK时，采用copy方式：
 *      1) 协程换出时，把协程栈从[co_t::rsp - CO_STACK_BOTTOM)拷贝到co_t::stack中(malloc的内存)
 *      2) 协程换入时，把协程从co_t::stack中拷贝到[co_t::rsp - CO_STACK_BOTTOM)内存
 *  协程栈消耗大于COPY_STACK时，采用mmap方式：
 *      3) 协程换出时：
 *          5) 首次换出，先申请MMAP_STACK大小的共享内存shm_open，然后把协程栈从
 *             [co_t::rsp - CO_STACK_BOTTOM)拷贝到共享内存中(称为`从copy栈切换到mmap栈`)
 *          6) 非首次换出，如果next不使用mmap栈则需要`重建copy栈`。
 *      4) 协程换入时，`建立mmap栈`
 *  重建copy栈：在[CO_STACK_BOTTOM-COPY_STACK, CO_STACK_BOTTOM] 建立线性映射，向下生长(MAP_GROWSDOWN)
 *  建立mmap栈：把协程的mmap栈映射到 [CO_STACK_BOTTOM-MMAP_STACK, CO_STACK_BOTTOM] 线性地址
**/
void __switch_stack(co_t *prev, co_t *next)
{
    if(prev->autostack) {
        co_onstack = prev;
    }
    if(next->autostack && co_onstack != next) {
        uint64_t stack_size;
        void *from, *to;
        int pagesize = getpagesize();
        if(co_onstack) {
            stack_size = (uint64_t)co_stack_bottom - co_onstack->rsp;
            stack_size = round_up(stack_size, pagesize);
            if(stack_size <= COPY_STACK &&
                !co_onstack->mmapstack) {
                // 1)
                if(stack_size > co_onstack->stack_size) {
                    free(co_onstack->stack);
                    co_onstack->stack = memalign(pagesize, stack_size);
                    co_onstack->stack_size = stack_size;
                }
                stack_size = (uint64_t)co_stack_bottom - co_onstack->rsp;
                from = (void *)co_onstack->rsp;
                to = co_onstack->stack + co_onstack->stack_size - stack_size;
                memcpy(to, from, stack_size);
            } else {
                // 3)
                if(!co_onstack->mmapstack) {
                    // 5)
                    //栈转换，从copy栈切换到mmap栈。
                    //1.释放copy栈
                    free(co_onstack->stack);
                    
                    //2.分配mmap栈
                    char filename[64];
                    co_onstack->stack_size = MMAP_STACK;
                    sprintf(filename, "%d-%d", getpid(), co_onstack->id);
                    co_onstack->shmfd = shm_open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    ftruncate(co_onstack->shmfd, co_onstack->stack_size);
                    co_onstack->stack = mmap(NULL, co_onstack->stack_size, 
                                        PROT_READ | PROT_WRITE, 
                                        MAP_SHARED, 
                                        co_onstack->shmfd, 0);
                    if(co_onstack->stack == MAP_FAILED) {
                        printf("coid %d switch to mmap stack failed, %m", co_onstack->id);
                        exit(1);
                    }
                    co_onstack->mmapstack = 1;
                    
                    //3.从公共栈拷贝到mmap栈
                    stack_size = (uint64_t)co_stack_bottom - co_onstack->rsp;
                    from = (void *)co_onstack->rsp;
                    to = co_onstack->stack + co_onstack->stack_size - stack_size;
                    memcpy(to, from, stack_size);
                    
                    //4.释放mmap映射
                    munmap(co_onstack->stack, co_onstack->stack_size);
                    co_onstack->stack = NULL;
                }else if(!next->mmapstack) {
                    // 6)
                    munmap(co_stack_bottom - co_onstack->stack_size, co_onstack->stack_size);
                    void *ptr = mmap(co_stack_bottom - COPY_STACK, COPY_STACK, 
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN | MAP_FIXED, 
                                    -1, 0);
                    if(ptr == MAP_FAILED) {
                        printf("rebuild copy stack for coid %d failed, %m", next->id);
                        exit(1);
                    }
                }
            }
        }
        
        stack_size = (uint64_t)co_stack_bottom - next->rsp;
        stack_size = round_up(stack_size, pagesize);
        if(stack_size <= COPY_STACK &&
            !next->mmapstack) {
            // 2)
            stack_size = (uint64_t)co_stack_bottom - next->rsp;
            from = next->stack + next->stack_size - stack_size;
            to = (void *)next->rsp;
            memcpy(to, from, stack_size);
        } else {
            // 4)
            void *ptr = mmap(co_stack_bottom - next->stack_size, next->stack_size, 
                            PROT_READ | PROT_WRITE, 
                            MAP_SHARED | MAP_FIXED, 
                            next->shmfd, 0);
            if(ptr == MAP_FAILED &&
                ptr != co_stack_bottom - next->stack_size) {
                printf("build mmap stack for coid %d failed, %m", next->id);
                exit(1);
            }
            
            /* 为大于4M(MMAP_STACK)的协程建立更大的GROWSDOWN栈? */
            /*ptr = mmap(co_stack_bottom - next->stack_size - pagesize, pagesize, 
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN | MAP_FIXED, 
                    -1, 0);
            if(ptr != co_stack_bottom - next->stack_size - pagesize) {
                printf("bug\n");
            }*/
        }
    }
}

void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    
    if(prev != &init) {
        uint64_t stack_bottom;
        if(!prev->autostack)
            stack_bottom = (uint64_t)prev->stack + prev->stack_size;
        else
            stack_bottom = (uint64_t)co_stack_bottom;
        if(co_info.max_stack_consumption < stack_bottom - prev->rsp)
            co_info.max_stack_consumption = stack_bottom - prev->rsp;
    }
    
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(prev->exit) {
        list_del(&prev->rq_node);
        rb_erase(&prev->rb, &co_root);
        if(co_onstack == prev) {
            if(!next->autostack && co_onstack->mmapstack) {
                munmap(co_stack_bottom - co_onstack->stack_size, co_onstack->stack_size);
                void *ptr = mmap(co_stack_bottom - COPY_STACK, COPY_STACK, 
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN | MAP_FIXED, 
                                -1, 0);
                if(ptr == MAP_FAILED) {
                    printf("rebuild copy stack for failed, %m");
                    exit(1);
                }
            }
            co_onstack = NULL;
        }
        if(prev->mmapstack) {
            char filename[64];
            //munmap(prev->stack, prev->stack_size);
            if(prev->stack) printf("BUG\n");
            close(prev->shmfd);
            sprintf(filename, "%d-%d", getpid(), prev->id);
            shm_unlink(filename);
        } else
            free(prev->stack);
        free(prev);
        co_info.co_num--;
    }
}

extern void switch_to(co_t *, co_t *);

static int __schedule(bool dequeue)
{
    /*
     * 选择下一个协程
     * 参考Linux内核的话，可以定义协程队列，并对每个协程定义优先级，
     * 在选择时，可以选择优先级高的协程先执行。
     * 这里最简处理。
    **/
    co_t *next = list_next_entry(current, rq_node);
    if(dequeue && current != &init)
        list_del_init(&current->rq_node);
    //协程切换
    if(current != next)
        switch_to(current, next);
    
    return !list_empty(&init.rq_node);
}

int schedule()
{
    return __schedule(false);
}

static void __new()
{
    //调用协程函数
    current->func(current->data);
    //通过exit字段标识协程执行完毕
    current->exit = 1;
    //调度，切换到下一个协程
    schedule();
}

int cocreate(int stack_size, co_routine f, void *d)
{
    static int co_id = 1;
    int pagesize = getpagesize();
    frame_t *frame;
    co_t *co;
    
    //分配新的协程co_t,并加入init队列中
    co = malloc(sizeof(co_t));
    co->id = co_id++;
    co->func = f;
    co->data = d;
    co->exit = 0;
    co->autostack = 0;
    co->mmapstack = 0;
    if(stack_size == AUTOSTACK) {
        co->autostack = 1;
        stack_size = pagesize;
    }
    co->stack = memalign(pagesize, stack_size);
    co->stack_size = stack_size;
    
    /*
     * 这里是整个协程的核心
     * 要初始化新创建的栈，并初始化切换到新协程时要执行的函数
    **/
    frame = (frame_t *)(co->stack + co->stack_size);
    frame--;
    memset(frame, 0, sizeof(frame_t));
    frame->ret = (uint64_t)__new;  /* 核心中的核心 */
    if(co->autostack)
        co->rsp = (uint64_t)co_stack_bottom - sizeof(frame_t);
    else
        co->rsp = (uint64_t)frame;
    
    //插入运行队列和红黑树
    list_add_tail(&co->rq_node, &init.rq_node);
    rb_init_node(&co->rb);
    rb_insert(&co_root, co, rb, co_cmp);
    co_info.co_num++;
    
    return co->id;
}

//返回当前协程id
int coid()
{
    return current->id;
}

void cokill(int coid)
{
    co_t key = { .id = coid };
    co_t *co = rb_search(&co_root, &key, rb, co_cmp);
    if(co) {
        //如果co睡眠则唤醒，放在current后面，schedule会尽快调度到。
        if(list_empty(&co->rq_node))
            list_add(&co->rq_node, &current->rq_node);
        co->exit = 1;
        schedule();
    }
}

int cowait()
{
    return __schedule(true);
}

void cowakeup(int coid)
{
    co_t key = { .id = coid };
    co_t *co = rb_search(&co_root, &key, rb, co_cmp);
    //插入运行队列，放在队列尾
    if(co && list_empty(&co->rq_node))
        list_add_tail(&co->rq_node, &init.rq_node);
}


static void do_page_fault(int sig, siginfo_t *siginfo, void *u)
{
    ucontext_t *ucontext = u;
    struct sigcontext *sigctx = (struct sigcontext *)&ucontext->uc_mcontext;
    void *addr = siginfo->si_addr;
    void *stack = current->stack, *newstack;
    int stack_size = current->stack_size, new_stack_size;
    
    #define P(r) printf("  %-3s %016" PRIx64 "\n", #r, sigctx->r)
    printf("Registers:\n");
    P(r8);  P(r9);  P(r10); P(r11); P(r12); P(r13); P(r14); P(r15);
    P(rdi); P(rsi); P(rbp); P(rbx); P(rdx); P(rax); P(rcx); P(rsp);
    P(rip);
    if(!current->autostack)
        printf("coid %d stack %016"PRIx64" - %016"PRIx64"\n", coid(), stack, stack+stack_size);
    else
        printf("coid %d stack %016"PRIx64" - %016"PRIx64"\n", coid(), current->rsp, co_stack_bottom);
    printf("addr %016"PRIx64"\n", addr);
    printf("Call Trace:\n");

    exit(128+SIGKILL);
}

static __init void co_init()
{
    int pagesize = getpagesize();
    
    rb_init_node(&init.rb);
    rb_insert(&co_root, &init, rb, co_cmp);
    
    co_stack_bottom = mmap((void*)CO_STACK_BOTTOM - pagesize, pagesize, 
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, 
                        -1, 0);
    if(co_stack_bottom == MAP_FAILED) {
        perror("mmap ");
        exit(1);
    }
    co_stack_bottom += pagesize;
    
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    
    struct sigaction sa;
    sa.sa_sigaction = do_page_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGKILL, &sa, NULL);
}