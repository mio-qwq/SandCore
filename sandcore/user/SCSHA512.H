#ifndef SCSHA512_H
#define SCSHA512_H
#include "SCIO.H"
/* 依据SHA-512算法定义独立编写；高低两个32位字模拟64位，
 * 不引入第三方实现、long long库或标准C运行库。16槽滚动调度
 * 避免每块复制80字。数值常量参照RFC6234第5.2/6.3节。 */
typedef struct {u32 hi,lo;} hash512_word;
typedef struct {hash512_word state[8];u32 bytes[4],used;u8 block[128];} hash512_state;
static hash512_word hash512_add(hash512_word a,hash512_word b)
{u32 lo=a.lo+b.lo;return (hash512_word){a.hi+b.hi+(lo<a.lo),lo};}
static hash512_word hash512_xor(hash512_word a,hash512_word b)
{return (hash512_word){a.hi^b.hi,a.lo^b.lo};}
static hash512_word hash512_rotate(hash512_word a,u32 n)
{if(n>=32){u32 t=a.hi;a.hi=a.lo;a.lo=t;n-=32;}if(!n)return a;return (hash512_word){(a.hi>>n)|(a.lo<<(32-n)),(a.lo>>n)|(a.hi<<(32-n))};}
static hash512_word hash512_shift(hash512_word a,u32 n)
{return (hash512_word){a.hi>>n,(a.lo>>n)|(a.hi<<(32-n))};}
static hash512_word hash512_sigma(hash512_word a,int big,int second)
{u32 x=big?(second?14:28):(second?19:1),y=big?(second?18:34):(second?61:8),z=big?(second?41:39):(second?6:7);
    return hash512_xor(hash512_xor(hash512_rotate(a,x),hash512_rotate(a,y)),big?hash512_rotate(a,z):hash512_shift(a,z));}
