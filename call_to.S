#include "co_offsets.h"

.text
.globl call_to
call_to:

#if defined(__i386__)
/*
 * %eax: prev co
 * %edx: next co
 */
    /*
     * Save callee-saved registers
     * This must match the order in struct stack_frame
     */
    pushl   %ebp
    pushl   %ebx
    pushl   %edi
    pushl   %esi
    
    /* save prev stack */
    movl    %esp, CO_rsp(%eax)
    
    cmpl    $0, CO_rsp(%edx)           //  if (next->esp != 0)
    jne 1f                             //      goto _1f;
    movl    %esp, %ebx                 //  %ebx = %esp;
    subl    CO_stack(%edx), %ebx       //  %ebx -= next->stack;
    movl    %ebx, CO_stack_size(%edx)  //  next->stack_size = %ebx;
    movl    %esp, CO_rsp(%edx)         //  next->rsp = %esp;
1:                                     // _1f:
    /* restore next stack */           //
    movl    CO_rsp(%edx), %esp         //  %esp = next->rsp;
    pushl   $__call                    //  *(long*)(++esp) = __call;
    jmp     __switch_to                //  goto __switch_to;
    
#elif defined(__x86_64__)
/*
 * %rdi: prev co
 * %rsi: next co
 */
    /*
     * Save callee-saved registers
     * This must match the order in stack_frame
     */
    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    
    /* save prev stack */
    movq    %rsp, CO_rsp(%rdi)
    
    cmpl    $0, CO_rsp(%rsi)           //  if (next->esp != 0)
    jne     1f                         //      goto _1f;
    mov     %rsp, %rbx                 //  %rbx = %rsp;
    sub     CO_stack(%rsi), %rbx       //  %ebx -= next->stack;
    mov     %rbx, CO_stack_size(%rsi)  //   next->stack_size = %ebx;
    mov     %rsp, CO_rsp(%rsi)         //  next->rsp = %esp;
1:                                     // _1f:
    /*  restore next stack  */         //
    mov     CO_rsp(%rsi), %rsp         //  %esp = next->rsp;
    pushq   $__call                    //  *(long*)(++esp) = __call;
    jmp     __switch_to                //  goto __switch_to;
    
#endif