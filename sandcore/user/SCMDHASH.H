#ifndef SCMDHASH_H
#define SCMDHASH_H
#include "SCIO.H"
/* 历史校验算法仅供文件互操作；密码/票据仍只走内核PBKDF2/HMAC。
 * 实现为32位字，长度用两个字累加，不要求SCCC提供64位运行库。 */
typedef struct {u32 state[5],bytes_lo,bytes_hi,used;u8 block[64];} legacy_hash;
static u32 legacy_rot(u32 x,int n){return (x<<n)|(x>>(32-n));}
static u32 legacy_word(const u8 *p,int big)
{return big?((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]:(u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static void legacy_store(u8 *p,u32 word,int big)
{for(int i=0;i<4;i++)p[i]=(u8)(word>>(big?(3-i)*8:i*8));}
static void legacy_transform(legacy_hash *h)
{
#if defined(SC_HASH_MD5)
    static const u32 constants[64]={
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u};
    static const u8 shifts[16]={7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};u32 words[16];for(int i=0;i<16;i++)words[i]=legacy_word(h->block+i*4,0);
    u32 a=h->state[0],b=h->state[1],c=h->state[2],d=h->state[3];
    for(u32 i=0;i<64;i++){u32 f,g;if(i<16){f=(b&c)|(~b&d);g=i;}else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)&15;}
        else if(i<48){f=b^c^d;g=(3*i+5)&15;}else {f=c^(b|~d);g=(7*i)&15;}u32 old=d;d=c;c=b;b+=legacy_rot(a+f+constants[i]+words[g],shifts[(i/16)*4+(i&3)]);a=old;}
    h->state[0]+=a;h->state[1]+=b;h->state[2]+=c;h->state[3]+=d;
#else
    u32 words[80];for(int i=0;i<16;i++)words[i]=legacy_word(h->block+i*4,1);for(int i=16;i<80;i++)words[i]=legacy_rot(words[i-3]^words[i-8]^words[i-14]^words[i-16],1);
    u32 a=h->state[0],b=h->state[1],c=h->state[2],d=h->state[3],e=h->state[4];
    for(int i=0;i<80;i++){u32 f,k;if(i<20){f=(b&c)|(~b&d);k=0x5a827999u;}else if(i<40){f=b^c^d;k=0x6ed9eba1u;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}else {f=b^c^d;k=0xca62c1d6u;}
        u32 t=legacy_rot(a,5)+f+e+k+words[i];e=d;d=c;c=legacy_rot(b,30);b=a;a=t;}
    h->state[0]+=a;h->state[1]+=b;h->state[2]+=c;h->state[3]+=d;h->state[4]+=e;
#endif
}
static void legacy_init(legacy_hash *h)
{h->state[0]=0x67452301u;h->state[1]=0xefcdab89u;h->state[2]=0x98badcfeu;h->state[3]=0x10325476u;h->state[4]=0xc3d2e1f0u;h->bytes_lo=h->bytes_hi=h->used=0;}
static void legacy_update(legacy_hash *h,const void *bytes,u32 n)
{u32 old=h->bytes_lo;h->bytes_lo+=n;if(h->bytes_lo<old)h->bytes_hi++;const u8 *p=bytes;for(u32 i=0;i<n;i++){h->block[h->used++]=p[i];if(h->used==64){legacy_transform(h);h->used=0;}}}
static void legacy_final(legacy_hash *h,u8 *out)
{
    u32 lo=h->bytes_lo<<3,hi=(h->bytes_hi<<3)|(h->bytes_lo>>29);h->block[h->used++]=0x80;
    if(h->used>56){while(h->used<64)h->block[h->used++]=0;legacy_transform(h);h->used=0;}while(h->used<56)h->block[h->used++]=0;
#if defined(SC_HASH_MD5)
    legacy_store(h->block+56,lo,0);legacy_store(h->block+60,hi,0);legacy_transform(h);for(int i=0;i<4;i++)legacy_store(out+i*4,h->state[i],0);
#else
    legacy_store(h->block+56,hi,1);legacy_store(h->block+60,lo,1);legacy_transform(h);for(int i=0;i<5;i++)legacy_store(out+i*4,h->state[i],1);
#endif
}
#endif
