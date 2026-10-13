# Cooperative kernel tasks

This stage adds actual independent kernel execution contexts. GUI windows remain
retained surfaces owned by the foreground task; creating a window does not create
a task or process. Every kernel task runs at CPL0 in the shared boot CR3. There
is no user-mode execution, process loader, private address space or preemption.

## Running behavior

The bootstrap context becomes task `foreground`, retaining the original boot
stack, keyboard/mouse queue draining, terminal and GUI rendering. A separate
`scheduler-monitor` task uses a guarded stack and sleeps for approximately one
second between diagnostic wakeups. It is a scheduling exercise, not an
application or invented desktop service. The foreground loop yields after each
input/render batch, and sleeps with the existing atomic STI/HLT sequence.

The scheduler chooses the next ready slot in round-robin order. Tasks explicitly
call `task_yield`, `task_sleep` or return/`task_exit`. IRQ0 only increments ticks
and accounting; it never switches contexts. Keyboard and mouse IRQ handlers
retain their existing queue-only role. No drawing, allocator calls or reclamation
are performed from IRQ0. A task that never yields can starve other tasks, even
though interrupts continue; there is no claim of timer preemption or hard
real-time fairness.

Type `tasks` in the terminal for IDs, names, states, dispatch counts and CPU/idle
sample counts. Debugcon/COM1 include the same scheduling diagnostics. CPU counts
are approximately 100 Hz PIT samples charged to the interrupted task; they are
coarse active-execution samples, include interrupt overhead, and can miss work
shorter than a tick. Foreground HLT intervals are counted separately as idle.
These are not cycle-accurate utilization percentages or host CPU measurements.
Total dispatches include tasks that have since been reaped.

## API and lifecycle

`kernel/include/task.h` defines a fixed eight-slot table, including the protected
bootstrap task. `kernel/sched/task.c` owns lifecycle and scheduling policy;
`kernel/arch/x86_64/task.c` owns guarded stack mappings; `task_switch.S` owns the
callee-saved machine context. `kernel/sched/boot.c` constructs the diagnostic
task and controlled test tasks. Window/process management remains separate.

| API / state | Contract |
| --- | --- |
| `task_init` | Adopt bootstrap stack as RUNNING; one initialization only |
| `task_create` | Allocate a guarded stack, initialize entry/argument, become READY; zero ID on failure |
| `task_yield` | RUNNING → READY, select the next ready context |
| `task_sleep` | RUNNING → SLEEPING until the absolute PIT deadline; bootstrap cannot sleep through this API |
| Entry return / `task_exit` | Current non-bootstrap task → ZOMBIE; never resumes |
| `task_terminate` | Other READY/SLEEPING task → ZOMBIE; cannot terminate self or bootstrap |
| `task_reap` | Release ZOMBIE stacks from another context and make slots FREE |
| Fatal kernel fault | Current task → FAULTED, report identity, halt the shared kernel |

IDs encode a 24-bit generation and 8-bit slot. Reclaimed slots get a different
ID on reuse; stale IDs are rejected. A slot is retired rather than wrapping its
generation. Snapshot/list operations are bounded and preserve interrupt state.
All lifecycle/scheduling mutations run with local interrupts disabled on the
single bootstrap CPU. Nested allocator/VMM operations preserve that disabled
state. Each resumed C invocation restores its own saved IF state. Newly created
tasks enter with interrupts enabled.

These APIs are for trusted kernel callers, never IRQ handlers and never while
holding allocator/device locks. Entry arguments and any task-owned resources
other than the scheduler's stack remain caller-owned. Controlled cancellation
requires the caller to arrange resource cleanup; this is not a general process
kill API. Task code must obey the existing freestanding C ABI and
`-mgeneral-regs-only` compilation contract. There is no FPU/SIMD, FS/GS/TLS,
address-space, per-task TSS or extended-state switching yet.

## Stack safety and memory limits

Each non-bootstrap task owns four 4 KiB frames (16 KiB stack), mapped supervisor
writable and NX, with an unmapped page below and above. Stack intervals use
`0xffffc80000000000 + slot * 0x6000`; the writable base is one page above the
interval start. Completely empty intervals are required; boot/heap/framebuffer
mappings are never replaced. Frames are zeroed before task execution. The
initial stack aligns the C entry ABI and supplies a halt return trap.

Partial provisioning failure unmaps/frees every owned frame and empty page table.
Reclamation occurs only after switching away from a terminated task's stack.
The current stack is never freed, and bootstrap storage is never reclaimed.
Descriptors are fixed storage, with bounded names and no heap allocation.
Capacity exhaustion fails cleanly. Missing PMM/VMM or unavailable NX stack
mappings leave the foreground-only path working. The kernel heap is unchanged.

Guard pages catch accidental stack boundary access; NX prevents execution through
the stack mapping, and supervisor permissions prepare a future CPL3 boundary.
These protections are not isolation between trusted ring-0 tasks. All tasks can
access shared kernel mappings, including HHDM aliases, and can corrupt the kernel
if buggy. Bootstrap and existing exception stacks retain their prior mappings.
A kernel fault reports the task and uses existing IST exception handling before
halting; there is no unsafe attempt to recover and continue other kernel tasks.

