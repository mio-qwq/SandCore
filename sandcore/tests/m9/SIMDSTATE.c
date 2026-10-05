#include "SCIO.H"
/* 编译器语言能力之外的现场测试使用用户允许的宿主编译；运行与
 * 抢占仍在SandCore，只有此夹具函数带SSE2目标，不影响内核CFLAGS。 */
static __attribute__((target("sse2"),noinline)) int exercise(u32 seed)
{
    u32 blank[4],expected[4],actual[4],mxcsr=0x1F80u|((seed&3u)<<13),readback;
    __asm__ __volatile__("movdqu %%xmm7,%0":"=m"(blank)::"memory");for(int i=0;i<4;i++)if(blank[i])return 1;
    for(int i=0;i<4;i++)expected[i]=seed+(u32)i*0x1941B07Du;int value=(int)seed,got=0;
    __asm__ __volatile__("movdqu %0,%%xmm7; ldmxcsr %1; fninit; fildl %2"::"m"(expected),"m"(mxcsr),"m"(value):"xmm7","memory");
    u32 start=sc_tick(),checks=0;while((u32)sc_tick()-start<100){sc_yield();__asm__ __volatile__("movdqu %%xmm7,%0; stmxcsr %1; fistl %2":"=m"(actual),"=m"(readback),"=m"(got)::"memory");
        if(readback!=mxcsr || got!=value)return 2;for(int i=0;i<4;i++)if(actual[i]!=expected[i])return 3;checks++;}
    __asm__ __volatile__("fstp %%st(0); fninit":::"memory");cli_text(1,"PASS XMM7 MXCSR x87 initial isolation and preemption ");cli_number(1,(int)checks);cli_text(1,"\n");return 0;
}
int main(void){if(cli_parse()!=1)return 2;int seed;if(cli_integer(cli_argv[0],&seed)<0 || seed<1)return 2;u32 info[8];if(sc_simd_info(info)<0 || !info[3]){cli_text(1,"UNSUPPORTED SSE2 state fixture\n");return 77;}return exercise((u32)seed);}
