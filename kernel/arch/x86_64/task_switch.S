.text
.global arch_task_switch
.type arch_task_switch,@function
arch_task_switch:
    /* Called with interrupts disabled. The C call boundary owns caller-saved
       registers; each suspended invocation restores its own saved IF flags. */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,(%rdi)
    movq %rsi,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size arch_task_switch,.-arch_task_switch
.section .note.GNU-stack,"",@progbits
#if ON_TASK_TEST == 1
.text
.global x86_task_register_test
.type x86_task_register_test,@function
x86_task_register_test:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8,%rsp
    movq $32,(%rsp)
    movq $111,%rbp
    movq $222,%rbx
    movq $333,%r12
    movq $444,%r13
    movq $555,%r14
    movq $666,%r15
1:
    call task_yield
    cmpq $111,%rbp
    jne 2f
    cmpq $222,%rbx
    jne 2f
    cmpq $333,%r12
    jne 2f
    cmpq $444,%r13
    jne 2f
    cmpq $555,%r14
    jne 2f
    cmpq $666,%r15
    jne 2f
    decq (%rsp)
    jnz 1b
    movl $1,%eax
    jmp 3f
2:
    xorl %eax,%eax
3:
    addq $8,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size x86_task_register_test,.-x86_task_register_test
#endif
