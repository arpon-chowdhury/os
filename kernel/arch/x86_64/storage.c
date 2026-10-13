#include "ata.h"
#include "arch.h"
#include "io.h"
static uint8_t read8(void *context,uint16_t port) {(void)context;return inb(port);}
static uint16_t read16(void *context,uint16_t port) {
    (void)context;uint16_t value;__asm__ volatile("inw %1,%0":"=a"(value):"Nd"(port));return value;
}
static void write8(void *context,uint16_t port,uint8_t value) {(void)context;outb(port,value);}
static void write16(void *context,uint16_t port,uint16_t value) {
    (void)context;__asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));
}
static uint64_t ticks(void *context) {(void)context;return arch_system_ticks();}
static void pci_address(uint32_t value) {__asm__ volatile("outl %0,%1"::"a"(value),"Nd"((uint16_t)0xcf8));}
static uint32_t pci_read(uint32_t address) {
    uint64_t flags=arch_interrupt_save();pci_address(address);uint32_t value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"((uint16_t)0xcfc));
    arch_interrupt_restore(flags);return value;
}
bool arch_ata_prepare(struct ata_bus *bus) {
    if(!bus) return false;
    /* Deliberately narrow: known PIIX3/4 compatibility IDE on root PCI bus.
       q35's AHCI and arbitrary BARs are not probed as legacy ATA ports. */
    for(unsigned slot=0;slot<32;++slot) for(unsigned function=0;function<8;++function) {
        uint32_t address=0x80000000u|(slot<<11)|(function<<8),id=pci_read(address);
        if(id!=0x70108086u && id!=0x71118086u) continue;
        uint32_t cls=pci_read(address+8);
        if((cls>>16)!=0x0101 || ((cls>>8)&5)) continue;
        uint16_t command=pci_read(address+4);
        uint64_t flags=arch_interrupt_save();pci_address(address+4);
        write16(0,0xcfc,(command|1)&~4u); /* I/O on, DMA bus mastering off; no status W1C. */
        arch_interrupt_restore(flags);
        *bus=(struct ata_bus){.read8=read8,.read16=read16,.write8=write8,.write16=write16,
            .ticks=ticks,.command=0x1f0,.control=0x3f6};return true;
    }
    return false;
}
