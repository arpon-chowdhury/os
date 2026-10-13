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

/* Wait for a peer without C calls: verify every interrupted general register. */
.global x86_task_preemption_register_test
.type x86_task_preemption_register_test,@function
x86_task_preemption_register_test:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq $100, %rax
    movq $101, %rcx
    movq $102, %rdx
    movq $103, %rbx
    movq $104, %rbp
    movq $105, %rsi
    movq $106, %rdi
    movq $107, %r8
    movq $108, %r9
    movq $109, %r10
    movq $110, %r11
    movq $111, %r12
    movq $112, %r13
    movq $113, %r14
    movq $114, %r15
4:
    cmpl $0,busy_started+4(%rip)
    je 4b
    cmpq $100, %rax
    jne 5f
    cmpq $101, %rcx
    jne 5f
    cmpq $102, %rdx
    jne 5f
    cmpq $103, %rbx
    jne 5f
    cmpq $104, %rbp
    jne 5f
    cmpq $105, %rsi
    jne 5f
    cmpq $106, %rdi
    jne 5f
    cmpq $107, %r8
    jne 5f
    cmpq $108, %r9
    jne 5f
    cmpq $109, %r10
    jne 5f
    cmpq $110, %r11
    jne 5f
    cmpq $111, %r12
    jne 5f
    cmpq $112, %r13
    jne 5f
    cmpq $113, %r14
    jne 5f
    cmpq $114, %r15
    jne 5f
    movl $1,%eax
    jmp 6f
5:
    xorl %eax,%eax
6:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size x86_task_preemption_register_test,.-x86_task_preemption_register_test
#endif
