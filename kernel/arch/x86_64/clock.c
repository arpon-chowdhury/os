#include "arch.h"
#include "io.h"
/* Read-only CMOS access. Bounded stable snapshots; no timezone inference. */
static uint8_t rtc(uint8_t reg) { outb(0x70,reg);return inb(0x71); }
static unsigned decode(uint8_t v,bool binary) {return binary?v:(v>>4)*10+(v&15);}
bool arch_clock_minute(unsigned int *minute) {
    if(!minute) return false;
    uint64_t flags=arch_interrupt_save();
    bool valid=false;
    for(unsigned i=0;i<4;++i) {
        if(rtc(0x0a)&0x80) continue;
        uint8_t m=rtc(2),h=rtc(4),mode=rtc(0x0b);
        if((rtc(0x0a)&0x80) || m!=rtc(2) || h!=rtc(4) || mode!=rtc(0x0b)) continue;
        bool binary=mode&4,pm=h&0x80;
        unsigned mm=decode(m,binary),hh=decode(h&0x7f,binary);
        if(!(mode&2)) {if(hh<1 || hh>12) continue;hh=hh%12+(pm?12:0);}
        if(mm>=60 || hh>=24) continue;
        *minute=hh*60+mm;valid=true;break;
    }
    arch_interrupt_restore(flags);return valid;
}
