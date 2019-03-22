main:switch_to.S sched.c main.c rbtree.c syscall.c event.c
	gcc -g -O2 -std=gnu99 $^ -o $@ -lrt
