#ifndef ON_TEST_ELF_FIXTURE_H
#define ON_TEST_ELF_FIXTURE_H
#include <stdint.h>
#include <string.h>
#define FIXTURE_SIZE 8192u
static void fixture_put(uint8_t *p,uint64_t value,unsigned bytes) {for(unsigned i=0;i<bytes;++i) p[i]=value>>(8*i);}
static void elf_fixture(uint8_t *b) {
    memset(b,0,FIXTURE_SIZE);
    memcpy(b,"\177ELF\2\1\1",7);
    fixture_put(b+16,2,2);fixture_put(b+18,62,2);fixture_put(b+20,1,4);
    fixture_put(b+24,0x400000,8);fixture_put(b+32,64,8);
    fixture_put(b+52,64,2);fixture_put(b+54,56,2);fixture_put(b+56,2,2);
    fixture_put(b+64,1,4);fixture_put(b+68,5,4);fixture_put(b+72,4096,8);
    fixture_put(b+80,0x400000,8);fixture_put(b+96,4,8);fixture_put(b+104,4096,8);fixture_put(b+112,4096,8);
    b[4096]=0x31;b[4097]=0xc0;b[4098]=0xcd;b[4099]=0x80;
    fixture_put(b+120,1,4);fixture_put(b+124,6,4);fixture_put(b+128,8191,8);
    fixture_put(b+136,0x600fff,8);fixture_put(b+152,1,8);fixture_put(b+160,4098,8);fixture_put(b+168,4096,8);
    b[8191]=42;
}
#endif
