#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "compiler.h"
#include "rbtree.h"
#include "list.h"
#include "co.h"
#include "co_inner.h"


void __call()
{
    co_t *current = coself();
    co_t *parent = current->parent;
    //调用协程函数
    current->func(current->data);
    //通过exit字段标识协程执行完毕
    current->exit = 1;
    //把父协程插入运行队列
    list_replace_init(&current->rq_node, &parent->rq_node);
    //返回到父协程，在__switch_to函数中会把子协程销毁
    return_to(current, parent);
}

void cocall(int stack_size, co_routine f, void *d)
{
    static void *share_stack = NULL;
    co_t co_on_stack;
    frame_t parent_frame;
    frame_t *frame;
    co_t *co = &co_on_stack;
    co_t *parent = coself();
    
    if(unlikely(parent->autostack)) {
        f(d);
        return;
    }
    
    //分配新的协程co_t,并加入init队列中
    co->id = parent->id;
    co->func = f;
    co->data = d;
    co->exit = 0;
    co->autostack = 0;
    co->mmapstack = 0;
    co->type = 1;
    co->sharestack = 0;
    co->parent = parent;
    co->specific = NULL;
    co->spec_num = 0;
    if(unlikely(share_stack == NULL)) {
        share_stack = memalign(getpagesize(), 1024*1024);
    }
    if(parent->type == 1 && parent->sharestack == 1) {
        /*
         * 3.父协程是call类型，且父协程是共享栈，子协程必定共享1M share_stack
        **/
        co->stack_size = 0;
        co->rsp = 0;
        co->stack = share_stack;
        co->sharestack = 1;
    } else if(stack_size == SHARESTACK) {
        /*
         * 1.父协程是create类型，直接使用1M share_stack
         * 2.父协程是call类型，且父协程是独占栈，直接使用1M share_stack
         * 3.父协程是call类型，且父协程是共享栈，和父协程共享1M share_stack
        **/
        // 1. 2.
        co->stack_size = 1024*1024;
        co->rsp = (unsigned long)share_stack + co->stack_size;
        co->stack = share_stack;
        co->sharestack = 1;
    } else {
        /*
         * 1.父协程是create类型，直接创建新独占栈
         * 2.父协程是call类型，且父协程是独占栈，直接创建新独占栈
         * 3.父协程是call类型，且父协程是共享栈，和父协程共享1M share_stack
        **/
        // 1. 2.
        co->stack = memalign(getpagesize(), stack_size);
        co->rsp = (unsigned long)co->stack + stack_size;
        co->stack_size = stack_size;
    }
    
    /*
     * 把子协程加入到父协程的尾部，
     * 把父协程从运行队列删除。
     * 子协程不放入红黑树。
    **/
    list_replace_init(&parent->rq_node, &co->rq_node);
    
    /*
     * 调度执行子协程, 子协程执行完后返回到call_to中
    **/
    call_to(parent, co);
}

