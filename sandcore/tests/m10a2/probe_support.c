#include "SCAPI.H"

/* 此文件是独立验收探针的私有后端，特意检查历史系统调用的边界。
 * 应用主体只包含 SCLDR.H；正式应用接口在 Phase 2 由 SC32API 提供。 */
void ProbeWrite(const char *text,unsigned int size)
{
    unsigned int at=0;
    while(at<size){
        int n=sc_stream_write(1,text+at,size-at);
        if(n==-6){sc_event_wait(SC_EVENT_STREAM,100);continue;}
        if(n<=0)return;
        at+=(unsigned int)n;
    }
}
unsigned int ProbeFreePages(void)
{unsigned int out[48];return sc_monitor(out)<0?0:out[4]/4096;}
int ProbeArgs(char *out,int size)
{return sc_args(out,size);}
int ProbeReadonlyOutput(const char *message)
{
    /* 库的只读字符串可以作为输入，但不能被内核输出权限覆盖。
     * 三个不同写回分支使用同一真实只读页，不增加无价值组合。 */
    if(sc_call(5,(int)message,0,32,0,0,0)!=-1)return -1;
    if(sc_call(0x26,(int)message,0,0,0,0,0)!=-1)return -2;
    if(sc_call(0x37,(int)message,(int)message,0,0,0,0)!=-1)return -3;
    return 0;
}
int ProbeMappings(void)
{
    unsigned int before=ProbeFreePages();
    unsigned int base=(unsigned int)sc_call(0x280,8192,0,0,0,0,0);
    if(base<0x50000000u || base>=0x54000000u)return -1;
    if(sc_call(0x282,(int)(base+4096),0,0,0,0,0)!=-1)return -2;
    if(sc_free((void *)base)!=-1)return -3;
    if(sc_call(0x281,(int)(base+4096),8192,1,0,0,0)!=-1)return -4;
    /* 上述越过尾页的保护必须原子失败，第二页仍能合法写回。 */
    if(sc_args((char *)(base+4096),64)<0)return -5;
    if(sc_call(0x281,(int)base,4096,1,0,0,0))return -6;
    if(sc_args((char *)base,64)!=-1)return -7;
    if(sc_call(0x281,(int)base,4096,3,0,0,0))return -8;
    if(sc_args((char *)base,64)<0)return -9;
    if(sc_call(0x282,(int)base,0,0,0,0,0))return -10;
    if(sc_call(0x282,(int)base,0,0,0,0,0)!=-1)return -11;
    return ProbeFreePages()==before?0:-12;
}
static void fault_exit(unsigned int *context)
{(void)context;sc_call(0,14,0,0,0,0,0);for(;;){}}
void ProbeFaultExit(void)
{
    sc_handler(14,fault_exit);
    *(volatile unsigned int *)0x1000=1;
}
