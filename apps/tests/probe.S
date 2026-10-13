#include "on/syscall.h"
.macro call number
    movq $\number,%rax
    int $ON_SYSCALL_VECTOR
.endm
.macro expect value
    cmpq $\value,%rax
    jne fail
.endm
.text
.global _start
_start:
    movq $1,%r15
    movq $0x1234,%r10
    movq $0x2234,%r8
    movq $0x3234,%r9
    movq $0xaaaa,%rdi
    movq $0xbbbb,%rsi
    movq $0xcccc,%rdx
    std
    stc
    call ON_SYS_ABI
    pushfq
    popq %r11
    cld
    expect ON_SYSCALL_ABI_VERSION
    testq $1,%r11
    jz fail
    testq $1024,%r11
    jz fail
    cmpq $0xaaaa,%rdi
    jne fail
    cmpq $0xbbbb,%rsi
    jne fail
    cmpq $0xcccc,%rdx
    jne fail
    call 65535
    expect -ON_ENOSYS
    movq $2,%r15
    movq $1,%rdi
    leaq message(%rip),%rsi
    movq $(message_end-message),%rdx
    call ON_SYS_WRITE
    expect (message_end-message)
    movq $0,%rsi
    movq $1,%rdx
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movq $0x1000,%rsi
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movabsq $0xffff800000000000,%rsi
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movabsq $0x800000000000,%rsi
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movq $-4,%rsi
    movq $16,%rdx
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movq $0x400ffc,%rsi
    movq $8,%rdx
    call ON_SYS_WRITE
    expect -ON_EFAULT
    movq $257,%rdx
    call ON_SYS_WRITE
    expect -ON_E2BIG
    xorq %rdx,%rdx
    call ON_SYS_WRITE
    expect 0
    xorq %rdi,%rdi
    call ON_SYS_WRITE
    expect -ON_EBADF
    movq $3,%r15
    movq $8192,%rdi
    call ON_SYS_MEM_ALLOC
    testq %rax,%rax
    js fail
    movq %rax,%r12
    cmpq $0,(%r12)
    jne fail
    cmpq $0,8184(%r12)
    jne fail
    leaq memory_message(%rip),%rsi
    leaq 4090(%r12),%rdi
    movq $(memory_end-memory_message),%rcx
    rep movsb
    movq $1,%rdi
    leaq 4090(%r12),%rsi
    movq $(memory_end-memory_message),%rdx
    call ON_SYS_WRITE
    expect (memory_end-memory_message)
    movq $0x400000,%rdi
    movq $4096,%rsi
    call ON_SYS_MEM_FREE
    expect -ON_EINVAL
    leaq 1(%r12),%rdi
    movq $8192,%rsi
    call ON_SYS_MEM_FREE
    expect -ON_EINVAL
    movq %r12,%rdi
    movq $4096,%rsi
    call ON_SYS_MEM_FREE
    expect -ON_EINVAL
    movq $8192,%rsi
    call ON_SYS_MEM_FREE
    expect 0
    call ON_SYS_MEM_FREE
    expect -ON_EINVAL
    movq $8192,%rdi
    call ON_SYS_MEM_ALLOC
    cmpq %r12,%rax
    jne fail
    cmpq $0,4088(%r12)
    jne fail
    cmpq $0,4096(%r12)
    jne fail
    movq %r12,%rdi
    movq $8192,%rsi
    call ON_SYS_MEM_FREE
    expect 0
    xorq %rdi,%rdi
    call ON_SYS_MEM_ALLOC
    expect -ON_EINVAL
    movq $(ON_ALLOC_MAX+1),%rdi
    call ON_SYS_MEM_ALLOC
    expect -ON_EINVAL
    movq $4,%r15
    subq $32,%rsp
    xorq %r13,%r13
1:
    movq $4096,%rdi
    call ON_SYS_MEM_ALLOC
    testq %rax,%rax
    js fail
    movq %rax,(%rsp,%r13,8)
    incq %r13
    cmpq $4,%r13
    jne 1b
    call ON_SYS_MEM_ALLOC
    expect -ON_ENOMEM
    xorq %r13,%r13
2:
    movq (%rsp,%r13,8),%rdi
    movq $4096,%rsi
    call ON_SYS_MEM_FREE
    expect 0
    incq %r13
    cmpq $4,%r13
    jne 2b
    addq $32,%rsp
    movq $5,%r15
    movq $16,%r14
3:
    call ON_SYS_YIELD
    expect 0
    decq %r14
    jnz 3b
    cmpq $17,initial_value(%rip)
    jne fail
    incq initial_value(%rip)
    cmpq $0,bss_counter(%rip)
    jne fail
    movq $30000000,%rcx
4:
    incq bss_counter(%rip)
    decq %rcx
    jnz 4b
    cmpq $30000000,bss_counter(%rip)
    jne fail
    cmpq $18,initial_value(%rip)
    jne fail
    cmpq $0x1234,%r10
    jne fail
    cmpq $0x2234,%r8
    jne fail
    cmpq $0x3234,%r9
    jne fail
    /* Leave a live allocation: normal exit must reclaim it with the process. */
    movq $4096,%rdi
    call ON_SYS_MEM_ALLOC
    testq %rax,%rax
    js fail
    xorq %rdi,%rdi
    call ON_SYS_EXIT
    ud2
fail:
    leaq 100(%r15),%rdi
    call ON_SYS_EXIT
    ud2
message: .ascii "ON.APP: ABI/syscalls/ELF probe\n"
message_end:
memory_message: .ascii "ON.APP: cross-page memory\n"
memory_end:
.data
initial_value: .quad 17
.bss
.balign 8
bss_counter: .skip 5000
.section .note.GNU-stack,"",@progbits
