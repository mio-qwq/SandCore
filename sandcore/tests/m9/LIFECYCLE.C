#include "SCIO.H"
/* 真正客体窗口/截图配额与代数：退出故意留下最后一个快照，由内核回收。
 * 不引入测试syscall；父测试从真实页计数核对正常/异常/任务槽复用。 */
static int failures;
static void check(int okay,const char *name)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,name);cli_text(1,"\n");if(!okay)failures++;}
static void exception_exit(u32 *context)
{
    check(context[12]==6,"actual UD2 enters user exception handler");
    sc_call(0,failures?1:23,0,0,0,0,0);for(;;)sc_yield();
}
int main(void)
{
    if(cli_parse()<0 || cli_argc>1)return 2;
    int window=sc_open_rgb("Snapshot QA",128,80);check(window>=0,"native window allocated");if(window<0)return 1;
    int dimensions[5];if(sc_info(window,dimensions)<0)return 1;
    u32 n=(u32)dimensions[2]*dimensions[3],*pixels=sc_alloc(n*4);if(!pixels)return 1;
    for(u32 i=0;i<n;i++)pixels[i]=0xFF000000u|SC_RGB_PAPER;
    check(sc_frame32(window,pixels,n*4)==0,"actual native pixels submitted");sc_free(pixels);
    u32 guarded[10];for(int i=0;i<10;i++)guarded[i]=0x1941B07Du;
    int tokens[8];for(int i=0;i<8;i++)tokens[i]=sc_capture_open(window,guarded+1);
    int all=1;for(int i=0;i<8;i++)if(tokens[i]<=0)all=0;
    check(all && guarded[0]==0x1941B07Du && guarded[9]==0x1941B07Du,"eight snapshots and 32 byte guard");
    check(sc_capture_open(window,guarded+1)<0,"snapshot quota rejects ninth");
    u32 bytes=guarded[7];u8 output[18];for(int i=0;i<18;i++)output[i]=0xA5;
    check(sc_capture_read(tokens[0],0,output+1,16)==16 && output[0]==0xA5 && output[17]==0xA5,"snapshot byte buffer bounds");
    check(sc_capture_read(tokens[0],bytes,output,1)==0 && sc_capture_read(tokens[0],bytes+1,output,1)<0,"snapshot exact EOF and past EOF");
    check(sc_capture_read(tokens[0],0,(void *)0x1000,16)<0,"snapshot unmapped output denied");
    int old=tokens[0];check(sc_capture_close(old)==0,"snapshot close");
    tokens[0]=sc_capture_open(window,guarded+1);check(tokens[0]>0 && tokens[0]!=old,"new token after slot reuse");
    check(sc_capture_read(old,0,output,1)<0 && sc_capture_close(old)<0,"stale snapshot token rejected");
    for(int i=0;i<7;i++)check(sc_capture_close(tokens[i])==0,"explicit snapshot reclaim");
    if(cli_argc && equal(cli_argv[0],"fault")){
        check(sc_call(9,6,(int)exception_exit,0,0,0,0)==0,"UD2 handler registered");
        cli_text(1,"FAULT READY\n");
        /* UD2属于真实三环异常，剩余窗口和第八个快照由任务退出回收。 */
        __asm__ __volatile__("ud2":::"memory");
    }
    return failures;
}
