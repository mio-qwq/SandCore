#include "SCIO.H"
/* 主体按内核既有无SSE编译约定；只有这个用户态夹具显式启用SSE2。
 * SIMD初值/非默认MXCSR/x87先建立再ACK，131个对象都在屏障处保存
 * 真实状态。编译器固定XMM7，不拿普通函数的临时寄存器污染证据。 */
static __attribute__((target("sse2"),noinline)) int exercise(u32 index)
{
    u32 seed=index+1,blank[4],expected[4],actual[4],mxcsr=0x1F80u|((seed&3u)<<13),readback;
    __asm__ __volatile__("movdqu %%xmm7,%0":"=m"(blank)::"memory");
    for(int i=0;i<4;i++)if(blank[i])return 71;
    for(int i=0;i<4;i++)expected[i]=seed+(u32)i*0x1941B07Du;
    int value=(int)seed,got=0;
    __asm__ __volatile__("movdqu %0,%%xmm7; ldmxcsr %1; fninit; fildl %2"::"m"(expected),"m"(mxcsr),"m"(value):"xmm7","memory");
    u32 self[8],auth[8],ack[8];if(sc_process_self(self)<0 || sc_auth_info(auth)<0)return 72;
    ack[0]=1;ack[1]=self[1];ack[2]=self[2];ack[3]=index;
    ack[4]=auth[1];ack[5]=auth[3];ack[6]=auth[4];ack[7]=0;
    if(cli_write(1,ack,sizeof(ack))!=(int)sizeof(ack))return 73;
    sc_stream_close(1,0);sc_stream_close(2,0);u8 byte;
    if(cli_read(0,&byte,1)!=0)return 74;
    u32 began=(u32)sc_tick(),checks=0;
    do{
        sc_yield();__asm__ __volatile__("movdqu %%xmm7,%0; stmxcsr %1; fistl %2":"=m"(actual),"=m"(readback),"=m"(got)::"memory");
        if(readback!=mxcsr || got!=value)return 75;
        for(int i=0;i<4;i++)if(actual[i]!=expected[i])return 76;
        checks++;
    }while((u32)sc_tick()-began<200u);
    __asm__ __volatile__("fstp %%st(0); fninit":::"memory");return checks?0:77;
}
int main(void)
{
    if(cli_parse()!=1)return 2;int index;
    if(cli_integer(cli_argv[0],&index)<0 || index<0)return 2;
    u32 info[8];if(sc_simd_info(info)<0 || !info[3])return 77;
    return exercise((u32)index);
}
