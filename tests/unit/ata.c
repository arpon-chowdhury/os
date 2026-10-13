#include "ata.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct model {uint8_t regs[8],status;uint16_t words[256];uint8_t disk[8192];unsigned position,remaining,reads,writes,commands,flushes;uint8_t command;bool absent,stuck,err,df,early,wrong_signature,ticking,slave_only;uint64_t tick;};
static uint8_t read8(void *context,uint16_t port) {
    struct model *m=context;++m->reads;
    if(port==0x3f6) {if(m->absent || (m->slave_only && !(m->regs[6]&16))) return 255;if(m->stuck) return 128;if(m->err) return 1;if(m->df) return 0x20;return m->status;}
    if(m->wrong_signature && port==0x1f4) return 0x14;
    return m->regs[port-0x1f0];
}
static uint16_t read16(void *context,uint16_t port) {
    struct model *m=context;assert(port==0x1f0 && (m->status&8));uint16_t value;
    if(m->command==0xec) value=m->words[m->position];
    else {unsigned offset=(m->regs[3]+m->position/256)*512+(m->position%256)*2;value=m->disk[offset]|((uint16_t)m->disk[offset+1]<<8);}
    ++m->position;if(m->position==m->remaining*256) m->status=0x40;return value;
}
static void write8(void *context,uint16_t port,uint8_t value) {
    struct model *m=context;if(port==0x3f6) {assert(value==2);return;}
    m->regs[port-0x1f0]=value;
    if(port==0x1f7) {
        ++m->commands;m->command=value;m->position=0;
        if(value==0xe7) {++m->flushes;m->status=0x40;return;}
        assert(value==0xec || value==0x20 || value==0x30);
        m->remaining=value==0xec?1:m->regs[2];m->status=m->early?0x40:0x48;
    }
}
static void write16(void *context,uint16_t port,uint16_t value) {
    struct model *m=context;assert(port==0x1f0 && m->command==0x30 && (m->status&8));
    unsigned offset=(m->regs[3]+m->position/256)*512+(m->position%256)*2;
    m->disk[offset]=value;m->disk[offset+1]=value>>8;++m->writes;
    if(++m->position==m->remaining*256) m->status=0x40;
}
static uint64_t ticks(void *context) {struct model *m=context;return m->ticking?m->tick++:m->tick;}
static struct ata_bus setup(struct model *m) {
    memset(m,0,sizeof(*m));m->status=0x40;m->words[49]=0x200;m->words[60]=16;m->words[83]=0x5000;
    return (struct ata_bus){m,read8,read16,write8,write16,ticks,0x1f0,0x3f6,false};
}
int main(void) {
    struct model m;struct ata_bus bus=setup(&m);struct block_info info;
    assert(ata_identify(&bus,0,&info) && info.present && info.sectors==16 && info.flush_supported);
    uint8_t bytes[4096];for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=i*13;
    assert(ata_transfer(&bus,0,4,8,bytes,true)==BLOCK_OK && m.writes==2048);
    assert(ata_flush(&bus,0)==BLOCK_OK && m.flushes==1);
    memset(bytes,0,sizeof(bytes));assert(ata_transfer(&bus,0,4,8,bytes,false)==BLOCK_OK);
    for(unsigned i=0;i<sizeof(bytes);++i) assert(bytes[i]==(uint8_t)(i*13));
    unsigned commands=m.commands;
    assert(ata_transfer(&bus,2,0,1,bytes,false)==BLOCK_INVALID);
    assert(ata_transfer(&bus,0,0x0fffffff,2,bytes,false)==BLOCK_INVALID && m.commands==commands);
    bus=setup(&m);m.absent=true;assert(!ata_identify(&bus,0,&info) && !m.commands);
    bus=setup(&m);m.slave_only=true;
    assert(!ata_identify(&bus,0,&info) && !bus.failed && ata_identify(&bus,1,&info) && info.present);
    assert(ata_transfer(&bus,1,0,1,bytes,false)==BLOCK_OK);
    bus=setup(&m);m.wrong_signature=true;assert(!ata_identify(&bus,0,&info) && !bus.failed);
    bus=setup(&m);m.err=true;assert(!ata_identify(&bus,0,&info) && !bus.failed);
    bus=setup(&m);m.words[49]=0;assert(!ata_identify(&bus,0,&info));
    bus=setup(&m);m.words[106]=0x5000;m.words[117]=2048;assert(!ata_identify(&bus,0,&info));
    bus=setup(&m);m.words[83]=0x1000;assert(ata_identify(&bus,0,&info) && !info.flush_supported);
    bus=setup(&m);m.stuck=true;assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_TIMEOUT && bus.failed && m.reads<=ATA_POLL_LIMIT+2);
    commands=m.commands;assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_IO && m.commands==commands);
    bus=setup(&m);m.stuck=true;m.ticking=true;m.tick=UINT64_MAX-4;
    assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_TIMEOUT && bus.failed && m.reads<ATA_POLL_LIMIT);
    bus=setup(&m);m.early=true;assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_TIMEOUT && bus.failed);
    bus=setup(&m);m.err=true;assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_IO && bus.failed);
    bus=setup(&m);m.df=true;assert(ata_transfer(&bus,0,0,1,bytes,false)==BLOCK_IO && bus.failed);
    puts("ATA PIO identify, capabilities, sector size, bounds, multi-sector protocol, flush, absent/ATAPI, missing DRQ, busy timeout, ERR/DF and quarantine passed.");
}
