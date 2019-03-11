#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct stack_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t ret;
}frame_t;

typedef void (*co_func)(void *);
typedef struct co_struct {
    uint64_t rsp;
    void *stack;
    int id;
    int exit;
    co_func func;
    void *data;
    struct co_struct *next;
}co_t;

// 初始协程, 标识主线程
static co_t init = {0,0,0,0,0,NULL};

//current 标识当前协程co_t
co_t *current=&init;

void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(prev->exit) {
        co_t *c = &init;
        while(c->next != prev) c = c->next;
        c->next = prev->next;
        free(prev);
    }
}

int schedule()
{
    /*
     * 选择下一个协程
     * 参考Linux内核的话，可以定义协程队列，并对每个协程定义优先级，
     * 在选择时，可以选择优先级高的协程先执行。
     * 这里最简处理。
    **/
    co_t *next = current->next;
    if(!next)
        next = &init;
    //协程切换
    switch_to(current, next);
    return (init.next != NULL);
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

int cocreate(int stack_size, co_func f, void *d)
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
    co->next = init.next;
    init.next = co;
    
    /*
     * 这里的整个协程的核心
     * 要初始化新创建的栈，并初始化切换到新协程时要执行的函数
    **/
    frame = (frame_t *)co->stack;
    frame--;
    memset(frame, 0, sizeof(frame_t));
    frame->ret = (uint64_t)__new;  /* 核心中的核心 */
    co->rsp = (uint64_t)frame;
    return 0;
}

//返回当前协程id
int coid()
{
    return current->id;
}