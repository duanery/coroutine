#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <alloca.h>
#include <sys/time.h>

#include "compiler.h"
#include "co.h"

static int g_verbose = 0;
static int g_loop = 100;
static int g_us = 0;

unsigned long long counter(void)
{
    register uint32_t lo, hi;
    register unsigned long long o;
    __asm__ __volatile__ (
            "rdtscp" : "=a"(lo), "=d"(hi)::"%rcx"
            );
    o = hi;
    o <<= 32;
    return (o | lo);
}

#define debug(...) \
    if(g_verbose) printf(__VA_ARGS__);

#define C(exp) do{      \
    unsigned long long c1;  \
    c1 = counter();         \
    exp;                    \
    debug(" %"PRIu64, counter()-c1); \
}while(0);

#define STEP 4096

void test_mmap_stack(void *d)
{
    size_t i, j, size;
    uint8_t *stack;
    
    size = (size_t)d - (CO_STACK_BOTTOM - (size_t)&stack);
    stack = alloca(size);
    
    debug("coid %d, ", coid());
    for(i=0; i<size; i+=STEP) { C(stack[i] = 5); }
    debug("\n");
    
    for(j = 0; j<g_loop; j++) {
        schedule();
        
        debug("coid %d, ", coid());
        for(i=0; i<size; i+=STEP) { C(stack[i] = 5); }
        debug("\n");
    }
}

void test_copy_stack(void *d)
{
    size_t i, j, size;
    uint8_t *stack;
    
    size = (size_t)d - (CO_STACK_BOTTOM - (size_t)&stack) - 128;
    stack = alloca(size);
    
    debug("coid %d, ", coid());
    for(i=0; i<size; i+=STEP) { C(stack[i] = 5); }
    debug("\n");
    
    for(j = 0; j<g_loop; j++) {
        schedule();
        
        debug("coid %d, ", coid());
        for(i=0; i<size; i+=STEP) { C(stack[i] = 5); }
        debug("\n");
    }
}

void main(int argc, char *argv[])
{
    int i, n = 100, copy = 0;
    int stack_size = COPY_STACK;
    struct timeval tv, tv1;
    unsigned long long c1, c2;
    int opt;
    
    while ((opt = getopt(argc, argv, "vn:l:uch")) != -1) {
        switch (opt) {
        case 'v':
            g_verbose = 1;
            break;
        case 'n':
            n = atoi(optarg);
            break;
        case 'l':
            g_loop = atoi(optarg);
            break;
        case 'u':
            g_us = 1;
            break;
        case 'c':
            copy = 1;
            break;
        default: /* '?' */
        case 'h':
            fprintf(stderr, "Usage: %s [-v] [-n co_num] [-l loop] [-u] [-c] [stack_size]\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if(optind < argc) {
        COPY_STACK = stack_size = atoi(argv[optind]);
    }
    printf("COPY_STACK %d\n", COPY_STACK);
    
    
    if(copy == 1) goto _copy;
    
    //MMAP test
    for(i=0; i<n; i++)
        cocreate(AUTOSTACK, test_mmap_stack, (void*)(size_t)stack_size);
    if(g_us) {
        gettimeofday(&tv, NULL);
        while(coloop());
        gettimeofday(&tv1, NULL);
        printf("mmap_stack cost %u us\n", (tv1.tv_sec - tv.tv_sec)*1000000 + tv1.tv_usec - tv.tv_usec);
    }else{
        c1 = counter();
        while(coloop());
        c2 = counter();
        printf("mmap_stack cost %u cycle\n", c2 - c1);
    }
    goto _end;
    return;


_copy:    
    //COPY test
    for(i=0; i<n; i++)
        cocreate(AUTOSTACK, test_copy_stack, (void*)(size_t)stack_size);
    if(g_us) {
        gettimeofday(&tv, NULL);
        while(coloop());
        gettimeofday(&tv1, NULL);
        printf("copy_stack cost %u us\n", (tv1.tv_sec - tv.tv_sec)*1000000 + tv1.tv_usec - tv.tv_usec);
    }else{
        c1 = counter();
        while(coloop());
        c2 = counter();
        printf("copy_stack cost %u cycle\n", c2 - c1);
    }


    extern struct co_info {
            unsigned long max_stack_consumption;
            unsigned long co_num;
    } co_info;
_end:
    printf("max_stack_consumption %u co_num %u\n", co_info.max_stack_consumption, co_info.co_num);
}
