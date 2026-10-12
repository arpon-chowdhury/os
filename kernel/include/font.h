#ifndef ON_FONT_H
#define ON_FONT_H
#include <stdint.h>
/* Original ON. 5x7 ASCII font. Unsupported bytes render as '?'. */
void font_glyph(unsigned char ch, uint8_t rows[7]);
#endif
