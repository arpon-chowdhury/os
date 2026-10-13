.text
.macro IRQ number
.global x86_irq_\number
.type x86_irq_\number, @function
x86_irq_\number:
    pushq $0
    pushq $(32 + \number)
    jmp x86_irq_common
.size x86_irq_\number, .-x86_irq_\number
.endm
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    IRQ \number
.endr
.global x86_user_exit_interrupt
.type x86_user_exit_interrupt,@function
x86_user_exit_interrupt:
    pushq $0
    pushq $128
    jmp x86_irq_common
.size x86_user_exit_interrupt,.-x86_user_exit_interrupt
.type x86_irq_common, @function
x86_irq_common:
    cld
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call x86_irq_dispatch
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp
    iretq
.size x86_irq_common, .-x86_irq_common
.section .rodata
.balign 8
.global x86_irq_stubs
x86_irq_stubs:
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    .quad x86_irq_\number
.endr
.section .note.GNU-stack,"",@progbits
