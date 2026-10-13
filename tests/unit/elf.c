#include "elf.h"
#include "arch.h"
#include "../elf_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
extern int model_fail_after;
extern unsigned model_live;
static bool enabled=true;
uint64_t arch_interrupt_save(void) {uint64_t f=enabled?512:0;enabled=false;return f;}
void arch_interrupt_restore(uint64_t f) {enabled=(f&512)!=0;}
__attribute__((noreturn)) void arch_halt(void) {abort();}
static void reject(unsigned offset,uint64_t value,unsigned bytes) {
    uint8_t image[FIXTURE_SIZE];elf_fixture(image);fixture_put(image+offset,value,bytes);
    struct elf_plan out;memset(&out,0xa5,sizeof(out));struct elf_plan before=out;
    assert(!elf64_validate(image,sizeof(image),&out) && !memcmp(&before,&out,sizeof(out)));
}
int main(void) {
    uint8_t image[FIXTURE_SIZE];elf_fixture(image);struct elf_plan plan;
    assert(elf64_validate(image,sizeof(image),&plan) && plan.entry==0x400000 && plan.pages==4 && plan.count==2);
    assert(!elf64_validate(0,sizeof(image),&plan) && !elf64_validate(image,63,&plan));
    assert(!elf64_validate(image,ELF_MAX_BYTES+1,&plan));
    reject(0,0,1);reject(4,1,1);reject(5,2,1);reject(6,0,1);reject(7,3,1);reject(9,1,1);
    reject(16,3,2);reject(18,3,2);reject(20,0,4);reject(48,1,4);reject(52,63,2);reject(54,55,2);
    reject(56,0,2);reject(56,9,2);reject(32,UINT64_MAX,8);reject(32,8180,8);
    reject(40,8000,8);reject(60,1,2);reject(62,1,2);reject(24,0x600fff,8);reject(24,0x400005,8);
    reject(64,2,4);reject(64,3,4);reject(64,7,4);reject(68,7,4);reject(68,1,4);reject(68,13,4);
    reject(72,UINT64_MAX,8);reject(96,4097,8);reject(104,0,8);reject(104,UINT64_MAX,8);
    reject(80,0,8);reject(80,0xffff800000000000ull,8);reject(80,0x800000000000ull,8);
    reject(80,0x7ff000,8);reject(80,0x802000,8);reject(80,ELF_IMAGE_LIMIT,8);
    reject(112,1,8);reject(112,6144,8);reject(112,0x400000,8);reject(80,0x400001,8);
    reject(136,0x400fff,8);reject(160,16*4096,8);
    /* RX bytes and multi-page BSS with an unaligned data segment are populated. */
    struct address_space s={0};uintptr_t entry=0;
    assert(address_space_create(&s) && elf64_populate(&s,image,sizeof(image),&entry));
    struct vmm_translation tr;assert(entry==0x400000 && s.pages==6);
    assert(address_space_translate(&s,entry,&tr) && tr.permissions==(VMM_USER|VMM_EXEC));
    assert(*(uint8_t *)pmm_page_pointer(tr.physical)==0x31);
    uint8_t bytes[3]={1,1,1};assert(address_space_copy_in(&s,bytes,0x600fff,3) && bytes[0]==42 && !bytes[1] && !bytes[2]);
    uint64_t zero=1;assert(address_space_copy_in(&s,&zero,0x602000,8) && !zero);
    assert(!address_space_translate(&s,ELF_STACK_BOTTOM-4096,&tr));
    assert(address_space_destroy(&s) && !model_live);
    for(int fail=0;fail<13;++fail) {
        assert(address_space_create(&s));model_fail_after=fail;entry=UINTPTR_MAX;
        bool ok=elf64_populate(&s,image,sizeof(image),&entry);
        assert(ok || entry==UINTPTR_MAX);model_fail_after=-1;
        assert(address_space_destroy(&s) && !model_live);
    }
    /* Deterministic malformed/truncated header corpus under ASan/UBSan. */
    uint32_t random=17;
    for(unsigned i=0;i<10000;++i) {
        elf_fixture(image);random=random*1664525+1013904223;
        unsigned at=random%176;image[at]^=1u<<(random%8);
        size_t length=(i&1)?sizeof(image):random%sizeof(image);
        if(elf64_validate(image,length,&plan)) {
            assert(plan.pages<=USER_PAGE_LIMIT-2 && plan.count<=ELF_MAX_SEGMENTS);
            for(unsigned n=0;n<plan.count;++n) assert((plan.segments[n].permissions&(VMM_WRITE|VMM_EXEC))!=(VMM_WRITE|VMM_EXEC));
        }
    }
    puts("ELF64 strict headers/segments/bounds/W^X/entry/stack guards, unaligned data+BSS, allocation rollback and 10000 malformed/truncated mutations passed.");
}