static u32 hash512_read(const u8 *p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static void hash512_store(u8 *p,u32 x){for(int i=0;i<4;i++)p[i]=(u8)(x>>(24-8*i));}
static void hash512_transform(hash512_state *s)
{
    static const hash512_word constants[80]={
    {0x428a2f98u,0xd728ae22u},
    {0x71374491u,0x23ef65cdu},
    {0xb5c0fbcfu,0xec4d3b2fu},
    {0xe9b5dba5u,0x8189dbbcu},
    {0x3956c25bu,0xf348b538u},
    {0x59f111f1u,0xb605d019u},
    {0x923f82a4u,0xaf194f9bu},
    {0xab1c5ed5u,0xda6d8118u},
    {0xd807aa98u,0xa3030242u},
    {0x12835b01u,0x45706fbeu},
    {0x243185beu,0x4ee4b28cu},
    {0x550c7dc3u,0xd5ffb4e2u},
    {0x72be5d74u,0xf27b896fu},
    {0x80deb1feu,0x3b1696b1u},
    {0x9bdc06a7u,0x25c71235u},
    {0xc19bf174u,0xcf692694u},
    {0xe49b69c1u,0x9ef14ad2u},
    {0xefbe4786u,0x384f25e3u},
    {0x0fc19dc6u,0x8b8cd5b5u},
    {0x240ca1ccu,0x77ac9c65u},
    {0x2de92c6fu,0x592b0275u},
    {0x4a7484aau,0x6ea6e483u},
    {0x5cb0a9dcu,0xbd41fbd4u},
    {0x76f988dau,0x831153b5u},
    {0x983e5152u,0xee66dfabu},
    {0xa831c66du,0x2db43210u},
    {0xb00327c8u,0x98fb213fu},
    {0xbf597fc7u,0xbeef0ee4u},
    {0xc6e00bf3u,0x3da88fc2u},
    {0xd5a79147u,0x930aa725u},
    {0x06ca6351u,0xe003826fu},
    {0x14292967u,0x0a0e6e70u},
    {0x27b70a85u,0x46d22ffcu},
    {0x2e1b2138u,0x5c26c926u},
    {0x4d2c6dfcu,0x5ac42aedu},
    {0x53380d13u,0x9d95b3dfu},
    {0x650a7354u,0x8baf63deu},
    {0x766a0abbu,0x3c77b2a8u},
    {0x81c2c92eu,0x47edaee6u},
    {0x92722c85u,0x1482353bu},
    {0xa2bfe8a1u,0x4cf10364u},
    {0xa81a664bu,0xbc423001u},
    {0xc24b8b70u,0xd0f89791u},
    {0xc76c51a3u,0x0654be30u},
    {0xd192e819u,0xd6ef5218u},
    {0xd6990624u,0x5565a910u},
    {0xf40e3585u,0x5771202au},
    {0x106aa070u,0x32bbd1b8u},
    {0x19a4c116u,0xb8d2d0c8u},
    {0x1e376c08u,0x5141ab53u},
    {0x2748774cu,0xdf8eeb99u},
    {0x34b0bcb5u,0xe19b48a8u},
    {0x391c0cb3u,0xc5c95a63u},
    {0x4ed8aa4au,0xe3418acbu},
    {0x5b9cca4fu,0x7763e373u},
    {0x682e6ff3u,0xd6b2b8a3u},
    {0x748f82eeu,0x5defb2fcu},
    {0x78a5636fu,0x43172f60u},
    {0x84c87814u,0xa1f0ab72u},
    {0x8cc70208u,0x1a6439ecu},
    {0x90befffau,0x23631e28u},
    {0xa4506cebu,0xde82bde9u},
    {0xbef9a3f7u,0xb2c67915u},
    {0xc67178f2u,0xe372532bu},
    {0xca273eceu,0xea26619cu},
    {0xd186b8c7u,0x21c0c207u},
    {0xeada7dd6u,0xcde0eb1eu},
    {0xf57d4f7fu,0xee6ed178u},
    {0x06f067aau,0x72176fbau},
    {0x0a637dc5u,0xa2c898a6u},
    {0x113f9804u,0xbef90daeu},
    {0x1b710b35u,0x131c471bu},
    {0x28db77f5u,0x23047d84u},
    {0x32caab7bu,0x40c72493u},
    {0x3c9ebe0au,0x15c9bebcu},
    {0x431d67c4u,0x9c100d4cu},
    {0x4cc5d4beu,0xcb3e42b6u},
    {0x597f299cu,0xfc657e2au},
    {0x5fcb6fabu,0x3ad6faecu},
    {0x6c44198cu,0x4a475817u}};
    hash512_word words[16],work[8];for(int i=0;i<8;i++)work[i]=s->state[i];
    for(int i=0;i<16;i++)words[i]=(hash512_word){hash512_read(s->block+i*8),hash512_read(s->block+i*8+4)};
    for(u32 t=0;t<80;t++){u32 at=t&15;if(t>=16)words[at]=hash512_add(hash512_add(hash512_sigma(words[(t-2)&15],0,1),words[(t-7)&15]),hash512_add(hash512_sigma(words[(t-15)&15],0,0),words[at]));
        hash512_word a=work[0],b=work[1],c=work[2],e=work[4],f=work[5],g=work[6];
        hash512_word choose={(e.hi&f.hi)^(~e.hi&g.hi),(e.lo&f.lo)^(~e.lo&g.lo)},major={(a.hi&b.hi)^(a.hi&c.hi)^(b.hi&c.hi),(a.lo&b.lo)^(a.lo&c.lo)^(b.lo&c.lo)};
        hash512_word one=hash512_add(hash512_add(work[7],hash512_sigma(e,1,1)),hash512_add(choose,hash512_add(constants[t],words[at]))),two=hash512_add(hash512_sigma(a,1,0),major);
        work[7]=work[6];work[6]=work[5];work[5]=work[4];work[4]=hash512_add(work[3],one);work[3]=work[2];work[2]=work[1];work[1]=work[0];work[0]=hash512_add(one,two);}
    for(int i=0;i<8;i++)s->state[i]=hash512_add(s->state[i],work[i]);
}
static void hash512_init(hash512_state *s)
{
    static const hash512_word initial[8]={{0x6a09e667u,0xf3bcc908u},{0xbb67ae85u,0x84caa73bu},{0x3c6ef372u,0xfe94f82bu},{0xa54ff53au,0x5f1d36f1u},
        {0x510e527fu,0xade682d1u},{0x9b05688cu,0x2b3e6c1fu},{0x1f83d9abu,0xfb41bd6bu},{0x5be0cd19u,0x137e2179u}};
    for(int i=0;i<8;i++)s->state[i]=initial[i];for(int i=0;i<4;i++)s->bytes[i]=0;s->used=0;
}
static void hash512_update(hash512_state *s,const void *data,u32 n)
{
    u32 old=s->bytes[0];s->bytes[0]+=n;u32 carry=s->bytes[0]<old;for(int i=1;i<4 && carry;i++){old=s->bytes[i];s->bytes[i]++;carry=s->bytes[i]<old;}
    const u8 *p=data;u32 at=0;while(at<n){u32 take=128-s->used;if(take>n-at)take=n-at;for(u32 i=0;i<take;i++)s->block[s->used++]=p[at++];
        if(s->used==128){hash512_transform(s);s->used=0;}}
}
static void hash512_final(hash512_state *s,u8 *out)
{
    u32 bits[4];for(int i=0;i<4;i++)bits[i]=(s->bytes[i]<<3)|(i?s->bytes[i-1]>>29:0);
    s->block[s->used++]=128;if(s->used>112){while(s->used<128)s->block[s->used++]=0;hash512_transform(s);s->used=0;}
    while(s->used<112)s->block[s->used++]=0;for(int i=0;i<4;i++)hash512_store(s->block+112+4*i,bits[3-i]);hash512_transform(s);
    for(int i=0;i<8;i++){hash512_store(out+8*i,s->state[i].hi);hash512_store(out+8*i+4,s->state[i].lo);}
}
#endif

