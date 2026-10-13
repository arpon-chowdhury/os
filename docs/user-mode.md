# Ring 3 and owned address spaces

ON. now runs small embedded x86-64 user programs in isolated page-table roots,
preempts them with the existing scheduler, and terminates a faulting user task
without halting the desktop. This is a user-execution foundation, not a native
application system. Normal boot creates no user applications. No executable
loader, general syscall dispatcher, user-pointer copying, filesystem or optional
feature is added in this phase.

Known-good scheduler checkpoint: Git revision `1460adf`. Build/test artifacts
remain project-local under ignored `build/`; host macOS settings, installation
and physical disks are unchanged.

## Privilege and entry contract

The GDT retains kernel code/data (0x08/0x10) and its 16-byte TSS at 0x18. It adds
64-bit DPL3 code (0x2b with RPL3) and DPL3 data (0x33). Every selected task sets
TSS.RSP0 to its guarded 16 KiB kernel stack before the context switch. Bootstrap
has no user entry and retains the exception-stack fallback. TSS.IOMAP is beyond
the TSS limit; user port I/O is denied. GDT/IDT/TSS remain kernel-only mappings.

The user-entry wrapper validates its current task/root, RX entry, RW/NX stack,
16-byte stack alignment and TSS kernel stack. IRET enters with RFLAGS=0x202
(IF=1, IOPL=0) and fixed user selectors. All general registers start at zero;
no kernel argument, address or uninitialized register is passed to the program.
FS/GS bases are cleared, FSGSBASE and protection-key controls disabled, and
CR0.TS prevents unsaved x87/SIMD state from crossing between tasks. Unsupported
FPU/SIMD execution terminates that user task; there is no extended-state ABI.

SYSCALL is disabled through EFER.SCE. SYSENTER CS/EIP/ESP MSRs are zeroed when
SEP is supported. All IDT gates remain DPL0 except vector 128: an experimental
exit-only interrupt gate, `int $0x80`, takes the signed exit status in RAX and
never returns to the caller. It reads no user pointer, offers no other operation,
and is not the reserved future syscall ABI in `syscall.h`. The future ABI hook
still returns -ENOSYS. User code should explicitly invoke this termination gate;
a bare RET has no user return trampoline and faults safely.

