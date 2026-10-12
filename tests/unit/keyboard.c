#include <assert.h>
#include <stdio.h>
#include "arch.h"
#include "keyboard.h"
static uint64_t state = 0x200;
uint64_t arch_interrupt_save(void) { uint64_t old = state; state = 0; return old; }
void arch_interrupt_restore(uint64_t flags) { state = flags; }
static struct keyboard_event next(void) {
    struct keyboard_event event;
    assert(keyboard_poll(&event)); assert(state == 0x200); return event;
}
int main(void) {
    keyboard_reset();
    assert(!keyboard_poll(NULL));
    keyboard_feed_scancode(0x1e); assert(next().character == 'a');
    keyboard_feed_scancode(0x9e); assert(!next().pressed);
    keyboard_feed_scancode(0x2a); assert(next().shift);
    keyboard_feed_scancode(0x36); assert(next().shift);
    keyboard_feed_scancode(0xaa); assert(next().shift);
    keyboard_feed_scancode(0x1e); assert(next().character == 'A');
    keyboard_feed_scancode(2); assert(next().character == '!');
    keyboard_feed_scancode(0xb6); assert(!next().shift);
    keyboard_feed_scancode(0x1c); assert(next().character == '\n');
    keyboard_feed_scancode(0x0e); assert(next().character == '\b');
    keyboard_feed_scancode(0xe0); keyboard_feed_scancode(0x1c);
    struct keyboard_event event = next(); assert(event.character == '\n' && event.scancode == 0x11c);
    keyboard_feed_scancode(0xe0); keyboard_feed_scancode(0x2a);
    keyboard_feed_scancode(0x1e); assert(next().character == 'a');
    unsigned char pause[] = {0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
    for (unsigned int i=0;i<sizeof(pause);++i) keyboard_feed_scancode(pause[i]);
    assert(!keyboard_poll(&event));
    keyboard_feed_scancode(0xfa); keyboard_feed_scancode(0xfe);
    assert(!keyboard_poll(&event));
    for (unsigned int i=0;i<KEYBOARD_BUFFER_CAPACITY;++i) keyboard_feed_scancode(0x1e);
    keyboard_feed_scancode(0x2a); keyboard_feed_scancode(0xaa);
    assert(keyboard_dropped_events() == 2);
    for (unsigned int i=0;i<KEYBOARD_BUFFER_CAPACITY;++i) assert(next().character == 'a');
    assert(!keyboard_poll(&event));
    for (unsigned int i=0;i<1000;++i) { keyboard_feed_scancode(0x1e); assert(next().character == 'a'); }
    state = 0; keyboard_feed_scancode(0x1e); assert(state == 0); assert(keyboard_poll(&event)); assert(state == 0);
    puts("Keyboard decoding, modifiers, overflow, wraparound, and IF preservation passed.");
}
