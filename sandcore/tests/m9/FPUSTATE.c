#include "SCIO.H"
/* 真实x87回退现场夹具；只用显式管理/整数转换指令，不把浮点加入
 * 内核或通用C库。初始环境、控制字和栈值分别核对，不能先FNINIT
 * 抹掉上一个任务泄露的现场再宣称隔离成功。 */
int main(void)
{
    if(cli_parse()!=1)return 2;int seed;if(cli_integer(cli_argv[0],&seed)<0 || seed<1)return 2;
    u32 info[8];if(sc_simd_info(info)<0 || !info[1]){cli_text(1,"UNSUPPORTED x87 state fixture\n");return 77;}
    u16 environment[14],control=(u16)(0x037Fu|((seed&3)<<10)),actual;
    __asm__ __volatile__("fnstenv %0":"=m"(environment)::"memory");
    if(environment[0]!=0x037F || (environment[2]&0x3800) || environment[4]!=0xFFFF)return 1;
    int value=seed,received=0;__asm__ __volatile__("fldcw %0; fildl %1"::"m"(control),"m"(value):"memory");
    u32 begin=sc_tick(),checks=0;while((u32)sc_tick()-begin<100){
        /* 同时覆盖PIT强制抢占和显式让出；不能仅靠两个协作yield
         * 就宣称异步抢占现场正确。整数忙段不读写x87，值跨IRQ保留。 */
        for(volatile u32 spins=0;spins<250000;spins++){}
        __asm__ __volatile__("fnstcw %0; fistl %1":"=m"(actual),"=m"(received)::"memory");
        if(actual!=control || received!=value)return 3;checks++;sc_yield();}
    __asm__ __volatile__("fstp %%st(0)":::"memory");
    cli_text(1,info[2]?"PASS x87 FXSAVE ":"PASS x87 FNSAVE ");
    cli_text(1,"initial isolation and preemption ");cli_number(1,(int)checks);cli_text(1,"\n");return 0;
}
