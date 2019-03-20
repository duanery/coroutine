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
    int id;
    int exit : 1;
    int autostack : 1;
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

static inline int co_cmp(co_t *co1, co_t *co2)
{
    return intcmp(co1->id, co2->id);
}

void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    
    if(prev != &init && !prev->autostack && 
        co_info.max_stack_consumption < ((uint64_t)prev->stack + prev->stack_size - prev->rsp))
        co_info.max_stack_consumption = (uint64_t)prev->stack + prev->stack_size - prev->rsp;
    
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(prev->exit) {
        list_del(&prev->rq_node);
        rb_erase(&prev->rb, &co_root);
        if(prev->autostack) {
            mprotect(prev->stack, getpagesize(), PROT_READ|PROT_WRITE);
        }
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
    if(stack_size == AUTOSTACK) {
        co->autostack = 1;
        stack_size = STACK_GROW * pagesize;
    } else
        co->autostack = 0;
    co->stack = memalign(pagesize, stack_size);
    co->stack_size = stack_size;
    co->id = co_id++;
    co->exit = 0;
    co->func = f;
    co->data = d;
    
    //保护栈
    if(co->autostack) {
        mprotect(co->stack, pagesize, PROT_READ); /* PROT_NONE */
    }
    
    /*
     * 这里是整个协程的核心
     * 要初始化新创建的栈，并初始化切换到新协程时要执行的函数
    **/
    frame = (frame_t *)(co->stack + stack_size);
    frame--;
    memset(frame, 0, sizeof(frame_t));
    frame->ret = (uint64_t)__new;  /* 核心中的核心 */
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
    int pagesize = getpagesize();
    
    if(likely(current != &init) &&
        current->autostack && 
        addr >= stack &&  addr <= stack + pagesize) {
        unsigned long offset;
        unsigned long *rbp = (unsigned long *)sigctx->rbp;
        unsigned long *rsp;
        
        //权限修改回来
        mprotect(stack, pagesize, PROT_READ|PROT_WRITE);
        
        //申请新的栈
        new_stack_size = stack_size + STACK_GROW * pagesize;
        newstack = memalign(pagesize, new_stack_size);
        
        //栈回溯，需要把栈上保存的所有rbp的值全部修改掉
        offset = newstack + new_stack_size - (stack + stack_size);
        while(*rbp != 0) {
            rsp = rbp;
            rbp = (unsigned long *)*rbp;
            *rsp += offset;
        }
        sigctx->rsp += offset;
        sigctx->rbp += offset;
        
        //建立新栈
        memcpy(newstack + STACK_GROW * pagesize + pagesize, stack + pagesize, stack_size - pagesize);
        mprotect(newstack, pagesize, PROT_READ);
        current->stack = newstack;
        current->stack_size = new_stack_size;
        free(stack);
    } else {
        #define P(r) printf("  %-3s %016" PRIx64 "\n", #r, sigctx->r)
        printf("Registers:\n");
        P(r8);  P(r9);  P(r10); P(r11); P(r12); P(r13); P(r14); P(r15);
        P(rdi); P(rsi); P(rbp); P(rbx); P(rdx); P(rax); P(rcx); P(rsp);
        P(rip);
        printf("coid %d stack %016"PRIx64" - %016"PRIx64"\n", coid(), stack, stack+stack_size);
        printf("addr %016"PRIx64"\n", addr);
        printf("Call Trace:\n");

        exit(128+SIGSEGV);
    }
}

static __init void co_init()
{
    rb_init_node(&init.rb);
    rb_insert(&co_root, &init, rb, co_cmp);
    
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    
    struct sigaction sa;
    sa.sa_sigaction = do_page_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
}