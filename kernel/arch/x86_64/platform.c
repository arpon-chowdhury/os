#include <stdint.h>
#include "arch.h"
#include "io.h"

const char *arch_name(void) {
    return "x86-64";
}

void arch_debug_init(void) {
    /* COM1, 115200 baud, 8N1, polled output with interrupts disabled. */
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x01);
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x03);
}

/* Debugcon plus COM1; a missing or stuck UART must not block the kernel. */
void arch_debug_write(const char *text,size_t length) {
    while (length--) {
        uint8_t byte = (uint8_t)*text++;
        outb(0xe9, byte);
        for (unsigned int attempt = 0; attempt < 10000; ++attempt) {
            uint8_t status = inb(0x3fd);
            if (status == 0xff) break; /* Absent legacy UART. */
            if (status & 0x20) {
                outb(0x3f8, byte);
                break;
            }
        }
    }
}

void arch_debug_puts(const char *text) {
    size_t length=0;while(text[length]) ++length;
    arch_debug_write(text,length);
}

__attribute__((noreturn)) void arch_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

uint64_t arch_interrupt_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void arch_interrupt_restore(uint64_t flags) {
    if (flags & 0x200) __asm__ volatile ("sti" : : : "memory");
    else __asm__ volatile ("cli" : : : "memory");
}
