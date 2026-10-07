#include "simd.h"
#include "task.h"
/* 内核普通C仍-msoft-float/-mno-sse。扩展现场独立于task_t/19字陷入
 * 帧；每次真正换任务才保存恢复，避免每个绘图系统调用重复搬512B。
 * 不用lazy #NM，生命周期更直接，退出/槽复用不会泄露上一用户向量。 */
typedef struct {u8 bytes[512];} simd_context_t;
#define SIMD_CONTEXT(pid) (((simd_context_t *)task_data(pid,TASK_DATA_SIMD))->bytes)
u32 simd_task_bytes(void){return sizeof(simd_context_t);}
static u8 initial[512] __attribute__((aligned(16)));
static u32 features,fxsr,sse2,ready,switches;
static int cpuid_available(void)
{
    u32 before,after;
    __asm__ __volatile__("pushfl; popl %0":"=r"(before));
    u32 probe=before^0x200000u;
    __asm__ __volatile__("pushl %0; popfl"::"r"(probe):"cc");
    __asm__ __volatile__("pushfl; popl %0":"=r"(after));
    __asm__ __volatile__("pushl %0; popfl"::"r"(before):"cc");
    return ((after^before)&0x200000u)!=0;
}
void simd_init(void)
{
    u32 a,b,c,d;features=fxsr=sse2=ready=switches=0;
    if(!cpuid_available())return;
    __asm__ __volatile__("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0));if(a<1)return;
    __asm__ __volatile__("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1));features=d;
    if(!(d&1))return;
    fxsr=(d>>24)&1u;sse2=fxsr && (d&(1u<<25)) && (d&(1u<<26));
    u32 cr0;__asm__ __volatile__("mov %%cr0,%0":"=r"(cr0));cr0=(cr0|0x22u)&~0xCu;
    __asm__ __volatile__("mov %0,%%cr0"::"r"(cr0):"memory");
    if(fxsr){
        u32 cr4;__asm__ __volatile__("mov %%cr4,%0":"=r"(cr4));cr4|=0x200u;if(d&(1u<<25))cr4|=0x400u;
        __asm__ __volatile__("mov %0,%%cr4"::"r"(cr4):"memory");
    }
    for(u32 i=0;i<512;i++)initial[i]=0;
    __asm__ __volatile__("fninit");
    if(fxsr){
        __asm__ __volatile__("fxsave %0":"=m"(initial));
        /* FNINIT不清XMM固件遗留内容；清除载荷不依赖SSE指令。 */
        for(u32 i=32;i<512;i++)initial[i]=0;
        if(d&(1u<<25))*(u32 *)(initial+24)=0x1F80u;
        __asm__ __volatile__("fxrstor %0"::"m"(initial):"memory");
    }else{
        __asm__ __volatile__("fnsave %0":"=m"(initial));
        __asm__ __volatile__("frstor %0"::"m"(initial):"memory");
    }
    ready=1;for(int pid=task_next(-1);pid>=0;pid=task_next(pid))simd_spawn(pid);
}
void simd_spawn(int pid){for(u32 i=0;i<512;i++)SIMD_CONTEXT(pid)[i]=initial[i];}
void simd_stop(int pid){for(u32 i=0;i<512;i++)SIMD_CONTEXT(pid)[i]=0;}
void simd_switch(int previous,int next)
{
    if(!ready)return;
    if(fxsr){
        __asm__ __volatile__("fxsave %0":"=m"(SIMD_CONTEXT(previous)));
        __asm__ __volatile__("fxrstor %0"::"m"(SIMD_CONTEXT(next)):"memory");
    }else{
        __asm__ __volatile__("fnsave %0":"=m"(SIMD_CONTEXT(previous)));
        __asm__ __volatile__("frstor %0"::"m"(SIMD_CONTEXT(next)):"memory");
    }
    switches++;if(TASK(previous).state==2)simd_stop(previous);
}
int simd_sse2(void){return ready && sse2;}
void simd_info(u32 out[8])
{
    for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=ready;out[2]=fxsr;out[3]=sse2;
    out[4]=features;out[5]=fxsr?512:ready?108:0;out[6]=switches;
}
