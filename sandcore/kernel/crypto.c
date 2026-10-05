#include "crypto.h"

void crypto_zero(void *data,u32 bytes)
{volatile u8 *p=(volatile u8 *)data;while(bytes--)*p++=0;}
int crypto_equal(const u8 *a,const u8 *b,u32 bytes)
{u32 difference=0;for(u32 i=0;i<bytes;i++)difference|=a[i]^b[i];return difference==0;}
static u32 rotate(u32 x,u32 n){return (x>>n)|(x<<(32-n));}
static u32 big(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void putbig(u8 *p,u32 value)
{p[0]=(u8)(value>>24);p[1]=(u8)(value>>16);p[2]=(u8)(value>>8);p[3]=(u8)value;}
static void transform(sha256_t *s)
{
    static const u32 constants[64]={
        0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
        0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
        0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
        0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
        0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
        0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
        0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
        0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2};
    u32 w[64];for(u32 i=0;i<16;i++)w[i]=big(s->block+4*i);
    for(u32 i=16;i<64;i++){
        u32 x=w[i-15],y=w[i-2];
        w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7]+(rotate(y,17)^rotate(y,19)^(y>>10));
    }
    u32 a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(u32 i=0;i<64;i++){
        u32 t=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+w[i];
        u32 z=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+z;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
    crypto_zero(w,sizeof(w));
}
void sha256_init(sha256_t *s)
{
    static const u32 initial[8]={0x6A09E667,0xBB67AE85,0x3C6EF372,0xA54FF53A,0x510E527F,0x9B05688C,0x1F83D9AB,0x5BE0CD19};
    crypto_zero(s,sizeof(*s));for(u32 i=0;i<8;i++)s->h[i]=initial[i];
}
void sha256_update(sha256_t *s,const void *data,u32 bytes)
{
    const u8 *p=(const u8 *)data;u32 old=s->lo;s->lo+=bytes;if(s->lo<old)s->hi++;
    while(bytes){
        u32 count=64-s->used;if(count>bytes)count=bytes;
        for(u32 i=0;i<count;i++)s->block[s->used+i]=p[i];
        s->used+=count;p+=count;bytes-=count;
        if(s->used==64){transform(s);s->used=0;}
    }
}
void sha256_final(sha256_t *s,u8 out[32])
{
    u32 lo=s->lo<<3,hi=(s->hi<<3)|(s->lo>>29);
    s->block[s->used++]=0x80;
    if(s->used>56){while(s->used<64)s->block[s->used++]=0;transform(s);s->used=0;}
    while(s->used<56)s->block[s->used++]=0;
    putbig(s->block+56,hi);putbig(s->block+60,lo);transform(s);
    for(u32 i=0;i<8;i++)putbig(out+4*i,s->h[i]);crypto_zero(s,sizeof(*s));
}
void sha256(const void *data,u32 bytes,u8 out[32])
{sha256_t s;sha256_init(&s);sha256_update(&s,data,bytes);sha256_final(&s,out);}
void hmac256_init(hmac256_t *s,const void *key,u32 bytes)
{
    u8 padded[64],digest[32];crypto_zero(padded,sizeof(padded));
    const u8 *p=(const u8 *)key;
    if(bytes>64){sha256(key,bytes,digest);p=digest;bytes=32;}
    for(u32 i=0;i<bytes;i++)padded[i]=p[i];
    for(u32 i=0;i<64;i++)padded[i]^=0x36;
    sha256_init(&s->inner);sha256_update(&s->inner,padded,64);
    for(u32 i=0;i<64;i++)padded[i]^=0x36^0x5C;
    sha256_init(&s->outer);sha256_update(&s->outer,padded,64);
    crypto_zero(padded,sizeof(padded));crypto_zero(digest,sizeof(digest));
}
void hmac256(const hmac256_t *s,const void *data,u32 bytes,u8 out[32])
{
    sha256_t inner=s->inner,outer=s->outer;u8 digest[32];
    sha256_update(&inner,data,bytes);sha256_final(&inner,digest);
    sha256_update(&outer,digest,32);sha256_final(&outer,out);crypto_zero(digest,32);
}
int pbkdf256_init(pbkdf256_t *s,const void *password,u32 bytes,const u8 salt[16],u32 iterations)
{
    if(!iterations)return -1;
    u8 first[20];for(u32 i=0;i<16;i++)first[i]=salt[i];
    first[16]=first[17]=first[18]=0;first[19]=1;
    crypto_zero(s,sizeof(*s));hmac256_init(&s->key,password,bytes);
    hmac256(&s->key,first,sizeof(first),s->u);
    for(u32 i=0;i<32;i++)s->value[i]=s->u[i];s->done=1;s->total=iterations;
    crypto_zero(first,sizeof(first));return 0;
}
int pbkdf256_step(pbkdf256_t *s,u32 budget)
{
    while(budget-- && s->done<s->total){
        hmac256(&s->key,s->u,32,s->u);
        for(u32 i=0;i<32;i++)s->value[i]^=s->u[i];s->done++;
    }
    return s->done==s->total;
}
