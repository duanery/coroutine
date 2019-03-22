.text
.globl switch_to
switch_to:
	/*
	 * Save callee-saved registers
	 * This must match the order in stack_frame
	 */
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
    
    /* save prev stack */
    movq	%rsp, 0(%rdi)
    
	/* switch stack */
    leaq    __switch_stack_end(%rip), %rsp
    pushq   %rsi
    pushq   %rdi
    call    __switch_stack
    
    /* restore next stack */
    popq    %rdi
    popq    %rsi
	movq	0(%rsi), %rsp

	/* restore callee-saved registers */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	jmp	__switch_to

.data
.balign 4096
    .fill 4096,1,0
__switch_stack_end:
