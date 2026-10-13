#include "address_space.h"
#include "arch.h"
#define P 1ull
#define W 2ull
#define U 4ull
#define NX (1ull << 63)
#define ADDR 0x000ffffffffff000ull
static uint64_t *entries(uint64_t page) { return pmm_page_pointer(page); }
static unsigned index_at(uintptr_t a, unsigned level) { return (a >> (12+9*level)) & 511; }
static bool allocate(uint64_t *page) {
    if(!pmm_alloc_page(page)) return false;
    uint64_t *p=entries(*page);
    if(!p) {pmm_free_page(*page);return false;}
    for(unsigned i=0;i<512;++i) p[i]=0;
    return true;
}
bool address_space_create(struct address_space *s) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    if(s && !s->root && arch_address_kernel_root()) {
        uint64_t page;
        if(allocate(&page)) {*s=(struct address_space){.root=page};ok=true;}
    }
    arch_interrupt_restore(flags);return ok;
}
static bool map_owned(struct address_space *s,uintptr_t a,const void *data,size_t length,unsigned permissions,bool live) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    uint64_t leaf=0,allocated[3],*parents[3];unsigned count=0;
    if(!s || !s->root || (!live && s->root==arch_address_current_root()) || s->pages>=USER_PAGE_LIMIT ||
       a<4096 || a>=0x0000800000000000ull || (a&4095) || length>4096 || (length && !data) ||
       !(permissions&VMM_USER) || (permissions&~(VMM_USER|VMM_WRITE|VMM_EXEC)) ||
       ((permissions&(VMM_WRITE|VMM_EXEC))==(VMM_WRITE|VMM_EXEC))) goto done;
    uint64_t *p=entries(s->root);
    if(!p || !allocate(&leaf)) goto done;
    for(int level=3;level>0;--level) {
        uint64_t *parent=&p[index_at(a,level)];
        if(!*parent) {
            uint64_t page;if(!allocate(&page)) goto rollback;
            parents[count]=parent;allocated[count++]=page;*parent=page|P|W|U;
        }
        if((*parent&(P|U|128))!=(P|U)) goto rollback;
        p=entries(*parent&ADDR);if(!p) goto rollback;
    }
    if(p[index_at(a,0)]) goto rollback;
    uint8_t *bytes=(uint8_t *)entries(leaf);
    for(size_t i=0;i<length;++i) bytes[i]=((const uint8_t *)data)[i];
    p[index_at(a,0)]=leaf|P|U|((permissions&VMM_WRITE)?W:0)|((permissions&VMM_EXEC)?0:NX);
    arch_address_invalidate(s->root,a);
    ++s->pages;ok=true;goto done;
rollback:
    while(count) {--count;*parents[count]=0;if(!pmm_free_page(allocated[count])) arch_halt();}
    if(!pmm_free_page(leaf)) arch_halt();
done:
    arch_interrupt_restore(flags);return ok;
}
bool address_space_map(struct address_space *s,uintptr_t a,const void *data,size_t length,unsigned permissions) {
    return map_owned(s,a,data,length,permissions,false);
}
bool address_space_map_zero(struct address_space *s,uintptr_t a) {
    return map_owned(s,a,0,0,VMM_USER|VMM_WRITE,true);
}
bool address_space_translate(const struct address_space *s,uintptr_t a,struct vmm_translation *out) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    if(!s || !s->root || !out || a>=0x0000800000000000ull) goto done;
    uint64_t physical=s->root;unsigned permissions=VMM_USER|VMM_WRITE|VMM_EXEC;
    for(int level=3;level>=0;--level) {
        uint64_t *p=entries(physical);if(!p) goto done;
        uint64_t e=p[index_at(a,level)];if(!(e&P) || (e&128)) goto done;
        if(!(e&U)) permissions&=~VMM_USER;
        if(!(e&W)) permissions&=~VMM_WRITE;
        if(e&NX) permissions&=~VMM_EXEC;
        physical=e&ADDR;
        if(!level) {*out=(struct vmm_translation){physical+(a&4095),4096,permissions};ok=true;}
    }
