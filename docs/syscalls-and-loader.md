# Native syscall ABI and static ELF applications

ON. OS now executes independent, bounded ELF64 applications in isolated Ring 3
address spaces. This completes the syscall/loader phase, not full application
compatibility or the Core Alpha release. Normal boot still launches no user
applications; the existing desktop, adaptive themes, bottom dock and input remain
kernel-owned. The subsequent [storage phase](storage.md) adds bounded kernel disk
I/O; filesystem execution and application GUI services are pending.

## Entry, return and ABI v1

The public contract is [include/on/syscall.h](../include/on/syscall.h). Applications
use `int $0x81`: RAX holds the number, and RDI, RSI, RDX, R10, R8, R9 hold arguments
in that order. RAX returns a signed result; errors are negative errno values.
All other general registers and arithmetic flags/DF are preserved. IF returns
set; IOPL, NT, VM, TF and other unsupported flags are cleared. Query number 0
returns ABI version 1. Numbers 0–3 retain their previously reserved assignments.

| Number | Operation | Arguments | Result |
| --- | --- | --- | --- |
| 0 | ABI version | none | 1 |
| 1 | Yield | none | 0 when rescheduled |
| 2 | Exit | signed status | does not return |
| 3 | Write | fd, user pointer, byte length | bytes written |
| 4 | Allocate | requested byte length | user virtual base |
| 5 | Free | allocation base, original requested length | 0 |

Unknown numbers return `-ENOSYS`. Write accepts fd 1 or 2, snapshots at most 256
bytes into a private kernel buffer, and writes that snapshot to COM1/debugcon.
It does not update the terminal or expose a kernel pointer. Invalid descriptors
return `-EBADF`, excessive lengths `-E2BIG`, invalid memory `-EFAULT`. A zero-length
write accesses no user memory; the descriptor must still be valid. Output from
several tasks can interleave.

Allocation accepts 1–32768 bytes, rounds up to pages, zeroes every frame and maps
RW/NX memory in the 64-page window starting at `0x10000000`. Each process has four
allocation records and a total limit of 16 user pages including ELF and stack.
Insufficient capacity returns `-ENOMEM`; invalid sizes return `-EINVAL`.
Free requires the exact owned base and original byte length. Interior pointers,
wrong lengths, double frees and attempts to free code/initial stack return
`-EINVAL`. Outstanding allocations are reclaimed on exit, fault or cancellation.
No sharing, protection changes or executable anonymous pages are exposed.

IDT vector 129 is a DPL3 interrupt gate with IST 0. Hardware uses the scheduled
task's TSS.RSP0 and guarded private 16 KiB supervisor stack. Assembly saves all
registers, clears DF for kernel execution, and returns with IRETQ. SYSCALL and
SYSENTER remain disabled; no SYSRET canonical-address hazard is introduced.
The kernel verifies fixed Ring 3 CS/SS, canonical lower-half executable RIP and
RW/NX RSP before dispatch **and again before return**. An empty one-past-end
stack pointer is accepted. An invalid continuation terminates that process with
`-EFAULT`; a syscall cannot return into kernel code or an unmapped continuation.
Service work permits timer interrupts; allocation/copy transactions save IF
and run atomically on the current single CPU. Existing user-fault containment
and deferred, inactive-stack/root reclamation remain in use.
The old `int $0x80` exit fixture remains separate from ABI v1; the old
`syscall_unavailable` compatibility hook still returns ENOSYS and is not dispatch.

## User copies and isolation

Copy helpers validate the **whole** byte range before copying anything: arithmetic
bounds, canonical lower-half addresses, present user permissions at every paging
level, and write permission for copy-out. They reject null-page/high-half,
unmapped, wrapping and cross-page invalid ranges. They copy through owned-frame
HHDM aliases rather than dereferencing untrusted virtual pointers. IF remains
clear across validation and copying, preventing mapping changes between the two
passes on this single CPU. Zero-length copies perform no memory access.

