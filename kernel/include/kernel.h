#ifndef ON_KERNEL_H
#define ON_KERNEL_H
#include <stdbool.h>
#include "framebuffer.h"
#include "memory.h"

/* The boot adapter supplies the result of protocol negotiation. */
__attribute__((noreturn)) void kernel_main(bool boot_supported, const struct kernel_framebuffer *framebuffer, const struct boot_memory_map *memory);

#endif
