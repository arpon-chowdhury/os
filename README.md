# ON.

The display name is **ON.**; filenames, C identifiers, and shared headers use
`on` / `ON`. The boot image is `build/on.iso` with ISO volume label `ON`.

Built-in features use standalone names: **Flow**, **Pulse**, **Spaces**,
**Studio**, and **Control**. Use these names consistently in GUI labels, menus,
documentation, and development plans; the operating system remains **ON.**

ON. is a general-purpose PC operating system written from scratch in C with
minimal assembly. Intel and AMD x86-64 computers are the first target, followed
by ARM64 PCs and potentially Apple Silicon Macs. ON. is not an Apple-only OS.

Development currently happens on an Apple M2 Mac. This ARM64 macOS **host**
cross-compiles an x86-64 ELF kernel; QEMU's TCG backend emulates the **target**
CPU. The host does not determine the operating system's supported hardware.
Only the x86-64 target is implemented today. See the
[architecture roadmap](docs/architecture.md) for port boundaries and milestones.

The current implementation is a minimal Limine boot entry: negotiate base
revision 6, initialize a supported Limine framebuffer, draw a welcome screen
with kernel version 0.1.0, emit debug-port and COM1 diagnostics, and idle between PIT timer interrupts.
Basic x86-64 fatal exception handling installs a GDT, TSS, and IDT; see the
[exception guide](docs/exceptions.md). The first legacy PIC/PIT interrupt subsystem provides IRQ dispatch and a 64-bit
system tick counter; see the [interrupt guide](docs/interrupts.md). Physical memory
management allocates protected 4 KiB frames from the Limine map; see the
[memory guide](docs/memory.md). Four-level x86-64 virtual memory provides translation,
mapping, permissions, and TLB invalidation while preserving boot mappings; see the
[virtual memory guide](docs/virtual-memory.md). The [kernel heap](docs/heap.md)
provides safe allocation, alignment, coalescing, and statistics. Preemptive kernel tasks, PS/2 input and a retained desktop are implemented;
isolated user execution, bounded syscalls and strict static ELF loading are
implemented. Bounded ATA storage is implemented; a reliable filesystem remains
future work. See [storage scope and verification](docs/storage.md).

## Build and run

Dependencies must already be available. No target installs software, and ordinary
build targets do not download dependencies. See [the build guide](docs/build.md)
for tool requirements, Apple Silicon setup, overrides, and verification status.

```sh
make help
make objects       # Compile only; works with the available Apple Clang
make kernel        # Requires ELF LLD (ld.lld)
make fetch-limine  # Explicit, checksum-pinned project-local dependency download
make iso           # Requires Limine binaries, a host C compiler, and xorriso
make run           # Requires qemu-system-x86_64; type quit in its monitor to exit
make verify-boot   # Bounded BIOS handoff and welcome-rendering check
make verify-framebuffer # Save the QEMU screen and test the no-VGA fallback
make test-framebuffer   # Native renderer memory safety tests
make test-heap          # Sanitized heap safety and stress tests
make verify-heap        # BIOS/UEFI heap stress and rollback tests
make test-memory        # Native physical allocator safety tests
make verify-virtual     # BIOS/UEFI virtual memory and rollback tests
make verify-memory      # Controlled BIOS/UEFI allocation and fallback tests
make run DISPLAY=cocoa # View the BIOS welcome screen in a macOS window
make verify-uefi        # Verify x86-64 OVMF entry and capture the welcome screen
make verify-exceptions  # Nine fatal fault cases, each under BIOS and UEFI
make verify-interrupts  # Timer progress, welcome captures, and no-VGA fallback
make run-uefi DISPLAY=cocoa # View the UEFI welcome screen
```

Successful boot should write `ON.: Limine handoff OK; x86-64 kernel entered.`
to `build/debug.log` and `build/serial.log`, followed by the version and
framebuffer status, then physical memory statistics. The kernel reports system ticks reaching 100 and 200, then continues sleeping
between interrupts. The welcome screen stays visible.
Kernel linking, ISO creation, and the QEMU BIOS handoff were verified on this
Apple M2 Mac on 2026-10-13. `make verify-boot` captured the same message in
`build/boot-test-debug.log`. x86-64 UEFI boot with QEMU's bundled OVMF firmware
and the welcome screen were also verified. Real hardware remains unverified.

## Project goals

- Keep the kernel small, understandable, and primarily written in C.
- Separate architecture-specific code from portable kernel logic.
- Use an existing bootloader and document its protocol contract.
- Test in an emulator before attempting supported real hardware.
- Pin dependencies and keep generated files separate from source.

