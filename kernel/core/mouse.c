#include "arch.h"
#include "mouse.h"
static uint8_t packet[3];
static unsigned int used, head, tail, count;
static uint64_t dropped;
static struct mouse_event events[MOUSE_BUFFER_CAPACITY];
void mouse_reset(void) {
    uint64_t flags = arch_interrupt_save();
    used = head = tail = count = 0;
    dropped = 0;
    arch_interrupt_restore(flags);
}
void mouse_discard_packet(void) {
    uint64_t flags = arch_interrupt_save();
    used = 0;
    arch_interrupt_restore(flags);
}
void mouse_feed_byte(uint8_t byte) {
    uint64_t flags = arch_interrupt_save();
    if (!used && !(byte & 8)) goto done;
    packet[used++] = byte;
    if (used != 3) goto done;
    used = 0;
    /* Overflow invalidates motion, but button changes still matter. */
    struct mouse_event event = {
        .dx = (packet[0] & 0x40) ? 0 : (int16_t)packet[1] - ((packet[0] & 0x10) ? 256 : 0),
        .dy = (packet[0] & 0x80) ? 0 : (int16_t)packet[2] - ((packet[0] & 0x20) ? 256 : 0),
        .left = (packet[0] & 1) != 0, .right = (packet[0] & 2) != 0
    };
    if (count == MOUSE_BUFFER_CAPACITY) { ++dropped; goto done; }
    events[head] = event;
    head = (head + 1) % MOUSE_BUFFER_CAPACITY;
    ++count;
done:
    arch_interrupt_restore(flags);
}
bool mouse_poll(struct mouse_event *event) {
    if (!event) return false;
    uint64_t flags = arch_interrupt_save();
    bool available = count != 0;
    if (available) {
        *event = events[tail];
        tail = (tail + 1) % MOUSE_BUFFER_CAPACITY;
        --count;
    }
    arch_interrupt_restore(flags);
    return available;
}
uint64_t mouse_dropped_events(void) {
    uint64_t flags = arch_interrupt_save();
    uint64_t result = dropped;
    arch_interrupt_restore(flags);
    return result;
}
