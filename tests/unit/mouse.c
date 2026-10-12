#include <assert.h>
#include <stdio.h>
#include "arch.h"
#include "mouse.h"
static uint64_t state=0x200;
uint64_t arch_interrupt_save(void) { uint64_t old=state; state=0; return old; }
void arch_interrupt_restore(uint64_t flags) { state=flags; }
static void packet(uint8_t a,uint8_t b,uint8_t c) { mouse_feed_byte(a);mouse_feed_byte(b);mouse_feed_byte(c); }
static struct mouse_event next(void) { struct mouse_event e;assert(mouse_poll(&e));assert(state==0x200);return e; }
int main(void) {
    mouse_reset();struct mouse_event e;
    assert(!mouse_poll(NULL));mouse_feed_byte(0);assert(!mouse_poll(&e));
    packet(8,127,5);e=next();assert(e.dx==127 && e.dy==5 && !e.left && !e.right);
    packet(0x3b,0,255);e=next();assert(e.dx==-256 && e.dy==-1 && e.left && e.right);
    packet(0xcb,255,255);e=next();assert(e.dx==0 && e.dy==0 && e.left && e.right);
    packet(0x48,200,12);e=next();assert(e.dx==0 && e.dy==12);
    mouse_feed_byte(8);mouse_discard_packet();packet(8,1,2);assert(next().dy==2);
    for(unsigned int i=0;i<MOUSE_BUFFER_CAPACITY;++i) packet(8,i,0);
    packet(9,1,0);assert(mouse_dropped_events()==1);
    for(unsigned int i=0;i<MOUSE_BUFFER_CAPACITY;++i) assert(next().dx==(int)i);
    assert(!mouse_poll(&e));
    for(unsigned int i=0;i<1000;++i) { packet(8,1,0);assert(next().dx==1); }
    state=0;packet(8,1,0);assert(mouse_poll(&e) && state==0);
    puts("Mouse sign extension, sync, overflow, buttons, queue bounds, wraparound, and IF preservation passed.");
}
