#include "elf.h"
static uint16_t u16(const uint8_t *p) {return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return (uint32_t)u16(p)|((uint32_t)u16(p+2)<<16);}
static uint64_t u64(const uint8_t *p) {return (uint64_t)u32(p)|((uint64_t)u32(p+4)<<32);}
static bool fits(uint64_t offset,uint64_t count,uint64_t stride,size_t length) {
    return offset<=length && stride && count<=(length-offset)/stride;
}
bool elf64_validate(const void *image,size_t length,struct elf_plan *out) {
    if(!image || !out || length<64 || length>ELF_MAX_BYTES) return false;
    const uint8_t *b=image;
    if(b[0]!=127 || b[1]!='E' || b[2]!='L' || b[3]!='F' || b[4]!=2 || b[5]!=1 || b[6]!=1 || b[7] || b[8]) return false;
    for(unsigned i=9;i<16;++i) if(b[i]) return false;
    if(u16(b+16)!=2 || u16(b+18)!=62 || u32(b+20)!=1 || u32(b+48) || u16(b+52)!=64 || u16(b+54)!=56) return false;
    uint64_t phoff=u64(b+32),shoff=u64(b+40);unsigned phnum=u16(b+56),shnum=u16(b+60),shstr=u16(b+62);
    if(!phnum || phnum>ELF_MAX_SEGMENTS || phoff<64 || !fits(phoff,phnum,56,length)) return false;
    if(shnum) {
        if(shnum>128 || u16(b+58)!=64 || shoff<64 || !fits(shoff,shnum,64,length) || shstr>=shnum) return false;
    } else if(shoff || shstr || (u16(b+58)!=0 && u16(b+58)!=64)) return false;
    struct elf_plan p={.entry=u64(b+24)};bool entry_ok=false,stack_seen=false;
    for(unsigned i=0;i<phnum;++i) {
        const uint8_t *h=b+phoff+i*56;uint32_t type=u32(h),flags=u32(h+4);
        uint64_t offset=u64(h+8),address=u64(h+16),filesz=u64(h+32),memsz=u64(h+40),align=u64(h+48);
        if(type==0) {for(unsigned j=0;j<56;++j) if(h[j]) return false;continue;}
        if(type==0x6474e551) {
            if(stack_seen || flags!=6 || offset || address || u64(h+24) || filesz || memsz || (align && align!=16)) return false;
            stack_seen=true;continue;
        }
        if(type!=1 || !(flags&4) || (flags&~7u) || (flags&3)==3 || !memsz || filesz>memsz ||
           offset>length || filesz>length-offset || address<ELF_IMAGE_MIN || address>=ELF_IMAGE_LIMIT ||
           memsz>ELF_IMAGE_LIMIT-address || align<4096 || align>0x200000 || (align&(align-1)) ||
           ((address-offset)&(align-1))) return false;
        uint64_t start=address&~4095ull,end=(address+memsz+4095)&~4095ull;
        if(start<ELF_STACK_TOP+4096 && end>ELF_STACK_BOTTOM-4096) return false;
        if(p.count && start<p.segments[p.count-1].end) return false; /* Sorted, page-disjoint. */
        uint64_t pages=(end-start)/4096;
        if(pages>USER_PAGE_LIMIT-2-p.pages) return false;
        p.pages+=pages;
        p.segments[p.count++]=(struct elf_segment){address,offset,filesz,memsz,start,end,
            VMM_USER|((flags&2)?VMM_WRITE:0)|((flags&1)?VMM_EXEC:0)};
        if((flags&1) && p.entry>=address && p.entry-address<filesz) entry_ok=true;
    }
    if(!p.count || !entry_ok) return false;
    *out=p;return true;
}
bool elf64_populate(struct address_space *s,const void *image,size_t length,uintptr_t *entry) {
    struct elf_plan p;
    if(!s || !s->root || s->pages || s->root==arch_address_current_root() || !entry || !elf64_validate(image,length,&p)) return false;
    uint8_t bytes[4096];const uint8_t *b=image;
    for(unsigned i=0;i<p.count;++i) {
        const struct elf_segment *seg=&p.segments[i];
        for(uint64_t a=seg->start;a<seg->end;a+=4096) {
            for(unsigned n=0;n<4096;++n) bytes[n]=0;
            uint64_t first=a>seg->address?a:seg->address;
            uint64_t last=a+4096<seg->address+seg->filesz?a+4096:seg->address+seg->filesz;
            if(first<last) for(uint64_t n=first;n<last;++n) bytes[n-a]=b[seg->offset+n-seg->address];
            if(!address_space_map(s,a,bytes,sizeof(bytes),seg->permissions)) return false;
        }
    }
    for(uintptr_t a=ELF_STACK_BOTTOM;a<ELF_STACK_TOP;a+=4096)
        if(!address_space_map(s,a,0,0,VMM_USER|VMM_WRITE)) return false;
    *entry=p.entry;return true;
}
