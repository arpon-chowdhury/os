#ifndef ON_VFS_H
#define ON_VFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define VFS_PATH_MAX 128u
#define VFS_NAME_MAX 31u
#define VFS_FILE_MAX 4096u
#define VFS_HANDLES 8u
#define VFS_READ 1u
#define VFS_WRITE 2u
typedef uint64_t vfs_handle;
enum vfs_result {VFS_OK,VFS_INVALID,VFS_NOT_FOUND,VFS_EXISTS,VFS_NOT_DIR,VFS_IS_DIR,VFS_NOT_EMPTY,VFS_RANGE,VFS_FULL,VFS_READ_ONLY,VFS_BUSY,VFS_BAD_HANDLE,VFS_IO,VFS_CORRUPT,VFS_UNMOUNTED};
struct vfs_stat {unsigned type;size_t size;};
struct vfs_dirent {char name[VFS_NAME_MAX+1];struct vfs_stat stat;};
struct vfs_volume {uint64_t generation;size_t used_bytes,free_bytes;unsigned nodes;bool read_only,degraded;};
/* Kernel-owned buffers/path strings only. Backends are trusted kernel objects. */
struct vfs_ops {
    enum vfs_result (*mount)(void *,bool);
    enum vfs_result (*unmount)(void *);
    enum vfs_result (*create)(void *,const char *,bool);
    enum vfs_result (*open)(void *,const char *,unsigned,vfs_handle *);
    enum vfs_result (*close)(void *,vfs_handle);
    enum vfs_result (*read)(void *,vfs_handle,uint64_t,void *,size_t,size_t *);
    enum vfs_result (*write)(void *,vfs_handle,uint64_t,const void *,size_t,size_t *);
    enum vfs_result (*remove)(void *,const char *);
    enum vfs_result (*stat)(void *,const char *,struct vfs_stat *);
    enum vfs_result (*readdir)(void *,const char *,unsigned,struct vfs_dirent *);
    enum vfs_result (*volume)(void *,struct vfs_volume *);
};
enum vfs_result vfs_mount(const struct vfs_ops *,void *,bool);
enum vfs_result vfs_unmount(void);
enum vfs_result vfs_create(const char *);
enum vfs_result vfs_mkdir(const char *);
enum vfs_result vfs_open(const char *,unsigned,vfs_handle *);
enum vfs_result vfs_close(vfs_handle);
enum vfs_result vfs_read(vfs_handle,uint64_t,void *,size_t,size_t *);
enum vfs_result vfs_write(vfs_handle,uint64_t,const void *,size_t,size_t *);
enum vfs_result vfs_remove(const char *);
enum vfs_result vfs_stat_path(const char *,struct vfs_stat *);
enum vfs_result vfs_readdir(const char *,unsigned,struct vfs_dirent *);
enum vfs_result vfs_volume_info(struct vfs_volume *);
void filesystem_boot_test(void);
#endif
