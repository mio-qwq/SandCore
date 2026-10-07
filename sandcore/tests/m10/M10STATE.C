#include "SCIO.H"
/* 131只是本轮证据规模；用真实管道屏障让孩子同时存活，分别触发
 * 环境COW/凭据/异常栈、调试现场及宿主编译的SIMD状态。只走公开API。 */
typedef struct {u32 job,pid,generation,reaped,index,debug_event;} child_t;
typedef struct {u32 owners,records,sides,free_pages;} ledger_t;
static child_t *children;
static u32 count,child_index;
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static void number(const char *name,u32 n)
{cli_text(1,name);cli_number(1,(int)n);cli_text(1,"\n");}
static int ledger(ledger_t *out)
{
    u32 page[32],memory[48];if(sc_process_page(page,32,0xFFFFFFFFu)<0 || sc_monitor(memory)<0)return -1;
    if(memory[4]%4096u || page[15]<page[14])return -1;
    out->owners=page[5];out->records=page[14];out->sides=page[15]-page[14];out->free_pages=memory[4]/4096u;return 0;
}
static int settle(ledger_t *out,const ledger_t *before)
{
    u32 began=(u32)sc_tick();do{
        if(ledger(out)<0)return -1;
        if(out->owners==before->owners && out->sides==before->sides)return 0;
        sc_event_wait(SC_EVENT_JOB,10);
    }while((u32)sc_tick()-began<6000u);return -1;
}
static int warm_hidden_view(void)
{
    /* 故障卡按所属会话准备view。这个SYSTEM测试首次创建自己的
     * 隐藏窗口，显式留下属于会话的桌面/主题/排序缓存；关窗后的
     * 缓存冷暖成本单列，不能把它归到后来退出的孩子身上。 */
    int window=sc_open_rgb("State QA",128,80);if(window<0)return -1;
    u32 visible[8];int status=sc_visibility(window,visible);
    sc_call(0x11,window,0,0,0,0,0);
    return status==0 && visible[0]==1 && !visible[1]?0:-1;
}
static void callback(u32 *context)
{
    char expected[16],value[32];u32 self[8],auth[8];decimal(expected,(int)child_index);
    int okay=context[12]==14 && sc_user(4,"M10_CHILD",value,sizeof(value))>=0 && equal(value,expected)
        && sc_auth_info(auth)==0 && sc_process_self(self)==0 && auth[1]==self[3] && auth[3]==self[5];
    sc_call(0,okay?14:77,0,0,0,0,0);for(;;)sc_yield();
}
static int worker(int unhandled,u32 index,int foreign)
{
    u32 self[8]={0},auth[8]={0},frame[24],ack[8];char value[32],expected[16];
    int okay=sc_process_self(self)==0 && sc_auth_info(auth)==0;
    okay=okay && sc_user(4,"M10_PARENT",value,sizeof(value))>=0 && equal(value,"parent")
        && sc_user(4,"M10_CHILD",value,sizeof(value))==-2;
    decimal(expected,(int)index);okay=okay && sc_env_set("M10_CHILD",expected,0)==0
        && sc_env_set("UID","31337",0)==0 && sc_auth_info(auth)==0 && auth[1]==self[3] && auth[3]==self[5];
    if(foreign>0){frame[22]=0x57A7E001u;frame[23]=0x57A7E002u;
        okay=okay && sc_debug(foreign,0,0,frame,88)==-1 && frame[22]==0x57A7E001u && frame[23]==0x57A7E002u;}
    child_index=index;if(!unhandled)okay=okay && sc_handler(14,callback)==0;
    ack[0]=okay?1u:77u;ack[1]=self[1];ack[2]=self[2];ack[3]=index;
    ack[4]=self[3];ack[5]=self[5];ack[6]=auth[4];ack[7]=0;
    if(cli_write(1,ack,sizeof(ack))!=(int)sizeof(ack))return 78;
    /* 每条ACK32B、4096B管道容量均是32的倍数，父亲逐条完整消费。
     * 关掉stdout订阅，后建孩子的ACK不会惊醒全部已等待的兄弟。 */
    sc_stream_close(1,0);sc_stream_close(2,0);
    u8 byte;int result=cli_read(0,&byte,1);if(result!=0)return 79;
    *(volatile u32 *)0x1000=1;return 99;
}
static int read_ack(int fd,const u32 identity[8])
{
    u32 ack[8];if(cli_read(fd,ack,sizeof(ack))!=(int)sizeof(ack) || ack[3]>=count)return -1;
    child_t *child=children+ack[3];
    if(child->index || ack[0]!=1 || ack[1]!=child->pid || ack[2]!=child->generation
        || ack[4]!=identity[1] || ack[5]!=identity[3] || ack[6]!=identity[4])return -1;
    child->index=1;return 0;
}
static int wait_children(int expected)
{
    u32 began=(u32)sc_tick();for(;;){u32 pending=0;
        for(u32 i=0;i<count;i++){child_t *child=children+i;if(child->reaped)continue;u32 state[8];int status=sc_wait2(child->job,state);
            if(!status){child->reaped=1;if((int)state[2]!=expected)return -1;}
            else if(status<0)return -1;else pending++;}
        if(!pending)return 0;if((u32)sc_tick()-began>=6000u)return -1;sc_event_wait(SC_EVENT_JOB,10);
    }
}
static void cleanup(void)
{
    for(u32 i=0;i<count;i++)if(!children[i].reaped){child_t *child=children+i;
        /* 创建成功就归属清理表；即使首次查询失败，也先重新获取该
         * 作业的真实身份，再按代数清理，不能使用未初始化PID。 */
        if(!child->pid){u32 state[8];if(sc_job_info2(child->job,state)>0){child->pid=state[3];child->generation=state[4];}}
        if(child->pid)sc_kill_generation((int)child->pid,child->generation);}
    u32 began=(u32)sc_tick();for(;;){u32 pending=0;
        for(u32 i=0;i<count;i++)if(!children[i].reaped){u32 state[8];int status=sc_wait2(children[i].job,state);
            if(status<=0)children[i].reaped=1;else pending++;}
        if(!pending || (u32)sc_tick()-began>=6000u)break;sc_event_wait(SC_EVENT_JOB,10);}
}
static int simultaneous(void)
{
    u32 page[SC_PROCESS_PAGE_WORDS],cursor=0xFFFFFFFFu,found=0;
    do{
        if(sc_process_page(page,SC_PROCESS_PAGE_WORDS,cursor)<0)return -1;
        for(u32 row=0;row<page[3];row++){u32 *item=page+16+16*row;
            for(u32 i=0;i<count;i++)if(children[i].pid==item[0] && children[i].generation==item[2] && item[1]==1)found++;}
        cursor=page[4];
    }while(cursor!=0xFFFFFFFFu);
    return found==count?0:-1;
}
static int reuse(int *input,int output,const int fds[3],const u32 identity[8])
{
    int job=sc_spawn2("/TMP/M10STATE.SCX unhandled 0 0",fds,0);u32 state[8],ack[8];
    if(job<1)return -1;
    if(sc_job_info2((u32)job,state)<1){
        /* 重试仅为取得清理身份，原查询失败仍返回失败。 */
        if(sc_job_info2((u32)job,state)>0){sc_kill_generation((int)state[3],state[4]);sc_wait2((u32)job,state);}
        return -1;}
    u32 pid=state[3],generation=state[4],old=0;for(u32 i=0;i<count;i++)if(children[i].pid==pid){old=children[i].generation;break;}
    int okay=old && old!=generation && cli_read(output,ack,sizeof(ack))==(int)sizeof(ack)
        && ack[0]==1 && ack[1]==pid && ack[2]==generation && ack[3]==0
        && ack[4]==identity[1] && ack[5]==identity[3] && ack[6]==identity[4];
    u32 context[24];context[22]=0x57A7E003u;context[23]=0x57A7E004u;
    okay=okay && sc_debug((int)pid,0,0,context,88)==-1 && context[22]==0x57A7E003u && context[23]==0x57A7E004u
        && sc_kill_generation((int)pid,old)==-8;
    for(u32 i=0;i<count;i++)if(children[i].pid==pid){u32 obsolete[8];okay=okay && sc_job_info2(children[i].job,obsolete)<0;}
    sc_stream_close(*input,0);*input=-1;u32 began=(u32)sc_tick();int paused=0;
    do{int status=sc_job_info2((u32)job,state);if(status==2){paused=1;break;}if(status<=0)break;
        sc_event_wait(SC_EVENT_JOB,10);}while((u32)sc_tick()-began<2000u);
    check(okay && paused,"reused PID has fresh identity environment and no inherited debugger or fault callback");
    sc_kill_generation((int)pid,generation);began=(u32)sc_tick();int status;
    do{status=sc_wait2((u32)job,state);if(status<=0)break;sc_event_wait(SC_EVENT_JOB,10);}while((u32)sc_tick()-began<6000u);
    return okay && paused && !status?0:-1;
}
static int batch(int debug,int simd)
{
    u32 identity[8];ledger_t cold,timer,before,peak,after;int gate[2]={-1,-1},ack[2]={-1,-1};count=0;
    if(sc_auth_info(identity)<0 || sc_env_set("M10_PARENT","parent",0)<0)return 1;
    children=sc_alloc(131u*sizeof(child_t));if(!children)return 1;
    /* 先实际建立/关闭管道，预热可归属的最小对象页，不拿首次缓存
     * 或PID历史页增长冒充泄漏；仍输出冷/暖真实PF差分。 */
    int warm[2];if(sc_stream_pipe(warm)<0)goto fail;sc_stream_close(warm[0],0);sc_stream_close(warm[1],0);
    /* MONITOR空闲字段的单位是字节，记录页字段才是页。真实执行一次
     * 有限等待，让内核首次建立的最小一页定时等待堆进入明确暖基点；
     * wait_remove只缓存这一页，大堆归还。冷暖差分仍完整输出，不能
     * 把全局一页缓存当作131孩子泄漏或把字节和页直接相加。 */
    if(ledger(&cold)<0 || sc_event_wait(0,1)<0)goto fail;
    if(ledger(&timer)<0)goto fail;
    number("state_cold_free=",cold.free_pages);number("state_timer_warm_free=",timer.free_pages);
    check(timer.free_pages<=cold.free_pages && cold.free_pages-timer.free_pages<=1u
          && timer.owners==cold.owners && timer.records==cold.records && timer.sides==cold.sides,
          "minimal timer wait cache costs at most one actual page before child baseline");
    if(warm_hidden_view()<0 || ledger(&before)<0 || warm_hidden_view()<0 || ledger(&after)<0)goto fail;
    number("state_view_cold_free=",timer.free_pages);number("state_view_warm_free=",before.free_pages);
    check(after.free_pages==before.free_pages && after.owners==before.owners
          && after.records==before.records && after.sides==before.sides,
          "hidden session view cache is separately measured and stable across second actual open close");
    if(sc_stream_pipe(gate)<0 || sc_stream_pipe(ack)<0)goto fail;
    int fds[3]={gate[0],ack[1],ack[1]};int okay=1;u32 entry=0,event=0;u8 opcode=0;
    for(u32 i=0;i<131u;i++){
        child_t *child=children+i;child->job=child->pid=child->generation=child->reaped=child->index=child->debug_event=0;
        char command[112],text[16];
        copy(command,simd?"/TMP/M10SIMD.SCX ":"/TMP/M10STATE.SCX worker ",sizeof(command));
        decimal(text,(int)i);append(command,text,sizeof(command));
        if(!simd){append(command," ",sizeof(command));decimal(text,i?(int)children[0].pid:0);append(command,text,sizeof(command));}
        int job=debug?sc_dbgspawn2(command,fds):sc_spawn2(command,fds,0);u32 state[8];
        if(job<1)goto fail;child->job=(u32)job;count++;
        if(sc_job_info2(child->job,state)<1)goto fail;
        child->pid=state[3];child->generation=state[4];
        if(debug){u32 context[24];context[22]=0x57A7E005u;context[23]=0x57A7E006u;
            if(sc_debug((int)child->pid,0,0,context,88)<0 || context[19]!=3
                || context[22]!=0x57A7E005u || context[23]!=0x57A7E006u)goto fail;
            child->debug_event=context[21];
            if(!i){entry=context[14];event=context[21];}}
        else if(read_ack(ack[0],identity)<0)goto fail;
    }
    number("state_created=",count);
    if(debug){
        check(count==131,"131 real debug targets paused before first instruction with exact 88B contexts");
        if(sc_debug((int)children[0].pid,3,entry,&opcode,1)<0 || sc_debug((int)children[0].pid,4,entry,0,0)<0)goto fail;
        u8 replaced;if(sc_debug((int)children[0].pid,3,entry,&replaced,1)<0 || replaced!=0xCC)goto fail;
        if(sc_debug((int)children[0].pid,2,0,0,0)<0)goto fail;
        u32 context[22],began=(u32)sc_tick();int stopped=0;
        do{if(sc_debug((int)children[0].pid,0,0,context,88)==0 && context[21]!=event){stopped=1;break;}
            sc_event_wait(SC_EVENT_JOB,10);}while((u32)sc_tick()-began<2000u);
        if(!stopped || context[12]!=3 || context[14]!=entry)goto fail;
        for(u32 i=1;i<count;i++){
            u32 sibling[22];if(sc_debug((int)children[i].pid,0,0,sibling,88)<0
                || sibling[19]!=3 || sibling[14]!=entry || sibling[21]!=children[i].debug_event)goto fail;}
        check(1,"INT3 executes while all 130 sibling targets retain their paused entry and event");
        if(sc_debug((int)children[0].pid,5,entry,0,0)<0 || sc_debug((int)children[0].pid,1,0,0,0)<0)goto fail;
        event=context[21];began=(u32)sc_tick();stopped=0;
        do{if(sc_debug((int)children[0].pid,0,0,context,88)==0 && context[21]!=event){stopped=1;break;}
            sc_event_wait(SC_EVENT_JOB,10);}while((u32)sc_tick()-began<2000u);
        if(!stopped || context[12]!=1 || context[14]==entry || sc_debug((int)children[0].pid,2,0,0,0)<0)goto fail;
        check(1,"real x86 single step resumes exactly one instruction after breakpoint removal");
        for(u32 i=1;i<count;i++)if(sc_debug((int)children[i].pid,2,0,0,0)<0)goto fail;
        for(u32 i=0;i<count;i++)if(read_ack(ack[0],identity)<0)goto fail;
    }
    if(simultaneous()<0 || ledger(&peak)<0)goto fail;
    char value[32];okay=sc_user(4,"M10_CHILD",value,sizeof(value))==-2
        && sc_user(4,"M10_PARENT",value,sizeof(value))>=0 && equal(value,"parent");
    check(okay && count==131,"131 simultaneous private task identities environments and ready acknowledgements");
    number("state_free_before=",before.free_pages);number("state_free_peak=",peak.free_pages);
    sc_stream_close(gate[1],0);gate[1]=-1;
    if(wait_children(simd?0:14)<0)goto fail;
    check(1,simd?"all 131 SIMD states preserve XMM7 MXCSR and x87 across real preemption":
                 "all 131 private fault callbacks receive real page faults and retained environments");
    sc_stream_close(gate[0],0);gate[0]=-1;sc_stream_close(ack[1],0);ack[1]=-1;sc_stream_close(ack[0],0);ack[0]=-1;
    if(settle(&after,&before)<0)goto fail;
    check(after.free_pages+(after.records-before.records)==before.free_pages,
          "all state-owned physical pages return with only committed PID history retained");
    number("state_free_after=",after.free_pages);number("state_records_before=",before.records);number("state_records_after=",after.records);
    if(!simd){
        if(sc_stream_pipe(gate)<0 || sc_stream_pipe(ack)<0)goto fail;
        fds[0]=gate[0];fds[1]=fds[2]=ack[1];
        if(reuse(&gate[1],ack[0],fds,identity)<0)goto fail;
        sc_stream_close(gate[0],0);gate[0]=-1;sc_stream_close(ack[1],0);ack[1]=-1;sc_stream_close(ack[0],0);ack[0]=-1;
        if(settle(&after,&before)<0)goto fail;
        number("state_reuse_free_after=",after.free_pages);number("state_reuse_records_after=",after.records);
        check(after.free_pages+(after.records-before.records)==before.free_pages,
              "PID reuse fault cleanup also restores exact state-owned physical pages");
    }
    sc_free(children);children=0;return failures?1:0;
fail:
    for(int i=0;i<2;i++){if(gate[i]>=0)sc_stream_close(gate[i],0);if(ack[i]>=0)sc_stream_close(ack[i],0);}
    cleanup();if(children)sc_free(children);children=0;return 1;
}
int main(void)
{
    if(cli_parse()<0 || !cli_argc)return 2;
    if((equal(cli_argv[0],"worker") || equal(cli_argv[0],"unhandled")) && cli_argc==3){int index,foreign;
        if(cli_integer(cli_argv[1],&index)<0 || index<0 || cli_integer(cli_argv[2],&foreign)<0)return 2;
        return worker(equal(cli_argv[0],"unhandled"),(u32)index,foreign);}
    if(cli_argc!=1)return 2;
    if(equal(cli_argv[0],"environments"))return batch(0,0);
    if(equal(cli_argv[0],"debug"))return batch(1,0);
    if(equal(cli_argv[0],"simd"))return batch(0,1);
    return 2;
}
