# x86-64 virtual memory

The virtual memory manager adopts the active Limine CR3 without replacing,
cloning, or reclaiming any bootloader page table. Kernel code/data, the boot
stack, exception stacks, framebuffer, HHDM, and firmware mappings remain intact.
It requires four-level paging, a validated memory map, and an initialized PMM.
Missing boot information disables VMM while existing boot features continue.
Five-level paging is rejected. Physical address width comes from CPUID.

`kernel/include/virtual.h` exposes:

- `vmm_translate(address, &result)` resolves any canonical mapped address,
  including offsets within 4 KiB, 2 MiB and 1 GiB pages. Results include physical
  address, leaf page size, and effective permissions combined across all levels.
- `vmm_map(address, physical, permissions)` creates a 4 KiB mapping. Addresses
  must be aligned, canonical, and within CPU physical address limits. Duplicate
  mappings are rejected. The physical frame belongs to the caller; mapping does
  not allocate or free the data frame, nor validate its ownership.
- `vmm_protect(address, permissions)` changes an owned leaf's permissions.
- `vmm_unmap(address, &physical)` removes an owned leaf and optionally returns its
  physical frame. Empty owned tables are freed; the data frame remains owned by
  the caller. A NULL output is supported.

Permissions are `VMM_WRITE`, `VMM_USER`, and `VMM_EXEC`; all present pages are
readable. Zero permissions means supervisor, read-only, non-executable. CR0.WP
is enabled for supervisor write protection. NX uses the bootloader's EFER.NXE;
if NX is disabled, requests for non-executable pages fail rather than silently
weakening protection. The design follows Intel's [system programming manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

For preservation, mutation is restricted to initially empty PML4 slots (each
covers 512 GiB). Existing bootloader slots are never extended or changed, even
if a lower-level hole exists. There is no huge-page splitting, replacement,
cache-policy API, separate address space, heap, or SMP shootdown. New intermediate
entries permit write/user access; the leaves enforce requested permissions.
Callers must keep aliases and frame lifetime consistent: permissions on one
alias do not change another alias, including the HHDM.

New tables are zeroed before publication. Partial allocation failures unwind
only tables created by that call, with no frame leak or surviving mapping.
Every leaf change and table removal invalidates the address with INVLPG before
reusing table memory. Calls preserve the prior interrupt state and serialize
on the single bootstrap CPU. Failures preserve output arguments.

## Verification

```sh
make verify-virtual
make verify-exceptions
make verify-memory verify-interrupts verify-boot test-memory test-framebuffer
```

`verify-virtual` uses isolated `VMM_TEST=1` images under BIOS and UEFI. Tests
verify unchanged kernel translation, refusal to modify boot mappings, invalid
inputs, duplicate mapping rejection, physical aliases and byte offsets,
effective write/user/execute permissions, adjacent mappings, stale-TLB prevention
after remapping to a different frame, and complete table reclamation. A low-memory
test leaves only two free frames, forces failure while constructing three levels,
and checks rollback and reuse. Success requires matching COM1/debugcon markers,
200 timer ticks, and the welcome screenshot.

Exception injection modes 7, 8, and 9 test actual CPU enforcement: a write after
making an alias read-only, execution from an NX alias, and a write after unmapping
a previously accessed alias. Both firmware paths must report page fault vector
14, the expected error bits (3, 17, 2), exact fault RIP and CR2, and a valid IST
stack. The existing six exception modes are retained. Physical hardware and SMP
remain unverified.

On 2026-10-13, BIOS and UEFI VMM tests passed, including allocation rollback
and mappings across PT, PD, PDPT, PML4 boundaries and the canonical upper half.
All nine fault cases passed under both firmware paths. The regression suite
passed all 12 PMM runs, normal BIOS/UEFI timer/framebuffer checks, the no-VGA
fallback, boot smoke test, and sanitized native allocator/renderer tests.
Normal and VMM BIOS/UEFI welcome screenshots had identical SHA-256 hashes.
The normal ELF had no undefined symbols. Logs are in
`build/vmm-verification.log`, `build/vmm-regression.log`, and `build/vmm-faults.log`.
