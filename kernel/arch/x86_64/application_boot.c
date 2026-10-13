#include "process.h"
#include "elf.h"
#include "syscall.h"
#include "arch.h"
#ifndef ON_APP_TEST
#define ON_APP_TEST 0
#endif
#if ON_APP_TEST
extern const uint8_t app_probe_start[],app_probe_end[],app_fault_start[],app_fault_end[],app_return_start[],app_return_end[],app_load_start[],app_load_end[];
static void require(bool ok) {
    if(!ok) {arch_debug_puts("ON.: APP TEST FAILED; halted safely.\n");arch_halt();}
}
#if ON_APP_TEST != 3
static bool same(struct pmm_statistics a,struct pmm_statistics b) {
    return a.free_frames==b.free_frames && a.allocated_frames==b.allocated_frames;
}
static void number(uint64_t value) {
    char text[21],reverse[20];unsigned n=0,i=0;
    do {reverse[n++]='0'+value%10;value/=10;} while(value);
    while(n) text[i++]=reverse[--n];text[i]=0;arch_debug_puts(text);
}
static struct process_result wait_app(task_id id) {
    struct process_result result={0};uint64_t deadline=arch_system_ticks()+2000;
    do {
        require(arch_system_ticks()<deadline);task_yield();task_reap();require(process_result(id,&result));
    } while(!result.reclaimed);
    if(result.status) {arch_debug_puts("ON.: app exit status magnitude=");number(result.status<0?(uint64_t)0-(uint64_t)result.status:(uint64_t)result.status);arch_debug_puts("\n");}
    require(result.finished && result.cs==0x2b && result.ss==0x33 && !(result.rflags&0x3000));
    return result;
}
#endif
#endif
#if ON_APP_TEST == 1
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();
    size_t length=app_probe_end-app_probe_start;
    task_id a=process_load_elf("elf-probe-a",app_probe_start,length),b=process_load_elf("elf-probe-b",app_probe_start,length);
    require(a && b);
    struct vmm_translation x,y;
    require(process_translate(a,0x600000,&x) && process_translate(b,0x600000,&y) && x.physical!=y.physical);
    require(x.permissions==(VMM_USER|VMM_WRITE));
    require(process_translate(a,0x400000,&x) && x.permissions==(VMM_USER|VMM_EXEC));
    struct process_result ar=wait_app(a),br=wait_app(b);
    require(!ar.status && !br.status && ar.vector==129 && br.vector==129);
    require(same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: APP TEST ELF execution, ABI, output, pointers, anonymous memory, yield, isolation and exit passed.\n");
    task_id fault=process_load_elf("elf-fault",app_fault_start,app_fault_end-app_fault_start);require(fault);
    struct process_result f=wait_app(fault);
    require(f.status==-2 && f.vector==14 && f.error==5 && f.cr2==0xffffffff80000000ull);
    require(same(before,pmm_get_statistics()));
    task_id bad=process_load_elf("elf-bad-return",app_return_start,app_return_end-app_return_start);require(bad);
    f=wait_app(bad);require(f.status==-ON_EFAULT && f.vector==129 && !f.rsp);
    require(same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: APP TEST loaded user fault and invalid syscall return containment passed.\n");
    for(unsigned i=0;i<20;++i) {
        task_id id=process_load_elf("elf-reuse",app_probe_start,length);require(id && id!=a && !task_terminate(a));
        struct process_result r=wait_app(id);require(!r.status && r.vector==129);
        require(same(before,pmm_get_statistics()));
    }
    require(arch_address_current_root()==arch_address_kernel_root());
    arch_debug_puts("ON.: APP TEST repeated ELF launches, live allocation cleanup and full reclamation passed.\n");
}
#elif ON_APP_TEST == 2
static uint8_t malformed[ELF_MAX_BYTES];
static void put(uint8_t *p,uint64_t value,unsigned bytes) {for(unsigned i=0;i<bytes;++i) p[i]=value>>(i*8);}
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();size_t length=app_probe_end-app_probe_start;require(length<=sizeof(malformed));
    const struct mutation {unsigned offset,bytes;uint64_t value;} changes[]={
        {0,1,0},{4,1,1},{5,1,2},{16,2,3},{18,2,3},{24,8,0x600000},{32,8,UINT64_MAX},
        {52,2,63},{54,2,55},{56,2,0},{56,2,9},{64,4,3},{68,4,7},{72,8,UINT64_MAX},
        {80,8,0xffff800000000000ull},{80,8,0x7ff000},{104,8,UINT64_MAX},{112,8,6144}
    };
    for(unsigned i=0;i<sizeof(changes)/sizeof(changes[0]);++i) {
        for(size_t n=0;n<length;++n) malformed[n]=app_probe_start[n];
        put(malformed+changes[i].offset,changes[i].value,changes[i].bytes);
        require(!process_load_elf("malformed",malformed,length));require(same(before,pmm_get_statistics()));
    }
    require(!process_load_elf("truncated",app_probe_start,63));require(same(before,pmm_get_statistics()));
    uint64_t head=0,page;
    while(pmm_get_statistics().free_frames>4) {require(pmm_alloc_page(&page));*(uint64_t *)pmm_page_pointer(page)=head;head=page;}
    require(!process_load_elf("rollback",app_probe_start,length) && pmm_get_statistics().free_frames==4);
    while(head) {uint64_t next=*(uint64_t *)pmm_page_pointer(head);require(pmm_free_page(head));head=next;}
    require(same(before,pmm_get_statistics()));
    task_id id=process_load_elf("retry",app_probe_start,length);require(id);
    struct process_result result=wait_app(id);require(!result.status && result.vector==129);
    require(same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: APP TEST malformed ELF, real allocation rollback, retry and reclamation passed.\n");
}
#elif ON_APP_TEST == 3
static void tests(void) {
    require(process_load_elf("app-load-a",app_load_start,app_load_end-app_load_start));
    require(process_load_elf("app-load-b",app_load_start,app_load_end-app_load_start));
    arch_debug_puts("ON.: APP TEST sustained ELF syscall load ready.\n");
}
#endif
void application_boot_test(void) {
#if ON_APP_TEST
    arch_interrupt_restore(0x200);tests();
#endif
}
