#ifndef ON_ELF_H
#define ON_ELF_H
#include "address_space.h"
#define ELF_MAX_SEGMENTS 8u
#define ELF_MAX_BYTES 131072u
#define ELF_IMAGE_MIN 0x400000ull
#define ELF_IMAGE_LIMIT 0x10000000ull
#define ELF_STACK_BOTTOM 0x800000ull
#define ELF_STACK_TOP 0x802000ull
struct elf_segment { uint64_t address,offset,filesz,memsz,start,end;unsigned permissions; };
struct elf_plan { uintptr_t entry;unsigned count,pages;struct elf_segment segments[ELF_MAX_SEGMENTS]; };
/* Kernel-owned bounded byte spans. Validation does not allocate or publish.
   Populate requires an empty inactive root; caller destroys it on any failure. */
bool elf64_validate(const void *image,size_t length,struct elf_plan *plan);
bool elf64_populate(struct address_space *space,const void *image,size_t length,uintptr_t *entry);
#endif
