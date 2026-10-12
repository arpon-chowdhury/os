# Physical memory management

The x86-64 boot adapter requests the Limine memory map and Higher Half Direct
Map (HHDM), then copies up to 256 map entries into a bootloader-independent
array in kernel BSS. It never reads responses if base-revision negotiation
fails. Only `LIMINE_MEMMAP_USABLE` becomes allocatable. Every other type,
including unknown types, remains protected. No bootloader or ACPI memory is
reclaimed: the current stack, responses, and page tables are still in use.
These contracts follow the official [Limine memory-map and direct-map
protocol](https://github.com/limine-bootloader/limine-protocol/blob/trunk/PROTOCOL.md).

## Initialization and protection

`pmm_init` validates the entire map before writing physical memory. Missing
responses, empty/oversized maps, zero-length entries, unsorted or overlapping
entries, overflowing address ranges, missing/unaligned direct maps, unusable
maps, and insufficient metadata space disable the allocator. Normal boot logs
the failure and continues displaying the welcome screen and running the PIT.
A failed initialization may be retried; a successful one cannot be reset.

Usable ranges are rounded inward to complete 4 KiB pages. Page zero, gaps,
partial pages, firmware/reserved/bad memory, kernel/modules, framebuffer memory,
and bootloader memory never enter the allocation pool. Range descriptors live
in the kernel image, already excluded by Limine's map. The bitmap is stored
in a contiguous run of usable frames, accessed through the supplied HHDM.
Those entire frames, including padding, are permanently reserved for metadata
and cannot be returned or freed by the public API.

The bitmap uses one bit per aligned usable frame, with compact bit ranges per
usable interval. Sparse addresses and high MMIO regions do not force metadata
to cover the intervening holes. Initialization does not retain pointers into
the boot memory map. Existing bootloader page tables remain active. The [virtual memory manager](virtual-memory.md)
adds owned mappings without replacing or reclaiming boot tables. No heap, SMP
support, or boot memory reclamation is introduced.

## API and statistics

`kernel/include/memory.h` exports:

- `pmm_alloc_page(&physical)`: allocate one unzeroed frame and return its physical
  address. False means failure; the output value remains unchanged.
- `pmm_free_page(physical)`: free a currently allocated frame. Rejects unaligned,
  reserved, metadata, out-of-range, never-allocated, and double-free addresses
  without changing statistics. Callers must own the frame being freed.
- `pmm_page_pointer(physical)`: borrow the HHDM pointer for a currently allocated
  frame, or return NULL. Keep ownership until all accesses finish; physical
  addresses themselves must not be dereferenced as virtual pointers.
- `pmm_get_statistics()`: return a consistent snapshot.

All state operations disable local interrupts and restore the caller's prior
IF state, allowing foreground and IRQ callers on the single bootstrap CPU.
This is not an SMP lock. A cursor keeps successive allocations efficient and
prefers a just-freed frame. Allocator metadata is private; page contents are
not cleared on allocation or deallocation.

Statistics report map byte coverage, protected bytes outside aligned usable
frames, usable frames, metadata frames, allocated frames, free frames, and
metadata physical base. Map coverage includes reserved address space and MMIO;
it is not the machine's installed RAM size. Holes outside the map are neither
covered nor allocatable. The accounting identities are:

```text
map_bytes = reserved_bytes + usable_frames * 4096
usable_frames = metadata_frames + allocated_frames + free_frames
```

Metadata is accounted for separately from reserved bytes. Initial allocated
frames are zero, and metadata reduces the initial free count.

## Controlled verification

```sh
make test-memory
make verify-memory
make verify-interrupts verify-boot test-framebuffer verify-exceptions
```

Native allocator tests use AddressSanitizer/UndefinedBehaviorSanitizer and a
modeled interrupt state. They cover malformed/missing maps, arithmetic overflow,
unaligned entries, page-zero exclusion, gaps, reserved ranges, metadata write
boundaries, invalid frees, double frees, uniqueness, exhaustion, reuse, sparse
RAM above 4 GiB, metadata placement in a later range, initialization lifetime,
10,000 mixed allocation/free operations, and statistics/IF preservation.

`verify-memory` builds isolated images beneath `build/memory/mode-<mode>/`:

| PMM_TEST | Controlled behavior |
| --- | --- |
| 0 | Normal initialization and statistics; no destructive allocator selftest |
| 1 | Allocate every free frame, touch both ends via HHDM, check provenance and exhaustion, then free all frames and verify restored statistics |
| 2 | Ignore the memory-map response, verify allocation stays disabled |
| 3 | Ignore the HHDM response, verify allocation stays disabled |

All three test modes run under both BIOS and fresh OVMF NVRAM with 128 MiB and
256 MiB guests: 12 emulator runs. Frame-linked test bookkeeping avoids a fixed
allocation-array limit; traversal and frame-content checks detect duplicate
allocation or corruption. Tests reject frees of each reported non-usable
region and every metadata frame. Full restoration includes another successful
allocation/free cycle. Every run must subsequently reach 100 and 200 timer
ticks and retain the welcome screen. Failure markers and inconsistent statistics
fail the verifier; debugcon and COM1 diagnostics must agree.

`PMM_TEST` must be 0–3, and nonzero modes require an isolated `BUILD` beneath
`build/`. PMM and exception injections cannot be combined. Both mode values
appear in object paths and `build-mode` records, preventing stale test images
when switching modes. Existing six exception tests still run separately under
both firmware paths, now after normal PMM initialization.

Screenshots, fresh serial/debug logs, and build logs remain beneath each mode
and RAM directory. Physical hardware remains unverified. The usable-only policy
intentionally leaves reclaimable memory untouched until later boot and paging
work can establish that it is no longer needed.

On 2026-10-13, all 12 memory runs passed, along with sanitized native allocator
and framebuffer tests, BIOS/UEFI normal boot and timer checks, the no-VGA
fallback, and all 12 existing exception tests. All 26 normal, memory-test, and
exception screenshots matched the pre-allocator welcome PNG byte for byte.
A PMM mode switch sequence 1 → 0 → 3 → 0 correctly relinked and excluded test
messages from normal kernels. The normal ELF had no undefined symbols.
The initial 64 MiB UEFI attempt stopped inside Limine with a high-memory
allocation panic before kernel entry; the shared matrix therefore uses
128 MiB and 256 MiB. Verification logs are in `build/pmm-verification.log`
and `build/pmm-native.log`.
