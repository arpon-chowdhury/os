# Architecture and hardware roadmap

ON. aims to become a general-purpose PC operating system. Development on an
Apple M2 Mac is a host toolchain choice, not a hardware compatibility constraint.
The first target is Intel and AMD x86-64 computers. ARM64 PCs follow later;
Apple Silicon Macs are a potential additional platform, not a promised target.

## Current implementation

Only x86-64 is implemented. Limine boots a minimal higher-half ELF kernel,
checks the protocol base revision, renders a framebuffer welcome screen with
kernel version 0.1.0, installs basic GDT/TSS/IDT exception handling, prints
debug-port and COM1 diagnostics, initializes physical page allocation from the
Limine memory map, and idles with a legacy PIC/PIT timer. Building
objects, linking, ISO creation, and QEMU BIOS handoff were verified on
2026-10-13 on Apple M2. x86-64 UEFI entry and welcome rendering also passed
with QEMU/OVMF. Real hardware remains unverified.
There is no ARM64 code, build target, boot image, or hardware support yet.

The current separation is:

| Location | Responsibility |
| --- | --- |
| `kernel/core/` | Architecture-independent control flow and framebuffer rendering |
| `kernel/mm/`, `kernel/include/memory.h` | Physical frame allocation, protection, statistics, and controlled tests |
| `kernel/mm/heap.c`, `heap_boot.c`, `kernel/include/heap.h` | Heap allocation, page provisioning, statistics, and safety validation |
| `kernel/arch/x86_64/virtual.c`, `kernel/include/virtual.h` | Four-level translation, owned mappings, permissions, and TLB invalidation |
| `kernel/sched/` | Future portable scheduling |
| `kernel/include/arch.h` | Shared contract for architecture services |
| `kernel/include/kernel.h` | Kernel entry contract used by the boot adapter |
| `kernel/arch/x86_64/entry.c` | Limine request markers and x86-64 boot entry |
| `kernel/arch/x86_64/cpu.c`, `exceptions.S` | Descriptor tables, exception entry, and fatal handling |
| `kernel/arch/x86_64/interrupts.c`, `irq_entry.S` | PIC/PIT initialization, returning IRQ dispatch, and 64-bit ticks |
| `kernel/arch/x86_64/platform.c` | CPU halt, emulator debug port, and COM1 operations |
| `kernel/arch/x86_64/linker.ld` | x86-64 ELF format and virtual address layout |
| `boot/protocol/`, `boot/config/` | Bootloader protocol definitions and configuration |
| `drivers/` | Future device drivers, with hardware-specific implementations |
| `libs/`, `include/on/` | Future freestanding routines and shared interfaces |
| `Makefile`, `scripts/` | Host tools, target compilation, packaging, and emulation |

The ELF entry remains `kmain`. The x86-64 boot adapter negotiates Limine and
passes its result, a bootloader-independent framebuffer descriptor, and a copied
memory map/direct-map offset to portable `kernel_main`. Limine structures remain confined to the boot adapter. The core uses `arch_name`,
`arch_debug_puts`, and `arch_halt` without embedding CPU instructions. Debug
output currently uses QEMU port `0xe9`; it is not a physical PC console.
The core still names Limine in its diagnostics; replacing the bootloader later
will also require revisiting that diagnostic contract.

## Boundaries for future development

Keep allocation policy, scheduler policy, process abstractions, filesystem
logic, and generic device interfaces in shared code. Keep CPU instructions,
registers, exceptions, interrupt entry, context switching, page-table formats,
TLB operations, and architecture ABI details under `kernel/arch/<architecture>/`.
Expose only the services shared code actually needs through kernel headers;
do not add speculative ARM64 stubs now.

CPU architecture and machine platform are separate concerns. Firmware handoff,
interrupt controllers, timers, buses, and device discovery need platform or
driver implementations. As those are implemented, give each implementation a
clear hardware scope under `drivers/` or a documented platform directory;
do not assume every x86-64 or ARM64 machine has identical hardware. Keep
bootloader structures in boot adapters and translate future boot data into a
shared kernel representation before handing it to portable subsystems.

Target build settings must select only one architecture's sources and explicitly
supply its compiler target, flags, linker layout, boot assets, and emulator
configuration. Host utilities run on the development host and must never be
linked into the kernel. The current Makefile accepts only `ARCH=x86_64` and
preserves the existing target triple, ISO layout, and QEMU TCG workflow.

## Roadmap

1. **Validate x86-64 boot:** obtain the documented dependencies, inspect the ELF,
   and confirm the expected Limine handoff in QEMU. Keep BIOS and UEFI validation
   distinct; `run` uses BIOS and `run-uefi` uses OVMF.
