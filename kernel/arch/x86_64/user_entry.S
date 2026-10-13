.text
.global x86_user_iret
.type x86_user_iret,@function
x86_user_iret:
    cli
    movw $0x33,%ax
    movw %ax,%ds
    movw %ax,%es
    xorw %ax,%ax
    movw %ax,%fs
    movw %ax,%gs
    pushq $0x33
    pushq %rsi
    pushq $0x202
    pushq $0x2b
    pushq %rdi
    /* No kernel pointers or uninitialized register state cross the boundary. */
    xorq %rax,%rax
    xorq %rbx,%rbx
    xorq %rcx,%rcx
    xorq %rdx,%rdx
    xorq %rbp,%rbp
    xorq %rsi,%rsi
    xorq %rdi,%rdi
    xorq %r8,%r8
    xorq %r9,%r9
    xorq %r10,%r10
    xorq %r11,%r11
    xorq %r12,%r12
    xorq %r13,%r13
    xorq %r14,%r14
    xorq %r15,%r15
    iretq
.size x86_user_iret,.-x86_user_iret
.section .note.GNU-stack,"",@progbits
