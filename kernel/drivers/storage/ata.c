#include "ata.h"
#define BSY 0x80
#define DRQ 0x08
#define ERR 0x01
#define DF 0x20
static uint8_t read_status(struct ata_bus *b) {return b->read8(b->context,b->control);}
static void delay(struct ata_bus *b) {for(unsigned i=0;i<4;++i) (void)read_status(b);}
static enum block_result wait_status(struct ata_bus *b,bool data,bool errors) {
    uint64_t start=b->ticks(b->context);
    for(unsigned n=0;n<ATA_POLL_LIMIT;++n) {
        uint8_t status=read_status(b);
        if(!status || status==255) {b->failed=true;return BLOCK_IO;}
        if(!(status&BSY)) {
            if(errors && (status&(ERR|DF))) {b->failed=true;return BLOCK_IO;}
            if((status&DRQ)==(data?DRQ:0)) return BLOCK_OK;
        }
        if(b->ticks(b->context)-start>=ATA_TIMEOUT_TICKS) break;
    }
    b->failed=true;return BLOCK_TIMEOUT;
}
static bool valid_bus(struct ata_bus *b) {
    return b && b->read8 && b->read16 && b->write8 && b->write16 && b->ticks && b->command && b->control;
}
static void write_reg(struct ata_bus *b,unsigned reg,uint8_t value) {b->write8(b->context,b->command+reg,value);}
static enum block_result select_drive(struct ata_bus *b,unsigned drive,uint64_t lba) {
    enum block_result result=BLOCK_OK;
    uint8_t status=read_status(b);
    if(status && status!=255 && (status&BSY)) {result=wait_status(b,false,false);if(result!=BLOCK_OK) return result;}
    write_reg(b,6,0xe0|(drive<<4)|((lba>>24)&15));delay(b);
    return wait_status(b,false,false);
}
bool ata_identify(struct ata_bus *b,unsigned drive,struct block_info *out) {
    if(!valid_bus(b) || b->failed || drive>=2 || !out) return false;
    *out=(struct block_info){0};
    b->write8(b->context,b->control,2); /* nIEN: polling only; do not unmask IDE IRQs. */
    uint8_t status=read_status(b);
    /* A missing selected device can report zero; select before the first wait. */
    if(status && status!=255 && (status&BSY) && wait_status(b,false,false)!=BLOCK_OK) return false;
    write_reg(b,6,0xa0|(drive<<4));delay(b);
    status=read_status(b);if(!status || status==255) return false;
    if(wait_status(b,false,false)!=BLOCK_OK) return false;
    for(unsigned reg=2;reg<=5;++reg) write_reg(b,reg,0);
    write_reg(b,7,0xec);delay(b);
    status=read_status(b);if(!status || status==255) return false;
    /* ATAPI signatures are unsupported and not errors for the channel. */
    uint64_t start=b->ticks(b->context);
    for(unsigned n=0;n<ATA_POLL_LIMIT && (status&BSY);++n) {
        if(b->ticks(b->context)-start>=ATA_TIMEOUT_TICKS) break;
        status=read_status(b);
    }
    if(status&BSY) {b->failed=true;return false;}
    /* IDENTIFY aborts for an absent master or unsupported device. Do not
       poison the channel and hide a valid slave behind that expected ERR. */
    if((status&ERR) && !(status&DRQ)) return false;
    if(b->read8(b->context,b->command+4) || b->read8(b->context,b->command+5)) return false;
    if(wait_status(b,true,true)!=BLOCK_OK) return false;
    uint16_t words[256];for(unsigned i=0;i<256;++i) words[i]=b->read16(b->context,b->command);
    delay(b);if(wait_status(b,false,true)!=BLOCK_OK) return false;
    if((words[0]&0x8000) || !(words[49]&0x200)) return false;
    if((words[106]&0xc000)==0x4000 && (words[106]&0x1000) &&
       ((uint32_t)words[117]|((uint32_t)words[118]<<16))!=256) return false;
    uint64_t sectors=(uint32_t)words[60]|((uint32_t)words[61]<<16);
    if(!sectors || sectors>0x10000000ull) return false;
    *out=(struct block_info){.sectors=sectors,.present=true,
        .flush_supported=(words[83]&0xd000)==0x5000};
    return true;
}
enum block_result ata_transfer(void *context,unsigned drive,uint64_t lba,unsigned count,void *buffer,bool write) {
    struct ata_bus *b=context;
    if(!valid_bus(b) || drive>=2 || !buffer || !count || count>BLOCK_MAX_SECTORS ||
       lba>=0x10000000ull || count>0x10000000ull-lba) return BLOCK_INVALID;
    if(b->failed) return BLOCK_IO;
    enum block_result result=select_drive(b,drive,lba);if(result!=BLOCK_OK) return result;
    write_reg(b,2,count);write_reg(b,3,lba);write_reg(b,4,lba>>8);write_reg(b,5,lba>>16);
    write_reg(b,7,write?0x30:0x20);delay(b);
    uint8_t *bytes=buffer;
    for(unsigned sector=0;sector<count;++sector) {
        result=wait_status(b,true,true);if(result!=BLOCK_OK) return result;
        for(unsigned i=0;i<256;++i) {
            size_t offset=sector*512+i*2;
            if(write) b->write16(b->context,b->command,(uint16_t)bytes[offset]|((uint16_t)bytes[offset+1]<<8));
            else {uint16_t value=b->read16(b->context,b->command);bytes[offset]=value;bytes[offset+1]=value>>8;}
        }
        delay(b);
    }
    return wait_status(b,false,true);
}
enum block_result ata_flush(void *context,unsigned drive) {
    struct ata_bus *b=context;if(!valid_bus(b) || drive>=2) return BLOCK_INVALID;
    if(b->failed) return BLOCK_IO;
    enum block_result result=select_drive(b,drive,0);if(result!=BLOCK_OK) return result;
    write_reg(b,7,0xe7);delay(b);return wait_status(b,false,true);
}