## Disabled user-process and syscall preparation

`process.h` defines a future entry/stack layout and a bounded lower-half address
shape validator. Shape validation is not mapping validation or an authorization
check. `process_user_execution_enabled()` always returns false. No process or
user address space is created. GDT descriptors and IDT gates remain DPL0, and
CPU initialization explicitly clears EFER.SCE to disable SYSCALL instruction
entry. Existing TSS/IST stacks remain exception infrastructure, not a user-entry
stack-switch implementation.

`syscall.h` reserves ABI version 1: RAX number/result; RDI, RSI, RDX, R10, R8,
R9 arguments; RCX/R11 clobbered; negative errors. Proposed numbers are ABI query
(0), yield (1), exit (2), and bounded write (3). They are reserved definitions,
not working hardware syscalls. The preparation hook `syscall_unavailable`
returns `-ENOSYS` for every request and never dereferences user pointers.

Before enabling CPL3: implement owned process page tables with supervisor-only
kernel mappings and validated user ranges; audit/remove exposed boot aliases;
verify code read/execute and stack write/NX permissions; install user descriptors,
per-task kernel entry stacks, validated IRET/SYSRET frames, and bounded syscall
entry/copy-in/copy-out; add process-specific faults/teardown and context ownership;
verify privilege transitions and isolation under controlled fault tests. Current
VMM adopts one CR3 and cannot yet construct these independent process roots.

## Verification commands

`make test-tasks` runs native ASan/UBSan policy/ownership tests with a modeled
architecture switch and allocator: states, round-robin decisions, sleep deadlines,
termination/reap, stale IDs, capacity, allocation failure, IF restoration,
accounting and disabled process/syscall contracts. This native model does not
execute or verify the x86 assembly switch.

`make verify-tasks` builds isolated images for three modes under both BIOS and
UEFI: real stack/context switching with three independent worker stacks, all
callee-saved registers and local sentinels, IF restoration, sleep/wakeup, entry
return/exit and complete PMM reclamation; a deliberate lower-guard write with
expected page-fault frame/IST/halted CPU and task identity; partial allocation
failure with exact PMM rollback and successful retry. Capacity and stale IDs are
also exercised on the actual kernel. The privilege test inspects CPL0, DPL0 GDT/
IDT and disabled SYSCALL entry without entering user mode.

The normal GUI regression checks also query `tasks` after desktop interactions
to confirm that the independent monitor ran while the foreground GUI remained
responsive. Existing boot, exceptions, physical/virtual memory, heap, terminal,
keyboard, mouse, graphics, desktop and window tests remain separate checks.
No host settings, installations, physical disks or privileged host operations
are needed. QEMU TCG x86-64 is the verified platform; real hardware, SMP, user
processes and preemptive scheduling remain unverified/unimplemented.

## Recorded results — 2026-10-13

Final controlled task verification exited 0: six QEMU boots, modes 1/2/3 under
both BIOS and UEFI. Real x86 context/register/local-stack preservation, saved IF
restoration, sleep/wakeup, return/exit, reaping, capacity, generation rejection,
supervisor/NX stack permissions and unmapped guards passed. The expected guard
fault verified vector/error/CR2, saved stack, CPL0 frame, IST handler stack, task
identity and halted CPU. Partial provisioning restored exact PMM statistics,
and retry succeeded. DPL0 descriptors/gates and disabled SYSCALL entry passed
without enabling user execution.

Ten final native ASan/UBSan suites passed: task policy, windows, UI, graphics,
framebuffer, terminal, keyboard, mouse, physical memory and heap. Native task
checks use a modeled switch; real assembly is covered only by QEMU tests.
The full regression matrix exited 0 with 54 QEMU boots/checks: windows, UI,
desktop, graphics, terminal, keyboard and mouse (two each), framebuffer/fallback
(two), extra UEFI boot (one), BIOS smoke (one), virtual memory (two), heap
stress/rollback (four), physical-memory modes/RAM sizes (twelve), and nine
exception cases on both firmwares (eighteen).

The final normal image in `build/tasks-normal` passed window/desktop checks
again under BIOS and UEFI after diagnostic-label refinements. Task diagnostics
after GUI interaction confirmed monitor dispatches while keyboard/mouse/window
controls and terminal restoration worked. Diagnostic and GUI PNGs were visually
inspected. The default kernel matches that tested normal kernel byte-for-byte.
No final test failures remain.

Logs: `build/tasks-final-verification.log`, `build/tasks-final-native.log`,
`build/tasks-gui-verification.log`, and `build/tasks-regressions.log`.
Captures include `build/tasks-final/tasks/mode-1/{bios,uefi}/task-diagnostics.png`
and `build/tasks-normal/windows/{bios,uefi}/windows-final.png`. Generated logs,
ISOs, local firmware variables and PNGs are ignored by Git.

An initial compile exposed a tag-name collision between the new task-state enum
and the architecture's TSS structure. Renaming the enum resolved it; subsequent
builds and checks passed. No host macOS settings, installations, physical disks
or privileged host operations were changed.
