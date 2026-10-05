/* =====================================================================
 * mio：桌面空等的真实三环对照，不依赖私有内核诊断来计算吞吐。
 * 为什么记录工作次数：用户样本增加本身不等于程序算得更多；同一
 * 原生SCX以相同2048次整数运算为一批，记录相同PIT时间中的批数，
 * 同时保留CPU分类和旧YIELD空闲窗口，区分吞吐与单纯统计变化。
 * 所有输入/关闭由真实系统调用和窗口处理，探针不改任务表/页表。
 * ===================================================================== */
#include "SCAPI.H"
volatile u32 sched_probe[40];
static u32 before[SC_CPU_WORDS],after[SC_CPU_WORDS];
static volatile u32 work=1;
static u32 batches;
static void batch(void)
{
    /* volatile禁止编译器把运算当作无用代码删掉。每一批工作完全
     * 一样，不把宿主计时/测试器读内存时间算成三环计算量。 */
    for(int i=0;i<2048;i++)work=work*1664525u+1013904223u;
    batches++;
}
static void sample(int row,int burn,u32 ticks)
{
    u32 *out=(u32 *)sched_probe+8+row*5;
    batches=0;sc_cpu(before);u32 start=(u32)sc_tick();
    while((u32)sc_tick()-start<ticks){if(burn)batch();else sc_yield();}
    sc_cpu(after);
    out[0]=after[3]-before[3];out[1]=after[4]-before[4];
    out[2]=after[5]-before[5];out[3]=after[6]-before[6];out[4]=batches;
    if(out[0]!=out[1]+out[2]+out[3])sched_probe[1]++;
}
int main(void)
{
    char args[128];sc_args(args,128);
    int win=sc_open_rgb("SCHEDULER / REAL WORK",640,360);if(win<0)return 1;
    sched_probe[3]=(u32)win;
    sc_fill_rgb(win,0,0,1920,1080,SC_RGB_PAPER);
    sc_text_rgb(win,20,18,"Same native SCX / real work / real PIT samples",SC_RGB_INK);
    if(equal(args,"child")){
        sched_probe[0]=3;
        while(sc_key()!=27){batch();sched_probe[4]=batches;}
        return 0;
    }
    sched_probe[0]=1;
    if(equal(args,"pair")){
        int child=sc_exec("HOME/SCHED.SCX child");
        sched_probe[5]=(u32)child;if(child<0)sched_probe[1]++;
        sample(0,1,250);sched_probe[0]=4;
        sc_text_rgb(win,20,60,"Two busy tasks / parent completed / close both",SC_RGB_ACCENT);
    }else{
        for(int row=0;row<6;row++){
            sched_probe[2]=(u32)row;
            sc_text_rgb(win,20,60,row<3?"Old YIELD idle sample":"Busy user sample / counted integer batches",SC_RGB_ACCENT);
            sample(row,row>=3,row<3?100u:200u);
        }
        sched_probe[0]=2;
        sc_text_rgb(win,20,104,"Finished / press A / click client / Esc to close",SC_RGB_ACCENT);
    }
    int pointer[6];
    while(1){
        int key=sc_key();if(key==27)break;if(key=='a')sched_probe[6]++;
        if(!sc_pointer(win,pointer) && pointer[3])sched_probe[7]++;
        sc_yield();
    }
    return sched_probe[1]?1:0;
}
