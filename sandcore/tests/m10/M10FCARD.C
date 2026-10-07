#include "SCIO.H"
/* 两个普通登录桌面、三个真实PF。外部SYSTEM仅控制登录/切换，
 * 异常和键盘读取都发生在普通身份里；2是夹具人数，不是产品上限。 */
#define MAGIC 0x3143464Du
typedef struct {int pid,input,output;u32 sample[32];} agent_t;
static agent_t agents[2];
static int failures;
static u32 original;
static void check(int okay,const char *text)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,text);cli_text(1,"\n");if(!okay)failures++;}
static void pause_ticks(u32 ticks)
{u32 start=(u32)sc_tick();while((u32)sc_tick()-start<ticks)sc_event_wait(SC_EVENT_STREAM|SC_EVENT_JOB,1);}
static int read_exact(int fd,void *buffer,u32 size)
{
    u32 at=0,start=(u32)sc_tick();
    while(at<size){int n=sc_stream_read(fd,(u8 *)buffer+at,size-at);
        if(n==-6){if((u32)sc_tick()-start>=2000u)return -1;pause_ticks(1);continue;}
        if(n<=0)return -1;at+=(u32)n;}
    return 0;
}
static int fault_worker(void)
{
    u32 self[8],page[32],identity[8],ack[5];
    if(sc_process_self(self)<0 || sc_session_page(page,32,0xFFFFFFFFu)<0 || sc_auth_info(identity)<0)return 2;
    ack[0]=MAGIC;ack[1]=self[1];ack[2]=self[2];ack[3]=page[5];ack[4]=identity[1];
    if(cli_write(1,ack,sizeof(ack))!=(int)sizeof(ack))return 3;
    sc_stream_close(1,0);sc_stream_close(2,0);
    u8 byte;if(cli_read(0,&byte,1)!=0)return 4;
    *(volatile u32 *)0x1000=1;return 99;
}
static int arm(int *job,u32 own,u32 uid)
{
    int gate[2]={-1,-1},reply[2]={-1,-1},result=-1;u32 info[8],ack[5];
    if(*job>0 || sc_stream_pipe(gate)<0 || sc_stream_pipe(reply)<0)goto done;
    int fds[3]={gate[0],reply[1],reply[1]};*job=sc_spawn2("/TMP/M10FCARD.SCX fault",fds,0);
    if(*job<=0 || sc_job_info2((u32)*job,info)!=1 || read_exact(reply[0],ack,sizeof(ack))<0
        || ack[0]!=MAGIC || ack[1]!=info[3] || ack[2]!=info[4] || ack[3]!=own || ack[4]!=uid)goto done;
    sc_stream_close(gate[1],0);gate[1]=-1;
    u32 start=(u32)sc_tick();
    do{int state=sc_job_info2((u32)*job,info);if(state==2){result=0;break;}
        if(state<=0)break;pause_ticks(1);}while((u32)sc_tick()-start<2000u);
done:
    for(int i=0;i<2;i++){if(gate[i]>=0)sc_stream_close(gate[i],0);if(reply[i]>=0)sc_stream_close(reply[i],0);}
    return result;
}
static int drop(int *job)
{
    if(*job<=0)return -1;u32 info[8];if(sc_job_info2((u32)*job,info)<=0)return -1;
    if(sc_kill_generation((int)info[3],info[4])<0)return -1;
    u32 start=(u32)sc_tick();int state;
    do{state=sc_wait2((u32)*job,info);if(state<=0)break;pause_ticks(1);}while((u32)sc_tick()-start<2000u);
    if(state)return -1;*job=-1;pause_ticks(2);return 0;
}
static void paint(int window)
{
    u32 visible[8];int bounds[5];
    if(sc_visibility(window,visible)<0 || !visible[1] || sc_info(window,bounds)<0)return;
    sc_fill_rgb(window,0,0,bounds[2],bounds[3],SC_RGB_PAPER);
    sc_fill_rgb(window,24,24,4,36,SC_RGB_ACCENT);
    sc_text_rgb(window,44,28,"Independent fault desktop",SC_RGB_INK);
    sc_text_rgb(window,24,86,"Multiple cards. Ordinary users. Real keys.",SC_RGB_ACCENT);
}
static int agent(void)
{
    u32 self[8],page[32],identity[8],packet[32],command[3],state[8];int jobs[2]={-1,-1};
    if(sc_process_self(self)<0 || sc_session_page(page,32,0xFFFFFFFFu)<0 || sc_auth_info(identity)<0)return 2;
    int window=sc_open_rgb("Fault desktop",720,260);if(window<0)return 3;
    for(int i=0;i<32;i++)packet[i]=0;
    packet[0]=MAGIC;packet[1]=1;packet[2]=self[1];packet[3]=self[2];packet[4]=identity[1];packet[5]=identity[3];
    packet[6]=page[5];packet[7]=(u32)window;packet[23]=identity[4];
    if(cli_write(1,packet,sizeof(packet))!=(int)sizeof(packet))goto done;
    for(;;){
        if(read_exact(0,command,sizeof(command))<0 || !command[0])break;
        int result=0,key=-1;
        if(command[0]==2)result=command[1]<2?arm(jobs+command[1],page[5],identity[1]):-1;
        else if(command[0]==3)result=command[1]<2?drop(jobs+command[1]):-1;
        else if(command[0]==4){u32 start=(u32)sc_tick();
            do{key=sc_key();if(key>=0)break;pause_ticks(1);}while((u32)sc_tick()-start<200u);
            result=key==(int)command[1]?0:-1;
        }else if(command[0]!=1)result=-1;
        paint(window);u32 visible[8];if(sc_visibility(window,visible)<0)break;
        packet[8]=visible[1];packet[9]=visible[5];packet[20]=(u32)result;packet[21]=(u32)key;packet[24]=command[2];
        for(int i=0;i<2;i++){int status=jobs[i]>0?sc_job_info2((u32)jobs[i],state):-1;
            packet[10+i]=(u32)status;packet[12+i]=status>0?state[3]:0;packet[14+i]=status>0?state[4]:0;}
        if(cli_write(1,packet,sizeof(packet))!=(int)sizeof(packet))break;
    }
done:
    for(int i=0;i<2;i++)if(jobs[i]>0)drop(jobs+i);
    sc_call(0x11,window,0,0,0,0,0);return 0;
}
static int authenticate(const char *name)
{
    u32 start=(u32)sc_tick(),info[8];int ticket;
    /* 空口令只走已获外部管理员权限的公开登录合同，不声称验证密码。 */
    do{ticket=sc_auth_login(name,"");if(ticket!=-6)break;pause_ticks(10);}while((u32)sc_tick()-start<2000u);
    if(ticket<=0)return -1;
    for(;;){int result=sc_auth_status((u32)ticket,info);if(!result)return ticket;
        if(result!=1 || (u32)sc_tick()-start>=2000u){sc_auth_cancel((u32)ticket);return -1;}pause_ticks(1);}
}
static int spawn_agent(agent_t *a,int ticket)
{
    int input[2]={-1,-1},output[2]={-1,-1};
    if(sc_stream_dup(0,13)<0 || sc_stream_dup(1,14)<0 || sc_stream_dup(2,15)<0)return -1;
    if(sc_stream_pipe(input)<0 || sc_stream_pipe(output)<0)return -1;
    if(sc_stream_dup(input[0],0)<0 || sc_stream_dup(output[1],1)<0 || sc_stream_dup(output[1],2)<0)return -1;
    a->pid=sc_auth_exec((u32)ticket,"/TMP/M10FCARD.SCX agent",0);
    sc_stream_dup(13,0);sc_stream_dup(14,1);sc_stream_dup(15,2);
    for(int fd=13;fd<16;fd++)sc_stream_close(fd,0);
    sc_stream_close(input[0],0);sc_stream_close(output[1],0);a->input=input[1];a->output=output[0];
    if(a->pid<1 || read_exact(a->output,a->sample,sizeof(a->sample))<0)return -1;
    return a->sample[0]==MAGIC && (int)a->sample[2]==a->pid?0:-1;
}
static int query(agent_t *a,u32 operation,u32 slot)
{
    static u32 serial;u32 command[3];command[0]=operation;command[1]=slot;command[2]=++serial;
    if(cli_write(a->input,command,sizeof(command))!=(int)sizeof(command)
        || read_exact(a->output,a->sample,sizeof(a->sample))<0)return -1;
    return a->sample[0]==MAGIC && a->sample[24]==serial?(int)a->sample[20]:-1;
}
static int stage(const char *name,agent_t *a)
{
    u32 page[32];if(query(a,1,0)<0 || sc_session_page(page,32,0xFFFFFFFFu)<0)return -1;
    cli_text(1,"M10FCARD_STAGE ");cli_text(1,name);cli_text(1," session=");cli_number(1,(int)page[6]);
    cli_text(1," window=");cli_number(1,(int)a->sample[7]);cli_text(1," focus=");cli_number(1,(int)a->sample[9]);cli_text(1,"\n");
    char answer;return cli_read(0,&answer,1)==1 && answer=='\n'?0:-1;
}
static void cleanup(void)
{
    if(original)sc_session_control(original,0);
    for(int i=0;i<2;i++){agent_t *a=agents+i;if(a->sample[6])sc_session_control(a->sample[6],1);
        if(a->input>=0)sc_stream_close(a->input,0);if(a->output>=0)sc_stream_close(a->output,0);}
}
static int controller(void)
{
    for(int i=0;i<2;i++){agents[i].pid=agents[i].input=agents[i].output=-1;agents[i].sample[6]=0;}
    u32 identity[8],page[32];if(sc_auth_info(identity)<0 || identity[4]!=1
        || sc_session_page(page,32,0xFFFFFFFFu)<0 || sc_permissions("/TMP/M10FCARD.SCX",-2,-2,63)<0)return 2;
    original=page[6];int ticket=authenticate("mio");
    if(ticket<1 || spawn_agent(agents,ticket)<0)goto fail;sc_auth_cancel((u32)ticket);
    ticket=authenticate("root");if(ticket<1 || spawn_agent(agents+1,ticket)<0)goto fail;sc_auth_cancel((u32)ticket);
    agent_t *a=agents,*b=agents+1;
    check(a->sample[4]==1000 && b->sample[4]==0 && !a->sample[5] && !b->sample[5]
        && !a->sample[23] && !b->sample[23] && a->sample[6]!=b->sample[6],
        "mio and root fault agents have ordinary identities and separate sessions");
    if(sc_session_control(a->sample[6],0)<0 || query(a,2,0)<0 || query(a,2,1)<0)goto fail;
    check(a->sample[10]==2 && a->sample[11]==2 && !a->sample[9],"two real fault children coexist and block only their desktop");
    if(stage("a-two-faults",a)<0 || query(a,3,0)<0)goto fail;
    check((int)a->sample[10]<0 && a->sample[11]==2 && !a->sample[9],"removing the older nonhead card preserves the newer modal fault");
    if(stage("a-one-fault",a)<0 || sc_session_control(b->sample[6],0)<0 || query(b,2,0)<0)goto fail;
    check(b->sample[10]==2 && b->sample[8] && !b->sample[9],"another ordinary user has its own real visible modal fault");
    if(stage("b-fault",b)<0 || query(a,3,1)<0 || query(b,1,0)<0)goto fail;
    check(!a->sample[8] && (int)a->sample[11]<0 && b->sample[10]==2 && !b->sample[9],
        "hidden last card cleanup preserves the other user's visible modal fault");
    if(stage("b-after-hidden-a-clean",b)<0 || sc_session_control(a->sample[6],0)<0 || query(a,1,0)<0)goto fail;
    check(a->sample[8] && a->sample[9],"returning to the cleaned ordinary desktop restores its focus");
    if(stage("a-clean",a)<0)goto fail;
    check(query(a,4,'d')==0 && a->sample[21]=='d',"cleaned mio desktop receives only its real HMP d key");
    if(sc_session_control(b->sample[6],0)<0 || query(b,1,0)<0)goto fail;
    check(b->sample[10]==2 && !b->sample[9],"switching back preserves the other user's outstanding fault");
    if(stage("b-still-fault",b)<0 || query(b,3,0)<0)goto fail;
    check(b->sample[8] && b->sample[9] && (int)b->sample[10]<0,"last visible root fault cleanup restores its own focus");
    if(stage("b-clean",b)<0)goto fail;
    check(query(b,4,'e')==0 && b->sample[21]=='e',"cleaned root desktop receives only its real HMP e key");
    cleanup();pause_ticks(2);if(sc_session_page(page,32,0xFFFFFFFFu)<0)goto fail;
    check(page[6]==original && sc_session_control(a->sample[6],0)==-5 && sc_session_control(b->sample[6],0)==-5,
        "both temporary sessions retire and the original desktop is restored");
    return failures?1:0;
fail:
    cleanup();cli_text(1,"FAIL multicard fixture could not complete declared stages\n");return 1;
}
int main(void)
{
    if(cli_parse()!=1)return 2;
    if(equal(cli_argv[0],"fault"))return fault_worker();
    if(equal(cli_argv[0],"agent"))return agent();
    return equal(cli_argv[0],"controller")?controller():2;
}
