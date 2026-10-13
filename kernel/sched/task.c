#include "task.h"
#include "arch.h"
struct task {
    struct task_info info;
    uint32_t generation;
    uintptr_t context;
    task_entry entry;
    void *argument;
};
static struct task tasks[TASK_MAX];
static unsigned current;
static bool initialized, idle;
static uint64_t dispatched;
static unsigned quantum;
static struct task *lookup(task_id id) {
    unsigned slot=id&255;
    if(!initialized || !id || slot>=TASK_MAX || tasks[slot].info.id!=id || tasks[slot].info.state==TASK_FREE) return 0;
    return &tasks[slot];
}
static void name_copy(char *out,const char *in) {
    unsigned i=0;if(in) while(i<23 && in[i]) {out[i]=in[i];++i;}out[i]=0;
}
bool task_init(void) {
    uint64_t flags=arch_interrupt_save();
    if(initialized) {arch_interrupt_restore(flags);return false;}
    tasks[0].generation=1;tasks[0].info.id=256;tasks[0].info.state=TASK_RUNNING;
    name_copy(tasks[0].info.name,"foreground");tasks[0].info.dispatches=1;
    current=0;dispatched=1;initialized=true;
    arch_interrupt_restore(flags);return true;
}
static __attribute__((noreturn)) void start(void) {
    arch_interrupt_restore(0x200);
    tasks[current].entry(tasks[current].argument);
    task_exit();
}
task_id task_create(const char *name,task_entry entry,void *argument) {
    uint64_t flags=arch_interrupt_save();task_id result=0;
    if(!initialized || !entry) goto done;
    for(unsigned slot=1;slot<TASK_MAX;++slot) {
        struct task *t=&tasks[slot];
        if(t->info.state!=TASK_FREE || t->generation==0xffffff) continue;
        uintptr_t base=0;
        if(!arch_task_stack_allocate(slot,&base)) goto done;
        ++t->generation;
        t->info=(struct task_info){.id=(t->generation<<8)|slot,.state=TASK_READY,.stack_base=base};
        name_copy(t->info.name,name);t->entry=entry;t->argument=argument;
        t->context=arch_task_context(base,start);result=t->info.id;break;
    }
done:
    arch_interrupt_restore(flags);return result;
}
task_id task_current(void) {return initialized?tasks[current].info.id:0;}
bool task_snapshot(task_id id,struct task_info *info) {
    uint64_t flags=arch_interrupt_save();struct task *t=lookup(id);
    bool ok=t && info;if(ok) *info=t->info;
    arch_interrupt_restore(flags);return ok;
}
size_t task_list(struct task_info *info,size_t capacity) {
    uint64_t flags=arch_interrupt_save();size_t count=0;
    if(initialized && info) for(unsigned i=0;i<TASK_MAX && count<capacity;++i)
        if(tasks[i].info.state!=TASK_FREE) info[count++]=tasks[i].info;
    arch_interrupt_restore(flags);return count;
}
static void reap_locked(void) {
    for(unsigned i=1;i<TASK_MAX;++i) if(i!=current && tasks[i].info.state==TASK_ZOMBIE) {
        arch_task_stack_release(i);
        tasks[i].info.state=TASK_FREE;tasks[i].info.id=0;
        tasks[i].info.stack_base=0;tasks[i].context=0;tasks[i].entry=0;tasks[i].argument=0;
    }
}
void task_reap(void) {uint64_t flags=arch_interrupt_save();if(initialized) reap_locked();arch_interrupt_restore(flags);}
bool task_terminate(task_id id) {
    uint64_t flags=arch_interrupt_save();struct task *t=lookup(id);
    bool ok=t && t!=&tasks[0] && t!=&tasks[current] && (t->info.state==TASK_READY || t->info.state==TASK_SLEEPING);
    if(ok) t->info.state=TASK_ZOMBIE;
    arch_interrupt_restore(flags);return ok;
}
static void schedule(bool reclaim) {
    if(reclaim) reap_locked();
    uint64_t now=arch_system_ticks();
    for(unsigned i=1;i<TASK_MAX;++i) if(tasks[i].info.state==TASK_SLEEPING && now>=tasks[i].info.wake_tick) tasks[i].info.state=TASK_READY;
    unsigned next=current;
    for(unsigned step=1;step<=TASK_MAX;++step) {
        unsigned i=(current+step)%TASK_MAX;
        if(tasks[i].info.state==TASK_READY) {next=i;break;}
    }
    if(next==current) {if(tasks[current].info.state==TASK_READY) tasks[current].info.state=TASK_RUNNING;return;}
    unsigned previous=current;current=next;idle=false;quantum=0;
    tasks[next].info.state=TASK_RUNNING;++tasks[next].info.dispatches;++dispatched;
    arch_task_switch(&tasks[previous].context,tasks[next].context);
}
void task_yield(void) {
    uint64_t flags=arch_interrupt_save();
    if(initialized) {tasks[current].info.state=TASK_READY;schedule(true);}
    arch_interrupt_restore(flags);
}
bool task_sleep(uint64_t ticks) {
    uint64_t flags=arch_interrupt_save();uint64_t now=arch_system_ticks();
    if(!initialized || current==0 || ticks>UINT64_MAX-now) {arch_interrupt_restore(flags);return false;}
    if(!ticks) tasks[current].info.state=TASK_READY;
    else {tasks[current].info.wake_tick=now+ticks;tasks[current].info.state=TASK_SLEEPING;}
    schedule(true);arch_interrupt_restore(flags);return true;
}
__attribute__((noreturn)) void task_exit(void) {
    (void)arch_interrupt_save();
    if(!initialized || current==0) arch_halt();
    tasks[current].info.state=TASK_ZOMBIE;schedule(true);
    arch_debug_puts("ON.: ERROR terminated task resumed.\n");arch_halt();
}
void task_tick(void) {
    if(!initialized) return;
    if(idle && current==0) ++tasks[current].info.idle_ticks;
    else ++tasks[current].info.cpu_ticks;
}
/* IRQ register frame stays on the interrupted task's owned stack. The PIC
   must be acknowledged before switching so another task can receive IRQ0.
   No allocation, reclamation, rendering or diagnostic output here. */
