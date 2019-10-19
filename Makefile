SRC := switch_to.S sched.c rbtree.c syscall.c event.c call_to.S call.c
HEADER := co.h  compiler.h  list.h  rbtree.h co_inner.h
OBJS := main example_echo example_signalfd example_autostack example_co example_specific example_teststack

CFLAGS += -g -O2 -std=gnu99 -Wp,-MMD,.$(notdir $@).d

ifeq ($(ARCH), i386)
    CFLAGS += -m32
    ASFLAGS += -m32
endif
CC		= $(CROSS_COMPILE)gcc
AR		= $(CROSS_COMPILE)ar
STRIP   = $(CROSS_COMPILE)strip

debug:$(OBJS)
	@:

release: $(OBJS)
	@$(STRIP) $^

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

switch_to.S call_to.S : co_offsets.h

co_offsets.h : autogen
	./autogen > $@

clean:
	@rm -f $(OBJS) *.o *.a .*.d co_offsets.h autogen
