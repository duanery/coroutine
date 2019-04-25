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
    
    jmp    __switch_to
#elif defined(__aarch64__)
/*
 * x0: prev co
 * x1: next co
 */
    sub sp, sp, 0x60
    stp x19, x20, [sp, 0x00]
    stp x21, x22, [sp, 0x10]
    stp x23, x24, [sp, 0x20]
    stp x25, x26, [sp, 0x30]
    stp x27, x28, [sp, 0x40]
    stp x29, x30, [sp, 0x50]
    
    # Save stack pointer to x0 (prev co)
    mov x2, sp
    str x2, [x0, 0]

    mov x19, x0
    mov x20, x1
    ldr x2, =__switch_stack_end
    mov sp, x2
    bl __switch_stack
    mov x0, x19
    mov x1, x20

    # Load stack pointer from x1 (next co)
    ldr x2, [x1, 0]
    mov sp, x2

    ldp x19, x20, [sp, 0x00]
    ldp x21, x22, [sp, 0x10]
    ldp x23, x24, [sp, 0x20]
    ldp x25, x26, [sp, 0x30]
    ldp x27, x28, [sp, 0x40]
    ldp x29, x30, [sp, 0x50]
    add sp, sp, 0x60
    
    b __switch_to
#endif


.data
.balign 4096
    .fill 4096,1,0
__switch_stack_end: