#ifndef ON_BLOCK_H
#define ON_BLOCK_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_MAX_SECTORS 8u
#define BLOCK_MAX_DEVICES 2u
enum block_result {BLOCK_OK, BLOCK_INVALID, BLOCK_ABSENT, BLOCK_BUSY, BLOCK_READ_ONLY, BLOCK_IO, BLOCK_TIMEOUT};
struct block_info {uint64_t sectors;bool present,flush_supported,writable;};
/* Trusted kernel buffers only; no physical/user address accepted as authority. */
struct block_backend {
    void *context;
    enum block_result (*transfer)(void *,unsigned,uint64_t,unsigned,void *,bool);
    enum block_result (*flush)(void *,unsigned);
};
bool block_init(const struct block_backend *,const struct block_info devices[BLOCK_MAX_DEVICES]);
bool block_get_info(unsigned,struct block_info *);
enum block_result block_set_writable(unsigned,bool);
enum block_result block_read(unsigned,uint64_t,unsigned,void *,size_t);
enum block_result block_write(unsigned,uint64_t,unsigned,const void *,size_t);
enum block_result block_flush(unsigned);
void storage_boot_start(void);
void storage_boot_test(void);
#endif
