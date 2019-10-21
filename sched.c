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
#include <errno.h>

#include "compiler.h"
#include "rbtree.h"
#include "list.h"
#include "co.h"
#include "co_inner.h"


struct co_info co_info = {0, 0};

// 初始协程, 标识主线程
static co_t init = {.rq_node = LIST_HEAD_INIT(init.rq_node)};

//current 标识当前协程co_t
co_t *current=&init;

// 协程红黑树的根
static struct rb_root co_root = RB_ROOT;

//公共协程栈以及在栈上的协程
static void *co_stack_bottom = NULL;
static co_t *co_onstack = NULL;
unsigned long COPY_STACK = DEFAULT_STACK;

//协程局部存储键值
static specific_t *co_specific = NULL;
static int co_specific_num = 0;

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
asmlinkage void __switch_stack(co_t *prev, co_t *next)
{
    if(prev->autostack) {
        co_onstack = prev;
    }
    if(next->autostack && co_onstack != next) {
        unsigned long stack_size;
        void *from, *to;
        int pagesize = getpagesize();
        if(co_onstack) {
            stack_size = (unsigned long)co_stack_bottom - co_onstack->rsp;
            stack_size = round_up(stack_size, pagesize);
            if(likely(stack_size <= COPY_STACK &&
                !co_onstack->mmapstack)) {
                // 1)
                if(stack_size > co_onstack->stack_size) {
                    free(co_onstack->stack);
                    co_onstack->stack = memalign(pagesize, stack_size);
                    co_onstack->stack_size = stack_size;
                }
                stack_size = (unsigned long)co_stack_bottom - co_onstack->rsp;
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
                    stack_size = (unsigned long)co_stack_bottom - co_onstack->rsp;
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
        
        stack_size = (unsigned long)co_stack_bottom - next->rsp;
        stack_size = round_up(stack_size, pagesize);
        if(likely(stack_size <= COPY_STACK &&
            !next->mmapstack)) {
            // 2)
            stack_size = (unsigned long)co_stack_bottom - next->rsp;
            from = next->stack + next->stack_size - stack_size;
            to = (void *)next->rsp;
            memcpy(to, from, stack_size);
        } else {
            // 4)
            void *ptr;
            ptr = mmap(co_stack_bottom - stack_size, stack_size, 
                        PROT_READ | PROT_WRITE, 
                        MAP_SHARED | MAP_FIXED | MAP_POPULATE, 
                        next->shmfd, next->stack_size - stack_size);
            ptr = mmap(co_stack_bottom - next->stack_size, next->stack_size - stack_size, 
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

asmlinkage void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    
    if(likely(prev != &init)) {
        unsigned long stack_bottom;
        if(!prev->autostack)
            stack_bottom = (unsigned long)prev->stack + prev->stack_size;
        else
            stack_bottom = (unsigned long)co_stack_bottom;
        if(co_info.max_stack_consumption < stack_bottom - prev->rsp)
            co_info.max_stack_consumption = stack_bottom - prev->rsp;
    }
    
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(unlikely(prev->exit)) {
        //cocall调用协程，执行完毕。
        if(prev->type == 1) {
            if(!prev->sharestack)
                free(prev->stack);
            return;
        }
        
        list_del(&prev->rq_node);
        rb_erase(&prev->rb, &co_root);
        if(co_onstack == prev) {
            if(unlikely(!next->autostack && co_onstack->mmapstack)) {
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
        if(unlikely(prev->mmapstack)) {
            char filename[64];
            close(prev->shmfd);
            sprintf(filename, "%d-%d", getpid(), prev->id);
            shm_unlink(filename);
        } else
            free(prev->stack);
        //释放线程局部存储
        if(prev->specific) {
            int i;
            for(i = 0; i < prev->spec_num; i++) {
                if(prev->specific[i] && co_specific[i].destructor) {
                    co_specific[i].destructor(prev->specific[i]);
                }
                prev->specific[i] = NULL;
            }
            free(prev->specific);
        }
        free(prev);
        co_info.co_num--;
    }
}

static int __schedule(bool dequeue)
{
    //cocall调用协程，且是共享栈的。
    bool share = current->type == 1 && current->sharestack == 1;
    /*
     * 选择下一个协程
     * 参考Linux内核的话，可以定义协程队列，并对每个协程定义优先级，
     * 在选择时，可以选择优先级高的协程先执行。
     * 这里最简处理。
    **/
    co_t *next = list_next_entry(current, rq_node);
    if(dequeue && likely(current != &init))
        list_del_init(&current->rq_node);
    /*
     * 协程切换
     * 1.切换到其他协程，而不是自己。
     * 2.非共享的，否则切换到next后，next可能调用cocall调用新协程，
     * 从而破坏栈。
    **/
    if(likely(current != next && !share))
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
    //调度，切换到下一个协程，不再返回
    schedule();
}

unsigned long cocreate(int stack_size, co_routine f, void *d)
{
    static unsigned long co_id = 1;
    int pagesize = getpagesize();
    frame_t *frame;
    co_t *co;
    
    //分配新的协程co_t,并加入init队列中
    co = memalign(SMP_CACHE_BYTES, sizeof(co_t));
    co->id = co_id++;
    co->func = f;
    co->data = d;
    co->exit = 0;
    co->autostack = 0;
    co->mmapstack = 0;
    co->type = 0;
    co->sharestack = 0;
    co->child = NULL;
    co->specific = NULL;
    co->spec_num = 0;
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
    frame->ret = (unsigned long)__new;  /* 核心中的核心 */
    if(co->autostack)
        co->rsp = (unsigned long)co_stack_bottom - sizeof(frame_t);
    else
        co->rsp = (unsigned long)frame;
    
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

void *coself()
{
    return current;
}

void cokill(int coid)
{
    co_t key = { .id = coid };
    co_t *co = rb_search(&co_root, &key, rb, co_cmp);
    if(likely(co)) {
        //如果co睡眠则唤醒，放在current后面，schedule会尽快调度到。
        if(likely(!co->child && list_empty(&co->rq_node)))
            list_add(&co->rq_node, &current->rq_node);
        co->exit = 1;
        schedule();
    }
}

int cowait()
{
    return __schedule(true);
}

void __cowakeup(void *c)
{
    co_t *co = c;
    //插入运行队列，放在队列尾
    if(likely(co && !co->child && list_empty(&co->rq_node)))
        list_add_tail(&co->rq_node, &init.rq_node);
}

void cowakeup(int coid)
{
    co_t key = { .id = coid };
    co_t *co = rb_search(&co_root, &key, rb, co_cmp);
    __cowakeup(co);
}

//内部使用
int __cotree_is_empty()
{
    return !RB_EMPTY_ROOT(&co_root);
}

//协程局部存储
int co_key_create(void (*destructor)(void*))
{
    specific_t *specific = co_specific;
    specific_t *specific_end = specific + co_specific_num;
    int old_num = co_specific_num;
    while(specific < specific_end) {
        if(!specific->used) {
            specific->used = 1;
            specific->destructor = destructor;
            return specific - co_specific;
        }
        specific++;
    }
    co_specific_num += 16;
    co_specific = realloc(co_specific, co_specific_num * sizeof(specific_t));
    memset(co_specific + old_num, 0, 16 * sizeof(specific_t));
    co_specific[old_num].used = 1;
    co_specific[old_num].destructor = destructor;
    return old_num;
}

int co_key_delete(int key)
{
    if(unlikely(key < 0 ||
        co_specific_num <= key ||
        !co_specific[key].used)) {
        return EINVAL;
    }
    co_specific[key].used = 0;
    co_specific[key].destructor = NULL;
    return 0;
}

void *co_getspecific(int key)
{
    co_t *curr;
    if(unlikely(key < 0 ||
        co_specific_num <= key ||
        !co_specific[key].used)) {
        return NULL;
    }
    curr = current->type == 1 ? current->top_parent : current;
    if(curr->spec_num <= key)
        return NULL;
    else
        return curr->specific[key];
}

int co_setspecific(int key, const void *value)
{
    co_t *curr;
    if(unlikely(key < 0 ||
        co_specific_num <= key ||
        !co_specific[key].used)) {
        return EINVAL;
    }
    curr = current->type == 1 ? current->top_parent : current;
    if(curr->spec_num <= key) {
        curr->specific = realloc(curr->specific, co_specific_num*sizeof(void *));
        memset(curr->specific + curr->spec_num, 0, (co_specific_num-curr->spec_num)*sizeof(void *));
        curr->spec_num = co_specific_num;
    }
    curr->specific[key] = (void *)value;
    return 0;
}

static void do_page_fault(int sig, siginfo_t *siginfo, void *u)
{
    ucontext_t *ucontext = u;
    struct sigcontext *sigctx = (struct sigcontext *)&ucontext->uc_mcontext;
    void *addr = siginfo->si_addr;
    void *stack = current->stack, *newstack;
    int stack_size = current->stack_size, new_stack_size;
    
    printf("Registers:\n");
#if defined(__x86_64__)
    #define P(r) printf("  %-3s %016" PRIx64 "\n", #r, sigctx->r)
    P(r8);  P(r9);  P(r10); P(r11); P(r12); P(r13); P(r14); P(r15);
    P(rdi); P(rsi); P(rbp); P(rbx); P(rdx); P(rax); P(rcx); P(rsp);
    P(rip);
#elif defined(__i386__)
    #define P(r) printf("  %-3s %08" PRIx32 "\n", #r, sigctx->r)
    P(edi); P(esi); P(ebp); P(esp); P(ebx); P(edx); P(ecx); P(eax);
    P(eip);
#elif defined(__aarch64__)
    int i;
    for(i=0; i<31; i++) {
        printf("  x%02d %016" PRIx64 "\n", i, sigctx->regs[i]);
    }
    printf("  %-3s %016" PRIx64 "\n", "sp", sigctx->sp);
    printf("  %-3s %016" PRIx64 "\n", "pc", sigctx->pc);
#endif
    if(!current->autostack)
        printf("coid %d stack %016"PRIx64" - %016"PRIx64"\n", coid(), stack, stack+stack_size);
    else
        printf("coid %d stack %016"PRIx64" - %016"PRIx64"\n", coid(), current->rsp, co_stack_bottom);
    printf("addr %016"PRIx64"\n", addr);
    printf("Call Trace:\n");

    exit(128+SIGSEGV);
}

static __init void co_init()
{
    int pagesize = getpagesize();
    
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
    sigaction(SIGSEGV, &sa, NULL);
}