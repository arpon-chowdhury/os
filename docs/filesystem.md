# Phase 10: bounded filesystem layer

This implements the filesystem work requested as **Phase 10**. It corresponds to
the filesystem gate in the original Core Alpha ordering; the native application
framework, file manager, optional features and release-hardening phases are not
included. The existing desktop, dock, themes, scheduler, Ring 3 and ABI v1 remain
in place. Normal boot does not mount, format or enable filesystem disk writes.

## Modular interface and scope

`kernel/fs/vfs.c` dispatches one mounted filesystem through a validated
`vfs_ops` table. `kernel/fs/onfs.c` implements ONFS v1 through a bounded device
callback interface, independently of ATA port code. The block adapter in the
controlled boot fixture uses the existing ATA-backed `block_read`/`block_write`;
successful writes already include FLUSH CACHE. All paths, buffers, filesystem
contexts and callback tables are trusted kernel objects. These interfaces are
not directly callable by Ring 3 programs; file syscalls and validated user-buffer
copies belong to subsequent application-service integration.

The API supports mount/unmount, create, mkdir, open/close, read/write at explicit
64-bit offsets, remove, path stat, directory enumeration and volume statistics.
Handles have separate read/write rights, eight bounded slots and monotonically
issued tokens that are not reused across remounts. Open files cannot be deleted;
nonempty directories cannot be deleted. Unmount refuses open handles, including
a poisoned mount until those handles are closed. Root cannot be removed.
Enumeration uses direct-child ordinals and reports name, type and size.

Paths must be absolute, NUL-terminated within 128 bytes, and contain components
of 1–31 ASCII letters/digits/underscore/hyphen/dot. `.` and `..`, repeated or
trailing slashes, relative paths and unsupported characters are rejected before
lookup. `/` names the root. Intermediate components must be directories. There
are no symlinks, hard links, mount crossing or implicit traversal normalization.

Reads and writes accept at most 4096 bytes per call. Offset beyond EOF fails;
reads at EOF succeed with zero bytes. Writes may overwrite or append but do not
create holes or truncate. `offset + length` is checked without wrapping, and a
file cannot exceed 4096 bytes. A zero-length valid operation makes no metadata
update. Output counts and output objects are published only on success. No
partial caller-buffer read occurs. Filesystem-owned workspaces must not be used
as caller buffers. A failed mutation does not publish a partial in-memory tree.

The VFS lock is nonblocking: concurrent/reentrant calls return BUSY, with local
interrupts disabled only for lock state changes. Backend I/O and checksumming
retain caller IF; runtime callers must use process context with interrupts
enabled, not interrupt handlers. The filesystem allocates no memory per operation.
Each mounted ONFS context requires about 33 KiB for two 16 KiB snapshots, bounded
handles and state; normal boot allocates no ONFS context. One mount and exclusive
ownership of its disk region are supported. Other kernel code must not issue raw
writes into a mounted volume.

## ONFS v1 disk format

This deliberately small custom format matches the current 512-byte/LBA28 ATA
PIO driver and its maximum eight-sector transfers. It avoids a complex external
filesystem implementation and confines every mutation to one explicitly supplied
region. It is not FAT, ext, POSIX or a general interoperability format.

A volume occupies **66 sectors** (33 KiB) at a kernel-supplied base LBA. Tests
bind it at LBA 64 on a fresh 4096-sector (2 MiB) raw image. On-disk metadata
cannot choose a different disk, base or volume size. Bounds are checked against
the device before any read, and every later I/O uses fixed bank-relative offsets.
Sectors outside that region are never touched by ONFS.

There are two 33-sector banks. Each contains a one-sector commit record followed
by a 32-sector payload. A payload has 2048 bytes of metadata and 14336 bytes of
file storage. There are 16 entries, including root; files/directories share the
remaining 15 slots. File storage is packed into 28 logical sectors. A mutation
rebuilds extents in node-index order, allowing deletion to reclaim and compact
space without modifying the active bank.

All integers are little-endian and decoded bytewise, never by casting disk bytes
to packed C structs. Reserved and unused bytes must be zero.

| Commit-record offset | Field |
| --- | --- |
| 0 | Eight-byte magic `ONFSCOW1` |
| 8 | u32 version = 1 |
| 12 | u32 bank index = 0 or 1 |
| 16 | u64 base LBA, exactly matching the caller's binding |
| 24 | u32 volume sectors = 66 |
| 28 | u32 payload sectors = 32 |
| 32 | u64 nonzero generation |
| 40 | u32 CRC-32 of all 16384 payload bytes |
| 44–507 | Zero |
| 508 | u32 CRC-32 of record bytes 0–507 |

Payload bytes 0–7 are `ONNODES1`, followed by u32 version 1 and u32 entry count
16 at offsets 8 and 12. Bytes 16–511 are zero. Sixteen 64-byte entries begin at
byte 512; bytes 1536–2047 are zero. Data begins at byte 2048.

| Entry offset | Field |
| --- | --- |
| 0 | Type: 0 unused, 1 regular file, 2 directory |
| 1–7 | Zero |
| 8 | u32 parent index |
| 12 | u32 file length in bytes |
| 16 | u32 data-sector offset relative to payload data area |
| 20 | u32 data-sector count |
| 24 | u64 creation generation |
| 32–63 | Name, NUL-terminated and zero-padded |

Unused entries are entirely zero. Root entry 0 is a directory with parent 0,
creation generation 1 and zero name/size/extents. Directory extents are zero.
Each other entry has an existing directory parent and a chain reaching root in
at most 16 steps. Every ancestor index is checked before dereference, including
ancestors not yet visited by the validation loop. Cycles, duplicates among
siblings, invalid names/types/lengths, future generations, overlapping or
noncanonical extents, nonzero file padding and nonzero free space fail validation.
Empty files have zero extent fields. Nonempty extents must exactly follow the
packed cursor; individual sizes and the total data bound are checked first.
The reflected CRC-32 uses polynomial `0xedb88320`, initial all-ones and final
complement. Checksums detect accidental damage; they are not authentication and
have collision limitations.