void task_preempt(void) {
    if(!initialized || ++quantum < TASK_QUANTUM_TICKS) return;
    quantum=0;
    if(tasks[current].info.state!=TASK_RUNNING) return;
    tasks[current].info.state=TASK_READY;
    schedule(false);
}
void task_idle_mark(bool value) {if(initialized && current==0) idle=value;}
static void number(uint64_t value) {
    char out[21],reverse[20];unsigned n=0,i=0;
    do {reverse[n++]='0'+value%10;value/=10;} while(value);
    while(n) out[i++]=reverse[--n];out[i]=0;arch_debug_puts(out);
}
void task_fault_report(void) {
    if(!initialized) return;
    tasks[current].info.state=TASK_FAULTED;
    arch_debug_puts("ON.: fault in kernel task ");number(task_current());arch_debug_puts(" ");arch_debug_puts(tasks[current].info.name);arch_debug_puts("; shared kernel halted.\n");
}
void task_diagnostics(void) {
    struct task_info info[TASK_MAX];size_t n=task_list(info,TASK_MAX);
    static const char *states[]={"free","ready","running","sleeping","zombie","faulted"};
    arch_debug_puts("ON.: preemptive task diagnostics; user mode disabled.\n");
    for(size_t i=0;i<n;++i) {
        arch_debug_puts("ON.: task ");number(info[i].id);arch_debug_puts(" ");arch_debug_puts(info[i].name);arch_debug_puts(" ");arch_debug_puts(states[info[i].state]);
        arch_debug_puts(" dispatches=");number(info[i].dispatches);arch_debug_puts(" cpu_ticks=");number(info[i].cpu_ticks);arch_debug_puts(" idle_ticks=");number(info[i].idle_ticks);arch_debug_puts("\n");
    }
    arch_debug_puts("ON.: total task dispatches=");number(dispatched);arch_debug_puts("\n");
}
