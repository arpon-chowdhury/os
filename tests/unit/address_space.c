#include "address_space.h"
#include "arch.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
extern int model_fail_after;
extern unsigned model_live;
static bool enabled=true;
uint64_t arch_interrupt_save(void) {uint64_t f=enabled?512:0;enabled=false;return f;}
void arch_interrupt_restore(uint64_t f) {enabled=(f&512)!=0;}
__attribute__((noreturn)) void arch_halt(void) {abort();}
int main(void) {
    struct address_space a={0},b={0};struct vmm_translation x,y;
    assert(address_space_create(&a) && !address_space_create(&a) && address_space_create(&b));
    uint64_t one=11,two=22;
    assert(address_space_map(&a,0x400000,&one,8,VMM_USER|VMM_WRITE));
    assert(address_space_map(&b,0x400000,&two,8,VMM_USER|VMM_WRITE));
    assert(address_space_translate(&a,0x400000,&x) && address_space_translate(&b,0x400000,&y));
    assert(x.physical!=y.physical && x.permissions==(VMM_USER|VMM_WRITE));
    assert(*(uint64_t *)pmm_page_pointer(x.physical)==11 && *(uint64_t *)pmm_page_pointer(y.physical)==22);
    assert(!address_space_map(&a,0x400000,0,0,VMM_USER));
    assert(!address_space_map(&a,0,0,0,VMM_USER));
    assert(!address_space_map(&a,0x400001,0,0,VMM_USER));
    assert(!address_space_map(&a,0xffff800000000000ull,0,0,VMM_USER));
    assert(!address_space_map(&a,0x800000000000ull,0,0,VMM_USER));
    assert(!address_space_map(&a,0x500000,0,1,VMM_USER));
    assert(!address_space_map(&a,0x500000,&one,4097,VMM_USER));
    assert(!address_space_map(&a,0x500000,0,0,VMM_WRITE));
    assert(!address_space_map(&a,0x500000,0,0,VMM_USER|VMM_WRITE|VMM_EXEC));
    assert(!address_space_translate(&a,0x500000,&x));
    assert(address_space_map(&a,0x500000,0,0,VMM_USER|VMM_EXEC));
    assert(address_space_translate(&a,0x500000,&x) && x.permissions==(VMM_USER|VMM_EXEC));
    uint8_t *zero=pmm_page_pointer(x.physical);for(unsigned i=0;i<4096;++i) assert(zero[i]==0);
    arch_address_activate(a.root);
    assert(!address_space_destroy(&a) && !address_space_map(&a,0x700000,0,0,VMM_USER));
    arch_address_activate(0);
    assert(address_space_destroy(&a) && !address_space_destroy(&a) && address_space_destroy(&b) && !model_live);
    for(int fail=0;fail<4;++fail) {
        assert(address_space_create(&a));unsigned before=model_live;
        model_fail_after=fail;assert(!address_space_map(&a,0x400000,0,0,VMM_USER|VMM_WRITE));
        assert(model_live==before && !a.pages && !address_space_translate(&a,0x400000,&x));
        model_fail_after=-1;assert(address_space_map(&a,0x400000,0,0,VMM_USER|VMM_WRITE));
        assert(address_space_destroy(&a) && !model_live);
    }
    assert(address_space_create(&a));
    for(unsigned i=0;i<USER_PAGE_LIMIT;++i) assert(address_space_map(&a,0x400000+i*4096,0,0,VMM_USER));
    assert(!address_space_map(&a,0x800000,0,0,VMM_USER));
    enabled=false;assert(address_space_destroy(&a) && !enabled && !model_live);
    puts("Owned address spaces: distinct same-VA frames, RX/RW-NX, zeroing, invalid ranges, W^X, active-root protection, all partial-allocation rollback points, capacity and reclamation passed.");
}
