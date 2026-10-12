#ifndef ON_KEYBOARD_H
#define ON_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>
#define KEYBOARD_BUFFER_CAPACITY 64u
struct keyboard_event {
    uint16_t scancode; /* Set 1 code; bit 8 marks E0 extended keys. */
    char character;    /* US layout; zero for unsupported keys. */
    bool pressed;
    bool shift;
};
/* Single boot CPU only. Queue operations preserve the caller's IF state. */
void keyboard_reset(void);
void keyboard_feed_scancode(uint8_t byte);
bool keyboard_poll(struct keyboard_event *event);
uint64_t keyboard_dropped_events(void);
#endif
