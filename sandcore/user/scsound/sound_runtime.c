#include "SOUNDPRIVATE.H"

ScStatus snd_backend_cause;
ScU32 snd_poisoned;
void *mp_memory_copy(void *d,const void *s,u32 n){return memcpy(d,s,n);}
void *mp_memory_move(void *d,const void *s,u32 n){return memmove(d,s,n);}
void *mp_memory_zero(void *d,u32 n){return memset(d,0,n);}
void *mp_allocate(u32 n){return malloc(n?n:1);}
void *mp_reallocate(void *p,u32 n){return realloc(p,n);}
void mp_release(void *p){free(p);}
void mp_assert(int condition){if(!condition){snd_poisoned=1;ScProcessExit(126);}}
static void snd_record(void *p,ScU32 bytes)
{memset(p,0,bytes);((ScU32 *)p)[0]=bytes;((ScU32 *)p)[1]=1;}
int sc_stream_read(ScFile file,void *out,u32 bytes)
{
    ScIoResult result;snd_record(&result,sizeof(result));ScStatus status=ScFileRead(file,out,bytes,&result);
    if(status==SC32_E_AGAIN || status==SC32_PENDING){
        if(result.transferred)return (int)result.transferred;
        ScU32 events=0;ScSystemWait(SC32_EVENT_STREAM,1,&events);return -6;
    }
    if(status<0){snd_backend_cause=status;return -1;}
    return (int)result.transferred;
}
int sc_stream_seek(ScFile file,u32 position)
{
    /* 当前SandFS文件上限小于INT_MAX；不把无符号大偏移截成负seek。 */
    if(position>0x7fffffffu){snd_backend_cause=SC32_E_RANGE;return -1;}
    ScFileInfo info;snd_record(&info,sizeof(info));ScStatus status=ScFileSeek(file,(ScI32)position,SC32_SEEK_SET,&info);
    if(status<0){snd_backend_cause=status;return -1;}return 0;
}
int sc_simd_info(u32 out[8])
{
    ScSimdInfo info;snd_record(&info,sizeof(info));ScStatus status=ScSimdQuery(&info);
    if(status<0){snd_backend_cause=status;return -1;}
    memset(out,0,32);out[0]=1;out[1]=info.ready;out[2]=info.fxsr;out[3]=info.sse2;
    out[4]=info.cpuid_features;out[5]=info.saved_bytes;out[6]=info.switches;
    if(!info.ready)snd_backend_cause=SC32_E_NOT_SUPPORTED;
    return 0;
}
void sc_yield(void){ScProcessYield();}

/* 沿既有音频后端的私有宽除法，不导出成标准库。每次最多64步，
 * 只供宿主编译解码核心使用；内核与普通SCCC整数ABI保持原样。 */
static unsigned long long snd_divide(unsigned long long a,unsigned long long b,unsigned long long *remainder)
{
    if(!b){snd_poisoned=1;ScProcessExit(125);return 0;}
    unsigned long long q=0,r=0;
    for(int i=63;i>=0;i--){u32 high=(u32)(r>>63);r=(r<<1)|((a>>i)&1);
        if(high || r>=b){r-=b;q|=1ull<<i;}}
    if(remainder)*remainder=r;
    return q;
}
unsigned long long __udivdi3(unsigned long long a,unsigned long long b){return snd_divide(a,b,0);}
unsigned long long __umoddi3(unsigned long long a,unsigned long long b){unsigned long long r=0;snd_divide(a,b,&r);return r;}
long long __divdi3(long long a,long long b)
{
    int negative=(a<0)^(b<0);unsigned long long x=(unsigned long long)a,y=(unsigned long long)b;
    if(a<0)x=0ull-x;if(b<0)y=0ull-y;x=snd_divide(x,y,0);
    return negative?(long long)(0ull-x):(long long)x;
}
long long __moddi3(long long a,long long b)
{
    unsigned long long x=(unsigned long long)a,y=(unsigned long long)b,r=0;
    if(a<0)x=0ull-x;if(b<0)y=0ull-y;snd_divide(x,y,&r);
    return a<0?(long long)(0ull-r):(long long)r;
}
