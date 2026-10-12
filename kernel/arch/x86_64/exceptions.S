.text
.global x86_load_gdt
.type x86_load_gdt, @function
x86_load_gdt:
    cli
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movw $0x18, %ax
    ltr %ax
    ret
.size x86_load_gdt, .-x86_load_gdt

.macro EXCEPTION number, error=0
.global x86_exception_\number
.type x86_exception_\number, @function
x86_exception_\number:
.if !\error
    pushq $0
.endif
    pushq $\number
    jmp x86_exception_common
.size x86_exception_\number, .-x86_exception_\number
.endm

/* Vectors with CPU-pushed error codes must not receive a synthetic one. */
EXCEPTION 0
EXCEPTION 1
EXCEPTION 2
EXCEPTION 3
EXCEPTION 4
EXCEPTION 5
EXCEPTION 6
EXCEPTION 7
EXCEPTION 8, 1
EXCEPTION 9
EXCEPTION 10, 1
EXCEPTION 11, 1
EXCEPTION 12, 1
EXCEPTION 13, 1
EXCEPTION 14, 1
EXCEPTION 15
EXCEPTION 16
EXCEPTION 17, 1
EXCEPTION 18
EXCEPTION 19
EXCEPTION 20
EXCEPTION 21, 1
EXCEPTION 22
EXCEPTION 23
EXCEPTION 24
EXCEPTION 25
EXCEPTION 26
EXCEPTION 27
EXCEPTION 28
EXCEPTION 29, 1
EXCEPTION 30, 1
EXCEPTION 31

.type x86_exception_common, @function
x86_exception_common:
    cli
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
    movq %cr2, %rsi
    andq $-16, %rsp
    call x86_exception_fatal
    /* A fatal handler must never return. */
1:  cli
    hlt
    jmp 1b
.size x86_exception_common, .-x86_exception_common

#if ON_EXCEPTION_TEST == 6
.global x86_test_bad_stack
.type x86_test_bad_stack, @function
x86_test_bad_stack:
    xor %esp, %esp
.global x86_test_fault
x86_test_fault:
    ud2
.size x86_test_bad_stack, .-x86_test_bad_stack
#endif

.section .rodata
.balign 8
.global x86_exception_stubs
x86_exception_stubs:
.macro POINTER number
    .quad x86_exception_\number
.endm
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    POINTER \number
.endr
.section .note.GNU-stack,"",@progbits
