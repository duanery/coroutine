#ifndef __CO_INNER__
#define __CO_INNER__

#include <stdint.h>

typedef struct stack_frame {
#if defined(__x86_64__)
    unsigned long r15;
    unsigned long r14;
    unsigned long r13;
    unsigned long r12;
    unsigned long rbx;
    unsigned long rbp;
#elif defined(__i386__)
    unsigned long esi;
    unsigned long edi;
    unsigned long ebx;
    unsigned long ebp;
#elif defined(__aarch64__)
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long x29;
#endif
    unsigned long ret;
}frame_t;

typedef struct co_struct {
    unsigned long rsp;
    void *stack;
    int stack_size;
    unsigned long id;
    int shmfd;
    uint32_t exit : 1;
    uint32_t autostack : 1;
    uint32_t mmapstack : 1;
    uint32_t type: 1;
    uint32_t sharestack : 1;
    struct list_head rq_node;
    union {
        struct rb_node rb;
        struct {
            struct co_struct *parent;
            struct co_struct *top_parent;
        };
    };
    struct co_struct *child;
    co_routine func;
    void *data;
    void **specific;
    int spec_num;
}__cacheline_aligned co_t;

typedef struct co_specific {
    uint32_t used : 1;
    void (*destructor)(void*);
}specific_t;

struct co_info {
    unsigned long max_stack_consumption;
    unsigned long co_num;
};

extern asmlinkage void switch_to(co_t *, co_t *);
extern asmlinkage void call_to(co_t *, co_t *);
extern asmlinkage void return_to(co_t *, co_t *);
#endif
