#include "random.h"
#include "crypto.h"
static u8 key[32];
static u32 ready,counter_lo,counter_hi;

static u32 firmware_big(void)
{u32 result=0;for(u32 i=0;i<4;i++)result=(result<<8)|inb(0x511);return result;}
static int firmware_seed(u8 out[32])
{
    outw(0x510,0);
    const char *signature="QEMU";
    for(u32 i=0;i<4;i++)if(inb(0x511)!=(u8)signature[i])return -1;
    outw(0x510,0x19);u32 count=firmware_big();if(count>128)return -1;
    u16 selector=0;
    for(u32 i=0;i<count;i++){
        u32 size=firmware_big();u16 entry=(u16)inb(0x511)<<8;entry|=inb(0x511);
        (void)inb(0x511);(void)inb(0x511);
        char name[56];for(u32 n=0;n<56;n++)name[n]=(char)inb(0x511);
        const char *wanted="opt/sandcore/entropy";u32 n=0;
        while(n<56 && wanted[n] && name[n]==wanted[n])n++;
        if(n<56 && !wanted[n] && !name[n] && size==32)selector=entry;
    }
    if(!selector)return -1;
    outw(0x510,selector);for(u32 i=0;i<32;i++)out[i]=inb(0x511);return 0;
}
static int hardware_seed(u8 out[32])
{
    u32 before,after;__asm__ __volatile__("pushfl; popl %0":"=r"(before));u32 probe=before^0x200000u;
    __asm__ __volatile__("pushl %0; popfl"::"r"(probe):"cc");__asm__ __volatile__("pushfl; popl %0":"=r"(after));
    __asm__ __volatile__("pushl %0; popfl"::"r"(before):"cc");if(!((before^after)&0x200000u))return -1;
    u32 a=1,b,c,d;
    __asm__ __volatile__("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if(!(c&(1u<<30)))return -1;
    for(u32 i=0;i<8;i++){
        u32 value=0;u8 ok=0;
        for(u32 attempt=0;attempt<32 && !ok;attempt++)
            __asm__ __volatile__(".byte 0x0f,0xc7,0xf0; setc %1" : "=a"(value),"=qm"(ok) :: "cc");
        if(!ok){crypto_zero(out,32);return -1;}
        for(u32 n=0;n<4;n++)out[4*i+n]=(u8)(value>>(8*n));
    }
    return 0;
}
void random_init(void)
{
    u8 seeds[64];crypto_zero(seeds,sizeof(seeds));crypto_zero(key,sizeof(key));
    ready=counter_lo=counter_hi=0;
    int hardware=hardware_seed(seeds),firmware=firmware_seed(seeds+32);
    if(!hardware || !firmware){sha256(seeds,sizeof(seeds),key);ready=1;}
    crypto_zero(seeds,sizeof(seeds));
}
int random_ready(void){return ready!=0;}
int random_bytes(void *data,u32 bytes)
{
    if(!ready || (!data && bytes))return -1;
    u8 *out=(u8 *)data,digest[32],input[16];hmac256_t mac;
    while(bytes){
        crypto_zero(input,sizeof(input));
        counter_lo++;if(!counter_lo && !++counter_hi){ready=0;return -1;}
        for(u32 n=0;n<4;n++){input[n]=(u8)(counter_lo>>(8*n));input[4+n]=(u8)(counter_hi>>(8*n));}
        input[8]='S';input[9]='C';input[10]='R';input[11]='N';
        hmac256_init(&mac,key,32);hmac256(&mac,input,sizeof(input),digest);
        sha256_t next;sha256_init(&next);sha256_update(&next,key,32);sha256_update(&next,digest,32);sha256_final(&next,key);
        u32 count=bytes<32?bytes:32;for(u32 n=0;n<count;n++)out[n]=digest[n];out+=count;bytes-=count;
    }
    crypto_zero(&mac,sizeof(mac));crypto_zero(digest,sizeof(digest));crypto_zero(input,sizeof(input));return 0;
}