Private lower-half page tables and supervisor-only shared kernel mappings retain
NX, CR0.WP and W^X. Live anonymous map/unmap invalidates current-root translations
before reuse/free; switching roots flushes inactive translations (PGE/PCID are
not enabled). No syscall accepts a physical address or another process ID as a
memory authority. Generic copy helpers also cap work at the 16-page quota.

## Accepted ELF subset and loading transaction

`kernel/exec/elf.c` uses bytewise reads and bounded arithmetic; it does not cast
unaligned ELF bytes to host structs. The loader accepts only:

- At most 128 KiB, ELF64 little-endian current-version System V ABI, x86-64
  `ET_EXEC`, normal 64-byte ELF and 56-byte program headers, no extended counts.
- One to eight program headers: readable PT_LOAD segments, all-zero PT_NULL,
  and optionally one zero-sized, non-executable RW PT_GNU_STACK.
- Ascending, page-disjoint segments with valid file extents, `filesz <= memsz`,
  power-of-two alignment from 4 KiB through 2 MiB, and congruent offset/address.
  Unknown permission bits and writable executable segments are rejected.
- Image pages from `0x400000` to below `0x10000000`, excluding the initial stack
  and both guard pages. At most 14 image pages leave two pages for the stack.
- Entry inside file-backed executable bytes. Entry in a gap, BSS or data fails.
  Optional section-table metadata has bounded extent/count/string-table index;
  sections are not loaded. `p_paddr` is ignored and grants no physical authority.

