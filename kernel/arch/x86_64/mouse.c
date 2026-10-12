#include "arch.h"
#include "mouse.h"
#include "keyboard.h"
#include "interrupts.h"
#include "io.h"
#include "ps2.h"
#define WAIT_LIMIT 100000u
static bool mouse_enabled;
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
static void route(uint8_t status, uint8_t byte) {
    if (status & 0xc0) { if (status & 0x20) mouse_discard_packet(); return; }
    if (status & 0x20) { if (mouse_enabled) mouse_feed_byte(byte); }
    else keyboard_feed_scancode(byte);
}
void x86_ps2_drain(void) {
    for (unsigned int i = 0; i < 32; ++i) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        route(status, inb(0x60));
    }
}
static bool read_response(bool auxiliary, uint8_t *byte) {
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) continue;
        uint8_t value = inb(0x60);
        if (status & 0xc0) continue;
        if (!!(status & 0x20) == auxiliary) { *byte = value; return true; }
        route(status, value);
    }
    return false;
}
static bool device_command(uint8_t byte) {
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        uint8_t response;
        if (!command(0xd4) || !data(byte) || !read_response(true, &response)) return false;
        if (response == 0xfa) return true;
        if (response != 0xfe) return false;
    }
    return false;
}
static void mouse_irq(const struct exception_frame *frame) {
    (void)frame;
    x86_ps2_drain();
}
bool arch_mouse_init(void) {
    uint64_t flags = arch_interrupt_save();
    uint8_t config = 0;
    bool have_config = false;
    mouse_enabled = false;
    mouse_reset();
    /* Quiesce both ports while reading the shared controller configuration. */
    if (!command(0xad) || !command(0xa7)) goto failed;
    x86_ps2_drain();
    if (inb(0x64) & 1) goto failed;
    if (!command(0x20) || !read_response(false, &config)) goto failed;
    have_config = true;
    if (!command(0x60) || !data((config | 0x10) & ~2u) || !command(0xa8)) goto failed;
    /* Defaults restore the standard three-byte protocol, then stream mode. */
    if (!device_command(0xf6) || !device_command(0xf4)) goto failed;
    if (!command(0x60) || !data((config | 2) & ~0x30u) ||
        !command(0xae) || !x86_irq_register(12, mouse_irq)) goto failed;
    mouse_enabled = true;
    arch_debug_puts("ON.: PS/2 mouse ready; IRQ12, three-byte packets.\n");
    arch_interrupt_restore(flags);
    return true;
failed:
    (void)x86_irq_register(12, 0);
    (void)command(0xa7);
    if (have_config && command(0x60)) (void)data((config | 0x20) & ~0x12u);
    (void)command(0xae);
    arch_debug_puts("ON.: PS/2 mouse unavailable; IRQ12 masked.\n");
    arch_interrupt_restore(flags);
    return false;
}
