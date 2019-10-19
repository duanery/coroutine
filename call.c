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

/*
 * ● cocreate 创建的非AUTOSTACK协程, schedule()切换不保存栈
 * ○ cocreate 创建的  AUTOSTACK协程, schedule()切换  保存栈
 * ☼ cocall   创建的  SHARESTACK协程, 不允许调用schedule()切换
 * ❶ cocall   创建的非SHARESTACK协程,   允许调用schedule()切换
 * 各种协程之间的关系图：
 *      ●
 *   ┏━┯┷┯━┓
 *   ● ○ ☼ ❶
 *   √ √ √ √ 全部都可以正常创建
 *
 *      ○
 *   ┏━┯┷┯━┓
 *   ● ○ ☼ ❶
 *   √ √ √ ✘
 *   ○→❶ 不允许创建，❶的co_t创建在○的栈上，而○的栈在CO_STACK_BOTTOM位置，
 *   cocall中把○从运行队列中删除，❶加入运行队列，之后不可能再调度到○中，
 *   但cocall的call_to经过特殊优化，没有调用__switch_stack没办法保存○的栈。
 *   即使调用__switch_stack保存○的栈，但在❶中允许调用schedule()切换出去，
 *   于是切换到其他○1协程，在__switch_stack中会把○协程的保存下来，于是就破坏
 *   了❶的co_t的结构体。
 *   最根本的原因是❶中允许schedule()切换，如果❶不会调度，即使call_to不调用
 *   __switch_stack，也不会出问题。○会占用co_stack_bottom；而❶会占用share_stack
 *   ❶执行返回，释放share_stack；○继续占用co_stack_bottom。
 *   所以 ○→☼ 允许创建。
 *
 *      ☼
 *   ┏━┯┷┯━┓
 *   ● ○ ☼ ❶→☼
 *   √ √ √ √
 *   ☼→❶ 会被转换成 ❶→☼ ：因为❶一旦切换出去，但☼还在占用share_stack，意味着
 *   其他协程有机会调用cocall创建☼1来再次占用share_stack。
 *
 *      ❶
 *   ┏━┯┷┯━┓
 *   ● ○ ☼ ❶
 *   √ √ √ √ 全部都可以正常创建 ❶跟●没什么区别
**/
void cocall(int stack_size, co_routine f, void *d)
{
    static void *share_stack = NULL;
    co_t co_on_stack;
    co_t *co = &co_on_stack;
    co_t *parent = coself();
    frame_t *frame;
    
    // ○→❶
    if(unlikely(parent->autostack && stack_size != SHARESTACK)) {
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
    co->top_parent = parent->type == 1 ? parent->top_parent : parent;
    co->specific = NULL;
    co->spec_num = 0;
    if(unlikely(share_stack == NULL)) {
        share_stack = memalign(getpagesize(), 1024*1024);
    }
    if(parent->type == 1 && parent->sharestack == 1) {
        /*
         * 3.父协程是call类型，且父协程是共享栈，子协程必定共享1M share_stack
         * ☼→☼ ☼→❶
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
         * ●→☼ ○→☼ ❶→☼
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
         * ●→❶ ❶→❶
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

