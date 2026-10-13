#include "onfs.h"
#include "arch.h"
#include "../onfs_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SECTORS 4096u
#define BASE 64u
static uint8_t disk[SECTORS*512],saved[SECTORS*512];
static struct onfs fs;
static bool enabled=true,read_fail;
static int fail_at=-1,fail_kind;
static unsigned writes;
uint64_t arch_interrupt_save(void) {uint64_t f=enabled?512:0;enabled=false;return f;}
void arch_interrupt_restore(uint64_t f) {enabled=(f&512)!=0;}
static bool read_disk(void *context,uint64_t lba,unsigned count,void *data) {
    assert(context==disk && enabled && count<=8 && lba>=BASE && lba+count<=BASE+66);
    struct vfs_stat stat;assert(vfs_stat_path("/",&stat)==VFS_BUSY);
    if(read_fail) return false;memcpy(data,disk+lba*512,count*512);return true;
}
static bool write_disk(void *context,uint64_t lba,unsigned count,const void *data) {
    assert(context==disk && enabled && count<=8 && lba>=BASE && lba+count<=BASE+66);
    assert(vfs_create("/reentrant")==VFS_BUSY);
    bool fail=(int)writes++==fail_at;
    if(fail && !fail_kind) return false;
    size_t bytes=count*512;if(fail && fail_kind==2) bytes=bytes>512?513:255;
    memcpy(disk+lba*512,data,bytes);return !fail;
}
static void setup(void) {
    memset(disk,0xa5,sizeof(disk));fs_fixture(disk,BASE);memset(&fs,0,sizeof(fs));
    fs.device=(struct onfs_device){disk,SECTORS,BASE,read_disk,write_disk};
    writes=0;fail_at=-1;read_fail=false;
}
static void mount_ok(bool writable) {assert(vfs_mount(&onfs_ops,&fs,writable)==VFS_OK);}
static void unmount_ok(void) {assert(vfs_unmount()==VFS_OK && enabled);}
static void outside_unchanged(void) {
    for(unsigned i=0;i<BASE*512;++i) assert(disk[i]==0xa5);
    for(unsigned i=(BASE+66)*512;i<sizeof(disk);++i) assert(disk[i]==0xa5);
}
static vfs_handle open_ok(const char *p,unsigned flags) {vfs_handle h=0;assert(vfs_open(p,flags,&h)==VFS_OK && h);return h;}
int main(void) {
    struct vfs_stat stat;struct vfs_volume info;size_t done=99;uint8_t data[4096];
    assert(vfs_create("/a")==VFS_UNMOUNTED);setup();mount_ok(true);
    assert(vfs_volume_info(&info)==VFS_OK && info.generation==2 && info.free_bytes==14336 && !info.read_only);
    const char *bad[]={"", "a", "/.", "/..", "/a/../b", "/a//b", "/a/", "/a b", "/a\\b", "/abcdefghijklmnopqrstuvwxyz123456"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(vfs_create(bad[i])==VFS_INVALID);
    char long_path[129];memset(long_path,'a',128);long_path[0]='/';long_path[128]=0;assert(vfs_create(long_path)==VFS_INVALID);
    assert(vfs_mkdir("/docs")==VFS_OK && vfs_mkdir("/docs/sub")==VFS_OK);
    assert(vfs_create("/docs/sub/readme")==VFS_OK && vfs_create("/docs/sub/readme")==VFS_EXISTS);
    assert(vfs_create("/missing/a")==VFS_NOT_FOUND);
    assert(vfs_create("/docs/sub/readme/a")==VFS_NOT_DIR);
    vfs_handle h=open_ok("/docs/sub/readme",VFS_READ|VFS_WRITE);
    assert(vfs_unmount()==VFS_BUSY && vfs_remove("/docs/sub/readme")==VFS_BUSY);
    for(unsigned i=0;i<sizeof(data);++i) data[i]=i*19;
    assert(vfs_write(h,0,data,600,&done)==VFS_OK && done==600);
    assert(vfs_write(h,512,"ON.FS",5,&done)==VFS_OK && done==5);
    memset(data,0,sizeof(data));assert(vfs_read(h,0,data,sizeof(data),&done)==VFS_OK && done==600 && data[511]==(uint8_t)(511*19) && !memcmp(data+512,"ON.FS",5));
    done=99;assert(vfs_write(h,UINT64_MAX,data,1,&done)==VFS_RANGE && done==99);
    assert(vfs_write(h,601,data,1,&done)==VFS_RANGE);
    assert(vfs_write(h,0,data,4097,&done)==VFS_INVALID);
    assert(vfs_read(h,601,data,1,&done)==VFS_RANGE);
    assert(vfs_read(h,600,0,0,&done)==VFS_OK && !done);
    assert(vfs_write(h,0,0,0,&done)==VFS_OK && !done);
    assert(vfs_close(h)==VFS_OK && vfs_close(h)==VFS_BAD_HANDLE);
    struct vfs_dirent entry;assert(vfs_readdir("/docs/sub",0,&entry)==VFS_OK && !strcmp(entry.name,"readme") && entry.stat.size==600);
    assert(vfs_readdir("/docs/sub",1,&entry)==VFS_NOT_FOUND && vfs_remove("/docs")==VFS_NOT_EMPTY);
    unmount_ok();mount_ok(true);
    assert(vfs_read(h,0,data,1,&done)==VFS_BAD_HANDLE);
    h=open_ok("/docs/sub/readme",VFS_READ);assert(vfs_write(h,0,data,1,&done)==VFS_BAD_HANDLE);
    assert(vfs_read(h,512,data,5,&done)==VFS_OK && !memcmp(data,"ON.FS",5));assert(vfs_close(h)==VFS_OK);
    assert(vfs_remove("/docs/sub/readme")==VFS_OK && vfs_remove("/docs/sub")==VFS_OK && vfs_remove("/docs")==VFS_OK);
    assert(vfs_remove("/")==VFS_INVALID && vfs_stat_path("/docs",&stat)==VFS_NOT_FOUND);
    /* Whole-volume data exhaustion, extent compaction and reuse. */
    memset(data,0x37,sizeof(data));
    for(unsigned n=0;n<4;++n) {char path[]="/f0";path[2]+=n;assert(vfs_create(path)==VFS_OK);vfs_handle file=open_ok(path,VFS_READ|VFS_WRITE);
        assert(vfs_write(file,0,data,n==3?2048:4096,&done)==VFS_OK);assert(vfs_close(file)==VFS_OK);}
    assert(vfs_volume_info(&info)==VFS_OK && info.free_bytes==0);
    h=open_ok("/f3",VFS_WRITE);unsigned before=writes;assert(vfs_write(h,2048,data,1,&done)==VFS_FULL && writes==before);assert(vfs_close(h)==VFS_OK);
    assert(vfs_remove("/f1")==VFS_OK);h=open_ok("/f3",VFS_WRITE);assert(vfs_write(h,2048,data,2048,&done)==VFS_OK);assert(vfs_close(h)==VFS_OK);
    h=open_ok("/f2",VFS_READ);assert(vfs_read(h,0,data,sizeof(data),&done)==VFS_OK && data[4095]==0x37);assert(vfs_close(h)==VFS_OK);
    outside_unchanged();unmount_ok();
    setup();mount_ok(true);
    for(unsigned n=0;n<15;++n) {char path[]="/n00";path[2]='0'+n/10;path[3]='0'+n%10;assert(vfs_create(path)==VFS_OK);}
    assert(vfs_create("/full")==VFS_FULL);
    vfs_handle handles[VFS_HANDLES];for(unsigned n=0;n<VFS_HANDLES;++n) handles[n]=open_ok("/n00",VFS_READ);
    assert(vfs_open("/n00",VFS_READ,&h)==VFS_FULL);for(unsigned n=0;n<VFS_HANDLES;++n) assert(vfs_close(handles[n])==VFS_OK);
    unmount_ok();mount_ok(false);assert(vfs_create("/read-only")==VFS_READ_ONLY);unmount_ok();outside_unchanged();
    /* Every commit write: no-write failure, lost acknowledgement and torn prefix. */
    for(int phase=0;phase<6;++phase) for(int kind=0;kind<3;++kind) {
        setup();mount_ok(true);assert(vfs_create("/keep")==VFS_OK);h=open_ok("/keep",VFS_READ|VFS_WRITE);
        assert(vfs_write(h,0,"old",3,&done)==VFS_OK);memcpy(saved,disk,sizeof(disk));writes=0;fail_at=phase;fail_kind=kind;done=99;
        assert(vfs_write(h,0,"new",3,&done)==VFS_IO && done==99 && vfs_read(h,0,data,3,&done)==VFS_IO);
        assert(vfs_close(h)==VFS_OK);unmount_ok();fail_at=-1;mount_ok(true);
        h=open_ok("/keep",VFS_READ);assert(vfs_read(h,0,data,3,&done)==VFS_OK && done==3);
        bool committed=phase==5 && kind==1;
        assert(!memcmp(data,committed?"new":"old",3));assert(vfs_close(h)==VFS_OK);
        assert(vfs_volume_info(&info)==VFS_OK);if(phase>0 && !committed) assert(info.degraded && info.read_only);
        unmount_ok();outside_unchanged();
    }
    /* Corruption with a correct checksum must still fail semantic validation. */
    for(unsigned mutation=0;mutation<13;++mutation) {
        setup();
        for(unsigned bank=0;bank<2;++bank) {
            uint8_t *header=disk+(BASE+bank*33)*512,*p=header+512,*e=p+576;
            e[0]=2;fs_put64(e+24,1);memcpy(e+32,"dir",4);
            switch(mutation) {
            case 0: fs_put32(e+8,UINT32_MAX);break;
            case 1: fs_put32(e+8,1);break;
            case 2: e[0]=3;break;
            case 3: e[32]='/';break;
            case 4: e[0]=1;fs_put32(e+12,UINT32_MAX);break;
            case 5: e[0]=1;fs_put32(e+12,1);fs_put32(e+16,UINT32_MAX);fs_put32(e+20,1);break;
            case 6: p[512]=1;break;
            case 7: p[2048]=7;break;
            case 8: e[1]=1;break;
            case 9: fs_put64(e+24,UINT64_MAX);break;
            case 10: memcpy(p+640,e,64);break;
            case 11: e[0]=1;fs_put32(e+12,1);fs_put32(e+20,1);p[2049]=1;break;
            case 12: memcpy(p+640,e,64);memcpy(p+672,"parent",7);fs_put32(e+8,2);fs_put32(p+648,UINT32_MAX);break;
            }
            fs_header(header,bank,BASE,bank+1,p);
        }
        assert(vfs_mount(&onfs_ops,&fs,true)==VFS_CORRUPT && !writes);outside_unchanged();
    }
    setup();disk[BASE*512+512+2048]^=1;mount_ok(true);assert(vfs_volume_info(&info)==VFS_OK && info.degraded && info.read_only);unmount_ok();
    setup();read_fail=true;assert(vfs_mount(&onfs_ops,&fs,true)==VFS_IO && !writes);read_fail=false;
    fs.device.base=UINT64_MAX;assert(vfs_mount(&onfs_ops,&fs,true)==VFS_INVALID && !writes);
    /* Bounded deterministic random metadata/header bytes, with no writes at mount. */
    uint32_t random=0x726f6f74;
    for(unsigned n=0;n<1000;++n) {
        setup();random=random*1664525u+1013904223u;
        size_t offset=BASE*512+(random%(66*512));disk[offset]^=(uint8_t)((random>>24)|1);
        enum vfs_result result=vfs_mount(&onfs_ops,&fs,true);assert(result==VFS_OK || result==VFS_CORRUPT);
        if(result==VFS_OK) unmount_ok();assert(!writes);
    }
    puts("ONFS/VFS paths, directories, handles, offsets, read/write/delete, persistence, disk/node/handle limits, corruption, 18 interrupted/torn commits, 1000 metadata mutations and volume boundaries passed.");
}
