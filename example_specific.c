#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

#include "co.h"

static int key1;
static int key2;
static int key3;

void f1(void *unused)
{
    void *value = (void *)(unsigned long)coid();
    printf("myid = %d\n", coid());
    
    co_setspecific(key1, value);
    co_setspecific(key2, value+1);
    co_setspecific(key3, malloc(sizeof(int)));
    
    yield();
    
    printf("key1 %u\n", co_getspecific(key1));
    printf("key2 %u\n", co_getspecific(key2));
    printf("key3 %016llx\n", co_getspecific(key3));
}

void f(void *unused)
{
    int i=5;
    key1 = co_key_create(NULL);
    key2 = co_key_create(NULL);
    key3 = co_key_create(free);
    printf("%d %d %d\n", key1, key2, key3);
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(16*1024, f1, NULL);
        i--;
        yield();
    }
}

void main()
{
    cocreate(16*1024, f, NULL);
    while(coloop());
}
