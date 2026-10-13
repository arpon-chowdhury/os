#ifndef ON_FS_H
#define ON_FS_H
#include "vfs.h"
#define ONFS_NODES 16u
#define ONFS_BANK_SECTORS 33u
#define ONFS_VOLUME_SECTORS 66u
#define ONFS_PAYLOAD_SECTORS 32u
#define ONFS_BYTES 16384u
#define ONFS_METADATA_BYTES 2048u
#define ONFS_DATA_BYTES (ONFS_BYTES-ONFS_METADATA_BYTES)
struct onfs_device {
    void *context;uint64_t sectors,base;
    /* Reads publish only complete data; successful writes include flush.
       Exactly one exclusively owned, bounded volume; kernel buffers only. */
    bool (*read)(void *,uint64_t,unsigned,void *);
    bool (*write)(void *,uint64_t,unsigned,const void *);
};
struct onfs_open {vfs_handle token;unsigned node,flags;};
struct onfs {
    struct onfs_device device;
    uint64_t generation;
    unsigned bank;
    bool mounted,read_only,degraded,poisoned;
    struct onfs_open handles[VFS_HANDLES];
    uint8_t current[ONFS_BYTES],work[ONFS_BYTES];
};
extern const struct vfs_ops onfs_ops;
uint32_t onfs_crc32(const void *,size_t);
#endif