2. **Build the x86-64 foundation:** basic fatal exceptions are implemented and
   tested (see [the exception guide](exceptions.md)); legacy PIC/PIT interrupts are
   implemented (see [the interrupt guide](interrupts.md)). Physical allocation is
   implemented (see [the memory guide](memory.md)); [virtual memory](virtual-memory.md) is implemented while context
   switching is now implemented with timer preemption for kernel tasks; see [multitasking](multitasking.md).
3. **Develop PC services:** device discovery, console, storage, filesystems,
   processes, system calls, and user space. Define a supported hardware profile
   as drivers become available; x86-64 CPU support does not mean every PC works.
4. **Validate Intel and AMD PCs:** test selected machines and firmware paths,
   document compatibility, and harden the shared kernel and x86-64 implementation.
5. **Port to ARM64 PCs:** after the x86-64 foundation, implement ARM64 boot,
   exceptions, MMU, interrupt/timer services, context switching, and build/image
   rules for selected PC platforms. Reuse shared subsystems and validate first
   in an emulator, then on documented hardware.
6. **Evaluate Apple Silicon:** assess boot integration and Apple-specific device
   support as a separate platform effort. ARM64 instruction-set support alone
   will not make an ARM64 PC image boot or operate on an Apple Silicon Mac.

These are ordered milestones, not release dates or claims of current hardware
compatibility. No ARM64 implementation is part of the present change.

## Graphics foundation

The console remains the default foreground interface. An optional `graphics`
command displays a static original scene; Escape restores terminal cells and
input. `graphics.c` provides clipped pixel/rectangle/line/text/clear operations,
`font.c` owns the shared original bitmap font, and `framebuffer.c` handles RGB
packing, pitch, and pointer-aware writes. A subsequent retained UI layer now composites opaque surfaces with focus,
input routing and theme tokens; see [ui-foundation.md](ui-foundation.md). A foreground desktop/window manager now adds independent retained windows, resize
and lifecycle controls, a dock, and a read-only guest RTC clock; see
[window-management.md](window-management.md). Preemptive kernel tasks now run
on separate stacks; isolated user execution is described in [user-mode.md](user-mode.md),
and static executable loading in [syscalls-and-loader.md](syscalls-and-loader.md).
Filesystem services remain future work. The current
capability audit and future visual/interaction specification are in
[gui-design.md](gui-design.md).

## Kernel task scheduling

See [preemptive kernel tasks](multitasking.md). The single-CPU foreground GUI owns input/render state while a separate diagnostic task sleeps/yields on a guarded supervisor/NX stack. IRQ0 switches after PIC EOI with a five-tick quantum; IRQ scheduling never allocates or reaps. Kernel tasks share CR3; user tasks select owned roots and per-task TSS.RSP0. Versioned bounded syscalls and strict static ELF loading are implemented; see [syscalls and loader](syscalls-and-loader.md).

## ON. OS Core Alpha release order

Complete and verify each phase before implementing the next. The current phase
was syscalls and executable loading and is verified; see [implementation and verification](syscalls-and-loader.md). Existing
Windows 11 + macOS-inspired desktop, adaptive theme tokens and bottom dock stay
in place. The graphical demo is not yet a usable Core Alpha release.

1. Preemptive scheduler: bounded single-CPU policy, context preservation, sleep,
   lifecycle, guards, rollback, responsive desktop and BIOS/UEFI regressions.
2. User execution and memory isolation (verified): owned address spaces, supervisor-only
   kernel mappings, validated privilege transitions and isolated fault teardown.
3. Working syscalls and application loading (verified current phase): bounded user copies, validated
   executable images and resource ownership.
4. Storage drivers and bounded disk I/O against disposable QEMU disk images.
5. Reliable filesystem: validated metadata, safe operations, failure recovery
   and integrity tests before writes become a release capability.
6. Basic native application framework separated from kernel, drivers and services.
7. Functional desktop, file manager and essential settings using those services.
8. Performance, crash handling and stability testing for Core Alpha release.

Security boundaries and filesystem integrity are release gates. Each phase
requires relevant native tests and QEMU BIOS/UEFI verification, with limitations
recorded explicitly. Prefer fixed/bounded storage, responsive queued input and
dirty-region rendering. Do not change host macOS installations or settings.
Wi-Fi/networking, audio/Bluetooth, advanced animations/customization, app store,
package management, expanded hardware support and additional features stay
behind Core Alpha stability. Architecture ports also remain later work.
