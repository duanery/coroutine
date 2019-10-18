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
    movl    %esp, CO_rsp(%eax)         //  prev->rsp = %rsp;
    
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

.global return_to
return_to:
/*
 * %eax: prev co
 * %edx: next co
 */
    movl    CO_rsp(%edx), %esp
    
    /* restore callee-saved registers */
    popl    %esi
    popl    %edi
    popl    %ebx
    popl    %ebp
    
    jmp    __switch_to
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
    movq    %rsp, CO_rsp(%rdi)         //  prev->rsp = %rsp;
    
    cmpl    $0, CO_rsp(%rsi)           //  if (next->esp != 0)
    jne     1f                         //      goto _1f;
    movq    %rsp, %rbx                 //  %rbx = %rsp;
    subq    CO_stack(%rsi), %rbx       //  %ebx -= next->stack;
    movq    %rbx, CO_stack_size(%rsi)  //   next->stack_size = %ebx;
    movq    %rsp, CO_rsp(%rsi)         //  next->rsp = %esp;
1:                                     // _1f:
    /*  restore next stack  */         //
    movq    CO_rsp(%rsi), %rsp         //  %esp = next->rsp;
    pushq   $__call                    //  *(long*)(++esp) = __call;
    jmp     __switch_to                //  goto __switch_to;

.global return_to
return_to:
/*
 * %rdi: prev co
 * %rsi: next co
 */
    movq    CO_rsp(%rsi), %rsp
    
    /* restore callee-saved registers */
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    
    jmp    __switch_to
#endif