#include "crc.h"
static u32 table[256],ready;
u32 crc32_update(u32 crc,const void *data,u32 bytes)
{
    if(!ready){
        for(u32 i=0;i<256;i++){
            u32 value=i;for(u32 bit=0;bit<8;bit++)value=(value>>1)^((0u-(value&1u))&0xEDB88320u);
            table[i]=value;
        }
        ready=1;
    }
    const u8 *p=(const u8 *)data;crc^=0xFFFFFFFFu;
    while(bytes--)crc=table[(crc^*p++)&255]^(crc>>8);
    return crc^0xFFFFFFFFu;
}
