.text
.globl switch_to
switch_to:

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
    movl    %esp, 0(%eax)
    
    /* switch stack */
    movl    $__switch_stack_end, %esp
    pushl   %eax
    pushl   %edx
    call    __switch_stack
    
    /* restore next stack */
    popl    %edx
    popl    %eax
    movl    0(%edx), %esp
    
    /* restore callee-saved registers */
    popl    %esi
    popl    %edi
    popl    %ebx
    popl    %ebp
#elif defined(__x86_64__)
/*
 * %rdi: prev task
 * %rsi: next task
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
    movq    %rsp, 0(%rdi)
    
    /* switch stack */
    leaq    __switch_stack_end(%rip), %rsp
    pushq   %rsi
    pushq   %rdi
    call    __switch_stack
    
    /* restore next stack */
    popq    %rdi
    popq    %rsi
    movq    0(%rsi), %rsp

    /* restore callee-saved registers */
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
#endif

    jmp    __switch_to

.data
.balign 4096
    .fill 4096,1,0
__switch_stack_end:
