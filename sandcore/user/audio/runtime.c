#include "runtime.h"
void *mp_memory_copy(void *d,const void *s,u32 n){for(u32 i=0;i<n;i++)((u8 *)d)[i]=((const u8 *)s)[i];return d;}
void *mp_memory_move(void *d,const void *s,u32 n){if((u32)d<=(u32)s)return mp_memory_copy(d,s,n);
    while(n){n--;((u8 *)d)[n]=((const u8 *)s)[n];}return d;}
void *mp_memory_zero(void *d,u32 n){for(u32 i=0;i<n;i++)((u8 *)d)[i]=0;return d;}
void *mp_allocate(u32 n){if(!n)n=1;if(n>32u*1024*1024-16)return 0;u32 *p=sc_alloc(n+16);if(!p)return 0;
    p[0]=n;p[1]=0x334D5041u;p[2]=p[3]=0;return p+4;}
void mp_release(void *p){if(p){u32 *h=(u32 *)p-4;if(h[1]!=0x334D5041u)sc_exit(127);h[1]=0;sc_free(h);}}
void *mp_reallocate(void *p,u32 n){if(!p)return mp_allocate(n);if(!n){mp_release(p);return 0;}
    u32 *h=(u32 *)p-4;if(h[1]!=0x334D5041u)sc_exit(127);void *next=mp_allocate(n);if(!next)return 0;
    mp_memory_copy(next,p,h[0]<n?h[0]:n);mp_release(p);return next;}
void mp_assert(int condition){if(!condition)sc_exit(126);}
/* 仅解码组件内部的freestanding宽除法帮助符号，不导出成系统libc。 */
static unsigned long long divide(unsigned long long a,unsigned long long b,unsigned long long *remainder)
{
    if(!b)sc_exit(125);unsigned long long q=0,r=0;
    for(int i=63;i>=0;i--){u32 high=(u32)(r>>63);r=(r<<1)|((a>>i)&1);if(high || r>=b){r-=b;q|=1ull<<i;}}
    if(remainder)*remainder=r;return q;
}
unsigned long long __udivdi3(unsigned long long a,unsigned long long b){return divide(a,b,0);}
unsigned long long __umoddi3(unsigned long long a,unsigned long long b){unsigned long long r;divide(a,b,&r);return r;}
long long __divdi3(long long a,long long b){int negative=(a<0)^(b<0);unsigned long long x=(unsigned long long)a,y=(unsigned long long)b;
    if(a<0)x=0ull-x;if(b<0)y=0ull-y;x=divide(x,y,0);return negative?(long long)(0ull-x):(long long)x;}
long long __moddi3(long long a,long long b){unsigned long long x=(unsigned long long)a,y=(unsigned long long)b,r;
    if(a<0)x=0ull-x;if(b<0)y=0ull-y;divide(x,y,&r);return a<0?(long long)(0ull-r):(long long)r;}
