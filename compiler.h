#ifndef __COMPILE_H__
#define __COMPILE_H__

#include <stddef.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define container_of(ptr, type, member) ({			\
    const typeof(((type *)0)->member) *__mptr = (ptr);	\
    (type *)((char *)__mptr - offsetof(type, member)); })

#define __init __attribute__((constructor))

#define SMP_CACHE_BYTES 64
#define __cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))

#define __LOCAL(var, line) __ ## var ## line
#define _LOCAL(var, line) __LOCAL(var, line)
#define LOCAL(var) _LOCAL(var, __LINE__)

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)


#if defined(__i386__)
    #define asmlinkage __attribute__((regparm(3)))
#else
    #define asmlinkage
#endif

/*
 * Compares two integer values
 *
 * If the first argument is larger than the second one, intcmp() returns 1.  If
 * two members are equal, returns 0.  Otherwise, returns -1.
 */
#define intcmp(x, y) \
({					\
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x < _y ? -1 : _x > _y ? 1 : 0;	\
})

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))


#endif /* __COMPILE_H__ */