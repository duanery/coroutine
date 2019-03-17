main:switch_to.S sched.c main.c rbtree.c syscall.c event.c
	gcc -g -std=gnu99 $^ -o $@
