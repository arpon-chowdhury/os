# Kernel heap

The kernel provisions a fixed 1 MiB heap at `0xffffd00000000000` after PMM and
VMM initialization. Its 256 owned physical frames are mapped supervisor-only,
writable and non-executable. Bootloader mappings remain untouched. Provisioning
failure unmaps only pages created by that call, returns their data frames, and
lets VMM reclaim empty page tables. Existing boot, screen, timer, and diagnostic
services continue when heap setup fails. The heap retains its backing pages for
the kernel's lifetime; it does not grow or return backing pages on individual frees.

`kernel/include/heap.h` exports:

- `kmalloc(bytes)`: uninitialized memory aligned to 16 bytes; NULL for zero size,
  overflow, exhaustion, unavailable heap, or detected invalid metadata.
- `kmalloc_aligned(bytes, alignment)`: nonzero power-of-two alignment. Alignments
  below 16 are promoted to 16; larger alignments are limited by available space
  and checked arithmetic. The returned memory has at least `bytes` usable bytes.
- `kfree(pointer)`: true for a successful free or NULL; false for an invalid,
  interior, foreign, or already-freed pointer. It searches live allocations
  without dereferencing the supplied pointer. A stale pointer cannot be
  distinguished from a new allocation that reuses exactly the same address.
- `kheap_get_statistics()`: a snapshot of arena size, live requested bytes,
  allocated capacity, free capacity, metadata, block/live allocation counts,
  peak requested bytes, successful allocations/frees, failed allocations,
  and invalid frees. NULL frees do not change counters.
- `kheap_validate()`: check the block chain and live requested-byte accounting.
- `kheap_init(arena, bytes)`: initialize one exclusively owned writable arena.
  Its base and size must be 16-byte aligned, non-overflowing, and large enough
  for a header and payload. Failures do not write arena memory. Only one
  successful initialization is permitted; the caller retains arena ownership.

Allocation uses first fit over an ordered list of contiguous block spans.
Headers occupy 48 bytes on the current 64-bit target. Tail splitting occurs only
when the remainder can hold another header and at least 16 payload bytes.
Alignment padding stays in the allocated span until it is freed. Freeing merges
both adjacent free neighbors, recovering obsolete headers and padding. No two
free blocks remain adjacent. The statistics satisfy:

```text
arena_bytes = metadata_bytes + allocated_bytes + free_bytes
requested_bytes <= allocated_bytes
```

`allocated_bytes` includes alignment padding and rounded/slack payload capacity,
but excludes headers. `free_bytes` excludes each free block's header and is not
a guarantee that one allocation of that size fits when the heap is fragmented.
Detected metadata corruption causes allocation/free to reject the operation;
validation checks bounds before dereferencing linked headers. It does not offer
protection against arbitrary client buffer overruns. Clients must respect the
requested size and stop using memory after free.

All state operations save and disable local interrupts and restore the prior
IF state. The allocator is for the single bootstrap CPU. Allocation, validation,
and free traverse a bounded list; they are not constant-time operations. NMI
allocation, SMP, automatic growth, realloc/calloc, and guard-page allocations are
outside this implementation.

## Controlled tests

```sh
make test-heap
make verify-heap
make test-memory test-framebuffer verify-boot verify-interrupts
make verify-virtual verify-memory verify-exceptions
```

The shared stress runner performs 20,000 deterministic mixed operations, checks
live payload patterns, proves allocations do not overlap, verifies statistics,
then frees everything and requires a single original free span. It also covers
alignment through 64 KiB, zero-size/overflow/invalid alignment, NULL/foreign/
interior/double frees, adjacent-block coalescing, full-arena allocation, complete
exhaustion, and reuse after restoration.

`test-heap` uses ASan/UBSan and runs the stress suite twice (40,000 mixed
operations), once with modeled IF set and once cleared. It additionally checks
malformed initialization without writes, repeated initialization, outer arena
guards, and fail-closed behavior for a corrupt metadata span. Native tests do not
link sanitizer/runtime code into the kernel.

`verify-heap` builds isolated images with `HEAP_TEST=1` (stress and page provenance/
permissions) and `HEAP_TEST=2` (failure after mapping three pages, followed by
full rollback checks). Both modes run under BIOS and fresh OVMF UEFI. Stress mode
checks that provisioning consumes exactly 256 frames plus three table frames,
and that allocation/free leaves those backing-frame counts unchanged. Every
successful test reaches 200 PIT ticks and retains the welcome display, with
matching debugcon/COM1 success markers. Normal images use `HEAP_TEST=0` and do
not contain the shared stress runner. Build modes encode all four injection flags.

On 2026-10-13, all four heap BIOS/UEFI runs and all 36 existing QEMU regression
runs passed. Sanitized heap, physical allocator, and framebuffer tests passed.
The final normal BIOS/UEFI timer/display checks and no-VGA fallback also passed.
All 38 fresh welcome captures matched the stable baseline byte for byte. The
normal ELF had no undefined symbols or stress/fault injection symbols. Evidence
is in `build/heap-verification.log`, `build/heap-regression.log`, and
`build/heap-final-boot.log`; physical hardware and SMP remain unverified.
