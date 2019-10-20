#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

#include "co.h"
#include "glibc.h"

void test__cocreate(void *unused)
{
    printf("== cocreate %d\n", coid());
}
void test__cocreate_cocall(void *unused)
{
    printf("== cocall %d\n", coid());
}
void test_cocreate()
{
    cocreate(16*1024, test__cocreate, NULL);
    cocreate(DEFAULT_STACK, test__cocreate, NULL);
    cocreate(AUTOSTACK, test__cocreate, NULL);
    cocall(SHARESTACK, test__cocreate_cocall, NULL);
    cocall(16*1024, test__cocreate_cocall, NULL);
}

int fib(int n)
{
	if(n == 1 || n == 2) //第一个和第二个数均为1
		return 1;
	else {
		return fib(n-2) + fib(n-1);
	}
}

void call(void *arg)
{
    int n = (int)(long)arg;
    printf("co %d fib(%d) = %d\n", coid(), n, fib(n));
    test_cocreate();
}

void call1(void *arg)
{
    int n = (int)(long)arg;
    printf("co %d fib1(%d) = %d\n", coid(), n, fib(n));
    schedule();
    printf("co %d fib1(%d) = %d\n", coid(), n, fib(n));
}

void test__cocall(void *fib)
{
    cocall(SHARESTACK, call, fib);
    cocall(DEFAULT_STACK, call1, fib+1);
    cocall(SHARESTACK, call1, fib+2);
    cocall(DEFAULT_STACK, call, fib+3);
}

void main()
{
    unsigned long i;
    for(i=10; i<20; i++) {
        cocreate(16*1024, test__cocall, (void *)i);
        cocreate(DEFAULT_STACK, test__cocall, (void *)i);
        cocreate(AUTOSTACK, test__cocall, (void *)i);
    }
    while(coloop());
    int len = co_printf("end fib(20) = %d\n", fib(20));
    printf("len = %d\n", len);
}