## Project structure and files

- `Makefile`: freestanding compilation, ELF linking, ISO packaging, explicit
  dependency fetching, tool checks, and QEMU commands.
- `kernel/core/main.c`: portable kernel logic using the architecture interface.
- `kernel/mm/physical.c`, `boot.c`, and `kernel/include/memory.h`: physical page
  allocation, reserved-memory protection, statistics, and isolated boot tests.
- `kernel/core/framebuffer.c`: validated RGB framebuffer and bitmap welcome text.
- `kernel/include/version.h`: the kernel version displayed and logged at boot.
- `kernel/include/{arch,kernel}.h`: architecture services and kernel entry contract.
- `kernel/arch/x86_64/entry.c`: Limine markers, revision check, and ELF entry.
- `kernel/arch/x86_64/cpu.c` and `exceptions.S`: GDT/TSS/IDT setup, CPU exception
  entry, fatal diagnostics, and isolated fault injection.
- `kernel/arch/x86_64/interrupts.c`, `interrupts.h`, and `irq_entry.S`: legacy PIC
  remapping, returning IRQ entry/dispatch, PIT programming, and the tick counter.
- `kernel/arch/x86_64/platform.c`: x86-64 debug port/COM1 output and CPU halt.
  Limine provides the initial stack and long-mode environment.
- `kernel/arch/x86_64/linker.ld`: higher-half ELF layout, entry symbol, retained
  Limine markers, and separate writable/executable/read-only segments.
- `boot/protocol/limine_base.h`: small attributed subset of the official protocol
  header, containing base-revision, request-boundary, framebuffer, memory-map,
  and HHDM definitions.
- `boot/config/limine.conf`: automatic ON. selection and kernel path.
- `scripts/fetch-limine.sh`: explicit Limine 12.9.3 download with SHA-256 checking;
  refuses to overwrite an existing dependency directory.
- `scripts/verify-boot.sh`: bounded BIOS handoff and welcome-rendering check.
- `scripts/verify-framebuffer.py`: QEMU BIOS/UEFI screenshots, serial checks, and BIOS no-VGA fallback.
- `tests/unit/physical.c`: sanitized allocator validation and protection tests.
- `tests/unit/framebuffer.c`: host renderer validation and memory guard tests.
- `docs/build.md`: dependencies, first-boot procedure, and verification limits.
- `docs/memory.md`: physical allocator contracts, statistics, and controlled tests.
- `docs/interrupts.md`: PIC/PIT contracts, tick API, and verification.
- `docs/exceptions.md`: exception contracts, test cases, evidence, and limitations.
- `docs/architecture.md`: target roadmap, portable interfaces, and platform boundaries.
- `.gitignore`: excludes generated artifacts, downloaded dependencies, local editor
  settings, logs, and macOS metadata.
- `.gitkeep` files: empty markers retaining the original scaffold directories.
  They have no role in compilation.

Reserved directories remain available for future work:

```text
boot/{config,protocol}/    Bootloader configuration and protocol integration
kernel/arch/x86_64/        CPU-specific code and linker script
kernel/{core,include}/    Kernel core and private interfaces
kernel/mm/                Physical and virtual memory boot support
kernel/sched/             Future scheduling
drivers/include/          Future drivers and private interfaces
libs/include/             Future freestanding libraries and headers
include/on/             Shared interfaces across components
scripts/                  Build and development helpers
tests/{unit,integration}/  Future host and emulator tests
docs/                     Developer guides and architecture decisions
```

All generated objects, dependency files, ELF/map files, host utilities, ISO staging
files, images, downloads, and emulator logs live in ignored `build/`. Downloaded
Limine files and their upstream license live in ignored `vendor/limine/`.
Git manages repository metadata in `.git/`.

## Development phases

1. **Repository foundation:** directory boundaries, Git hygiene, and roadmap.
2. **Build and boot integration (verified):** freestanding toolchain configuration,
   Limine protocol, linker layout, ISO packaging, and emulator workflow. Compilation
   and linking are verified.
3. **First boot validation (verified):** ELF segments, ISO, BIOS boot handoff, and
   diagnostic output passed under QEMU TCG on Apple M2.
4. **CPU and memory foundation (current):** fatal exceptions, legacy PIC/PIT interrupts,
   physical page allocation, virtual memory management, and a kernel heap are implemented.
