/* Native owned-frame model; actual CR3/TSS/IRET behavior is QEMU-only. */
#include "address_space.h"
#include <assert.h>
#include <stdlib.h>
static void *pages[128];
static uint64_t active=4096;
int model_fail_after=-1;
unsigned model_live;
bool pmm_alloc_page(uint64_t *out) {
    if(model_fail_after==0) return false;
    if(model_fail_after>0) --model_fail_after;
    for(unsigned i=2;i<128;++i) if(!pages[i]) {
        pages[i]=malloc(4096);assert(pages[i]);*out=i*4096;++model_live;return true;
    }
    return false;
}
bool pmm_free_page(uint64_t p) {
    if((p&4095) || p/4096>=128 || !pages[p/4096]) return false;
    free(pages[p/4096]);pages[p/4096]=0;--model_live;return true;
}
void *pmm_page_pointer(uint64_t p) {return !(p&4095) && p/4096<128?pages[p/4096]:0;}
uint64_t arch_address_kernel_root(void) {return 4096;}
uint64_t arch_address_current_root(void) {return active;}
void arch_address_activate(uint64_t root) {active=root?root:4096;}
