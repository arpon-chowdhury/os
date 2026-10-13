#include "ata.h"
#include "arch.h"
static struct ata_bus bus;
void storage_boot_start(void) {
    struct block_info info[BLOCK_MAX_DEVICES]={{0}};
    if(arch_ata_prepare(&bus)) for(unsigned i=0;i<BLOCK_MAX_DEVICES;++i) (void)ata_identify(&bus,i,&info[i]);
    if(bus.failed) for(unsigned i=0;i<BLOCK_MAX_DEVICES;++i) info[i].present=false;
    const struct block_backend backend={&bus,ata_transfer,ata_flush};
    if(!block_init(&backend,info)) arch_halt();
    arch_debug_puts(info[0].present || info[1].present?
        "ON.: bounded ATA storage ready; writes disabled.\n":"ON.: supported ATA storage absent; desktop available.\n");
}
