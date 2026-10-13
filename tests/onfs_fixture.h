#ifndef ON_TEST_ONFS_FIXTURE_H
#define ON_TEST_ONFS_FIXTURE_H
#include "onfs.h"
#include <string.h>
static void fs_put32(uint8_t *b,uint32_t n) {for(unsigned i=0;i<4;++i) b[i]=n>>(8*i);}
static void fs_put64(uint8_t *b,uint64_t n) {fs_put32(b,n);fs_put32(b+4,n>>32);}
static void fs_header(uint8_t *h,unsigned bank,uint64_t base,uint64_t generation,const uint8_t *payload) {
    memset(h,0,512);memcpy(h,"ONFSCOW1",8);fs_put32(h+8,1);fs_put32(h+12,bank);fs_put64(h+16,base);
    fs_put32(h+24,66);fs_put32(h+28,32);fs_put64(h+32,generation);
    fs_put32(h+40,onfs_crc32(payload,ONFS_BYTES));fs_put32(h+508,onfs_crc32(h,508));
}
static void fs_fixture(uint8_t *disk,uint64_t base) {
    for(unsigned bank=0;bank<2;++bank) {
        uint8_t *h=disk+(base+bank*33)*512,*p=h+512;memset(p,0,ONFS_BYTES);
        memcpy(p,"ONNODES1",8);fs_put32(p+8,1);fs_put32(p+12,16);p[512]=2;fs_put64(p+512+24,1);
        fs_header(h,bank,base,bank+1,p);
    }
}
#endif
