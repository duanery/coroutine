# 最简协程实现	

参考Linux内核x86_64位体系结构中进程切换原理，实现的最简协程。

## 编译

键入make命令，可以编译出一个main程序。运行main程序获得结果。

- 交叉32位程序。`make ARCH=i386`
- 编译arm64位程序。`make CROSS_COMPILE=aarch64-linux-gnu-`

## 原理

### 1.相关结构体

```c
typedef struct stack_frame {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t ret;
}frame_t;

typedef void (*co_func)(void *);
typedef struct co_struct {
	uint64_t rsp;
	void *stack;
	int id;
	int exit;
	co_func func;
	void *data;
	struct co_struct *next;
}co_t;
```

`frame_t` 定义新建协程的初始化栈的结构。

`co_t` 定义一个协程，rsp字段用于保存协程的栈顶指针，stack保存协程的栈底指针，id表示协程id，exit表示协程退出，func是协程的处理函数，data是给func的参数，next指向下一个协程。

### 2.初始化

进程运行时，默认只有一个主线程，主线程的地址空间及栈由操作系统负责初始化了。

那这个主线程的栈，也同时会作为init协程的栈。

```c
// 初始协程, 标识主线程
static co_t init = {0,0,0,0,0,0,NULL};

//current 标识当前协程co_t
co_t *current=&init;
```

定义一个init协程，这里只需要把co_t的所有字段初始化为空即可，不需要初始化rsp及stack等字段，init协程的id=0。

current标识当前的协程，在初始化时，把current设置为init协程，那么在进程的main函数执行时，就可以使用current来读取协程id。

### 3.协程切换

协程使用类似Linux内核进程切换的接口：`switch_to(co_t *prev, co_t *next);`

```asm
.globl switch_to
switch_to:
	/*
	 * Save callee-saved registers
	 * This must match the order in inactive_task_frame
	 */
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* switch stack */
	movq	%rsp, 0(%rdi)
	movq	0(%rsi), %rsp

	/* restore callee-saved registers */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	jmp	__switch_to
```

switch_to中先保存rbp到r15寄存器到当前栈上，当前栈也是prev协程的栈。

当前栈指的是：

- 如果当前是init协程，其当前栈就是进程中主线程的栈。
- 如果当前不是init协程，其当前栈就是协程自身的栈(co_t::stack)。

切换栈：

- 如果当前是init协程，则把rsp保证到init协程的`co_t::rsp`字段。
- 如果当前不是init协程，则把rsp保存到当前协程的`co_t::rsp`字段

然后把next协程的栈恢复。

- 如果下一个要运行的协程是init协程，则从init协程的`co_t::rsp`字段找到该协程的栈。
- 如果下一个要运行的协程不是init协程，则从next协程的`co_t::rsp`字段找到该协程的栈。

最后从next协程的栈上，恢复r15到rbp寄存器。

跳入\_\_switch_to。

```c
void __switch_to(co_t *prev, co_t *next)
{
    //赋值current, 切换当前协程
    current = next;
    //如果前一个协程执行完毕，则释放前一个协程的数据
    if(prev->exit) {
        co_t *c = &init;
        while(c->next != prev) c = c->next;
        c->next = prev->next;
        free(prev);
    }
}
```

`__switch_to`主要是执行一些清理工作，并修改current指向的协程。

切换到next协程后，就完全不会再使用prev协程的栈了，于是可以判断prev协程是否执行完毕，执行完毕，可以完全释放prev协程的结构及栈。

#### \_\_switch_to的返回

- 对应新创建的协程，\_\_switch_to返回后，会进入__new函数。
- 非新创建的协程，\_\_switch_to返回后，会返回到schedule()函数中，执行switch_to函数后面的一条指令。

为什么呢？可以参考新建协程部分的内容。

### 4.调度

```c
int schedule()
{
    /*
     * 选择下一个协程
     * 参考Linux内核的话，可以定义协程队列，并对每个协程定义优先级，
     * 在选择时，可以选择优先级高的协程先执行。
     * 这里最简处理。
    **/
    co_t *next = current->next;
    if(!next)
        next = &init;
    //协程切换
    switch_to(current, next);
    return (init.next != NULL);
}
```

最简单的是从选择current协程的下一个协程开始运行，执行switch_to切换到next协程。

schedule函数的返回值：1,还有其他协程，0,协程全部执行完毕。通过返回值可以确定main函数是否该退出了。

选择下一个协程执行时，决定了协程的类型：对称协程还是非对称协程。随机选择下一个协程意味着是对称协程，依据协程调用关系来选择下一个协程意味着是非对称协程。

目前实现的是对称协程。

### 5.创建新协程

