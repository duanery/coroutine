#include <stdio.h>


void f1(void *unused)
{
    printf("myid = %d\n", coid());
}

void f(void *unused)
{
    int i=10;
    while(i>0) {
        printf("i=%d\n", i);
        cocreate(128*1024, f1, NULL);
        i--;
        schedule();
    }
}

void main()
{
    cocreate(128*1024, f, NULL);
    while(schedule()) ;
}
