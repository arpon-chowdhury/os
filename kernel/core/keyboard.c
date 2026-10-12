#include <stddef.h>
#include "arch.h"
#include "keyboard.h"

static struct keyboard_event events[KEYBOARD_BUFFER_CAPACITY];
static unsigned int head, tail, count;
static uint64_t dropped;
static bool left_shift, right_shift, extended;
static unsigned int pause_remaining;
static const char plain[128] = {
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
    [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',
    [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
    [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
    [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[55]='*',[57]=' '
};
static const char shifted[128] = {
    [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
    [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',
    [51]='<',[52]='>',[53]='?'
};
void keyboard_reset(void) {
    uint64_t flags = arch_interrupt_save();
    head = tail = count = 0;
    dropped = 0;
    left_shift = right_shift = extended = false;
    pause_remaining = 0;
    arch_interrupt_restore(flags);
}
void keyboard_feed_scancode(uint8_t byte) {
    uint64_t flags = arch_interrupt_save();
    if (pause_remaining) { --pause_remaining; goto done; }
    if (byte == 0xe1) { pause_remaining = 5; extended = false; goto done; }
    if (byte == 0xe0) { extended = true; goto done; }
    /* ACK/RESEND and error bytes are not keys. 0xAA is Shift release. */
    if (byte == 0xfa || byte == 0xfe || byte == 0xff || byte == 0) {
        extended = false;
        goto done;
    }
    bool ext = extended;
    extended = false;
    unsigned int code = byte & 0x7f;
    bool pressed = !(byte & 0x80);
    /* Print Screen uses fake extended Shift bytes; never change modifiers. */
    if (ext && (code == 0x2a || code == 0x36)) goto done;
    if (!ext && code == 0x2a) left_shift = pressed;
    if (!ext && code == 0x36) right_shift = pressed;
    bool shift = left_shift || right_shift;
    char character = 0;
    if (pressed) {
        if (ext) {
            if (code == 0x1c) character = '\n';
            if (code == 0x35) character = '/';
        } else {
            character = plain[code];
            if (shift && character >= 'a' && character <= 'z') character -= 'a' - 'A';
            else if (shift && shifted[code]) character = shifted[code];
        }
    }
    /* Drop newest on overflow; decode modifiers even when the queue is full. */
    if (count == KEYBOARD_BUFFER_CAPACITY) { ++dropped; goto done; }
    events[head] = (struct keyboard_event){code | (ext ? 0x100 : 0), character, pressed, shift};
    head = (head + 1) % KEYBOARD_BUFFER_CAPACITY;
    ++count;
done:
    arch_interrupt_restore(flags);
}
bool keyboard_poll(struct keyboard_event *event) {
    if (!event) return false;
    uint64_t flags = arch_interrupt_save();
    bool available = count != 0;
    if (available) {
        *event = events[tail];
        tail = (tail + 1) % KEYBOARD_BUFFER_CAPACITY;
        --count;
    }
    arch_interrupt_restore(flags);
    return available;
}
uint64_t keyboard_dropped_events(void) {
    uint64_t flags = arch_interrupt_save();
    uint64_t result = dropped;
    arch_interrupt_restore(flags);
    return result;
}
