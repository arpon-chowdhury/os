# Phase 11: lightweight native applications

Baseline: Phase 10 commit `3fd2097` remains the known-good Git checkpoint.
The framework builds on the existing isolated ELF64 loader, Ring 3 execution,
private address spaces, scheduler, and syscall ABI v1. This is a small ON. native
ABI, not desktop application compatibility with another operating system.

## Build and use

`make apps iso` builds the independent freestanding C sample as
`build/apps/sample.elf` and embeds its bytes in the kernel image. In the terminal,
enter `desktop`, then press **F5** to launch an instance. Repeat F5 for independent
instances. The application displays “Hello from Ring 3”, changes its title after
keyboard or mouse input, and exits on **q** or the window close control / F9.
**c** deliberately executes UD2 to demonstrate crash containment. F6 cycles
focus, F7 minimizes, F8 maximizes/restores; the bottom dock restores live windows.
Escape returns to the terminal and cancels applications attached to that scene.

Launch is the trusted kernel `process_load_elf` operation. There is no new spawn
syscall or filesystem executable discovery. The sample uses the same loader as
existing independent ELF probes, with an RX text segment, RW/NX data and guarded
stack. A raw `_start` entry must exit explicitly; returning is invalid. Build
without a runtime, red zone, stack protector, PIC, or floating-point/SIMD use.
Existing loader size, segment and mapping bounds remain in force.

## ABI and bounded messages

The public contract is `include/on/application.h`; existing syscalls 0–5 and
INT 0x80 legacy exit remain unchanged. INT 0x81 uses RAX for number/result and
RDI/RSI for these arguments. ABI version remains 1: additions are compatible.
Errors are negative errno values from `include/on/syscall.h`.

| Number | Operation | RDI | RSI | Success |
| --- | --- | --- | --- | --- |
| 6 | Create | pointer to `on_window_create` | unused | positive handle |
| 7 | Title | handle | pointer to exactly 24 title bytes | 0 |
| 8 | Draw | handle | pointer to `on_draw` | 0 |
| 9 | Event | handle | writable pointer to `on_event` | 1 event, 0 empty |
| 10 | Close | handle | unused | 0 |

Messages contain fixed-width fields: create is 32 bytes, draw 52, event 24.
Titles and text must terminate within their 24/32-byte arrays. Dimensions are
120–640 by 90–480; the desktop may reject a window too large for its work area.
Client coordinates start four pixels inside the left edge and 24 below the top.
A draw fills its bounded rectangle and renders optional text in RGB 0xRRGGBB;
zero width/height requests text only. Commands are retained for repaint and
clipped to the client rectangle and current damaged region.

Four global application window records, sixteen draw commands per window, and
sixteen queued events per window bound all service work and storage. Draws append
until the list is full (`-ON_E2BIG`); there is no bitmap sharing or unrestricted
framebuffer access. A window handle is monotonically generated and checked
against the calling task's generation-bearing ID. Stale and foreign handles
return `-ON_EBADF`. Handle exhaustion fails safely rather than wrapping.

Events report key scancode/character, client-relative mouse coordinates and
button flags. Key flags are pressed=bit 0, shift=bit 1; mouse flags are left=bit 0,
right=bit 1. The current terminal input dispatcher forwards key presses only.
Keyboard events target the focused application; mouse events target its client
area while it has focus. Desktop hotkeys/search remain desktop-owned. Normal
queue overflow drops newest input. A close event takes priority over a full queue.
A failed copy-out does not consume an event. Empty polls write a zero event.

## Ownership and lifecycle

`kernel/services/application.c` owns copied messages and never changes the UI.
Syscalls validate user memory through the existing owned-page copy routines.
Short interrupt exclusions protect service records on the single bootstrap CPU.
The foreground adapter in `kernel/core/ui_demo.c` creates/reuses scene slots,
applies titles/damage, routes input and renders copied snapshots with interrupts
enabled. Windows participate in the existing order, chrome, themes, focus and
dock. Dirty flags avoid continuous repaint of idle applications.

Exit, cancellation and user faults all enter existing deferred process cleanup.
It releases private mappings, heap allocations and task stacks, marks associated
windows for cleanup, and the desktop releases their retained commands/events.
Retired scene slots are excluded from the dock and launcher and cannot be
restored; later applications reuse them. An explicit window close releases the
window only; its process must still exit. A desktop close sends an event rather
than forcibly killing a process. Escape cancels scene applications.

The framework supplies no user mapping of kernel framebuffer/service records,
and no mapping of another application's private memory. RX/RW-NX separation,
user/supervisor permissions, guarded stacks and user-fault containment are inherited
from the verified process implementation. It is a single-CPU framework, not an
SMP security model.

## Verification and limits

`make test-native` checks bounds, foreign ownership, invalid pointers, command
limits, event overflow, failed copy-out, polling and cleanup with ASan/UBSan.
`make verify-native` boots BIOS and UEFI, checks native text pixels, mouse/key
consumption, title changes, dock restoration, maximize/restore, normal exit,
UD2 crash reclamation, independent instances and close cleanup. Screenshots and
fresh debug logs are under `build/native/{bios,uefi}`.

All existing native sanitizer suites passed; the ELF/syscall and loaded-application
BIOS/UEFI regressions passed. Filesystem BIOS/UEFI tests passed for reboot
persistence, corruption, recovery, interrupted updates, durability errors and
background filesystem reads during desktop interaction. Existing window tests
also passed. Logs: `build/native-all-sanitizers.log`,
`build/native-final-verification.log`, and `build/native-regressions.log`.

Limits: no shared pixel buffers, dynamic linking, standard C library, filesystem
launch UI, resize notification, key-release dispatch, event blocking, redraw-list
replacement, arbitrary application compatibility, networking, audio or Bluetooth.
Application drawing colors are explicit RGB; window chrome follows adaptive themes.
Creation is asynchronous: a handle acknowledges an accepted bounded request;
if the scene cannot accommodate it, the handle is closed during foreground work.
A noncooperative application can ignore a close event; scene cancellation still
terminates it. QEMU verification covers the configured emulated hardware rather
than physical hardware.
