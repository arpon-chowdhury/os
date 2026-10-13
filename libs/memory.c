/* Freestanding compiler runtime. Volatile stores prevent recursive lowering
   of these implementations back into compiler-generated memory calls. */
#include <stddef.h>
void *memcpy(void *destination,const void *source,size_t length) {
    volatile unsigned char *out=destination;const unsigned char *in=source;
    for(size_t i=0;i<length;++i) out[i]=in[i];
    return destination;
}
void *memset(void *destination,int value,size_t length) {
    volatile unsigned char *out=destination;
    for(size_t i=0;i<length;++i) out[i]=(unsigned char)value;
    return destination;
}
