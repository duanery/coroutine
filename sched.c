#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
    int id;
    int exit;
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

static __init void co_init()
{
    rb_init_node(&init.rb);
    rb_insert(&co_root, &init, rb, co_cmp);
}

void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    
    if(prev != &init &&
        co_info.max_stack_consumption < ((uint64_t)prev->stack - prev->rsp))
        co_info.max_stack_consumption = (uint64_t)prev->stack - prev->rsp;
    
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(prev->exit) {
        list_del(&prev->rq_node);
        rb_erase(&prev->rb, &co_root);
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
    frame_t *frame;
    //分配新的协程co_t,并加入init队列中
    co_t *co = malloc(sizeof(co_t) + stack_size);
    co->stack = (void *)(co + 1);
    co->stack += stack_size;
    co->id = co_id++;
    co->exit = 0;
    co->func = f;
    co->data = d;
    
    /*
     * 这里是整个协程的核心
     * 要初始化新创建的栈，并初始化切换到新协程时要执行的函数
    **/
    frame = (frame_t *)co->stack;
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
