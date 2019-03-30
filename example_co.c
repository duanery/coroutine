#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "co.h"

void f1(void *unused)
{
    struct timeval tv, tv1;
    printf("myid = %d\n", coid());
    gettimeofday(&tv, NULL);
    cousleep(coid()*10000);
    gettimeofday(&tv1, NULL);
    printf("cousleep %d us\n", (tv1.tv_sec - tv.tv_sec)*1000000+tv1.tv_usec - tv.tv_usec);
}

void f(void *unused)
{
    int i=5;
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(16*1024, f1, NULL);
        i--;
        schedule();
    }
}

void main()
{
    cocreate(16*1024, f, NULL);
    while(coloop());
}
