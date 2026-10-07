#include "SCIO.H"
#include "NUI.inc"
/* 通过真实登录票据、私有管道、窗口和可见性入口检查会话。SYSTEM
 * 控制者必须来自外部管理串口，孩子只取得普通身份；没有测试内核
 * 开关。NUI使用与Notes等工具相同的原绘制代码，计数在实际提交后
 * 取样。这里的3/65是夹具规模，不是系统对象或每用户数量上限。 */
#define PACKET_WORDS 32
#define PACKET_MAGIC 0x3153534Du
typedef struct {int pid,input,output;u32 sample[PACKET_WORDS];} agent_t;
static agent_t agents[3];
static u32 packet[PACKET_WORDS],request[4],sequence;
static int failures,system_window=-1;

static void check(int okay,const char *text)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,text);cli_text(1,"\n");if(!okay)failures++;}
static void metric(const char *name,u32 value)
{cli_text(1,name);cli_number(1,(int)value);cli_text(1,"\n");}
static void pause_ticks(u32 ticks)
{u32 start=(u32)sc_tick();while((u32)sc_tick()-start<ticks)sc_event_wait(SC_EVENT_STREAM|SC_EVENT_JOB,10);}
static int read_exact(int fd,void *buffer,u32 bytes)
{
    u32 at=0,start=(u32)sc_tick();
    while(at<bytes){int count=sc_stream_read(fd,(u8 *)buffer+at,bytes-at);
        if(count==-6){if((u32)sc_tick()-start>2000u)return -1;sc_event_wait(SC_EVENT_STREAM,10);continue;}
        if(count<=0)return -1;at+=(u32)count;}
    return 0;
}
static int authenticate(const char *name)
{
    u32 started=(u32)sc_tick(),info[8];int ticket;
    /* 空口令只用于已有外部管理员获准的身份启动，不声称验证普通
     * 密码登录。冷却仍遵守原内核规则，不能改成隐藏快速验收入口。 */
    do{ticket=sc_auth_login(name,"");if(ticket!=-6)break;pause_ticks(10);}while((u32)sc_tick()-started<2000u);
    if(ticket<=0)return -1;
    for(;;){int result=sc_auth_status((u32)ticket,info);if(result==0)return ticket;
        if(result!=1 || (u32)sc_tick()-started>2000u)return -1;pause_ticks(10);}
}
static u32 own_session(void)
{u32 page[32];return sc_session_page(page,32,0xFFFFFFFFu)>=0?page[5]:0;}
static void paint_agent(u32 uid,int keys,int last)
{
    ui_pointer();ui_header("Login desktop", "Independent windows, input and background work");
    ui_panel(24,88,UI_W-48,160);
    ui_text(44,110,"USER",PAL_UI_MUTED);ui_number(160,110,(int)uid,PAL_UI_TEXT);
    ui_text(44,148,"KEY EVENTS",PAL_UI_MUTED);ui_number(160,148,keys,PAL_UI_TEXT);
    ui_text(44,186,"LAST KEY",PAL_UI_MUTED);ui_number(160,186,last,PAL_UI_TEXT);
    ui_footer("This window belongs to one login session.");ui_present();ui_followup=0;
}
static int worker(int idle)
{
    u32 self[10],identity[10],background=0;int keys=0,last=-1;
    self[8]=identity[8]=0xA1510001;self[9]=identity[9]=0xA1510002;
    if(sc_process_self(self)<0 || sc_auth_info(identity)<0 || self[8]!=0xA1510001 || self[9]!=0xA1510002
        || identity[8]!=0xA1510001 || identity[9]!=0xA1510002)return 1;
    /* 原生标题栏随150%桌面变为36px；48px外框只剩11px客户区，
     * 会按既有最小客户区规则拒绝。计数夹具用真实可创建的小窗口，
     * 不修改内核尺寸合同，也不让65份NUI整帧缓冲耗尽测试内存。 */
    int window=idle?sc_open_rgb("Session count",128,96):ui_open("Login desktop");if(window<0)return 2;
    if(idle){for(int fd=0;fd<3;fd++)sc_stream_close(fd,0);
        for(;;){u32 visibility[8];if(sc_visibility(window,visibility)<0)return 3;sc_event_wait(SC_EVENT_VISIBILITY,20);}}
    u32 session=own_session();if(!session)return 3;
    for(u32 i=0;i<PACKET_WORDS;i++)packet[i]=0;
    packet[0]=PACKET_MAGIC;packet[1]=1;packet[4]=self[1];packet[5]=self[2];
    packet[6]=identity[1];packet[7]=identity[2];packet[8]=identity[3];packet[9]=identity[4];
    packet[10]=session;packet[11]=(u32)window;
    if(cli_write(1,packet,sizeof(packet))!=(int)sizeof(packet))return 4;
    for(;;){
        background++;
        if(ui_visibility_sample()){
            int key=sc_key();if(key>=0){keys++;last=key;ui_followup=1;}
            if(ui_followup)paint_agent(identity[1],keys,last);
        }
        int count=sc_stream_read(0,request,sizeof(request));
        if(count==0)return 0;
        if(count==-6){sc_event_wait(SC_EVENT_STREAM|SC_EVENT_INPUT|SC_EVENT_VISIBILITY,10);continue;}
        if(count!=(int)sizeof(request))return 5;
        for(u32 i=18;i<PACKET_WORDS;i++)packet[i]=0;
        packet[2]=request[0];packet[3]=request[1];packet[12]=(u32)ui_visible;packet[13]=ui_visibility_epoch;
        packet[14]=(u32)ui_frames;packet[15]=background;packet[16]=(u32)keys;packet[17]=(u32)last;
        if(request[0]==2)packet[18]=(u32)sc_session_control(request[2],request[3]);
        else if(request[0]==3)packet[18]=(u32)sc_call(0x11,(int)request[2],0,0,0,0,0);
        else if(request[0]==4){u32 capture[8];int token=sc_capture_open((int)request[2],capture);packet[18]=(u32)token;
            if(token>0)sc_capture_close((u32)token);}
        else if(request[0]==5){u32 page[34];page[32]=0xA1510003;page[33]=0xA1510004;
            int rows=sc_session_page(page,32,0xFFFFFFFFu);packet[18]=(u32)rows;packet[19]=page[5];
            packet[20]=page[6];packet[21]=page[32];packet[22]=page[33];}
        else if(request[0]==6){char path[66];for(int i=0;i<66;i++)path[i]='!';
            const char *leaf=request[2]?"../THEME.CFG":"THEME.CFG";
            int result=sc_session_path(leaf,path,64);packet[18]=(u32)result;
            packet[19]=(u32)(u8)path[64];packet[20]=(u32)(u8)path[65];
            if(request[2]){for(int i=0;i<64;i++)if(path[i]!='!')packet[21]=1;}
            else {for(int i=0;i<40 && path[i];i++)((char *)(packet+22))[i]=path[i];}}
        else if(request[0]==7)packet[18]=(u32)sc_window(window,(int)request[2]);
        else if(request[0]!=1)return 6;
        if(cli_write(1,packet,sizeof(packet))!=(int)sizeof(packet))return 7;
    }
}
static int spawn_agent(agent_t *agent,int ticket)
{
    int input[2]={-1,-1},output[2]={-1,-1};agent->pid=agent->input=agent->output=-1;
    /* 旧公开描述符域是0..15；全局端点动态并不扩大单任务旧域。
     * 13..15只暂存本控制者stdio，不占用前两代理的管道端点。 */
    if(sc_stream_dup(0,13)<0 || sc_stream_dup(1,14)<0 || sc_stream_dup(2,15)<0)return -1;
    if(sc_stream_pipe(input)<0 || sc_stream_pipe(output)<0)return -1;
    if(sc_stream_dup(input[0],0)<0 || sc_stream_dup(output[1],1)<0 || sc_stream_dup(output[1],2)<0)return -1;
    agent->pid=sc_auth_exec((u32)ticket,"/TMP/M10SESS.SCX agent",0);
    sc_stream_dup(13,0);sc_stream_dup(14,1);sc_stream_dup(15,2);
    for(int fd=13;fd<16;fd++)sc_stream_close(fd,0);
    sc_stream_close(input[0],0);sc_stream_close(output[1],0);
    agent->input=input[1];agent->output=output[0];
    if(agent->pid<1 || read_exact(agent->output,agent->sample,sizeof(agent->sample))<0)return -1;
    return agent->sample[0]==PACKET_MAGIC && (int)agent->sample[4]==agent->pid?0:-1;
}
static int query(agent_t *agent,u32 operation,u32 first,u32 second)
{
    u32 command[4];command[0]=operation;command[1]=++sequence;command[2]=first;command[3]=second;
    if(cli_write(agent->input,command,sizeof(command))!=(int)sizeof(command)
        || read_exact(agent->output,agent->sample,sizeof(agent->sample))<0)return -1;
    return agent->sample[0]==PACKET_MAGIC && agent->sample[2]==operation && agent->sample[3]==sequence?0:-1;
}
static int await_key(agent_t *agent,u32 expected,int last)
{
    u32 start=(u32)sc_tick();
    /* HMP按键和管理LF分属硬件输入/串口队列。应用可能正在首次
     * 绘制，LF不能冒充按键已经消费的确认；只等实际计数，不送假键。
     * 多键/错误键仍立即失败，正常读取必须在同一有界期限内完成。 */
    do{if(query(agent,1,0,0)<0)return -1;
        if(agent->sample[16]>=expected)return agent->sample[16]==expected && (int)agent->sample[17]==last?0:-1;
        pause_ticks(1);
    }while((u32)sc_tick()-start<200u);
    return -1;
}
static u32 canvas_hash(int window)
{
    u32 info[8],buffer[1024],hash=2166136261u;int token=sc_capture_open(window,info);if(token<1)return 0;
    for(u32 offset=0;offset<info[6];){u32 bytes=info[6]-offset;if(bytes>sizeof(buffer))bytes=sizeof(buffer);
        if(sc_capture_read((u32)token,offset,buffer,bytes)!=(int)bytes){sc_capture_close((u32)token);return 0;}
        for(u32 i=0;i<bytes;i++){hash^=((u8 *)buffer)[i];hash*=16777619u;}offset+=bytes;}
    if(sc_capture_close((u32)token)<0)return 0;return hash;
}
static int stage(const char *label,u32 session,int window)
{
    char line[256],digits[12];copy(line,"M10SESS_STAGE ",sizeof(line));append(line,label,sizeof(line));
    append(line," session=",sizeof(line));decimal(digits,(int)session);append(line,digits,sizeof(line));
    append(line," window=",sizeof(line));decimal(digits,window);append(line,digits,sizeof(line));
    append(line,"\n",sizeof(line));cli_text(1,line);char answer;
    return cli_read(0,&answer,1)==1 && answer=='\n'?0:-1;
}
static void stop_agent(agent_t *agent)
{
    if(agent->sample[10])sc_session_control(agent->sample[10],1);
    if(agent->input>=0)sc_stream_close(agent->input,0);if(agent->output>=0)sc_stream_close(agent->output,0);
    agent->input=agent->output=-1;
}
static int controller(void)
{
    u32 identity[8],page[34],original=0;if(sc_auth_info(identity)<0 || identity[4]!=1)return 1;
    if(sc_permissions("/TMP/M10SESS.SCX",-2,-2,63)<0)return 1;
    page[32]=0xA1510005;page[33]=0xA1510006;
    if(sc_session_page(page,32,0xFFFFFFFFu)<0)return 1;original=page[6];u32 system=page[5];
    check(page[32]==0xA1510005 && page[33]==0xA1510006,"session page preserves capacity boundary");
    system_window=sc_open_rgb("SYSTEM QA",320,140);u32 visibility[8];
    check(system_window>0 && sc_visibility(system_window,visibility)==0 && !visibility[1],"external SYSTEM window starts hidden from mio");
    for(int i=0;i<3;i++){agents[i].pid=agents[i].input=agents[i].output=-1;agents[i].sample[10]=0;}
    int ticket=authenticate("mio");if(ticket<1)goto fail;
    if(spawn_agent(agents,ticket)<0 || spawn_agent(agents+1,ticket)<0)goto fail;
    ticket=authenticate("root");if(ticket<1 || spawn_agent(agents+2,ticket)<0)goto fail;
    check(agents[0].sample[6]==1000 && agents[1].sample[6]==1000 && agents[2].sample[6]==0
        && agents[0].sample[10]!=agents[1].sample[10],"same UID logins own different desktops and root stays ordinary");
    for(int i=0;i<3;i++)check(agents[i].sample[8]==0 && agents[i].sample[9]==0 && !agents[i].sample[12]
        && agents[i].sample[14]==0,"ordinary child has no SYSTEM realm and no hidden initial frames");
    if(stage("initial",original,system_window)<0)goto fail;
    if(sc_session_control(agents[0].sample[10],0)<0 || query(agents,1,0,0)<0)goto fail;
    if(stage("mio-first",agents[0].sample[10],(int)agents[0].sample[11])<0)goto fail;
    int first_key=await_key(agents,1,97);metric("mio_first_keys=",agents[0].sample[16]);metric("mio_first_last=",agents[0].sample[17]);
    check(agents[0].sample[12] && agents[0].sample[14]>0 && agents[0].sample[16]==1 && agents[0].sample[17]==97,"first mio session receives only its real HMP key");
    if(first_key<0)goto fail;
    if(stage("mio-first-capture",agents[0].sample[10],(int)agents[0].sample[11])<0)goto fail;
    u32 frames=agents[0].sample[14],background=agents[0].sample[15],hash=canvas_hash((int)agents[0].sample[11]);
    if(!hash || sc_session_control(agents[1].sample[10],0)<0)goto fail;
    if(stage("mio-second",agents[1].sample[10],(int)agents[1].sample[11])<0)goto fail;
    pause_ticks(50);if(query(agents,1,0,0)<0 || query(agents+1,1,0,0)<0)goto fail;
    check(!agents[0].sample[12] && agents[0].sample[14]==frames && agents[0].sample[15]>background+2u
        && canvas_hash((int)agents[0].sample[11])==hash,"hidden NUI submits zero new frames and preserves canvas while background advances");
    check(agents[1].sample[16]==1 && agents[1].sample[17]==98 && agents[0].sample[16]==1,"same UID sessions do not share keyboard events");
    if(stage("mio-second-capture",agents[1].sample[10],(int)agents[1].sample[11])<0)goto fail;
    if(query(agents,4,agents[1].sample[11],0)<0)goto fail;
    check((int)agents[0].sample[18]==-5,"same UID foreign desktop capture is denied");
    if(query(agents,3,agents[1].sample[11],0)<0)goto fail;
    /* 旧WINCLOSE固定返回0，不能为验收悄悄改ABI。拒绝的证据是
     * 外部管理者仍能打开原窗口的真实快照，而非改造旧返回码。 */
    check(agents[0].sample[18]==0 && canvas_hash((int)agents[1].sample[11])!=0,
        "foreign close preserves window and original WINCLOSE return ABI");
    if(query(agents,2,system,0)<0 || query(agents+2,2,system,0)<0)goto fail;
    check((int)agents[0].sample[18]==-5 && (int)agents[2].sample[18]==-5,"mio and ordinary root cannot display SYSTEM");
    if(query(agents+2,4,(u32)system_window,0)<0)goto fail;
    check((int)agents[2].sample[18]==-5,"ordinary root cannot capture SYSTEM window");
    if(query(agents,6,1,0)<0 || query(agents+2,6,1,0)<0)goto fail;
    check((int)agents[0].sample[18]<0 && !agents[0].sample[21] && agents[0].sample[19]=='!'
        && agents[0].sample[20]=='!' && (int)agents[2].sample[18]<0 && !agents[2].sample[21],"session path traversal fails without modifying output");
    if(sc_session_control(agents[2].sample[10],0)<0)goto fail;
    if(stage("root",agents[2].sample[10],(int)agents[2].sample[11])<0)goto fail;
    int root_key=await_key(agents+2,1,99);metric("root_keys=",agents[2].sample[16]);metric("root_last=",agents[2].sample[17]);
    check(agents[2].sample[6]==0 && agents[2].sample[8]==0 && !agents[2].sample[9]
        && agents[2].sample[16]==1 && agents[2].sample[17]==99,"root desktop receives its key without SYSTEM rights");
    if(root_key<0)goto fail;
    if(stage("root-capture",agents[2].sample[10],(int)agents[2].sample[11])<0)goto fail;
    if(sc_session_control(system,0)<0 || sc_visibility(system_window,visibility)<0)goto fail;
    u32 theme[32];if(sc_theme(theme)<0)return 1;
    sc_fill_rgb(system_window,0,0,320,140,theme[8+SC_THEME_PAPER]);
    sc_text_rgb(system_window,24,28,"SYSTEM management",theme[8+SC_THEME_TEXT]);
    if(stage("system",system,system_window)<0)goto fail;
    check(sc_key()==113,"explicit external switch displays SYSTEM and routes only its key");
    if(stage("system-capture",system,system_window)<0)goto fail;
    if(sc_session_control(agents[0].sample[10],0)<0)goto fail;
    if(stage("mio-return",agents[0].sample[10],(int)agents[0].sample[11])<0)goto fail;
    int return_key=await_key(agents,2,100);metric("mio_return_keys=",agents[0].sample[16]);metric("mio_return_last=",agents[0].sample[17]);
    check(agents[0].sample[14]>frames && agents[0].sample[16]==2 && agents[0].sample[17]==100,
        "returning desktop redraws and has no leaked old root SYSTEM keys");
    if(return_key<0)goto fail;
    if(stage("mio-return-capture",agents[0].sample[10],(int)agents[0].sample[11])<0)goto fail;
    if(query(agents,7,0,0)<0 || query(agents,1,0,0)<0)goto fail;
    frames=agents[0].sample[14];background=agents[0].sample[15];pause_ticks(50);
    if(query(agents,1,0,0)<0)goto fail;
    check(!agents[0].sample[12] && agents[0].sample[14]==frames && agents[0].sample[15]>background+2u,
        "minimized NUI stops frame generation and background continues");
    if(query(agents,7,2,0)<0 || query(agents,1,0,0)<0)goto fail;
    check(agents[0].sample[12] && agents[0].sample[14]>frames,"restored window redraws at current visibility generation");
    u32 retired=agents[1].sample[10],generation=agents[1].sample[5];int retired_pid=agents[1].pid;
    stop_agent(agents+1);pause_ticks(50);
    check(sc_session_control(retired,0)==-5 && query(agents,1,0,0)==0 && agents[0].sample[6]==1000,
        "logging out one mio session preserves the other session identity");
    ticket=authenticate("mio");if(ticket<1 || spawn_agent(agents+1,ticket)<0)goto fail;
    check(agents[1].sample[10]!=retired && sc_session_control(retired,0)==-5
        && (agents[1].pid!=retired_pid || agents[1].sample[5]!=generation),"new login cannot inherit retired session ID or PID generation");
    if(sc_session_control(original,0)<0)goto fail;
    for(int i=0;i<3;i++)stop_agent(agents+i);pause_ticks(50);
    sc_call(0x11,system_window,0,0,0,0,0);system_window=-1;
    check(sc_session_page(page,32,0xFFFFFFFFu)>=0 && page[6]==original,"original mio desktop is restored after cleanup");
    metric("session_failures=",(u32)failures);return failures?1:0;
fail:
    sc_session_control(original,0);for(int i=0;i<3;i++)stop_agent(agents+i);
    if(system_window>=0)sc_call(0x11,system_window,0,0,0,0,0);
    check(0,"session probe operation completed before timeout");return 1;
}
static int many(void)
{
    int pids[65];u32 ids[65],handles[65],page[528],start=(u32)sc_tick(),identity[8],memory[48];
    u32 before[34],after[34],free_before;
    if(sc_auth_info(identity)<0 || identity[4]!=1 || sc_permissions("/TMP/M10SESS.SCX",-2,-2,63)<0)return 1;
    before[32]=after[32]=0xA1510007;before[33]=after[33]=0xA1510008;
    if(sc_monitor(memory)<0 || sc_process_page(before,32,0xFFFFFFFFu)<0)return 1;
    free_before=memory[4]/4096u;metric("many_free_before=",free_before);
    metric("many_owned_before=",before[5]);metric("many_record_pages_before=",before[14]);
    metric("many_side_pages_before=",before[15]-before[14]);
    int ticket=authenticate("mio"),created=0;if(ticket<1)return 1;
    for(int i=0;i<65;i++){ids[i]=handles[i]=0;pids[i]=-1;}
    for(;created<65;created++){pids[created]=sc_auth_exec((u32)ticket,"/TMP/M10SESS.SCX idle",0);if(pids[created]<1)break;}
    u32 found=0;
    do{
        found=0;u32 cursor=0xFFFFFFFFu;
        do{if(sc_window_page(page,528,cursor)<0)goto cleanup;
            for(u32 row=0;row<page[3];row++){u32 *entry=page+16+row*16;
                for(int i=0;i<created;i++)if((u32)pids[i]==entry[1]){ids[i]=entry[3];handles[i]=entry[0];}}
            cursor=page[4];}while(cursor!=0xFFFFFFFFu);
        for(int i=0;i<created;i++)if(ids[i] && handles[i])found++;
        if(found<(u32)created)pause_ticks(10);
    }while(found<(u32)created && (u32)sc_tick()-start<3000u);
    metric("many_created=",(u32)created);metric("many_ready=",found);
    check(created==65 && found==65,"65 ordinary same UID login desktops and windows coexist");
    int unique=1;for(int i=0;i<created;i++)for(int j=0;j<i;j++)if(ids[i]==ids[j])unique=0;
    check(unique,"each of 65 real login processes owns a unique session");
    u32 cursor=0xFFFFFFFFu,listed=0,foreign=0;
    do{if(sc_session_page(page,528,cursor)<0)goto cleanup;
        for(u32 row=0;row<page[3];row++){u32 *entry=page+16+row*16;
            for(int i=0;i<created;i++)if(ids[i]==entry[0]){listed++;if(entry[1]!=1000 || entry[3]!=0 || entry[4])foreign++;}}
        cursor=page[4];}while(cursor!=0xFFFFFFFFu);
    check(listed==65 && !foreign,"paged session query includes all 65 ordinary hidden desktops");
    if(sc_monitor(memory)<0)goto cleanup;metric("many_free_peak=",memory[4]/4096u);
    for(int i=0;i<created;i++){
        if(ids[i]){if(sc_session_control(ids[i],1)<0)failures++;}
        else if(pids[i]>0)sc_kill2(pids[i]);
    }
    /* 没取得真实会话ID时，拒绝ID0不能证明65个注销对象已失效。 */
    pause_ticks(100);int stale=created==65 && found==65;
    for(int i=0;i<created;i++)if(sc_session_control(ids[i],0)!=-5)stale=0;
    check(stale,"all 65 retired session IDs are invalid after logout");
    if(sc_monitor(memory)<0 || sc_process_page(after,32,0xFFFFFFFFu)<0)return 1;
    metric("many_free_after=",memory[4]/4096u);metric("many_owned_after=",after[5]);
    metric("many_record_pages_after=",after[14]);metric("many_side_pages_after=",after[15]-after[14]);
    check(before[32]==0xA1510007 && before[33]==0xA1510008 && after[32]==0xA1510007 && after[33]==0xA1510008,
        "session resource samples preserve public page buffer guards");
    check(before[5]==after[5] && before[15]-before[14]==after[15]-after[14],
        "all 65 task owners and dynamic side pages return to baseline");
    /* 旧快照保留已提交PID的历史正文。冷增长后增加的页只有真实
     * records缓存能解释；暖重复仍增加页会失败，不能把所有差额
     * 统称缓存来掩盖窗口、会话或身份旁表泄漏。 */
    check(after[14]>=before[14] && free_before>=memory[4]/4096u
        && free_before-memory[4]/4096u==after[14]-before[14],
        "physical page delta equals only retained committed PID history pages");
    metric("session_failures=",(u32)failures);return failures?1:0;
cleanup:
    for(int i=0;i<created;i++){if(ids[i])sc_session_control(ids[i],1);else if(pids[i]>0)sc_kill2(pids[i]);}
    check(0,"65 session probe finished before the fixture timeout");return 1;
}
int main(void)
{
    if(cli_parse()<1)return 2;
    if(equal(cli_argv[0],"agent"))return worker(0);
    if(equal(cli_argv[0],"idle"))return worker(1);
    if(equal(cli_argv[0],"controller"))return controller();
    if(equal(cli_argv[0],"many"))return many();
    if(equal(cli_argv[0],"many-cycle")){if(many())return 1;return many();}
    return 2;
}
