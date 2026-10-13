#include "onfs.h"
#define FILE 1u
#define DIR 2u
#define ENTRY_BASE 512u
#define ENTRY_BYTES 64u
static uint32_t u32(const uint8_t *b) {return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static uint64_t u64(const uint8_t *b) {return u32(b)|((uint64_t)u32(b+4)<<32);}
static void put32(uint8_t *b,uint32_t v) {for(unsigned i=0;i<4;++i) b[i]=v>>(i*8);}
static void put64(uint8_t *b,uint64_t v) {put32(b,v);put32(b+4,v>>32);}
static void copy(void *out,const void *in,size_t n) {for(size_t i=0;i<n;++i) ((uint8_t *)out)[i]=((const uint8_t *)in)[i];}
static void zero(void *out,size_t n) {for(size_t i=0;i<n;++i) ((uint8_t *)out)[i]=0;}
static bool zeros(const uint8_t *b,size_t n) {for(size_t i=0;i<n;++i) if(b[i]) return false;return true;}
static bool equal(const void *a,const void *b,size_t n) {for(size_t i=0;i<n;++i) if(((const uint8_t *)a)[i]!=((const uint8_t *)b)[i]) return false;return true;}
static uint8_t *node(uint8_t *p,unsigned n) {return p+ENTRY_BASE+n*ENTRY_BYTES;}
static const uint8_t *cnode(const uint8_t *p,unsigned n) {return p+ENTRY_BASE+n*ENTRY_BYTES;}
uint32_t onfs_crc32(const void *data,size_t n) {
    uint32_t crc=UINT32_MAX;const uint8_t *p=data;
    for(size_t i=0;i<n;++i) {crc^=p[i];for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}
    return ~crc;
}
static bool character(char c) {return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.';}
static bool name_valid(const char *p,size_t n) {
    if(!n || n>VFS_NAME_MAX || (n==1 && p[0]=='.') || (n==2 && p[0]=='.' && p[1]=='.')) return false;
    for(size_t i=0;i<n;++i) if(!character(p[i])) return false;
    return true;
}
static bool payload_valid(const uint8_t *p,uint64_t generation) {
    if(!equal(p,"ONNODES1",8) || u32(p+8)!=1 || u32(p+12)!=ONFS_NODES || !zeros(p+16,496) ||
       !zeros(p+ENTRY_BASE+ONFS_NODES*ENTRY_BYTES,ONFS_METADATA_BYTES-ENTRY_BASE-ONFS_NODES*ENTRY_BYTES)) return false;
    const uint8_t *root=cnode(p,0);
    if(root[0]!=DIR || u32(root+8) || u32(root+12) || u32(root+16) || u32(root+20) || u64(root+24)!=1 || !zeros(root+32,32)) return false;
    unsigned cursor=0;
    for(unsigned i=0;i<ONFS_NODES;++i) {
        const uint8_t *e=cnode(p,i);
        if(!e[0]) {if(!zeros(e,64)) return false;continue;}
        if((e[0]!=FILE && e[0]!=DIR) || !zeros(e+1,7) || !u64(e+24) || u64(e+24)>generation) return false;
        if(i) {
            unsigned parent=u32(e+8),n=0;
            while(n<32 && e[32+n]) ++n;
            if(parent>=ONFS_NODES || cnode(p,parent)[0]!=DIR || !name_valid((const char *)e+32,n) || !zeros(e+32+n,32-n)) return false;
            unsigned ancestor=i,depth=0;
            while(ancestor && depth++<ONFS_NODES) {
                if(ancestor>=ONFS_NODES) return false;
                unsigned next=u32(cnode(p,ancestor)+8);
                if(next>=ONFS_NODES || cnode(p,next)[0]!=DIR) return false;
                ancestor=next;
            }
            if(ancestor) return false;
            for(unsigned j=1;j<i;++j) {const uint8_t *other=cnode(p,j);if(other[0] && u32(other+8)==parent && equal(e+32,other+32,32)) return false;}
        }
        unsigned size=u32(e+12),start=u32(e+16),blocks=u32(e+20);
        if(e[0]==DIR) {if(size || start || blocks) return false;continue;}
        if(size>VFS_FILE_MAX || blocks!=(size+511)/512 || (blocks?start!=cursor:start!=0) || blocks>ONFS_DATA_BYTES/512-cursor) return false;
        if(blocks && !zeros(p+ONFS_METADATA_BYTES+start*512+size,blocks*512-size)) return false;
        cursor+=blocks;
    }
    return zeros(p+ONFS_METADATA_BYTES+cursor*512,ONFS_DATA_BYTES-cursor*512);
}
static bool header_valid(struct onfs *f,const uint8_t *h,unsigned bank,uint64_t *generation) {
    if(!equal(h,"ONFSCOW1",8) || u32(h+8)!=1 || u32(h+12)!=bank || u64(h+16)!=f->device.base ||
       u32(h+24)!=ONFS_VOLUME_SECTORS || u32(h+28)!=ONFS_PAYLOAD_SECTORS || !u64(h+32) ||
       !zeros(h+44,464) || u32(h+508)!=onfs_crc32(h,508)) return false;
    *generation=u64(h+32);return true;
}
static bool read_bank(struct onfs *f,unsigned bank,uint64_t *generation,bool *valid) {
    uint8_t header[512];uint64_t base=f->device.base+bank*ONFS_BANK_SECTORS;
    *valid=false;
    if(!f->device.read(f->device.context,base,1,header)) return false;
    if(!header_valid(f,header,bank,generation)) return true;
    for(unsigned n=0;n<ONFS_PAYLOAD_SECTORS;n+=8)
        if(!f->device.read(f->device.context,base+1+n,8,f->work+n*512)) return false;
    *valid=u32(header+40)==onfs_crc32(f->work,ONFS_BYTES) && payload_valid(f->work,*generation);
    return true;
}
static enum vfs_result mount(void *context,bool writable) {
    struct onfs *f=context;
    if(f->mounted || !f->device.read || !f->device.write || f->device.base>=f->device.sectors ||
       ONFS_VOLUME_SECTORS>f->device.sectors-f->device.base) return VFS_INVALID;
    bool first=false,second=false;uint64_t a=0,b=0;
    if(!read_bank(f,0,&a,&first)) return VFS_IO;
    if(first) copy(f->current,f->work,ONFS_BYTES);
    if(!read_bank(f,1,&b,&second)) return VFS_IO;
    if(!first && !second) return VFS_CORRUPT;
    if(first && second && (a==b || (a>b?a-b:b-a)!=1)) return VFS_CORRUPT;
    if(second && (!first || b>a)) {copy(f->current,f->work,ONFS_BYTES);f->generation=b;f->bank=1;}
    else {f->generation=a;f->bank=0;}
    f->degraded=!(first && second);f->read_only=!writable || f->degraded;f->poisoned=false;f->mounted=true;
    zero(f->handles,sizeof(f->handles));return VFS_OK;
}
static enum vfs_result unmount(void *context) {
    struct onfs *f=context;
    for(unsigned i=0;i<VFS_HANDLES;++i) if(f->handles[i].token) return VFS_BUSY;
    f->mounted=false;zero(f->current,sizeof(f->current));zero(f->work,sizeof(f->work));return VFS_OK;
}
static enum vfs_result state(struct onfs *f,bool write) {
    if(!f->mounted) return VFS_UNMOUNTED;
    if(f->poisoned) return VFS_IO;
    return write && f->read_only?VFS_READ_ONLY:VFS_OK;
}
/* Parse the whole bounded path before lookup; no relative traversal/symlinks. */
static bool path_valid(const char *path) {
    if(!path || path[0]!='/') return false;
    size_t length=0;while(length<VFS_PATH_MAX && path[length]) ++length;
    if(!length || length==VFS_PATH_MAX) return false;
    if(length==1) return true;
    size_t start=1;
    for(size_t i=1;i<=length;++i) if(!path[i] || path[i]=='/') {
        if(!name_valid(path+start,i-start)) return false;start=i+1;
    }
    return true;
}
static enum vfs_result resolve(struct onfs *f,const char *path,bool parent,unsigned *id,char leaf[32]) {
    if(!path_valid(path)) return VFS_INVALID;
    unsigned current=0;size_t start=1;
    if(!path[1]) {if(parent) return VFS_INVALID;*id=0;return VFS_OK;}
    for(size_t i=1;;++i) if(!path[i] || path[i]=='/') {
        size_t length=i-start;char component[32]={0};copy(component,path+start,length);
        if(parent && !path[i]) {*id=current;copy(leaf,component,32);return VFS_OK;}
        if(cnode(f->current,current)[0]!=DIR) return VFS_NOT_DIR;
        unsigned found=ONFS_NODES;
        for(unsigned n=1;n<ONFS_NODES;++n) {const uint8_t *e=cnode(f->current,n);if(e[0] && u32(e+8)==current && equal(e+32,component,32)) {found=n;break;}}
        if(found==ONFS_NODES) return VFS_NOT_FOUND;
        current=found;if(!path[i]) {*id=current;return VFS_OK;}start=i+1;
    }
}
static enum vfs_result commit(struct onfs *f) {
    if(f->generation==UINT64_MAX || !payload_valid(f->work,f->generation+1)) return VFS_RANGE;
    unsigned bank=1-f->bank;uint64_t base=f->device.base+bank*ONFS_BANK_SECTORS;
    uint8_t header[512]={0};
    /* Invalidate the old inactive commit before touching its payload. Each
       write callback must flush before success. The active bank is immutable. */
    if(!f->device.write(f->device.context,base,1,header)) goto failed;
    for(unsigned n=0;n<ONFS_PAYLOAD_SECTORS;n+=8)
        if(!f->device.write(f->device.context,base+1+n,8,f->work+n*512)) goto failed;
    copy(header,"ONFSCOW1",8);put32(header+8,1);put32(header+12,bank);put64(header+16,f->device.base);
    put32(header+24,ONFS_VOLUME_SECTORS);put32(header+28,ONFS_PAYLOAD_SECTORS);put64(header+32,f->generation+1);
    put32(header+40,onfs_crc32(f->work,ONFS_BYTES));put32(header+508,onfs_crc32(header,508));
    if(!f->device.write(f->device.context,base,1,header)) goto failed;
    copy(f->current,f->work,ONFS_BYTES);f->bank=bank;++f->generation;return VFS_OK;
failed:
    f->poisoned=true;return VFS_IO;
}
/* Rebuild canonical packed extents entirely from the immutable active snapshot. */
static enum vfs_result pack(struct onfs *f,unsigned changed,uint64_t offset,const void *data,size_t length) {
    zero(f->work+ONFS_METADATA_BYTES,ONFS_DATA_BYTES);unsigned cursor=0;
    for(unsigned i=1;i<ONFS_NODES;++i) {
        uint8_t *next=node(f->work,i);const uint8_t *old=cnode(f->current,i);
        if(next[0]!=FILE) continue;
        unsigned size=u32(next+12),blocks=(size+511)/512;
        if(blocks>ONFS_DATA_BYTES/512-cursor) return VFS_FULL;
        put32(next+16,blocks?cursor:0);put32(next+20,blocks);
        uint8_t *out=f->work+ONFS_METADATA_BYTES+cursor*512;
        if(old[0]==FILE && u64(old+24)==u64(next+24)) copy(out,f->current+ONFS_METADATA_BYTES+u32(old+16)*512,u32(old+12));
        if(i==changed && length) copy(out+offset,data,length);
        cursor+=blocks;
    }
    return VFS_OK;
}
static enum vfs_result create(void *context,const char *path,bool directory) {
    struct onfs *f=context;enum vfs_result result=state(f,true);if(result!=VFS_OK) return result;
    unsigned parent,existing;char name[32];result=resolve(f,path,true,&parent,name);if(result!=VFS_OK) return result;
    if(cnode(f->current,parent)[0]!=DIR) return VFS_NOT_DIR;
    result=resolve(f,path,false,&existing,0);if(result==VFS_OK) return VFS_EXISTS;
    if(result!=VFS_NOT_FOUND) return result;
    if(f->generation==UINT64_MAX) return VFS_RANGE;
    unsigned n=1;while(n<ONFS_NODES && cnode(f->current,n)[0]) ++n;if(n==ONFS_NODES) return VFS_FULL;
    copy(f->work,f->current,ONFS_BYTES);uint8_t *e=node(f->work,n);e[0]=directory?DIR:FILE;put32(e+8,parent);put64(e+24,f->generation+1);copy(e+32,name,32);
    return commit(f);
}
static uint64_t handle_serial;
static enum vfs_result open_file(void *context,const char *path,unsigned flags,vfs_handle *out) {
    struct onfs *f=context;enum vfs_result result=state(f,(flags&VFS_WRITE)!=0);if(result!=VFS_OK) return result;
    if(!out || !flags || (flags&~(VFS_READ|VFS_WRITE))) return VFS_INVALID;
    unsigned n;result=resolve(f,path,false,&n,0);if(result!=VFS_OK) return result;
    if(cnode(f->current,n)[0]!=FILE) return VFS_IS_DIR;
    for(unsigned slot=0;slot<VFS_HANDLES;++slot) if(!f->handles[slot].token) {
        if(handle_serial>=UINT64_MAX>>8) return VFS_FULL;
        vfs_handle token=(++handle_serial<<8)|(slot+1);f->handles[slot]=(struct onfs_open){token,n,flags};*out=token;return VFS_OK;
    }
    return VFS_FULL;
}
static struct onfs_open *handle(struct onfs *f,vfs_handle token) {
    unsigned slot=token&255;if(!slot || slot>VFS_HANDLES || f->handles[slot-1].token!=token) return 0;return &f->handles[slot-1];
}
static enum vfs_result close_file(void *context,vfs_handle token) {
    struct onfs *f=context;struct onfs_open *h=handle(f,token);if(!h) return VFS_BAD_HANDLE;*h=(struct onfs_open){0};return VFS_OK;
}
static enum vfs_result read_file(void *context,vfs_handle token,uint64_t offset,void *data,size_t length,size_t *done) {
    struct onfs *f=context;enum vfs_result result=state(f,false);if(result!=VFS_OK) return result;
    struct onfs_open *h=handle(f,token);if(!h || !(h->flags&VFS_READ)) return VFS_BAD_HANDLE;
    if(!done || (length && !data) || length>VFS_FILE_MAX) return VFS_INVALID;
    const uint8_t *e=cnode(f->current,h->node);size_t size=u32(e+12);
    if(offset>size) return VFS_RANGE;
    size_t n=size-offset;if(n>length) n=length;
    copy(data,f->current+ONFS_METADATA_BYTES+u32(e+16)*512+offset,n);*done=n;return VFS_OK;
}
static enum vfs_result write_file(void *context,vfs_handle token,uint64_t offset,const void *data,size_t length,size_t *done) {
    struct onfs *f=context;enum vfs_result result=state(f,true);if(result!=VFS_OK) return result;
    struct onfs_open *h=handle(f,token);if(!h || !(h->flags&VFS_WRITE)) return VFS_BAD_HANDLE;
    if(!done || (length && !data) || length>VFS_FILE_MAX) return VFS_INVALID;
    const uint8_t *e=cnode(f->current,h->node);unsigned size=u32(e+12);
    if(offset>size || length>VFS_FILE_MAX-offset) return VFS_RANGE;
    if(!length) {*done=0;return VFS_OK;}
    copy(f->work,f->current,ONFS_BYTES);if(offset+length>size) put32(node(f->work,h->node)+12,offset+length);
    result=pack(f,h->node,offset,data,length);if(result!=VFS_OK) return result;
    result=commit(f);if(result==VFS_OK) *done=length;return result;
}
static enum vfs_result remove_node(void *context,const char *path) {
    struct onfs *f=context;enum vfs_result result=state(f,true);if(result!=VFS_OK) return result;
    unsigned n;result=resolve(f,path,false,&n,0);if(result!=VFS_OK) return result;if(!n) return VFS_INVALID;
    for(unsigned i=0;i<VFS_HANDLES;++i) if(f->handles[i].token && f->handles[i].node==n) return VFS_BUSY;
    for(unsigned i=1;i<ONFS_NODES;++i) if(cnode(f->current,i)[0] && u32(cnode(f->current,i)+8)==n) return VFS_NOT_EMPTY;
    copy(f->work,f->current,ONFS_BYTES);zero(node(f->work,n),64);result=pack(f,ONFS_NODES,0,0,0);
    return result==VFS_OK?commit(f):result;
}
static enum vfs_result stat_path(void *context,const char *path,struct vfs_stat *out) {
    struct onfs *f=context;enum vfs_result result=state(f,false);if(result!=VFS_OK) return result;if(!out) return VFS_INVALID;
    unsigned n;result=resolve(f,path,false,&n,0);if(result!=VFS_OK) return result;
    const uint8_t *e=cnode(f->current,n);*out=(struct vfs_stat){e[0],u32(e+12)};return VFS_OK;
}
static enum vfs_result readdir(void *context,const char *path,unsigned ordinal,struct vfs_dirent *out) {
    struct onfs *f=context;enum vfs_result result=state(f,false);if(result!=VFS_OK) return result;if(!out) return VFS_INVALID;
    unsigned n;result=resolve(f,path,false,&n,0);if(result!=VFS_OK) return result;
    if(cnode(f->current,n)[0]!=DIR) return VFS_NOT_DIR;
    for(unsigned i=1;i<ONFS_NODES;++i) {const uint8_t *e=cnode(f->current,i);if(e[0] && u32(e+8)==n) {
        if(!ordinal) {copy(out->name,e+32,32);out->stat=(struct vfs_stat){e[0],u32(e+12)};return VFS_OK;}--ordinal;
    }}return VFS_NOT_FOUND;
}
static enum vfs_result volume(void *context,struct vfs_volume *out) {
    struct onfs *f=context;enum vfs_result result=state(f,false);if(result!=VFS_OK) return result;if(!out) return VFS_INVALID;
    unsigned nodes=0,used=0;for(unsigned n=0;n<ONFS_NODES;++n) {const uint8_t *e=cnode(f->current,n);nodes+=e[0]!=0;used+=u32(e+20)*512;}
    *out=(struct vfs_volume){f->generation,used,ONFS_DATA_BYTES-used,nodes,f->read_only,f->degraded};return VFS_OK;
}
const struct vfs_ops onfs_ops={mount,unmount,create,open_file,close_file,read_file,write_file,remove_node,stat_path,readdir,volume};
