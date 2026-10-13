#include "vfs.h"
#include "arch.h"
static const struct vfs_ops *operations;
static void *context;
static bool busy;
static bool lock(void) {
    uint64_t flags=arch_interrupt_save();bool ok=!busy;if(ok) busy=true;
    arch_interrupt_restore(flags);return ok;
}
static void unlock(void) {uint64_t flags=arch_interrupt_save();busy=false;arch_interrupt_restore(flags);}
enum vfs_result vfs_mount(const struct vfs_ops *ops,void *ctx,bool writable) {
    if(!ops || !ctx || !ops->mount || !ops->unmount || !ops->create || !ops->open || !ops->close ||
       !ops->read || !ops->write || !ops->remove || !ops->stat || !ops->readdir || !ops->volume) return VFS_INVALID;
    if(!lock()) return VFS_BUSY;
    enum vfs_result result=operations?VFS_BUSY:ops->mount(ctx,writable);
    if(result==VFS_OK) {operations=ops;context=ctx;}unlock();return result;
}
enum vfs_result vfs_unmount(void) {
    if(!lock()) return VFS_BUSY;
    enum vfs_result result=operations?operations->unmount(context):VFS_UNMOUNTED;
    if(result==VFS_OK) {operations=0;context=0;}unlock();return result;
}
#define RUN(expression) do {if(!lock()) return VFS_BUSY;enum vfs_result r=operations?(expression):VFS_UNMOUNTED;unlock();return r;} while(0)
enum vfs_result vfs_create(const char *p) {RUN(operations->create(context,p,false));}
enum vfs_result vfs_mkdir(const char *p) {RUN(operations->create(context,p,true));}
enum vfs_result vfs_open(const char *p,unsigned f,vfs_handle *h) {RUN(operations->open(context,p,f,h));}
enum vfs_result vfs_close(vfs_handle h) {RUN(operations->close(context,h));}
enum vfs_result vfs_read(vfs_handle h,uint64_t o,void *b,size_t n,size_t *done) {RUN(operations->read(context,h,o,b,n,done));}
enum vfs_result vfs_write(vfs_handle h,uint64_t o,const void *b,size_t n,size_t *done) {RUN(operations->write(context,h,o,b,n,done));}
enum vfs_result vfs_remove(const char *p) {RUN(operations->remove(context,p));}
enum vfs_result vfs_stat_path(const char *p,struct vfs_stat *s) {RUN(operations->stat(context,p,s));}
enum vfs_result vfs_readdir(const char *p,unsigned n,struct vfs_dirent *e) {RUN(operations->readdir(context,p,n,e));}
enum vfs_result vfs_volume_info(struct vfs_volume *v) {RUN(operations->volume(context,v));}