5. **Drivers and libraries:** foundational device support and freestanding routines.
6. **Tasks and services:** scheduling, process boundaries, system calls, and storage.
7. **x86-64 PC hardening:** automated tests, reproducible builds, interface
   documentation, and validation on selected Intel and AMD computers.
8. **ARM64 PC port (future):** reuse portable subsystems and implement ARM64 CPU
   and supported PC platform services after the x86-64 foundation is established.
9. **Apple Silicon evaluation (potential):** assess a separate platform port;
   ARM64 CPU support alone does not provide Apple hardware support.

## PS/2 keyboard

The x86-64 boot CPU initializes the first PS/2 port and handles IRQ1 with
translated scan-code set 1 and a US keyboard layout. Both Shift keys, key
release, Enter (including keypad Enter), and Backspace are supported. The idle
loop consumes events and reports printable input and control keys to debugcon
and COM1, and forwards input to the interactive framebuffer terminal.

`keyboard_poll()` returns make/break events from a 64-event FIFO and preserves
the caller's interrupt state. Overflow drops new events and increments
`keyboard_dropped_events()`; modifier decoding continues even when full.
This queue supports the single boot CPU, with no allocation or output in IRQ1.
Initialization waits and IRQ work are bounded; failed initialization masks IRQ1.
Caps Lock, other layouts, USB keyboards, and SMP consumers are not implemented.

Run `make test-keyboard` for sanitized decoder/queue tests and
`make verify-keyboard` for real BIOS and UEFI QEMU key injection, alongside
existing framebuffer, memory, heap, and timer assertions.

## Interactive terminal

After boot initialization, the welcome screen becomes a framebuffer terminal
with a blinking underline cursor, line wrapping, and upward scrolling. Type a
command and press Enter; Backspace edits the current line without deleting the
`on> ` prompt. Input uses the PS/2 US layout, including Shift.

- `help`: list commands.
- `clear`: clear the screen and start a fresh prompt.
- `echo TEXT`: print text.
- `version`: show the kernel version.
- `ticks`: show the PIT tick count (approximately 100 per second).
- `mem`: show physical-frame allocation statistics.
- `heap`: show heap allocation statistics.
- `graphics`: display the Phase 1 graphics demo; Escape returns to the terminal.
- `ui`: open the retained UI scene; Escape restores the terminal.

Run `make run DISPLAY=cocoa` or `make run-uefi DISPLAY=cocoa` to use it.
`make test-terminal` runs sanitized command, input-boundary, cursor, wrapping,
and scrolling checks. `make verify-terminal` injects commands under BIOS and
UEFI and saves display captures beneath `build/terminal/`.

The terminal uses static storage (up to 160 columns by 64 rows) and a bounded
255-character input line, further limited to fit the visible rows. It needs no
heap allocation. IRQ handlers only enqueue input; foreground code renders and
executes commands with interrupts enabled. Unsupported or missing framebuffers
retain the existing serial/debug output. History, navigation editing, command
pipelines, and ANSI escape sequences are not implemented.

## PS/2 mouse

The first boot CPU initializes the second PS/2 port in standard three-byte
stream mode and unmasks IRQ12 plus the PIC cascade. Both PS/2 IRQ handlers drain
the shared controller buffer and route keyboard/mouse bytes using its AUX bit.
Initialization preserves keyboard translation and IRQ1 configuration; waits,
command retries, and IRQ work are bounded. A failed mouse initialization masks
IRQ12 and retains the keyboard and other services.

Signed relative movement drives a white arrow, clamped to the framebuffer
edges, with PS/2 upward motion converted to screen coordinates. Left/right
presses and releases are detected and reported to debugcon and COM1. The pointer
saves the pixels underneath it; terminal drawing updates that saved background,
so typing, clearing, scrolling, and the blinking text cursor remain intact.
Drawing and reporting happen in foreground code, never in IRQ12.

`make test-mouse` runs sanitized packet and queue checks. `make verify-mouse`
tests BIOS and UEFI QEMU motion in both directions, individual/simultaneous
left/right buttons and releases, edge clamping, keyboard commands while the
pointer covers text, and pixel restoration. Captures and logs are under
`build/mouse/`. Sanitized framebuffer tests also cover pointer clipping, pitch
padding, and drawing beneath the pointer.

