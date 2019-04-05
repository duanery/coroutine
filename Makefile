SRC := switch_to.S sched.c rbtree.c syscall.c event.c
HEADER := co.h  compiler.h  list.h  rbtree.h
OBJS := main example_echo example_signalfd example_autostack example_co example_specific example_teststack

CFLAGS += -g -O2 -std=gnu99 -Wp,-MMD,.$(notdir $@).d

ifeq ($(ARCH), i386)
    CFLAGS += -m32
    ASFLAGS += -m32
endif

all:$(OBJS)
	@:

-include $(foreach o, $(OBJS) $(addsuffix .o,$(basename $(SRC))), .$(notdir $o).d)

$(OBJS) : libco.a
$(OBJS) : % : %.c
	$(CC) $(CFLAGS) -o $@ $< -L. -lco -lrt

libco.a : Makefile
libco.a : $(addsuffix .o,$(basename $(SRC)))
	$(AR) rcs $@ $(filter %.o,$^)

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<
%.o : %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

clean:
	@rm -f $(OBJS) *.o *.a .*.d