## Metadata updates and recovery

Each modifying call forms a complete validated candidate in private memory and:

1. Writes a zero commit record to the inactive bank and waits for its flush.
2. Writes its complete payload in four eight-sector requests, flushing each.
3. Writes the checksummed commit record with generation + 1 and flushes it.
4. Only after success publishes the new in-memory state and bank selection.

The current bank is never modified during that transaction. Directory creation,
file creation, file writes and deletion each publish one complete snapshot.
Capacity/path validation failures make no disk write. Generation exhaustion
fails before I/O. If any write/flush fails, the mount is poisoned: further data
and metadata operations return IO; handles may still close and the mount may
unmount. The block layer also quarantines a failing shared ATA channel. No
blind retries or automatic repair writes occur.

Mount independently validates both banks' commit records, full checksums and
metadata. Two valid banks must have distinct adjacent generations; the newer is
selected. Conflicting or implausible generations are rejected. With exactly one
valid bank, mount exposes that snapshot **read-only**, marked degraded, even if
writable mount was requested. Two invalid banks fail as CORRUPT. Transport read
errors fail as IO rather than guessing from a partial snapshot. Read-only mount
and failed mount never change the disk.

A failed final commit can nevertheless have reached the disk before its
acknowledgment was lost; reboot may expose the complete new snapshot. Earlier
failures expose the previous committed snapshot under the tested ordered-write
model. The caller must inspect recovery state rather than assuming every error
rolled back. Recovery may expose an older snapshot after independent later
corruption of the newest bank; degraded status explicitly signals that risk.
Automatic writable repair, journaling, fsck, format, resize and migration APIs
are absent. A degraded volume remains read-only; recovery/export into a fresh
volume is future work.

## Safe fixtures and verification

```sh
make test-fs
make verify-fs
```

There is **no kernel formatter**. The verification harness exclusively creates
new images with `open(..., 'xb')` inside a fresh `build/filesystem/run-*` directory,
initializes two empty valid banks and marks all unrelated sectors with sentinels.
It does not overwrite an existing image or discover/format user data. QEMU boots
from the ISO separately; only its explicit disposable IDE image is mounted.
Normal boot leaves the VFS unmounted and block writes disabled.

Native ASan/UBSan tests exercise the full VFS/backend path, nested directories,
invalid/overlong paths, offset/length overflow, rights/stale handles, handle and
node limits, full data space, deletion/compaction/reuse, remount persistence,
read-only/degraded policy and reentrant BUSY. They inject no-write failures,
complete writes with lost acknowledgment and torn prefixes at all six commit
writes (18 cases); subsequent remount must show complete old or complete new
contents. Semantic corruption has recomputed valid CRCs to test bounds and
parent graphs independently of checksum rejection, including an unvisited
ancestor with an out-of-range parent. A thousand deterministic header/payload
mutations test mount rejection or conservative degraded recovery. Outside-volume
sentinels are checked, and mount never writes.

QEMU runs nine configurations per firmware: normal create/read/write/delete plus
full desktop/window/input/theme regression during a filesystem reader; reboot
persistence; both-bank checksum corruption; both-bank semantic corruption;
single-bank read-only recovery; full data/node capacity and reuse; forced VM kill
after the first inactive payload write; read-only recovery of that interrupted
image; and a backend rejecting writes. The existing blkdebug EROFS fixture
supplies write rejection. Every successful write callback includes the real ATA
flush. The forced-stop fixture halts at a named boundary and the harness sends
SIGKILL to its own QEMU child, then boots the same raw image for recovery.
Whole-file bytes and metadata are inspected from host-side snapshots, and every
run verifies all sectors outside the 66-sector volume. Read-only/corrupt/error
runs must leave the complete input image unchanged. Evidence is recorded in the
run's `results.json`; `build/filesystem/latest-run.txt` points to the last fully
successful run.

## Actual results — 2026-10-13

Final results will be recorded after the verification matrix completes.
Known-good storage revision `3d5a8ae` was preserved as
`checkpoint/storage-3d5a8ae` before changes. No host macOS settings, installation,
physical disk or existing user-data image was modified. Early controlled-test
build errors (a missing PMM header and mode-specific unused test arrays) were
corrected before QEMU verification; no isolation or integrity check was weakened.

## Limits and power-loss claims

The verified target is single-CPU QEMU TCG x86-64 with the existing PIIX ATA PIO
scope. CRC collisions, a lying flush implementation, controller/media faults,
write reordering outside the callback contract and real physical power-loss
behavior are not exhaustively covered. Native torn-prefix tests and one QEMU
forced-kill boundary do **not** establish physical power-loss safety. No such
claim is made. Damage to both snapshots, unreadable media or inconsistent
otherwise-valid generations causes mount failure, not attempted repair.

Files are bounded to 4 KiB, names to 31 ASCII bytes, the tree to 16 entries and
total data to 14 KiB. Larger/general filesystem compatibility is not implemented.
There is no rename, truncate, ownership/ACL, timestamps, dynamic volume growth,
asynchronous file service, per-process file descriptor ownership, file syscall,
ELF loading from files or file-manager integration. Kernel callers are trusted;
a buggy kernel/raw-disk writer can violate exclusive volume ownership. Existing
Ring 3 protections remain unchanged. Core Alpha is not release-ready. This
request stops after Phase 10's filesystem layer and its documented tests.
