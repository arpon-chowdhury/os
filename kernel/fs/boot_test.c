#include "onfs.h"
#include "block.h"
#include "memory.h"
#include "arch.h"
#include "task.h"
#ifndef ON_FS_TEST
#define ON_FS_TEST 0
#endif
#if ON_FS_TEST
static struct onfs filesystem;
#if ON_FS_TEST == 1 || ON_FS_TEST == 5
static uint8_t bytes[4096];
#endif
#if ON_FS_TEST == 1 || ON_FS_TEST == 2 || ON_FS_TEST == 4 || ON_FS_TEST == 7
static uint8_t readback[4096];
#endif
static unsigned write_calls;
static void require(bool ok) {if(!ok) {arch_debug_puts("ON.: FS TEST FAILED; halted safely.\n");arch_halt();}}
static bool read_disk(void *context,uint64_t lba,unsigned count,void *data) {
    (void)context;return block_read(0,lba,count,data,count*512)==BLOCK_OK;
}
static bool write_disk(void *context,uint64_t lba,unsigned count,const void *data) {
    (void)context;bool ok=block_write(0,lba,count,data,count*512)==BLOCK_OK;
    ++write_calls;
#if ON_FS_TEST == 6
    if(ok && write_calls==2) {arch_debug_puts("ON.: FS TEST crash point after inactive payload write reached.\n");arch_halt();}
#endif
    return ok;
}
#if ON_FS_TEST != 3 && ON_FS_TEST != 5 && ON_FS_TEST != 6 && ON_FS_TEST != 7
static uint8_t pattern(unsigned n) {return (uint8_t)(n*23+(n>>8));}
static void check_note(void) {
    vfs_handle file;size_t done;
    require(vfs_open("/docs/sub/note",VFS_READ,&file)==VFS_OK);
    require(vfs_read(file,0,readback,sizeof(readback),&done)==VFS_OK && done==1512);
    for(unsigned n=0;n<1512;++n) require(readback[n]==(n>=510 && n<515?(uint8_t)"ONFS!"[n-510]:pattern(n)));
    require(vfs_close(file)==VFS_OK);
}
#endif
#if ON_FS_TEST == 1
static void reader(void *argument) {(void)argument;for(;;) {check_note();task_yield();}}
#endif
#endif
void filesystem_boot_test(void) {
#if ON_FS_TEST
    arch_interrupt_restore(0x200);struct block_info info;
    require(block_get_info(0,&info) && info.present && info.sectors==4096);
    filesystem.device=(struct onfs_device){0,info.sectors,64,read_disk,write_disk};
#if ON_FS_TEST == 1 || ON_FS_TEST == 5 || ON_FS_TEST == 6 || ON_FS_TEST == 7
    require(block_set_writable(0,true)==BLOCK_OK);
#endif
    enum vfs_result result=vfs_mount(&onfs_ops,&filesystem,ON_FS_TEST==1 || ON_FS_TEST==5 || ON_FS_TEST==6 || ON_FS_TEST==7);
#if ON_FS_TEST == 3
    require(result==VFS_CORRUPT && !write_calls);
    arch_debug_puts("ON.: FS TEST corrupted metadata rejected without writes passed.\n");
#else
    require(result==VFS_OK);
#if ON_FS_TEST == 1
    struct pmm_statistics before=pmm_get_statistics();size_t done;vfs_handle file;
    require(vfs_mkdir("/docs")==VFS_OK && vfs_mkdir("/docs/sub")==VFS_OK);
    require(vfs_create("/docs/sub/note")==VFS_OK && vfs_open("/docs/sub/note",VFS_READ|VFS_WRITE,&file)==VFS_OK);
    for(unsigned n=0;n<sizeof(bytes);++n) bytes[n]=pattern(n);
    require(vfs_write(file,0,bytes,1000,&done)==VFS_OK && done==1000);
    require(vfs_write(file,510,"ONFS!",5,&done)==VFS_OK && done==5);
    require(vfs_write(file,1000,bytes+1000,512,&done)==VFS_OK && done==512);
    unsigned calls=write_calls;done=99;
    require(vfs_write(file,UINT64_MAX,bytes,1,&done)==VFS_RANGE && done==99);
    require(vfs_write(file,1513,bytes,1,&done)==VFS_RANGE && write_calls==calls);
    require(vfs_create("/../bad")==VFS_INVALID && vfs_create("/docs//bad")==VFS_INVALID);
    require(vfs_create("/docs/sub/note/bad")==VFS_NOT_DIR && vfs_remove("/docs")==VFS_NOT_EMPTY);
    require(vfs_remove("/docs/sub/note")==VFS_BUSY && vfs_close(file)==VFS_OK);
    require(vfs_create("/trash")==VFS_OK && vfs_remove("/trash")==VFS_OK);
    require(vfs_mkdir("/empty")==VFS_OK && vfs_remove("/empty")==VFS_OK);
    require(vfs_create("/stable")==VFS_OK && vfs_open("/stable",VFS_WRITE,&file)==VFS_OK);
    require(vfs_write(file,0,"anchor",6,&done)==VFS_OK && vfs_close(file)==VFS_OK);
    for(unsigned round=0;round<64;++round) check_note();
    struct pmm_statistics after=pmm_get_statistics();require(before.free_frames==after.free_frames && before.allocated_frames==after.allocated_frames);
    require(task_create("filesystem-reader",reader,0));
    arch_debug_puts("ON.: FS TEST directories, create/open/read/write/delete, boundaries and allocation-free operations passed.\n");
#elif ON_FS_TEST == 2 || ON_FS_TEST == 4
    check_note();struct vfs_volume volume;struct vfs_stat stat;
    require(vfs_volume_info(&volume)==VFS_OK && volume.read_only);
    require(vfs_stat_path("/trash",&stat)==VFS_NOT_FOUND && vfs_stat_path("/stable",&stat)==VFS_OK && stat.size==6);
    require(vfs_create("/blocked")==VFS_READ_ONLY && !write_calls);
#if ON_FS_TEST == 4
    require(volume.degraded);
    arch_debug_puts("ON.: FS TEST interrupted snapshot recovery read-only passed.\n");
#else
    arch_debug_puts("ON.: FS TEST persistent files after reboot passed.\n");
#endif
#elif ON_FS_TEST == 5
    size_t done;vfs_handle file;
    for(unsigned n=0;n<sizeof(bytes);++n) bytes[n]=0x6d;
    for(unsigned n=0;n<4;++n) {
        char path[]="/f0";path[2]+=n;require(vfs_create(path)==VFS_OK && vfs_open(path,VFS_WRITE,&file)==VFS_OK);
        require(vfs_write(file,0,bytes,n==3?2048:4096,&done)==VFS_OK && vfs_close(file)==VFS_OK);
    }
    require(vfs_open("/f3",VFS_WRITE,&file)==VFS_OK);
    unsigned calls=write_calls;require(vfs_write(file,2048,bytes,1,&done)==VFS_FULL && write_calls==calls);
    require(vfs_close(file)==VFS_OK);
    for(unsigned n=0;n<4;++n) {char path[]="/f0";path[2]+=n;require(vfs_remove(path)==VFS_OK);}
    for(unsigned n=0;n<15;++n) {char path[]="/n00";path[2]='0'+n/10;path[3]='0'+n%10;require(vfs_create(path)==VFS_OK);}
    require(vfs_create("/full")==VFS_FULL);
    for(unsigned n=0;n<15;++n) {char path[]="/n00";path[2]='0'+n/10;path[3]='0'+n%10;require(vfs_remove(path)==VFS_OK);}
    struct vfs_volume volume;require(vfs_volume_info(&volume)==VFS_OK && volume.nodes==1 && volume.used_bytes==0);
    arch_debug_puts("ON.: FS TEST full data/node capacity, delete and reuse passed.\n");
#elif ON_FS_TEST == 7
    vfs_handle file;size_t done=99;require(vfs_open("/docs/sub/note",VFS_READ|VFS_WRITE,&file)==VFS_OK);
    require(vfs_write(file,0,"bad",3,&done)==VFS_IO && done==99);
    require(vfs_read(file,0,readback,3,&done)==VFS_IO);
    require(block_get_info(0,&info) && !info.present && !info.writable);
    require(vfs_close(file)==VFS_OK && vfs_unmount()==VFS_OK);
    arch_debug_puts("ON.: FS TEST write failure and poisoned volume containment passed.\n");
#elif ON_FS_TEST == 6
    vfs_handle file;size_t done;require(vfs_open("/docs/sub/note",VFS_WRITE,&file)==VFS_OK);
    (void)vfs_write(file,0,"cut",3,&done);require(false);
#endif
#endif
#endif
}