PT_INTERP, PT_DYNAMIC, PT_TLS and other unsupported headers fail validation.
There is no dynamic linking, relocation, PIE, Linux ABI or executable stack.
The supported subset follows the [ELF program-header specification](https://gabi.xinuos.com/elf/07-pheader.html);
privilege transitions follow the [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

`process_load_elf` accepts a trusted kernel-owned immutable byte span, not a user
pointer. Validation allocates nothing. Loading creates an inactive owned root,
zeroes pages including BSS/padding, copies file bytes, and maps final RX/RW-NX
permissions. It adds a private two-page RW/NX stack at `0x800000`–`0x802000`,
with unmapped guards, then creates a scheduler task with its own kernel stack.
Only complete tasks are published. Any failure destroys the partial root and
leaves no live process. The API returns task ID or 0; detailed loader errno is
not currently exposed. A fresh process enters at its ELF entry with cleared
general registers and aligned empty stack; no argc/env/auxv or return address.
A bare function return therefore faults safely. Each process has one thread.

The modular boundary is x86 entry/return in `kernel/arch/x86_64/syscall.c`, portable
service dispatch in `kernel/services/syscall.c`, executable parsing/population in
`kernel/exec/elf.c`, and resource/task ownership in `kernel/sched/process.c`.
Freestanding bytewise memcpy/memset support compiler-generated structure copies.

## Programs and verification

`make apps` builds separate static ELF executables from `apps/tests/*.S` using
`apps/linker.ld` and the public ABI header. Test images embed these ELF files;
the normal image embeds none. This proves independent executable loading, not
filesystem launch support.

```sh
make test-elf test-address-spaces test-tasks
make verify-apps
make verify-app-load
```

Native ASan/UBSan checks cover malformed/truncated headers, 10,000 deterministic
header mutations, RX/RW-NX/BSS/guards, all modeled population failure budgets,
whole-loader rollback including task-stack failure, invalid pointers, all-or-none
copies, allocation rollback, quotas, exact free and deferred cancellation.
Native scheduler/CR3 switching remains a model; real transitions are QEMU checks.

QEMU modes 1 and 2 run under both BIOS and UEFI: two same-image processes have
distinct frames at identical virtual addresses and independent initialized/BSS
state; ABI/register/flags, valid output, cross-page copies, invalid pointers,
unknown syscalls, allocation/free/zeroing, yield and exit execute through the
actual interrupt gate. Loaded kernel-page faults and invalid return stacks are
contained, including outstanding allocation reclamation. Twenty repeated
launches exit cleanly with exact PMM restoration and stale-ID rejection. Mode 2
rejects 18 malformed real-ELF mutations plus truncation without allocating, forces
real PMM exhaustion during loading, restores frames, and successfully retries.
Mode 3 keeps two loaded applications allocating/touching private memory and
issuing yield syscalls while the full desktop/window/input regression runs.

### Actual results — 2026-10-13

All 12 native ASan/UBSan suites passed: tasks, address spaces, ELF, physical
memory, heap, framebuffer, graphics, keyboard, mouse, terminal, retained UI and
windows. All 72 QEMU checks passed, counting each firmware/configuration once:

| Checks | Count | Evidence |
| --- | --- | --- |
| New ELF/syscall modes 1–2, BIOS/UEFI | 4 | `build/apps-final-verification.log`, `build/apps-test/mode-{1,2}/{bios,uefi}/` |
| Loaded ELF desktop/window stress, BIOS/UEFI | 2 | `build/apps-load-verification.log`, `build/apps-test/load/{bios,uefi}/` |
| Existing Ring 3 modes 1–2, BIOS/UEFI | 4 | Passing first four checks in `build/apps-user-regression.log`, `build/user/mode-{1,2}/` |
| Existing Ring 3 desktop load and task modes 1–3, BIOS/UEFI | 8 | `build/apps-user-load-regression.log`, `build/user/load/`, `build/tasks/` |
| Exceptions, PMM at 128/256 MiB, paging and heap | 36 | `build/apps-foundation-regression.log` |
| Existing windows, desktop, UI, input, graphics, framebuffer/fallback and BIOS/UEFI boot | 18 | `build/apps-desktop-regression.log` |

Native evidence is in `build/apps-native-core.log` and the native prerequisite
output in the foundation/desktop logs. Each final command exited 0. The earlier
combined user log ends in the diagnostic timing failure described below; its
four isolation checks passed, and its load check was rerun successfully in the
separate final user-load log. Failed exploratory runs and duplicate reruns are
not included in the 72-check count.

The UEFI loaded-app screenshot `build/apps-test/load/uefi/windows-final.png` was
visually inspected: theme controls, multiple retained windows, dock and launcher
remain present. Both loaded tasks reported repeated dispatches and nonzero CPU
ticks during desktop interaction. Normal `build/on.iso` was also verified and
`build/build-mode` is `0-0-0-0-0-0-0` (no injections or embedded test apps).

Known-good revision `c7f694b` was preserved as `checkpoint/ring3-c7f694b` before
implementation. Only repository/build files were changed; no host macOS
installation, system setting or physical disk was modified.
The initial kernel link exposed missing freestanding memcpy/memset; bytewise
routines fixed the link. Initial desktop stress verification sampled incomplete
task diagnostics and a fast-yielding task with zero sampled CPU ticks. The verifier
now waits for the complete COM1/debugcon report, and the application performs
longer work between yields to exercise timer preemption. No permission was relaxed.

## Remaining limits and risks

Verified target is single-CPU QEMU TCG x86-64 (`qemu64`), BIOS and OVMF UEFI.
Real hardware, SMP and other CPU models remain unverified. FPU/SIMD, TLS, shared
memory, signals, IPC, file handles, app discovery/launch UI, filesystem source,
GUI application APIs and package compatibility are absent. KPTI, DMA isolation
and speculative side-channel hardening are not implemented. Kernel faults stay
fatal; bounded user faults terminate only their process. This is a lightweight
static ON. ABI foundation, not general Linux/Windows/macOS application support.
This historical phase record predates [bounded disk I/O](storage.md). Filesystem
integrity remains pending; no
optional features, host macOS settings, or Phase 8 release work were changed.
