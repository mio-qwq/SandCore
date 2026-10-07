#include "SCIO.H"
/* 外部SYSTEM控制者只在自己的测试桌面创建窗口，真实PF发生在私有
 * 子任务。隐藏/可见清理用同一个窗口和程序，最后恢复原桌面。 */
static int failures,window=-1;
static u32 own,original;
static void check(int okay,const char *text)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,text);cli_text(1,"\n");if(!okay)failures++;}
static void metric(const char *name,int value)
{cli_text(1,name);cli_text(1,"=");cli_number(1,value);cli_text(1,"\n");}
static void pause_ticks(u32 ticks)
{u32 began=(u32)sc_tick();while((u32)sc_tick()-began<ticks)sc_event_wait(SC_EVENT_JOB|SC_EVENT_INPUT,1);}
static int visibility(u32 out[8])
{return sc_visibility(window,out);}
static void paint(void)
{
    u32 state[8];int bounds[5];
    if(visibility(state)<0 || !state[1] || sc_info(window,bounds)<0)return;
    sc_fill_rgb(window,0,0,bounds[2],bounds[3],SC_RGB_PAPER);
    sc_fill_rgb(window,0,0,bounds[2],4,SC_RGB_ACCENT);
    sc_text_rgb(window,24,24,"Fault cleanup",SC_RGB_INK);
    sc_text_rgb(window,24,64,"Hidden and visible desktop paths",SC_RGB_ACCENT);
    sc_text_rgb(window,24,108,"Physical HMP keyboard verification",SC_RGB_INK);
}
static int stage(const char *label,int key)
{
    u32 state[8],page[32];if(visibility(state)<0 || sc_session_page(page,32,0xFFFFFFFFu)<0)return -1;
    cli_text(1,"M10MODAL_STAGE ");cli_text(1,label);cli_text(1," session=");cli_number(1,(int)page[6]);
    cli_text(1," window=");cli_number(1,window);cli_text(1," focus=");cli_number(1,(int)state[5]);cli_text(1,"\n");
    int received=-1;
    if(key>=0){u32 began=(u32)sc_tick();
        do{received=sc_key();if(received>=0)break;pause_ticks(1);}while((u32)sc_tick()-began<200u);
        cli_text(1,"modal_key_");cli_text(1,label);cli_text(1,"=");cli_number(1,received);cli_text(1,"\n");}
    /* 管理串口LF只作阶段握手；HMP键必须由真实键盘入口独立取到。 */
    u8 byte;if(cli_read(0,&byte,1)!=1 || byte!='\n')return -1;
    return key<0 || received==key?0:-1;
}
static int worker(void)
{
    u32 self[8],page[32],ack[4];
    if(sc_process_self(self)<0 || sc_session_page(page,32,0xFFFFFFFFu)<0)return 2;
    ack[0]=1;ack[1]=self[1];ack[2]=self[2];ack[3]=page[5];
    if(cli_write(1,ack,sizeof(ack))!=(int)sizeof(ack))return 3;
    sc_stream_close(1,0);sc_stream_close(2,0);
    u8 byte;if(cli_read(0,&byte,1)!=0)return 4;
    *(volatile u32 *)0x1000=1;return 99;
}
static int fault_cycle(int hidden)
{
    int gate[2]={-1,-1},ack[2]={-1,-1},job=-1,result=-1;u32 pid=0,generation=0,state[8];
    if(sc_stream_pipe(gate)<0 || sc_stream_pipe(ack)<0)goto done;
    int fds[3]={gate[0],ack[1],ack[1]};job=sc_spawn2("/TMP/M10MODAL.SCX fault",fds,0);
    if(job<1 || sc_job_info2((u32)job,state)!=1)goto done;
    pid=state[3];generation=state[4];u32 identity[4];
    if(cli_read(ack[0],identity,sizeof(identity))!=(int)sizeof(identity)
        || identity[0]!=1 || identity[1]!=pid || identity[2]!=generation || identity[3]!=own)goto done;
    sc_stream_close(gate[1],0);gate[1]=-1;
    u32 began=(u32)sc_tick();int paused=0;
    do{int status=sc_job_info2((u32)job,state);if(status==2){paused=1;break;}if(status<=0)break;
        pause_ticks(1);}while((u32)sc_tick()-began<2000u);
    if(!paused)goto done;
    check(1,hidden?"hidden child actually faults and pauses in its own SYSTEM session":
                   "visible child actually faults and pauses in its own SYSTEM session");
    if(stage(hidden?"hidden-fault":"visible-fault",-1)<0)goto done;
    if(sc_kill_generation((int)pid,generation)<0)goto done;
    began=(u32)sc_tick();int status;
    do{status=sc_wait2((u32)job,state);if(status<=0)break;pause_ticks(1);}while((u32)sc_tick()-began<2000u);
    if(status)goto done;job=-1;
    /* wait票据完成先于地址空间末尾回收；至少让出真实服务轮，
     * 仍按原2000tick界限。窗口/卡片清理在JOB完成之前完成。 */
    pause_ticks(2);
    if(sc_session_control(own,0)<0)goto done;
    u32 view[8];if(visibility(view)<0)goto done;paint();
    metric(hidden?"modal_hidden_focus":"modal_visible_focus",(int)view[5]);
    check(view[1] && view[5],hidden?"hidden fault cleanup restores focus when its desktop returns":
                                  "visible fault cleanup restores focus on the same desktop");
    int input=stage(hidden?"hidden-clean":"visible-clean",hidden?'b':'c');
    check(input==0,hidden?"hidden fault cleanup accepts its real HMP key":
                          "visible fault cleanup accepts its real HMP key");
    result=0;
done:
    if(job>0){if(!pid && sc_job_info2((u32)job,state)>0){pid=state[3];generation=state[4];}
        if(pid)sc_kill_generation((int)pid,generation);
        u32 began=(u32)sc_tick();while(sc_wait2((u32)job,state)>0 && (u32)sc_tick()-began<2000u)pause_ticks(1);}
    for(int i=0;i<2;i++){if(gate[i]>=0)sc_stream_close(gate[i],0);if(ack[i]>=0)sc_stream_close(ack[i],0);}
    return result;
}
int main(void)
{
    int argc=cli_parse();if(argc==1 && equal(cli_argv[0],"fault"))return worker();
    if(argc!=1 || !equal(cli_argv[0],"controller"))return 2;
    u32 identity[8],page[32];if(sc_auth_info(identity)<0 || !identity[4]
        || sc_session_page(page,32,0xFFFFFFFFu)<0)return 2;
    own=page[5];original=page[6];if(!own || !original || own==original)return 2;
    window=sc_open_rgb("Fault cleanup",640,280);if(window<0)return 2;
    u32 state[8];if(visibility(state)<0)goto done;
    check(!state[1],"external SYSTEM test window starts hidden from ordinary desktop");
    if(sc_session_control(own,0)<0)goto done;paint();
    if(visibility(state)<0)goto done;check(state[1] && state[5],"same test window has focus before any child fault");
    check(stage("baseline",'a')==0,"baseline accepts its real HMP key");
    if(fault_cycle(0)<0)goto done;
    /* 先收齐可见清理正对照。若隐藏路径有模态残留，被阻挡的b键
     * 可能留在输入队列，不能拿它污染随后的独立可见路径对照。 */
    if(sc_session_control(original,0)<0 || fault_cycle(1)<0)goto done;
    sc_session_control(original,0);sc_call(0x11,window,0,0,0,0,0);window=-1;
    if(sc_session_page(page,32,0xFFFFFFFFu)<0)goto done;
    check(page[6]==original,"original ordinary desktop is restored after both cleanup paths");
    metric("modal_failures",failures);return failures?1:0;
done:
    if(original)sc_session_control(original,0);if(window>=0)sc_call(0x11,window,0,0,0,0,0);
    cli_text(1,"FAIL modal fixture could not complete its declared stages\n");return 1;
}
