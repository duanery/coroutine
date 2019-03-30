#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "co.h"

#define MAXRAND 400
//递归消耗栈
void buf(int loop)
{
    uint8_t buff[512];
    int i = 0;
    int k = 0;
    uint8_t c = (uint8_t)loop;
    if(c == 0) c = 1;
    for(;i<512;i++)
        buff[i] = c;
    if(loop == 0) {
        schedule();   //在栈消耗最大处切换协程
        schedule();
        return;
    }
    buf(loop-1);
    for(i=0;i<512;i++) {
        if(buff[i] != c) k++;
    }
    if(k)
        printf("coid %d, buff %d, %d\n", coid(), buff[0], k);
}

void autostack(void *d)
{
    int loop = rand() % MAXRAND;
    buf(loop);
    buf(loop);
}

void main()
{
    int i=0;
    srand(time(0));
    for(; i<MAXRAND; i++)
        cocreate(AUTOSTACK, autostack, NULL);
    while(coloop());
}