```c
static void __new()
{
    //调用协程函数
    current->func(current->data);
    //通过exit字段标识协程执行完毕
    current->exit = 1;
    //调度，切换到下一个协程
    schedule();
}

int cocreate(int stack_size, co_func f, void *d)
{
    static int co_id = 1;
    frame_t *frame;
    //分配新的协程co_t,并加入init队列中
    co_t *co = malloc(sizeof(co_t) + stack_size);
    co->stack = (void *)(co + 1);
    co->stack += stack_size;
    co->id = co_id++;
    co->exit = 0;
    co->func = f;
    co->data = d;
    co->next = init.next;
    init.next = co;
    
    /*
     * 这里是整个协程的核心
     * 要初始化新创建的栈，并初始化切换到新协程时要执行的函数
    **/
    frame = (frame_t *)co->stack;
    frame--;
    memset(frame, 0, sizeof(frame_t));
    frame->ret = (uint64_t)__new;  /* 核心中的核心 */
    co->rsp = (uint64_t)frame;
    return 0;
} 
```

通过cocreate创建新协程。只需要指定（栈大小，协程函数，参数）这三个参数即可。

1. 分配co_t结构及栈。
2. 初始化co_t结构，id字段使用持续递增的唯一id。
3. 加入协程链表，表头是init协程。
4. 初始化新协程的栈。主要是初始化rbp到r15的寄存器及frame->ret。

`frame->ret`是新创建协程开始运行的地方。

主要考虑，通过switch_to开始切换到新建的协程时的运行过程。

switch_to开始先保存rbp-r15寄存器到当前协程栈上，然后把rsp放到prev协程的co_t::rsp字段。然后把新建协程的co_t::rsp字段的值作为当前栈，然后弹出到r15-rbp寄存器。从源码可以看到新建协程的栈被清空了，所有r15-rbp都被初始化为0，此时新建协程的栈上还保留一个地址，就是frame->ret的值。

然后通过jmp指令跳入\_\_switch_to，当该函数返回时，就会弹出栈上的地址到rip，开始执行。可以看到\_\_switch_to返回后即开始执行\_\_new函数。

也即，开始执行新的协程。

### 6.协程回收

__new是新协程开始执行的地方，会调用协程的func函数，func执行结束意味着协程也执行完了，会标记co_t::exit字段为1，然后调用schedule，协程再也不会返回。

参考_\_switch_to函数，在切换到next协程时，会判断prev->exit字段，并回收前一个执行完毕的协程。动态回收。

### 7.协程id

```c
//返回当前协程id
int coid()
{
    return current->id;
}
```

通过coid返回协程的id，类似pthread_self。

### 8.main函数模型

```c
void main()
{
    cocreate(128*1024, f, NULL);
    while(schedule()) ;
}
```

在main函数中主要通过cocreate创建足够的协程后，通过while循环不断的进行调度即可，当schedule返回0时，意味着全部协程执行完了，进程退出。

### 9.主线程和init协程之间的身份互换

main函数执行时，可以认为是运行在主线程中，也可以认为是运行在init协程中。

main函数运行完意味着所有的协程(除了init协程)全部执行完，main函数退出意味着主线程退出和init协程退出，只是init协程退出进程结束，而不是切换到其他协程。

init协程是个特殊的存在，不需要动态分配栈，栈利用了主线程的栈。不需要协程处理函数，处理函数默认是main函数。init协程和其他协程相同点，只是有一个栈顶指针（co_t::rsp），用于init协程切换到其他协程时保存栈顶指针，从其他协程切换回init协程时恢复rsp寄存器。

init协程是其他协程的管理员，其他协程执行完最终控制权会回到init协程，init协程还可以做一些别的事情，再把控制权交回到协程中。如init协程拿回控制权之后，可以执行event事件轮训，生成其他协程。协程可以和事件处理模型相互融合。

## 更进一步

### 1.系统调用

可以把`read,write,sendmsg,connect,sleep`等阻塞的系统调用，全部实现为非阻塞版本，当这些系统调用返回EAGAIN时，立即调用schedule函数，调度到下一个协程上执行。

那么这些系统调用就会变成轮训方式，直到不再返回EAGAIN为止。是通过执行其他协程的方式来等待某文件描述符可以读，不再返回EAGAIN。

### 2.调度队列

可以实现特定优先级方式的调度队列，而不只是一个单链表。

### 3.其他体系结构

可以参考Linux内核中其他体系结构的switch_to的代码。

目前已支持`i386,x86_64,aarch64`.

### 4.协程退出状态

协程函数执行完，可以返回一个数值，作为协程的退出状态。

### 5.协程kill

不等协程执行完就把协程kill掉。

已支持。

### 6.协程等待队列

把协程从运行队列中移除，并挂入协程等待队列中，或者仅仅是保存co_t到特定位置，在适当的时机可以唤醒某个协程。

已支持。

### 7.每线程协程

可以设计个多线程应用，但为每个线程都设定一个协程执行空间。以及协程在线程之间互相迁移。

### 8.协程与事件

协程和epoll事件轮询可以共存。

epoll轮询的问题：当epoll返回一个套接字可以读，去读的时候只能读到一部分数据，此时就必须保持读取到的数据及位置，然后返回，再次epoll等待该套接字可以读，然后再次执行一遍刚才的过程，直到读到足够的数据之后才能进行后续处理。这里的问题是，再次执行一遍数据读取的过程会有一定的消耗。

