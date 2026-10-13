#include "block.h"
#include "arch.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static bool enabled=true,fail_read,fail_flush;
static unsigned calls,flushes;
static uint8_t disk[8192],caller[4096];
uint64_t arch_interrupt_save(void) {uint64_t value=enabled?512:0;enabled=false;return value;}
void arch_interrupt_restore(uint64_t value) {enabled=(value&512)!=0;}
static enum block_result transfer(void *context,unsigned device,uint64_t lba,unsigned count,void *data,bool write) {
    assert(context==disk && !device && enabled);++calls;
    uint8_t nested[512];assert(block_read(0,0,1,nested,sizeof(nested))==BLOCK_BUSY);
    assert(block_set_writable(0,false)==BLOCK_BUSY && block_flush(0)==BLOCK_BUSY);
    if(write) {memset(caller,0xee,sizeof(caller));memcpy(disk+lba*512,data,count*512);}
    else {memcpy(data,disk+lba*512,count*512);if(fail_read) return BLOCK_IO;}
    return BLOCK_OK;
}
static enum block_result flush(void *context,unsigned device) {
    assert(context==disk && !device && enabled);++flushes;return fail_flush?BLOCK_TIMEOUT:BLOCK_OK;
}
int main(int argc,char **argv) {
    (void)argv;
    assert(block_read(0,0,1,caller,512)==BLOCK_ABSENT);
    struct block_backend backend={disk,transfer,flush};
    struct block_info devices[2]={{.sectors=16,.present=true,.flush_supported=true},{.sectors=16,.present=true}};
    assert(!block_init(0,devices) && block_init(&backend,devices) && !block_init(&backend,devices));
    struct block_info info;assert(block_get_info(0,&info) && !info.writable && !block_get_info(2,&info));
    assert(block_read(2,0,1,caller,512)==BLOCK_ABSENT);
    assert(block_write(0,0,1,caller,512)==BLOCK_READ_ONLY && !calls);
    assert(block_set_writable(1,true)==BLOCK_READ_ONLY);
    assert(block_read(0,0,0,caller,0)==BLOCK_INVALID);
    assert(block_read(0,0,9,caller,4608)==BLOCK_INVALID);
    assert(block_read(0,15,2,caller,1024)==BLOCK_INVALID);
    assert(block_read(0,UINT64_MAX,1,caller,512)==BLOCK_INVALID);
    assert(block_read(0,0,1,0,512)==BLOCK_INVALID);
    assert(block_read(0,0,1,caller,511)==BLOCK_INVALID && !calls);
    memset(caller,0xa5,sizeof(caller));assert(block_set_writable(0,true)==BLOCK_OK);
    assert(block_write(0,4,8,caller,sizeof(caller))==BLOCK_OK && calls==1 && flushes==1);
    for(unsigned i=0;i<4096;++i) assert(disk[2048+i]==0xa5 && caller[i]==0xee);
    assert(block_read(0,4,8,caller,sizeof(caller))==BLOCK_OK && caller[0]==0xa5 && caller[4095]==0xa5);
    assert(block_read(0,15,1,caller,512)==BLOCK_OK);
    memset(caller,0x77,sizeof(caller));
    if(argc>1) {fail_flush=true;assert(block_write(0,0,1,caller,512)==BLOCK_TIMEOUT);}
    else {fail_read=true;assert(block_read(0,0,8,caller,sizeof(caller))==BLOCK_IO);for(unsigned i=0;i<4096;++i) assert(caller[i]==0x77);}
    assert(block_get_info(0,&info) && !info.present && !info.writable);
    assert(block_get_info(1,&info) && !info.present);
    unsigned before=calls;assert(block_read(0,0,1,caller,512)==BLOCK_ABSENT && calls==before && enabled);
    puts("Block bounds, read-only policy, snapshot writes, reentrant busy, full read commit, flush failure and controller quarantine passed.");
}