Limitations: standard PS/2 mice only; no USB tablet, wheel, extra-button,
acceleration, text selection, or clickable terminal commands. The fixed
64-event queue supports the single boot CPU and drops newest events when full;
`mouse_dropped_events()` reports overflow. Motion overflow flags suppress the
invalid axis while retaining button flags. Packet alignment uses the mandatory
header bit and controller errors discard partial packets; arbitrary lost bytes
cannot always be recovered unambiguously. Real PC mouse hardware has not been
tested. A missing/unsupported framebuffer keeps mouse events in debug output
without a visible cursor. No macOS system settings are changed.

## Phase 1 graphics foundation

Type `graphics` at the terminal prompt to display the original ON. drawing demo;
press Escape to redraw and resume the terminal. Mouse movement and PIT delivery
continue in the demo. It is a static scene, with no window manager or clickable
dock. Flow, Pulse, Spaces, Studio, and Light/Dark/Auto theme behavior are future
designs described in [the GUI specification](docs/gui-design.md).

`kernel/include/graphics.h` exposes a framebuffer context with signed-coordinate
pixels, clipped fills/outlines/lines, clear, and scaled original bitmap text.
The backend respects pitch and nonoverlapping eight-bit RGB masks in validated
32-bit framebuffers, including unaligned pitch/address. Unsupported modes fail
without drawing. It uses no heap allocation or dependencies and preserves the
mouse pointer background. `kernel/include/font.h` shares the original ASCII
font between graphics and terminal rendering.

Run `make test-graphics` for sanitized primitive tests, or `make verify-graphics`
for BIOS/UEFI pixel verification, display captures, and keyboard return to the
terminal. Captures live in `build/graphics/{bios,uefi}/graphics-demo.png`.
Direct front-buffer rendering can tear; there is no GPU acceleration, alpha
compositing, Unicode font engine, automatic theme schedule, or real-hardware
validation yet.

## Retained UI foundation

Type `ui` to open retained Control and Canvas sample windows with a centered
floating dock. Click a window to focus it, drag its title bar to move it, and
click buttons to activate them. Control selects Light, Dark, or Auto. Tab and
Shift-Tab select controls; Enter/Space activate; F6 cycles windows. Escape restores
the terminal. Dock entries remain Flow, Pulse, Spaces, Studio and Control.

Opaque surfaces are recomposited only within clipped dirty regions. Shared
theme tokens apply to windows, buttons, dock and top bar. Auto adapts to local
time supplied by a future clock service; the current demo explicitly falls back
to Dark because configured local civil time is unavailable. The desktop can separately show guest RTC firmware time. No host settings are read or
changed. The GUI remains a foreground owner; preemptive kernel tasks are now documented separately below.

Run `make test-ui` for sanitized state/compositing checks and `make verify-ui`
for BIOS/UEFI mouse, keyboard, palette and terminal-restoration tests.
See [ui-foundation.md](docs/ui-foundation.md) for APIs, limits and test details.

Interactive shell: type `desktop` in the terminal. See [desktop controls and limitations](docs/desktop.md). Run `make verify-desktop` for BIOS/UEFI interaction checks and screenshots.

Independent foreground windows: see [window management](docs/window-management.md). Run `make test-windows` and `make verify-windows` for sanitizer and BIOS/UEFI lifecycle/geometry tests.

Preemptive kernel tasks: type `tasks` for states and sampled CPU/idle diagnostics. Run `make test-tasks` and `make verify-tasks` for native policy and BIOS/UEFI context/guard/rollback checks. See [multitasking architecture and limits](docs/multitasking.md). Isolated user tasks, bounded ABI v1 syscalls and strict static ELF loading are implemented. See [user execution](docs/user-mode.md).

Core Alpha work follows the [ordered release gates](docs/architecture.md#on-os-core-alpha-release-order). The syscall/loader phase is verified; the verified storage phase is documented in [storage and disk I/O](docs/storage.md); optional features wait until the core is stable.

Run `make test-address-spaces`, `make verify-user`, and `make verify-user-load`
for native ownership checks, BIOS/UEFI isolation/fault/rollback tests and desktop
interaction under sustained Ring 3 load. Normal boot creates no user applications;
`make apps` builds independent static ELF test executables. Run `make test-elf`,
`make verify-apps` and `make verify-app-load` for loader/ABI safety and BIOS/UEFI
desktop stress checks. The loader accepts kernel-owned images; filesystem launch
and full application compatibility remain pending.

Bounded storage: run `make test-storage` and `make verify-storage` for sanitized
ATA/block safety, BIOS/UEFI disposable-disk writes, flush/reboot persistence,
error containment and desktop interaction during background reads. Normal boot
keeps writes disabled. No filesystem or application disk access is implemented.