如果利用协程，创建套接字时同时为套接字创建协程，在epoll返回一个套接字可以读时，在协程中执行读操作，读的时候只读到一部分数据，此时把协程从运行队列移除，并调度执行其他协程。当epoll等待该套接字再次可以读时，此时仅仅恢复协程即可。就可以省掉再次执行数据读取的过程。

### 9.协程栈的自动扩容

#### 1）基于信号实现

1. 分配协程栈，把协程栈最后一个字节所在的页设置为不可读写（使用mprotect来设置）
2. 当协程栈使用到这个不可读写的页时，会触发**SIGSEGV**信号。
3. 为**SIGSEGV**信号建立独立的信号处理栈（使用sigaltstack来处理）
4. 在**SIGSEGV**信号处理程序内部，获取pagefault的内存地址，然后判断内存地址是否是在当前协程的栈内部。
   - 如果在当前协程的栈内部，则为协程分配更大的栈，然后把原先的栈copy过来，并且需要把新的栈顶指针赋值到ucontext::sigcontext::rsp,rbp中，确保信号处理程序返回到协程时可以使用新的栈。还需要进行栈回溯，把所有栈帧中的保存的rbp全部切换成新的栈值。
   - 如果不在当前协程的栈内部，则exit(128+SIGSEGV);
   - 请参考`man sigaction`建立**SIGSEGV**信号的信号处理程序。

该方案的缺陷：

1. 不能够在栈上动态分配对象，并把对象挂入到其他链表里。这样在切换栈之后，链表就引用旧栈的数据了。
2. 不能够在栈上申请很大的数组。
3. 不能使用-O1,-O2等优化编译

[实现源码](https://github.com/duanery/coroutine/tree/2354b31559df7c863ac2659ecad493e5164b4e5b)

#### 2）基于copy实现

1. 初始化时申请一个大小为4M的公共栈空间。
2. 协程切换时，把栈上的数据拷贝到prev协程的栈空间上，把next协程的栈拷贝到公共栈空间上。
3. 在把栈上的数据拷贝到prev协程的栈空间时，如果超过了prev协程目前的栈空间，则需要扩容prev协程栈。分配新栈，释放旧栈。

该方案缺陷：

1. 需要把栈拷贝出去，再拷贝回来。但不需要执行栈回溯，可以使用-O1,-O2等优化选项。
2. 随着栈越来越大，copy的性能越来越低。随着栈扩大，copy的性能没有下限。

#### 3）基于mmap实现

1. 初始化时使用mmap申请一个固定大小为4M的栈空间，假定映射在地址A。
2. 协程创建时，使用shm_open来打开并创建一个4k大小的栈，保存fd，及栈大小到co_t。（主要是利用tmpfs来创建协程栈）
3. 协程切换时，把next协程的栈固定映射到地址A，MAP_FIXED。会自动释放prev在地址A处的映射。

该方案的缺陷：

1. 在栈比较小时，性能比copy的性能差。随着栈的扩大，mmap性能会下降到一个下限，不再继续下降。不需要执行栈回溯，可以使用-O1,-O2等优化选项。

#### 目前实现

采用了copy和mmap相互结合的方式。

### 10.协程同步

互斥锁。读写锁。协程队列。

## 新增

### 1.协程事件

利用epoll实现事件，但事件的处理都在协程内部执行。一般用于长连接的套接字。协程事件利用的是ET，进一步减少epoll的无效轮询。

### 2.系统调用

新增`cosleep,cousleep,conanosleep,coread,coread1,cowrite,coaccept`等在协程内部使用的系统调用。

### 3.遗留问题

1. 等待一个协程执行完。

## ARM测试

### 在x86_64环境下测试arm64位程序

1. 从libaro官网获得交叉编译工具。<https://releases.linaro.org/components/toolchain/binaries/latest-7/aarch64-linux-gnu/>

   ```bash
   mkdir -p /home/arm64
   cd /home/arm64
   xz -d gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu.tar.gz
   tar -xvf gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu.tar
   export PATH=$PATH:/home/arm64/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/bin
   ```

2. 编译qemu-aarch64。下载qemu源码，进入qemu目录。

   ```bash
   ./configure --target-list=aarch64-linux-user
   make && make install
   ```

   安装后可获得qemu-aarch64程序，通过这个程序可以直接执行arm64位程序，而不需要arm64位虚拟机。

3. 编译协程例子。下载本demo代码，编译。

   ```bash
   git clone git@github.com:duanery/coroutine.git
   cd coroutine
   make CROSS_COMPILE=aarch64-linux-gnu-
   ```

4. 测试

   ```bash
   qemu-aarch64 -L /home/arm64/gcc-linaro-7.4.1-2019.02-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc/ ./example_co
   ```

   通过-L参数来指定arm64体系结构解释器的路径。

### arm64环境下测试

​	未验证。