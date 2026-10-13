#ifndef ON_ATA_H
#define ON_ATA_H
#include "block.h"
#define ATA_POLL_LIMIT 100000u
#define ATA_TIMEOUT_TICKS 200u
struct ata_bus {
    void *context;
    uint8_t (*read8)(void *,uint16_t);
    uint16_t (*read16)(void *,uint16_t);
    void (*write8)(void *,uint16_t,uint8_t);
    void (*write16)(void *,uint16_t,uint16_t);
    uint64_t (*ticks)(void *);
    uint16_t command,control;
    bool failed;
};
bool ata_identify(struct ata_bus *,unsigned,struct block_info *);
enum block_result ata_transfer(void *,unsigned,uint64_t,unsigned,void *,bool);
enum block_result ata_flush(void *,unsigned);
bool arch_ata_prepare(struct ata_bus *);
#endif
