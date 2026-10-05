/* mio：完整源同一函数由G2执行；正常WRITE/EXIT回传。
 * 两段负载各取实际公开基点与终点；保留完整64字，比例参考
 * 由宿主无限精度除法计算。循环不篡改IRQ/PIT或任务状态。 */
#define main monitor_original_main
#include "MONITOR.inc"
#undef main
static u32 ratio_input[]={0x0u,0x0u,0x1u,0x0u,0x0u,0x1u,0x1u,0x1u,0x2u,0x1u,0x1u,0x3u,0x2u,0x3u,0x63u,0x64u,0x64u,0x65u,0x7fffffffu,0xffffffffu,0xfffffffeu,0xffffffffu,0xffffffffu,0xffffffffu,0x80000000u,0x80000001u,0x12345678u,0x87654321u,0x989680u,0x1312d01u};
static u32 ratio_output[15];
static u8 name_input[]={65,66,67,68,69,70,71,72,73,74,75,76,165,165,165,165,230,178,153,230,160,184,97,98,99,100,101,102,165,165,165,165,65,66,67,68,69,70,71,72,73,74,75,194,165,165,165,165,65,66,67,224,128,128,122,122,122,0,0,0,165,165,165,165,65,66,67,237,160,128,0,0,0,0,0,0,165,165,165,165,65,66,67,244,144,128,128,0,0,0,0,0,165,165,165,165,65,66,67,240,128,128,128,0,0,0,0,0,165,165,165,165,65,66,67,194,255,0,0,0,0,0,0,0,165,165,165,165,65,66,244,143,191,191,120,121,122,0,0,0,165,165,165,165};
static u8 name_output[180];
static u32 api_output[150],records[288];
static volatile u32 computation;
int main(void)
{
    for(int i=0;i<15;i++)ratio_output[i]=(u32)percent(ratio_input[i*2],ratio_input[i*2+1]);
    for(int i=0;i<sizeof(name_output);i++)name_output[i]=0xA5;
    for(int i=0;i<9;i++)task_name((char*)name_output+i*20,(char*)name_input+i*16);
    for(int i=0;i<150;i++)api_output[i]=0xA5C37E91u;
    if(sc_monitor(api_output+1)||sc_storage(api_output+51)||sc_cpu(api_output+85))return 1;
    snapshots();
    for(int phase=0;phase<2;phase++){
        baseline();int start=sc_tick();
        while((u32)sc_tick()-(u32)start<200){
            if(!phase)sc_yield();else for(int i=0;i<50000;i++)computation=computation*1664525u+1013904223u;
        }
        u32 *out=records+phase*144;
        for(int i=0;i<64;i++)out[i]=before[i];
        sample();for(int i=0;i<64;i++)out[64+i]=cpu[i];
        out[128]=(u32)cpu_valid;out[129]=(u32)cpu_busy;out[130]=(u32)cpu_idle;
        out[131]=(u32)cpu_kernel;out[132]=(u32)cpu_user;out[133]=(u32)cpu_graphics;
        for(int i=0;i<8;i++)out[134+i]=(u32)task_percent[i];
        out[142]=(u32)start;out[143]=(u32)last_sample;
    }
    if(sc_write("HOME/RATIO.BIN",ratio_output,sizeof(ratio_output))<0)return 2;
    if(sc_write("HOME/NAMES.BIN",name_output,sizeof(name_output))<0)return 3;
    if(sc_write("HOME/NINPUT.BIN",name_input,sizeof(name_input))<0)return 4;
    if(sc_write("HOME/API.BIN",api_output,sizeof(api_output))<0)return 5;
    if(sc_write("HOME/SAMPLES.BIN",records,sizeof(records))<0)return 6;
    return 0;
}
