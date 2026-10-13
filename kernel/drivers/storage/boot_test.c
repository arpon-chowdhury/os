#include "block.h"
#include "arch.h"
#include "task.h"
#ifndef ON_STORAGE_TEST
#define ON_STORAGE_TEST 0
#endif
#if ON_STORAGE_TEST
static uint8_t readback[4096];
#if ON_STORAGE_TEST != 3
static uint8_t bytes[4096];
#endif
static void require(bool ok) {if(!ok) {arch_debug_puts("ON.: STORAGE TEST FAILED; halted safely.\n");arch_halt();}}
#if ON_STORAGE_TEST != 3
static uint8_t original(uint64_t lba,unsigned n) {return (uint8_t)(lba*17+n*13+(n>>8));}
static uint8_t written(unsigned n) {return (uint8_t)(0xa7+n*29+(n>>8));}
#if ON_STORAGE_TEST != 5
static void check_original(uint64_t lba) {
    require(block_read(0,lba,1,readback,512)==BLOCK_OK);
    for(unsigned i=0;i<512;++i) require(readback[i]==original(lba,i));
}
#endif
#endif
#if ON_STORAGE_TEST == 1
static void reader(void *argument) {
    (void)argument;uint8_t data[512];
    for(;;) {
        require(block_read(0,0,1,data,sizeof(data))==BLOCK_OK);
        for(unsigned n=0;n<512;++n) require(data[n]==original(0,n));
        task_yield();
    }
}
#endif
#endif
void storage_boot_test(void) {
#if ON_STORAGE_TEST
    arch_interrupt_restore(0x200);struct block_info info;
    require(block_get_info(0,&info));
#if ON_STORAGE_TEST == 3
    require(!info.present && block_read(0,0,1,readback,512)==BLOCK_ABSENT);
    arch_debug_puts("ON.: STORAGE TEST absent device fallback passed.\n");
#elif ON_STORAGE_TEST == 5
    require(!info.present && block_get_info(1,&info) && info.present && info.sectors==4096);
    for(unsigned end=0;end<2;++end) {
        uint64_t lba=end?4095:0;
        require(block_read(1,lba,1,readback,512)==BLOCK_OK);
        for(unsigned n=0;n<512;++n) require(readback[n]==original(lba,n));
    }
    for(unsigned n=0;n<sizeof(bytes);++n) bytes[n]=written(n);
    require(block_set_writable(1,true)==BLOCK_OK);
    require(block_write(1,32,8,bytes,sizeof(bytes))==BLOCK_OK);
    require(block_read(1,32,8,readback,sizeof(readback))==BLOCK_OK);
    for(unsigned n=0;n<sizeof(bytes);++n) require(bytes[n]==readback[n]);
    require(block_set_writable(1,false)==BLOCK_OK && block_read(0,0,1,readback,512)==BLOCK_ABSENT);
    arch_debug_puts("ON.: STORAGE TEST slave-only selection and flushed I/O passed.\n");
#else
    require(info.present && info.sectors==4096 && !info.writable && info.flush_supported);
    check_original(0);check_original(4095);
    require(block_read(0,4096,1,readback,512)==BLOCK_INVALID);
    require(block_read(0,4095,2,readback,1024)==BLOCK_INVALID);
    require(block_read(0,UINT64_MAX,1,readback,512)==BLOCK_INVALID);
    require(block_read(0,0,0,readback,0)==BLOCK_INVALID);
    require(block_read(0,0,9,readback,4096)==BLOCK_INVALID);
    require(block_read(0,0,1,0,512)==BLOCK_INVALID);
    require(block_read(0,0,1,readback,511)==BLOCK_INVALID);
    require(block_read(1,0,1,readback,512)==BLOCK_ABSENT);
    for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=written(i);
    require(block_write(0,32,8,bytes,sizeof(bytes))==BLOCK_READ_ONLY);
#if ON_STORAGE_TEST == 1
    require(block_set_writable(0,true)==BLOCK_OK);
    for(unsigned round=0;round<32;++round) {
        require(block_write(0,32,8,bytes,sizeof(bytes))==BLOCK_OK);
        require(block_read(0,32,8,readback,sizeof(readback))==BLOCK_OK);
        for(unsigned n=0;n<sizeof(bytes);++n) require(bytes[n]==readback[n]);
    }
    require(block_flush(0)==BLOCK_OK && block_set_writable(0,false)==BLOCK_OK);
    check_original(31);check_original(40);
    require(task_create("storage-reader",reader,0));
    arch_debug_puts("ON.: STORAGE TEST bounded reads, writes, flush, boundaries and repeated I/O passed.\n");
#elif ON_STORAGE_TEST == 2
    require(block_read(0,32,8,readback,sizeof(readback))==BLOCK_OK);
    for(unsigned n=0;n<sizeof(bytes);++n) require(bytes[n]==readback[n]);
    check_original(31);check_original(40);
    arch_debug_puts("ON.: STORAGE TEST persisted data after reboot passed.\n");
#elif ON_STORAGE_TEST == 4
    require(block_set_writable(0,true)==BLOCK_OK);
    enum block_result result=block_write(0,32,8,bytes,sizeof(bytes));
    require(result==BLOCK_IO || result==BLOCK_TIMEOUT);
    require(block_get_info(0,&info) && !info.present && !info.writable);
    require(block_read(0,0,1,readback,512)==BLOCK_ABSENT);
    arch_debug_puts("ON.: STORAGE TEST backend write failure quarantine passed.\n");
#endif
#endif
#endif
}