done:
    arch_interrupt_restore(flags);return ok;
}
static void release_tree(uint64_t physical,unsigned level,unsigned limit) {
    uint64_t *p=entries(physical);if(!p) arch_halt();
    for(unsigned i=0;i<limit;++i) if(p[i]) {
        if(!(p[i]&P) || (p[i]&128)) arch_halt();
        if(level) release_tree(p[i]&ADDR,level-1,512);
        else if(!pmm_free_page(p[i]&ADDR)) arch_halt();
    }
    if(!pmm_free_page(physical)) arch_halt();
}
bool address_space_destroy(struct address_space *s) {
    uint64_t flags=arch_interrupt_save();bool ok=s && s->root && s->root!=arch_address_current_root();
    if(ok) {release_tree(s->root,3,256);*s=(struct address_space){0};}
    arch_interrupt_restore(flags);return ok;
}

static bool copy_user(const struct address_space *s,uintptr_t user,void *kernel,size_t length,bool write) {
    uint64_t flags=arch_interrupt_save();bool ok=false;
    if(!s || !s->root || length>USER_PAGE_LIMIT*4096u || (length && !kernel)) goto done;
    if(!length) {ok=true;goto done;}
    if(user<4096 || user>=0x800000000000ull || length>0x800000000000ull-user) goto done;
    /* Validate the entire range first; no partial copy across invalid pages.
       Local IF stays clear across both passes: no TOCTOU on this single CPU. */
    for(size_t done=0;done<length;) {
        struct vmm_translation tr;
        if(!address_space_translate(s,user+done,&tr) || !(tr.permissions&VMM_USER) ||
           (write && !(tr.permissions&VMM_WRITE)) || !entries(tr.physical&~4095ull)) goto done;
        size_t chunk=4096-((user+done)&4095);if(chunk>length-done) chunk=length-done;
        done+=chunk;
    }
    for(size_t done=0;done<length;) {
        struct vmm_translation tr;
        if(!address_space_translate(s,user+done,&tr)) arch_halt();
        uint8_t *page=(uint8_t *)entries(tr.physical&~4095ull)+(tr.physical&4095);
        size_t chunk=4096-((user+done)&4095);if(chunk>length-done) chunk=length-done;
        for(size_t i=0;i<chunk;++i) {
            if(write) page[i]=((const uint8_t *)kernel)[done+i];
            else ((uint8_t *)kernel)[done+i]=page[i];
        }
        done+=chunk;
    }
    ok=true;
done:
    arch_interrupt_restore(flags);return ok;
}
bool address_space_copy_in(const struct address_space *s,void *out,uintptr_t user,size_t length) {
    return copy_user(s,user,out,length,false);
}
bool address_space_copy_out(const struct address_space *s,uintptr_t user,const void *in,size_t length) {
    return copy_user(s,user,(void *)in,length,true);
}
bool address_space_unmap(struct address_space *s,uintptr_t a) {
    uint64_t flags=arch_interrupt_save();bool ok=false;uint64_t pages[4],*path[4];
    if(!s || !s->root || a<4096 || a>=0x800000000000ull || (a&4095)) goto done;
    pages[3]=s->root;
    for(int level=3;level>=0;--level) {
        uint64_t *p=entries(pages[level]);if(!p) goto done;
        path[level]=&p[index_at(a,level)];
        if(!(*path[level]&P) || !(*path[level]&U) || (*path[level]&128)) goto done;
        if(level) pages[level-1]=*path[level]&ADDR;
    }
    uint64_t leaf=*path[0]&ADDR;*path[0]=0;
    arch_address_invalidate(s->root,a);
    if(!pmm_free_page(leaf)) arch_halt();
    for(unsigned level=0;level<3;++level) {
        uint64_t *p=entries(pages[level]);bool empty=true;
        for(unsigned i=0;i<512;++i) if(p[i]) {empty=false;break;}
        if(!empty) break;
        *path[level+1]=0;arch_address_invalidate(s->root,a);
        if(!pmm_free_page(pages[level])) arch_halt();
    }
    --s->pages;ok=true;
done:
    arch_interrupt_restore(flags);return ok;
}
