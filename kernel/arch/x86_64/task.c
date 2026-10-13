#include "task.h"
#include "arch.h"
#include "virtual.h"
#include "address_space.h"
#include "process.h"
#define STACK_BASE UINT64_C(0xffffc80000000000)
#define STACK_STRIDE ((TASK_STACK_PAGES+2u)*PMM_PAGE_SIZE)
static uint64_t frames[TASK_MAX][TASK_STACK_PAGES];
static bool live[TASK_MAX];
#if ON_TASK_TEST == 3
static bool injected;
#endif
bool arch_task_stack_allocate(unsigned slot,uintptr_t *base) {
    if(!slot || slot>=TASK_MAX || !base || live[slot]) return false;
    uintptr_t guard=STACK_BASE+slot*STACK_STRIDE;
    struct vmm_translation tr;
    /* Reserve only a completely empty interval; never alter boot mappings. */
    for(unsigned i=0;i<TASK_STACK_PAGES+2;++i) if(vmm_translate(guard+i*PMM_PAGE_SIZE,&tr)) return false;
    unsigned mapped=0;
    for(;mapped<TASK_STACK_PAGES;++mapped) {
#if ON_TASK_TEST == 3
        if(!injected && mapped==2) {injected=true;goto rollback;}
#endif
        uint64_t page;
        if(!pmm_alloc_page(&page)) goto rollback;
        uintptr_t address=guard+(mapped+1)*PMM_PAGE_SIZE;
        if(!vmm_map(address,page,VMM_WRITE)) {pmm_free_page(page);goto rollback;}
        frames[slot][mapped]=page;
        volatile uint8_t *bytes=(volatile uint8_t *)address;
        for(unsigned n=0;n<PMM_PAGE_SIZE;++n) bytes[n]=0;
    }
    live[slot]=true;*base=guard+PMM_PAGE_SIZE;return true;
rollback:
    while(mapped) {
        --mapped;
        if(!vmm_unmap(guard+(mapped+1)*PMM_PAGE_SIZE,0) || !pmm_free_page(frames[slot][mapped])) arch_halt();
        frames[slot][mapped]=0;
    }
    return false;
}
void arch_task_stack_release(unsigned slot) {
    if(!slot || slot>=TASK_MAX || !live[slot]) arch_halt();
    uintptr_t base=STACK_BASE+slot*STACK_STRIDE+PMM_PAGE_SIZE;
    for(unsigned i=0;i<TASK_STACK_PAGES;++i) {
        if(!vmm_unmap(base+i*PMM_PAGE_SIZE,0)||!pmm_free_page(frames[slot][i])) arch_halt();
        frames[slot][i]=0;
    }
    live[slot]=false;
}
uintptr_t arch_task_context(uintptr_t base,void (*start)(void)) {
    uintptr_t *stack=(uintptr_t *)(base+TASK_STACK_BYTES-64);
    for(unsigned i=0;i<6;++i) stack[i]=0;
    stack[6]=(uintptr_t)start;stack[7]=(uintptr_t)arch_halt;
    return (uintptr_t)stack;
}

bool arch_task_privilege_boundary(void) {
    struct descriptor {uint16_t limit;uint64_t base;} __attribute__((packed)) gdtr,idtr;
    uint32_t lo,hi;uint16_t cs;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(0xc0000080));
    (void)hi;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("sidt %0" : "=m"(idtr));
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    if((lo&1) || cs!=8 || gdtr.limit!=55 || idtr.limit!=4095) return false;
    uint64_t cr0,cr4;
    __asm__ volatile ("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr4,%0" : "=r"(cr4));
    if(!(cr0&8) || (cr4&((1ull<<16)|(1ull<<22)|(1ull<<24)))) return false;
    uint32_t a,b,c,d;
    __asm__ volatile ("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(1),"c"(0));
    if(d&(1u<<11)) {
        __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(0x174));
        if(lo || hi) return false;
    }
    const uint64_t *gdt=(const uint64_t *)(uintptr_t)gdtr.base;
    if(((gdt[1]>>45)&3) || ((gdt[2]>>45)&3) || ((gdt[3]>>45)&3)) return false;
    if(((gdt[5]>>45)&3)!=3 || ((gdt[6]>>45)&3)!=3) return false;
    const uint8_t *idt=(const uint8_t *)(uintptr_t)idtr.base;
    for(unsigned i=0;i<256;++i) if((idt[i*16+5]&0x60)!=(i==128?0x60:0)) return false;
    return true;
}

void arch_task_activate(uint64_t root,uintptr_t stack_top) {
    arch_address_activate(root);
    arch_user_kernel_stack(stack_top);
}
