#include "process.h"
#include "arch.h"
#ifndef ON_USER_TEST
#define ON_USER_TEST 0
#endif
#if ON_USER_TEST
static void require(bool ok) {
    if(!ok) {arch_debug_puts("ON.: USER TEST FAILED; halted safely.\n");arch_halt();}
}
#if ON_USER_TEST != 3
static bool memory_same(struct pmm_statistics a,struct pmm_statistics b) {
    return a.free_frames==b.free_frames && a.allocated_frames==b.allocated_frames;
}
static struct process_result wait_user(task_id id) {
    struct process_result result={0};uint64_t deadline=arch_system_ticks()+1000;
    do {
        require(arch_system_ticks()<deadline);
        task_yield();task_reap();
        require(process_result(id,&result));
    } while(!result.reclaimed);
    require(result.finished && result.cs==0x2b && result.ss==0x33 &&
            !(result.rflags&0x3000) && (result.rflags&0x200));
    return result;
}
#endif
#endif
#if ON_USER_TEST == 1
extern const uint8_t user_busy_start[],user_busy_end[];
static void fault_case(const char *name,const uint8_t *code,size_t length,
                       unsigned vector,unsigned error,uintptr_t address) {
    struct pmm_statistics before=pmm_get_statistics();
    task_id id=process_create(name,code,length,0);require(id);
    struct task_info task;require(task_snapshot(id,&task));
    struct process_result result=wait_user(id);
    require(result.status==-2 && result.vector==vector && result.error==error);
    require(result.kernel_frame>=task.stack_base && result.kernel_frame+176<=task.stack_base+TASK_STACK_BYTES);
    if(vector==14) require(result.cr2==address);
    require(arch_address_current_root()==arch_address_kernel_root());
    require(memory_same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: USER TEST isolated fault passed: ");arch_debug_puts(name);arch_debug_puts("\n");
}
static void access_case(const char *name,uintptr_t address,bool write,bool execute,unsigned error) {
    uint8_t code[20]={0x48,0xb8};
    for(unsigned i=0;i<8;++i) code[2+i]=address>>(8*i);
    unsigned n=10;
    if(execute) {code[n++]=0xff;code[n++]=0xe0;}
    else if(write) {code[n++]=0xc6;code[n++]=0x00;code[n++]=1;}
    else {code[n++]=0x8a;code[n++]=0x00;}
    code[n++]=0x31;code[n++]=0xc0;code[n++]=0xcd;code[n++]=0x80;
    fault_case(name,code,n,14,error,address);
}
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();
    task_id a=process_create("user-a",user_busy_start,user_busy_end-user_busy_start,11);
    task_id b=process_create("user-b",user_busy_start,user_busy_end-user_busy_start,22);
    require(a && b);
    struct vmm_translation x,y;
    require(process_translate(a,PROCESS_DATA,&x) && process_translate(b,PROCESS_DATA,&y) && x.physical!=y.physical);

    require(x.permissions==(VMM_USER|VMM_WRITE) && y.permissions==x.permissions);
    require(process_translate(a,PROCESS_CODE,&x) && x.permissions==(VMM_USER|VMM_EXEC));
    require(process_translate(a,PROCESS_STACK,&x) && x.permissions==(VMM_USER|VMM_WRITE));
    require(!process_translate(a,PROCESS_STACK-4096,&x) && !process_translate(a,PROCESS_STACK+PROCESS_STACK_PAGES*4096,&x));
    require(!process_translate(a,0x1000,&x) && !process_translate(a,(uintptr_t)tests,&x));
    struct task_info ai,bi;require(task_snapshot(a,&ai) && task_snapshot(b,&bi));
    require(ai.address_root!=bi.address_root && ai.address_root!=arch_address_kernel_root());
    /* Busy-wait bootstrap too: timer preemption alone runs both CPL3 workers. */
    uint64_t deadline=arch_system_ticks()+1000;struct process_result ar,br;
    do {require(arch_system_ticks()<deadline);require(process_result(a,&ar) && process_result(b,&br));}
    while(!ar.finished || !br.finished);
    require(task_snapshot(a,&ai) && task_snapshot(b,&bi) && ai.dispatches>=2 && bi.dispatches>=2 && ai.cpu_ticks && bi.cpu_ticks);
    require(ar.status==11 && br.status==22 && ar.vector==128 && br.vector==128 && ar.cs==0x2b && br.cs==0x2b);
    /* High-half entries must be supervisor, including HHDM, tables and stacks. */
    const uint64_t *ra=pmm_page_pointer(ai.address_root),*rb=pmm_page_pointer(bi.address_root);
    require(ra && rb);
    for(unsigned i=256;i<512;++i) require(!(ra[i]&4) && !(rb[i]&4));
    task_reap();require(memory_same(before,pmm_get_statistics()));
    require(arch_address_current_root()==arch_address_kernel_root());
    arch_debug_puts("ON.: USER TEST ring-3 entry, exit, distinct same-VA data, RX/RW-NX, guards, CR3 and timer preemption passed.\n");
    access_case("kernel-read",(uintptr_t)tests,false,false,5);
    access_case("kernel-write",(uintptr_t)tests,true,false,7);
    access_case("kernel-execute",(uintptr_t)tests,false,true,21);
    /* Translate an actual kernel page to derive the shared HHDM offset. */
    require(vmm_translate((uintptr_t)tests,&x));
    void *direct;
    /* Boot root is reserved; allocate an owned frame to identify the HHDM. */
    uint64_t page;require(pmm_alloc_page(&page));direct=pmm_page_pointer(page);require(direct);
    uintptr_t hhdm=(uintptr_t)direct-page;
    access_case("hhdm-read",(uintptr_t)direct,false,false,5);
    access_case("page-table-write",hhdm+arch_address_kernel_root(),true,false,7);
    const uint8_t spin[]={0xeb,0xfe};
    task_id victim=process_create("live-victim",spin,sizeof(spin),99);require(victim);
    require(process_translate(victim,PROCESS_DATA,&y));
    struct task_info victim_task;require(task_snapshot(victim,&victim_task));
    access_case("foreign-frame-read",hhdm+y.physical,false,false,5);
    access_case("kernel-stack-read",victim_task.stack_base,false,false,5);
    require(task_terminate(victim));task_reap();
    require(pmm_free_page(page));
    access_case("code-write",PROCESS_CODE,true,false,7);
    access_case("stack-execute",PROCESS_STACK,false,true,21);
    access_case("lower-stack-guard",PROCESS_STACK-4096,true,false,6);
    access_case("upper-stack-guard",PROCESS_STACK+PROCESS_STACK_PAGES*4096,true,false,6);
    access_case("unmapped-read",0x1000,false,false,4);
    const uint8_t ud[]={0x0f,0x0b},cli[]={0xfa},io[]={0xe6,0xe9},forbidden_gate[]={0xcd,0x20};
    const uint8_t syscall[]={0x0f,0x05},sysenter[]={0x0f,0x34},fpu[]={0xd9,0xe8};
    const uint8_t bad_stack[]={0x31,0xe4,0x0f,0x0b};
    const uint8_t divide[]={0x31,0xc0,0x31,0xd2,0x31,0xc9,0xf7,0xf1};
    fault_case("invalid-opcode",ud,sizeof(ud),6,0,0);
    fault_case("privileged-cli",cli,sizeof(cli),13,0,0);
    fault_case("port-io",io,sizeof(io),13,0,0);
    fault_case("kernel-gate",forbidden_gate,sizeof(forbidden_gate),13,0x102,0);
    fault_case("disabled-syscall",syscall,sizeof(syscall),6,0,0);
    fault_case("disabled-sysenter",sysenter,sizeof(sysenter),6,0,0);
    fault_case("unsupported-fpu",fpu,sizeof(fpu),7,0,0);
    fault_case("invalid-user-stack",bad_stack,sizeof(bad_stack),6,0,0);
    fault_case("divide-error",divide,sizeof(divide),0,0,0);
    require(memory_same(before,pmm_get_statistics()));
    /* Repeat fault/exit reuse with generation rejection. */
    const uint8_t exit[]={0x31,0xc0,0xcd,0x80};
    for(unsigned i=0;i<20;++i) {
        task_id id=process_create("reuse",exit,sizeof(exit),0);require(id && id!=a && !task_terminate(a));
        struct process_result r=wait_user(id);require(r.status==0 && r.vector==128);
    }
    require(memory_same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: USER TEST fault containment, kernel survival, generations and full reclamation passed.\n");
}
#elif ON_USER_TEST == 2
static void tests(void) {
    struct pmm_statistics before=pmm_get_statistics();
    uint64_t head=0,page;
    /* Leave four frames: root, code leaf and two tables succeed, last fails. */
    while(pmm_get_statistics().free_frames>4) {
        require(pmm_alloc_page(&page));*(uint64_t *)pmm_page_pointer(page)=head;head=page;
    }
    const uint8_t exit[]={0x31,0xc0,0xcd,0x80};
    require(!process_create("rollback",exit,sizeof(exit),0));
    require(pmm_get_statistics().free_frames==4);
    while(head) {uint64_t next=*(uint64_t *)pmm_page_pointer(head);require(pmm_free_page(head));head=next;}
    require(memory_same(before,pmm_get_statistics()));
    task_id id=process_create("retry",exit,sizeof(exit),0);require(id);
    struct process_result result=wait_user(id);require(result.status==0 && result.vector==128);
    require(memory_same(before,pmm_get_statistics()));
    arch_debug_puts("ON.: USER TEST partial address-space allocation rollback, retry and reclamation passed.\n");
}
#elif ON_USER_TEST == 3
static void tests(void) {
    const uint8_t load[]={0x48,0xc7,0xc3,0x00,0x00,0x60,0x00,0x48,0xff,0x43,0x08,0xeb,0xfa};
    require(process_create("user-load-a",load,sizeof(load),11));
    require(process_create("user-load-b",load,sizeof(load),22));
    arch_debug_puts("ON.: USER TEST sustained ring-3 load ready.\n");
}
#endif
void process_boot_test(void) {
#if ON_USER_TEST
    arch_interrupt_restore(0x200);tests();
#endif
}
