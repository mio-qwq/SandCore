/* mio：真实普通三环协调程序，公开DEBUG只控制自己创建的
 * Studio。编译器PID来自CPUINFO的新槽，不从宿主写入。目标暂停
 * 后编译器照常完成，再运行另一个返回0的程序真实复用那个槽。
 * 只有按R才恢复Studio，便于验收只读保存暂停/复用的完整事实。 */
#include "SCAPI.H"
volatile u32 coordination[16];
int main(void)
{
    u32 initial[SC_CPU_WORDS],cpu[SC_CPU_WORDS];
    int win=sc_open("PID reuse / mio",240,100);
    if(win<0)return 1;
    sc_text(win,8,12,"Compiler PID reuse / R resume",PAL_UI_TEXT);
    if(sc_cpu(initial))return 2;
    int target=sc_dbgexec("HOME/IDE.SCX HOME/LARGE.C");
    if(target<0)return 3;
    coordination[1]=target;
    if(sc_debug(target,2,0,0,0))return 4;
    coordination[0]=1;
    int child=-1;
    while(child<0){
        if(sc_key()==27)return 5;
        if(sc_cpu(cpu))return 6;
        for(int i=1;i<8;i++)if(i!=target&&cpu[16+i*6+1]==1&&cpu[16+i*6+2]!=initial[16+i*6+2]){
            child=i;coordination[2]=child;coordination[3]=cpu[16+i*6+2];break;
        }
        if(child<0)sc_yield();
    }
    if(sc_debug(target,7,0,0,0))return 7;
    coordination[0]=2;
    int state;
    do{state=sc_status(child);sc_yield();}while(state==0x40000000||state==0x40000001);
    coordination[4]=state;
    do{sc_cpu(cpu);sc_yield();}while(cpu[16+child*6+1]!=0);
    int next=sc_exec("HOME/ZERO.SCX");coordination[5]=next;
    if(next<0)return 8;
    do{state=sc_status(next);sc_yield();}while(state==0x40000000||state==0x40000001);
    sc_cpu(cpu);coordination[6]=cpu[16+next*6+2];coordination[7]=state;coordination[0]=3;
    sc_window(win,2);
    while(sc_key()!='r')sc_yield();
    coordination[8]=sc_debug(target,2,0,0,0);coordination[0]=4;
    while(sc_key()!=27)sc_yield();
    return 0;
}
