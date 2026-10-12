#ifndef ON_MOUSE_H
#define ON_MOUSE_H
#include <stdbool.h>
#include <stdint.h>
#define MOUSE_BUFFER_CAPACITY 64u
struct mouse_event { int16_t dx, dy; bool left, right; };
/* Single boot CPU, fixed FIFO, drop newest on overflow; IF preserved. */
void mouse_reset(void);
void mouse_discard_packet(void);
void mouse_feed_byte(uint8_t byte);
bool mouse_poll(struct mouse_event *event);
uint64_t mouse_dropped_events(void);
#endif
