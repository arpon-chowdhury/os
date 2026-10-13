#include "block.h"
#include "arch.h"
static struct block_backend backend;
static struct block_info devices[BLOCK_MAX_DEVICES];
static bool initialized,busy;
static uint8_t bounce[BLOCK_MAX_SECTORS*BLOCK_SECTOR_SIZE];
static bool acquire(void) {
    uint64_t flags=arch_interrupt_save();bool ok=!busy;
    if(ok) busy=true;
    arch_interrupt_restore(flags);return ok;
}
static void release(void) {
    uint64_t flags=arch_interrupt_save();busy=false;arch_interrupt_restore(flags);
}
bool block_init(const struct block_backend *b,const struct block_info info[BLOCK_MAX_DEVICES]) {
    if(!b || !b->transfer || !b->flush || !info || initialized || !acquire()) return false;
    if(initialized) {release();return false;}
    backend=*b;
    for(unsigned i=0;i<BLOCK_MAX_DEVICES;++i) {
        devices[i]=info[i];devices[i].writable=false;
        if(!devices[i].sectors || devices[i].sectors>0x10000000ull) devices[i].present=false;
    }
    initialized=true;release();return true;
}
bool block_get_info(unsigned device,struct block_info *out) {
    uint64_t flags=arch_interrupt_save();bool ok=initialized && device<BLOCK_MAX_DEVICES && out;
    if(ok) *out=devices[device];
    arch_interrupt_restore(flags);return ok;
}
enum block_result block_set_writable(unsigned device,bool enabled) {
    if(!initialized || device>=BLOCK_MAX_DEVICES || !devices[device].present) return BLOCK_ABSENT;
    if(!acquire()) return BLOCK_BUSY;
    if(!devices[device].present) {release();return BLOCK_ABSENT;}
    enum block_result result=enabled && !devices[device].flush_supported?BLOCK_READ_ONLY:BLOCK_OK;
    if(result==BLOCK_OK) devices[device].writable=enabled;
    release();return result;
}
static enum block_result io(unsigned device,uint64_t lba,unsigned count,void *buffer,size_t bytes,bool write) {
    if(!initialized || device>=BLOCK_MAX_DEVICES || !devices[device].present) return BLOCK_ABSENT;
    if(!buffer || !count || count>BLOCK_MAX_SECTORS || bytes!=(size_t)count*BLOCK_SECTOR_SIZE ||
       lba>=devices[device].sectors || count>devices[device].sectors-lba) return BLOCK_INVALID;
    if(!acquire()) return BLOCK_BUSY;
    enum block_result result;
    if(!devices[device].present) {result=BLOCK_ABSENT;goto done;}
    if(write && !devices[device].writable) {result=BLOCK_READ_ONLY;goto done;}
    if(write) for(size_t i=0;i<bytes;++i) bounce[i]=((const uint8_t *)buffer)[i];
    result=backend.transfer(backend.context,device,lba,count,bounce,write);
    /* Every successful write includes durable completion, not just data acceptance. */
    if(result==BLOCK_OK && write) result=backend.flush(backend.context,device);
    if(result==BLOCK_OK && !write) for(size_t i=0;i<bytes;++i) ((uint8_t *)buffer)[i]=bounce[i];
    if(result==BLOCK_IO || result==BLOCK_TIMEOUT) {
        /* No retry of a partial write; quarantine the shared controller. */
        for(unsigned i=0;i<BLOCK_MAX_DEVICES;++i) devices[i].present=devices[i].writable=false;
    }
done:
    for(size_t i=0;i<sizeof(bounce);++i) bounce[i]=0;
    release();return result;
}
enum block_result block_read(unsigned device,uint64_t lba,unsigned count,void *buffer,size_t bytes) {
    return io(device,lba,count,buffer,bytes,false);
}
enum block_result block_write(unsigned device,uint64_t lba,unsigned count,const void *buffer,size_t bytes) {
    return io(device,lba,count,(void *)buffer,bytes,true);
}
enum block_result block_flush(unsigned device) {
    if(!initialized || device>=BLOCK_MAX_DEVICES || !devices[device].present) return BLOCK_ABSENT;
    if(!acquire()) return BLOCK_BUSY;
    if(!devices[device].present) {release();return BLOCK_ABSENT;}
    enum block_result result=devices[device].flush_supported?backend.flush(backend.context,device):BLOCK_READ_ONLY;
    if(result==BLOCK_IO || result==BLOCK_TIMEOUT)
        for(unsigned i=0;i<BLOCK_MAX_DEVICES;++i) devices[i].present=devices[i].writable=false;
    release();return result;
}
