#include "arch.h"
#include "keyboard.h"
#include "interrupts.h"
#include "io.h"
#include "ps2.h"

#define WAIT_LIMIT 100000u
static bool wait_input(void) {
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i)
        if (!(inb(0x64) & 2)) return true;
    return false;
}
static bool command(uint8_t byte) {
    if (!wait_input()) return false;
    outb(0x64, byte);
    return true;
}
static bool data(uint8_t byte) {
    if (!wait_input()) return false;
    outb(0x60, byte);
    return true;
}
static bool read_byte(uint8_t *byte) {
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        uint8_t status = inb(0x64);
        if (status & 1) {
            uint8_t value = inb(0x60);
            if (!(status & 0xe0)) { *byte = value; return true; }
        }
    }
    return false;
}
static bool device_command(uint8_t byte) {
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        uint8_t response;
        if (!data(byte) || !read_byte(&response)) return false;
        if (response == 0xfa) return true;
        if (response != 0xfe) return false;
    }
    return false;
}
static void keyboard_irq(const struct exception_frame *frame) {
    (void)frame;
    x86_ps2_drain();
}
void arch_keyboard_init(void) {
    uint64_t flags = arch_interrupt_save();
    uint8_t config;
    keyboard_reset();
    if (!command(0xad)) goto failed;
    for (unsigned int i = 0; i < 256 && (inb(0x64) & 1); ++i) (void)inb(0x60);
    if (inb(0x64) & 1) goto failed;
    if (!command(0x20) || !read_byte(&config)) goto failed;
    /* Disable IRQ1 during negotiation; translate explicit set 2 to set 1. */
    config = (config | 0x40) & ~0x11;
    if (!command(0x60) || !data(config) || !command(0xae)) goto failed;
    if (!device_command(0xf5) || !device_command(0xf0) ||
        !device_command(2) || !device_command(0xf4)) goto failed;
    if (!command(0x60) || !data(config | 1) || !x86_irq_register(1, keyboard_irq)) goto failed;
    arch_debug_puts("ON.: PS/2 keyboard ready; IRQ1, translated set 1.\n");
    arch_interrupt_restore(flags);
    return;
failed:
    (void)x86_irq_register(1, 0);
    (void)command(0xad);
    arch_debug_puts("ON.: PS/2 keyboard unavailable; IRQ1 masked.\n");
    arch_interrupt_restore(flags);
}
