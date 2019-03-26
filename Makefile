SRC := switch_to.S sched.c main.c rbtree.c syscall.c event.c
HEADER := co.h  compiler.h  list.h  rbtree.h

main: $(SRC) $(HEADER) Makefile
	gcc -g -O2 -std=gnu99 $(SRC) -o $@ -lrt

main32: $(SRC) $(HEADER) Makefile
	gcc -g -m32 -O2 -std=gnu99 $(SRC) -o $@ -lrt

clean:
	@rm main main32