Privilege/stack rules follow the [Intel system programming manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
The verified QEMU `qemu64` CPU uses AMD long-mode SYSENTER behavior: #UD, per
[AMD's system programming manual](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2).
An early test incorrectly expected Intel's #GP for that instruction; the final
fixture checks #UD exactly. Kernel tests also inspect the disabled SYSENTER MSR.

## Memory ownership and isolation

`address_space.c` allocates a zeroed root and private lower-half tables. It never
copies Limine identity mappings or other boot lower-half entries. Each user
leaf owns a newly allocated, zeroed physical frame; it cannot name a physical
frame, borrow a kernel page, map a high address or create an alias. Mappings must
be aligned, canonical lower-half pages above the null page. W+X is rejected;
code is user RX, data/stack user RW/NX. All intermediate user tables enforce U/S.
Private mapping mutations are permitted only while the root is inactive.

Each process starts with these fixed fixtures:

| Region | Address | Size | Permissions |
| --- | --- | --- | --- |
| Embedded machine code | 0x400000 | one page, at most 4096 supplied bytes | user RX |
| Initial value / private data | 0x600000 | one zeroed page | user RW/NX |
| User stack | 0x800000–0x802000 | two zeroed pages | user RW/NX |
| Stack guards | 0x7ff000 and 0x802000 | unmapped | no access |

The address-space API caps owned user leaves at 16, and the scheduler caps total
tasks at eight, including bootstrap. The current fixed process layout typically
owns four user frames, six paging frames and four kernel-stack frames: 56 KiB
per process, excluding shared kernel page-table overhead. Descriptors/results
are static; there is no process heap or dynamic result log.

On each dispatch, the architecture copies only upper-half kernel PML4 entries,
clearing U/S in every copied entry, then writes CR3 and updates RSP0 before
switching stacks. Shared high-half tables include kernel code/data, HHDM,
framebuffer, heap, exception stacks and all kernel entry stacks. Top-level
supervisor protection prevents user access even if a lower kernel table contains
U/S. Refreshing these entries handles kernel stack allocation/reclamation without
making private roots owners of shared tables. Kernel tasks select the adopted
boot root. NX is required; LA57 and active PCID configurations are unsupported.
PGE is disabled so inherited global translations cannot bypass a fresh root;
CR3 reloads flush non-global translations on every switch. No PCID/TLB shortcut
is used. CR0.WP remains set.

## Scheduling, faults and cleanup

A process is one scheduler task with an owned root and deferred cleanup callback.
IRQ0 retains each interrupted user frame on that task's RSP0 stack. After PIC
EOI, the existing five-tick round-robin scheduler can switch between CPL0 and
CPL3 contexts; the eventual IRET restores the user frame. IRQ handlers perform
no address-space allocation or reclamation.

Synchronous user exceptions initially enter the existing shared IST. Assembly
copies the complete saved frame to the current task's private RSP0 stack before
calling the user termination path. The task never suspends or is abandoned on
the shared IST. Exit traps already use private RSP0. The process result records
status, vector, error, RIP/RSP, selectors/flags, CR2, kernel frame and original
root. The task becomes ZOMBIE and switches away without freeing anything.
Foreground reap destroys only inactive lower-half tables/owned user frames,
then frees the inactive guarded kernel stack. Shared/boot tables are never freed.
Partial construction rolls back all owned frames; stale task generations are
rejected. Kernel cancellation records a separate status (-1).

User faults record status -2; exit records RAX. Results remain in bounded kernel
storage until the process slot is reused, after which stale result IDs fail.
Recorded physical root/frame values are diagnostic history after reclamation,
not live handles. Kernel faults still halt the shared kernel. NMI, double fault,
machine check and reserved-bit page-table faults remain fatal system conditions.
This is not recovery from a damaged kernel or arbitrary hardware failure.

## Verification

- `make test-address-spaces`: ASan/UBSan tests of actual portable page-table code
  with modeled physical frames: distinct same-VA frames, zeroing, W^X, invalid
  ranges, capacity, active-root destruction refusal, every partial table/leaf
  allocation failure point, retry and complete reclamation.
- `make test-tasks`: existing scheduler policy tests plus process construction,
  kernel-stack provisioning failure, cancellation, inactive-root cleanup and
  disabled general syscall contracts. Native switches/CR3 are models.
- `make verify-user`: mode 1 and 2 under BIOS and UEFI. Actual CPL3 workers use
  the same virtual data address with different values and counters while the
  bootstrap busy-waits; repeated preemption, root switching, NX/RX/W permissions,
  selectors, exit and exact PMM reclamation are checked. Twenty-one isolated
  fault cases cover kernel read/write/execute, HHDM, page-table writes, another
  live process's frame and kernel stack, code writes, stack execution/guards,
  absent pages, UD, CLI, port I/O, forbidden gates, SYSCALL/SYSENTER, unsupported
  FPU, unusable user RSP, and divide error. Repeated exit/reuse validates cleanup
  and generations. Mode 2 exhausts real PMM frames partway through page-table
  construction, checks exact rollback, restores capacity and retries successfully.
- `make verify-user-load`: two continuously executing CPL3 workers alongside
  real desktop/window/mouse/keyboard/theme operations on both firmwares. Task
  diagnostics prove both user workers received multiple dispatches and timer
  samples while GUI focus, resize, maximize, close, dock/launcher restoration,
  appearance changes and terminal return work. This disposable fixture stops
  with QEMU; normal boot does not spawn these infinite workloads.

All fixture images are isolated and fail closed on an unexpected result. Tests
verify actual fault vectors/error bits and kernel survival, rather than treating
any fault as success. Native tests cannot validate CPU privilege transitions;
only QEMU covers GDT/TSS/CR3/IRET and assembly context behavior.

## Limits and next work

Single bootstrap CPU, QEMU TCG/q35/qemu64 and four-level paging only. Real
hardware, Intel-specific entry behavior, SMP and timing guarantees are not yet
verified. Kernel threads still share trusted ring-0 memory and can corrupt the
kernel. There is no KPTI or speculative-execution mitigation, no DMA/device
isolation, no multithreaded user process, TLS, signals, IPC, user heap, resource
quota beyond fixed limits, FPU/SIMD switching or process-specific I/O ownership.
Shared kernel mappings must stay supervisor-only as future drivers evolve.

User-pointer validation/copying and a general syscall dispatcher, validated
executable loading and application resource ownership remain the next work.
These are required before claiming application support. Storage/filesystem
integrity and Core Alpha release hardening are also pending. This phase stops
here; Phase 8 performance/crash/release work and optional features are not started.

## Recorded results — 2026-10-13

Final verification exited 0: eleven native ASan/UBSan suites and 68 QEMU
BIOS/UEFI boots/checks. Six covered Ring 3 isolation/rollback and sustained user
load; six covered existing kernel task/context/guard/rollback behavior; 54 covered
all existing desktop/input/graphics/boot/memory/heap/exception regressions; two
repeated normal window/desktop checks after the final launcher-label correction.
The 21 expected user fault cases passed on both firmwares without a kernel halt.
Exact PMM reclamation and the real partial allocation rollback passed. Native
process checks also passed task-capacity rollback, kernel-stack allocation failure,
cancellation and stale result IDs after reuse. Desktop screenshots under user
load were visually inspected; design, adaptive themes and bottom dock are preserved.

Evidence:

- `build/user-final-verification.log`: native ownership/task checks and six
  BIOS/UEFI isolation/rollback/load checks.
- `build/user-final-desktop.log`: nine other sanitizer suites and 18 normal
  graphical/input/boot regression checks.
- `build/user-final-foundation.log`: six kernel task checks plus 36 CPU/memory/
  paging/heap regression checks.
- `build/user-final-review.log`: expanded native process checks and final normal
  BIOS/UEFI window/desktop checks.
- `build/user/load/{bios,uefi}/windows-final.png`: desktop with sustained users.
- `build/user/mode-1/{bios,uefi}/*-debug.log`: each expected isolated fault.

No final verification failures remain. The initial SYSENTER fixture expectation
was corrected as described above; no access permission was relaxed. The default
image is a normal boot with no test workers. The current phase is complete;
no subsequent syscall/loader phase or Phase 8 work was started.
