#include "arch.h"
#include "kernel.h"
#include "version.h"
#include "virtual.h"

__attribute__((noreturn)) void kernel_main(bool boot_supported, const struct kernel_framebuffer *framebuffer, const struct boot_memory_map *memory) {
    if (boot_supported) {
        arch_debug_puts("ON.: Limine handoff OK; ");
        arch_debug_puts(arch_name());
        arch_debug_puts(" kernel entered.\n");
        arch_debug_puts("ON.: kernel version " ON_KERNEL_VERSION ".\n");
        if (!framebuffer) {
            arch_debug_puts("ON.: framebuffer unavailable; screen skipped.\n");
        } else if (!framebuffer_show_welcome(framebuffer)) {
            arch_debug_puts("ON.: unsupported framebuffer; screen skipped.\n");
        } else {
            arch_debug_puts("ON.: framebuffer welcome screen ready.\n");
        }
    } else {
        arch_debug_puts("ON.: unsupported Limine base revision.\n");
    }
    if (boot_supported) {
        pmm_boot_start(memory);
        vmm_boot_start(memory);
        arch_exception_test();
        arch_interrupt_init();
        arch_idle();
    }
    arch_halt();
}
