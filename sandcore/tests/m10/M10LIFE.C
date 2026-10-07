#include "SCIO.H"
/* 只用公开三环入口，不在内核设置测试模式。131是本次并发证据规模，
 * 16384是耗尽用例的时间/输出预算；达到预算而没耗尽必须报告未完成，
 * 不能把它变成系统任务上限。每个存活孩子另持有一个真实私有管道，
 * 因而任务/作业之外也同时超过旧全局端点与管道数量。 */
typedef struct {
    u32 job,pid,generation,ack,found,running,dispatch,delta_running,delta_dispatch;
    int reaped;
} child_record;
typedef struct {u32 owned,records,metadata,free_pages,total;} resource_sample;
static child_record *children;
static u32 *order,count,capacity;
static int failures;
static volatile u32 work_value;
static u32 legacy_buffer[522];

static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static void number(const char *label,u32 value)
{cli_text(1,label);cli_number(1,(int)value);cli_text(1,"\n");}
static int expired(u32 began,u32 timeout)
{return (u32)sc_tick()-began>=timeout;}
static int resources(resource_sample *out)
{
    u32 page[34],memory[50];page[32]=memory[48]=0xA1100001;page[33]=memory[49]=0xA1100002;
    if(sc_process_page(page,32,0xFFFFFFFFu)<0 || page[0]!=1 || page[32]!=0xA1100001 || page[33]!=0xA1100002
        || sc_monitor(memory)<0 || memory[48]!=0xA1100001 || memory[49]!=0xA1100002 || page[15]<page[14])return -1;
    out->owned=page[5];out->records=page[14];out->metadata=page[15]-page[14];out->free_pages=memory[4]/4096u;out->total=page[8];return 0;
}
static int reserve(u32 needed)
{
    if(needed<=capacity)return 0;u32 next=capacity?capacity:32;
    while(next<needed){if(next>0x7FFFFFFFu/2)return -1;next*=2;}
    if(next>0x7FFFFFFFu/sizeof(child_record))return -1;
    child_record *fresh=sc_alloc(next*sizeof(child_record));if(!fresh)return -1;
    u32 *indices=sc_alloc(next*sizeof(u32));if(!indices){sc_free(fresh);return -1;}
    for(u32 i=0;i<count;i++)fresh[i]=children[i];
    if(children)sc_free(children);if(order)sc_free(order);children=fresh;order=indices;capacity=next;return 0;
}
static void sift(u32 at,u32 size)
{
    u32 value=order[at];while(at<size/2){u32 next=at*2+1;
        if(next+1<size && children[order[next+1]].pid>children[order[next]].pid)next++;
        if(children[order[next]].pid<=children[value].pid)break;order[at]=order[next];at=next;}
    order[at]=value;
}
static int index_tasks(void)
{
    /* ACK按创建索引直接匹配；公开页按PID二分查独立排序索引。耗尽
     * 用例不能把数千任务的逐行核对写成每次全表的平方扫描。 */
    for(u32 i=0;i<count;i++)order[i]=i;
    for(u32 i=count/2;i;i--)sift(i-1,count);
    for(u32 i=count;i>1;i--){u32 swap=order[0];order[0]=order[i-1];order[i-1]=swap;sift(0,i-1);}
    for(u32 i=1;i<count;i++)if(children[order[i-1]].pid==children[order[i]].pid)return -1;
    return 0;
}
static child_record *find(u32 pid)
{
    u32 low=0,high=count;while(low<high){u32 mid=low+(high-low)/2,id=children[order[mid]].pid;
        if(id<pid)low=mid+1;else high=mid;}
    return low<count && children[order[low]].pid==pid?children+order[low]:0;
}
static int collect(int difference)
{
    u32 page[SC_PROCESS_PAGE_WORDS+2],cursor=0xFFFFFFFFu,last=0;int have_last=0;
    for(u32 i=0;i<count;i++)children[i].found=0;
    do{
        page[SC_PROCESS_PAGE_WORDS]=0xA1100003;page[SC_PROCESS_PAGE_WORDS+1]=0xA1100004;
        int rows=sc_process_page(page,SC_PROCESS_PAGE_WORDS,cursor);
        if(rows<0 || page[0]!=1 || page[1]!=16 || page[2]!=16 || page[3]>(SC_PROCESS_PAGE_WORDS-16)/16 || (u32)rows!=page[3]
            || page[SC_PROCESS_PAGE_WORDS]!=0xA1100003 || page[SC_PROCESS_PAGE_WORDS+1]!=0xA1100004)return -1;
        for(u32 i=0;i<page[3];i++){
            u32 *row=page+16+i*16;if(have_last && row[0]<=last)return -1;last=row[0];have_last=1;
            child_record *child=find(row[0]);if(!child)continue;
            if(child->found || child->generation!=row[2] || row[1]!=1 || !row[11])return -1;
            child->found=1;if(difference){child->delta_running=row[5]-child->running;child->delta_dispatch=row[6]-child->dispatch;}
            child->running=row[5];child->dispatch=row[6];
        }
        u32 next=page[4];if(next!=0xFFFFFFFFu && (!page[3] || next!=last || (cursor!=0xFFFFFFFFu && next<=cursor)))return -1;cursor=next;
    }while(cursor!=0xFFFFFFFFu);
    for(u32 i=0;i<count;i++)if(!children[i].found)return -1;return 0;
}
static int receive_ack(int fd,child_record *child,u32 index)
{
    u32 began=(u32)sc_tick(),ack[4],job[8];
    for(;;){int result=sc_stream_read(fd,ack,sizeof(ack));
        if(result==sizeof(ack)){
            if(ack[1]!=child->pid || ack[2]!=child->generation || ack[3]!=index || child->ack)return -20;
            child->ack=ack[0];return ack[0]==1?0:(int)ack[0];
        }
        if(result!=-6)return -20;
        if(sc_job_info2(child->job,job)!=1 || expired(began,1000))return -110;
        sc_event_wait(SC_EVENT_STREAM|SC_EVENT_JOB,10);
    }
}
static int worker(int busy,u32 index)
{
    u32 self[8],ack[4];int pipe[2],private_pipe=sc_stream_pipe(pipe);
    if(sc_process_self(self)<0)return 41;
    ack[0]=private_pipe<0?(u32)private_pipe:1u;ack[1]=self[1];ack[2]=self[2];ack[3]=index;
    /* 所有生产者只写16字节，消费者每次只读16字节。初始4096字节
     * 余量和每次增减均为16的倍数，故本夹具不会产生交错的半条ACK。
     * 这是本协议约束，不宣称一般管道已有POSIX PIPE_BUF保证。 */
    u32 began=(u32)sc_tick();for(;;){int result=sc_stream_write(1,ack,sizeof(ack));
        if(result==sizeof(ack))break;if(result!=-6 || expired(began,1000))return 42;sc_event_wait(SC_EVENT_STREAM,10);}
    /* ACK已经交给管道，孩子不再输出。撤它的stdout/stderr订阅，
     * 避免后建孩子的ACK反复唤醒所有旧等待者；测试的空闲任务
     * 应只等自己的stdin EOF，而非人为制造全局惊群/超时。 */
    sc_stream_close(1,0);sc_stream_close(2,0);
    work_value=index+1;
    for(;;){u8 byte;int result=sc_stream_read(0,&byte,1);if(!result)break;if(result!=-6)return 43;
        if(busy){for(int i=0;i<512;i++)work_value=work_value*1664525u+1013904223u;}
        else sc_event_wait(SC_EVENT_STREAM,0xFFFFFFFFu);
    }
    if(private_pipe>=0){sc_stream_close(pipe[0],0);sc_stream_close(pipe[1],0);}return 0;
}
static int start(child_record *child,u32 index,int busy,const int fds[3])
{
    child->job=child->pid=child->generation=child->ack=0;child->reaped=0;
    char command[96],text[12];copy(command,"/TMP/M10LIFE.SCX ",sizeof(command));append(command,busy?"work ":"hold ",sizeof(command));
    decimal(text,(int)index);append(command,text,sizeof(command));int job=sc_spawn2(command,fds,0);if(job<0)return job;
    u32 state[8];child->job=(u32)job;child->ack=0;child->reaped=0;
    if(sc_job_info2((u32)job,state)!=1 || !state[3] || !state[4])return -21;
    child->pid=state[3];child->generation=state[4];return 0;
}
static int reap(child_record *rows,u32 size,u32 timeout)
{
    u32 began=(u32)sc_tick();for(;;){u32 pending=0;
        for(u32 i=0;i<size;i++){if(rows[i].reaped)continue;u32 state[8];int result=sc_wait2(rows[i].job,state);
            if(!result){rows[i].reaped=1;if(state[2])return -22;}
            else if(result<0)return -23;else pending++;}
        if(!pending)return 0;if(expired(began,timeout))return -110;sc_event_wait(SC_EVENT_JOB,10);
    }
}
static int settle(resource_sample *after,const resource_sample *before)
{
    /* JOB完成先于最后几步地址空间回收。实际拥有对象回到原值后再读
     * 页数，不能只见stdout EOF或wait退出码就声称物理页已经归还。 */
    u32 began=(u32)sc_tick();do{
        if(resources(after)<0)return -1;if(after->owned==before->owned && after->metadata==before->metadata)return 0;
        sc_event_wait(SC_EVENT_JOB,10);
    }while(!expired(began,6000));return -110;
}
static void legacy(void)
{
    legacy_buffer[520]=0xA1100005;legacy_buffer[521]=0xA1100006;
    check(sc_processes2(legacy_buffer,520)>=0 && legacy_buffer[520]==0xA1100005 && legacy_buffer[521]==0xA1100006,"old PROCESSINFO2 exact 520 words");
    legacy_buffer[200]=0xA1100005;legacy_buffer[201]=0xA1100006;
    check(sc_cpu2(legacy_buffer)==0 && legacy_buffer[7]==32 && legacy_buffer[200]==0xA1100005 && legacy_buffer[201]==0xA1100006,"old CPUINFO2 exact 200 words");
    legacy_buffer[64]=0xA1100005;legacy_buffer[65]=0xA1100006;
    check(sc_call(0x79,(int)legacy_buffer,-1,-1,-1,-1,-1)==0 && legacy_buffer[8]==8
        && legacy_buffer[64]==0xA1100005 && legacy_buffer[65]==0xA1100006,"old CPUINFO exact 64 words and ignored registers");
    legacy_buffer[48]=0xA1100005;legacy_buffer[49]=0xA1100006;
    check(sc_process_page(legacy_buffer,49,0xFFFFFFFFu)>=0 && legacy_buffer[48]==0xA1100005 && legacy_buffer[49]==0xA1100006,"PROCESSPAGE incomplete row is untouched");
    check(sc_process_page(legacy_buffer,31,0xFFFFFFFFu)==-1 && sc_process_page(legacy_buffer,1041,0xFFFFFFFFu)==-1
        && sc_process_page((u32 *)0xFFFFFFFCu,32,0xFFFFFFFFu)==-1,"PROCESSPAGE validates capacity and pointer");
}
static int batch(u32 wanted,int busy,u32 ticks,int exhaust)
{
    count=0;if(reserve(wanted)<0)return cli_error("fixture heap allocation",-12);
    resource_sample before,peak,after;if(resources(&before)<0)return cli_error("resource snapshot",-1);
    int gate[2],ack[2];if(sc_stream_pipe(gate)<0)return cli_error("fixture gate",-4);
    if(sc_stream_pipe(ack)<0){sc_stream_close(gate[0],0);sc_stream_close(gate[1],0);return cli_error("fixture ACK pipe",-4);}
    int fds[3]={gate[0],ack[1],ack[1]},create_failure=0,pipe_failures=0,protocol_failure=0;
    /* 分开记录真实创建/等待/枚举/回收成本。已有公平用例整项包含
     * 串行启动，不把数分钟总耗时误当成600tick计算区间或猜硬件原因。 */
    u32 phase_began=(u32)sc_tick(),create_calls=0,ack_waits=0,ack_max=0;
    for(;count<wanted;){
        u32 began=(u32)sc_tick();child_record *child=children+count;int result=start(child,count,busy,fds);
        create_calls+=(u32)sc_tick()-began;
        if(result<0){if(child->job){count++;protocol_failure=result;}create_failure=result;break;}
        began=(u32)sc_tick();result=receive_ack(ack[0],child,count);count++;
        u32 elapsed=(u32)sc_tick()-began;ack_waits+=elapsed;if(elapsed>ack_max)ack_max=elapsed;
        if(result<0){if(exhaust && child->ack && child->ack!=1)pipe_failures++;else {protocol_failure=result;break;}}
    }
    number("phase_create_ticks=",(u32)sc_tick()-phase_began);
    number("create_call_ticks=",create_calls);number("ack_wait_ticks=",ack_waits);number("ack_wait_max=",ack_max);
    phase_began=(u32)sc_tick();
    number("created=",count);number("private_pipe_failures=",(u32)pipe_failures);cli_text(1,"create_failure=");cli_number(1,create_failure);cli_text(1,"\n");
    check(!protocol_failure,"each child ran and acknowledged its PID generation");
    if(exhaust)check(create_failure<0 && count>64,"real child creation failed before fixture budget");
    else check(count==wanted && !create_failure && !pipe_failures,"requested simultaneous tasks jobs endpoints and pipes");
    if(index_tasks()<0){check(0,"distinct simultaneous child PID values");protocol_failure=-20;}
    else check(collect(0)==0,"paged query includes every live child with same generation");
    u32 alive=0;for(u32 i=0;i<count;i++){u32 state[8];if(sc_job_info2(children[i].job,state)==1 && state[3]==children[i].pid && state[4]==children[i].generation)alive++;}
    check(alive==count,"all owned job tickets remain live simultaneously");legacy();
    if(resources(&peak)<0){check(0,"peak resource snapshot");peak=before;}
    number("owned_before=",before.owned);number("owned_peak=",peak.owned);number("free_pages_before=",before.free_pages);number("free_pages_peak=",peak.free_pages);
    number("phase_prepare_ticks=",(u32)sc_tick()-phase_began);
    if(exhaust && create_failure<0 && !protocol_failure){
        int stable=1;for(int i=0;i<16;i++){int result=sc_spawn2("/TMP/M10LIFE.SCX hold 0",fds,0);
            if(result>=0){u32 state[8];if(sc_job_info2((u32)result,state)>0)sc_kill_generation((int)state[3],state[4]);stable=0;break;}}
        resource_sample retry;if(resources(&retry)<0 || retry.owned!=peak.owned || retry.metadata!=peak.metadata || retry.records!=peak.records || retry.free_pages!=peak.free_pages)stable=0;
        check(stable,"repeated allocation failures preserve owned objects and physical pages");
    }
    if(busy && !protocol_failure && count==wanted){
        u32 began=(u32)sc_tick();cli_text(1,"WORKERS_READY\n");
        for(;;){u32 elapsed=(u32)sc_tick()-began;if(elapsed>=ticks)break;sc_event_wait(SC_EVENT_JOB,ticks-elapsed);}
        int result=collect(1);u32 min=0xFFFFFFFFu,max=0,missing=0,sample_missing=0;
        for(u32 i=0;i<count;i++){u32 value=children[i].delta_dispatch;if(value<min)min=value;if(value>max)max=value;
            if(!value)missing++;if(!children[i].delta_running)sample_missing++;}
        number("fair_ticks=",(u32)sc_tick()-began);number("dispatch_min=",min);number("dispatch_max=",max);
        number("dispatch_zero=",missing);number("running_sample_zero=",sample_missing);
        check(!result && !missing && !sample_missing,"all compute workers received CPU in measured interval");
    }
    phase_began=(u32)sc_tick();
    sc_stream_close(gate[0],0);sc_stream_close(ack[1],0);sc_stream_close(gate[1],0);
    int result=reap(children,count,6000);check(!result,"gate EOF completes all child jobs with actual exit zero");sc_stream_close(ack[0],0);
    number("phase_reap_ticks=",(u32)sc_tick()-phase_began);phase_began=(u32)sc_tick();
    result=settle(&after,&before);check(!result,"owned tasks and task sidecar pages return to baseline");
    number("phase_settle_ticks=",(u32)sc_tick()-phase_began);
    if(!result){number("owned_after=",after.owned);number("free_pages_after=",after.free_pages);number("record_pages_before=",before.records);number("record_pages_after=",after.records);
        check(!count || after.free_pages>peak.free_pages,"real physical pages increase after child reclamation");}
    return failures?1:0;
}
static int reuse(void)
{
    resource_sample before,after;if(resources(&before)<0)return -1;
    int gate[2],ack[2];if(sc_stream_pipe(gate)<0)return -1;
    if(sc_stream_pipe(ack)<0){sc_stream_close(gate[0],0);sc_stream_close(gate[1],0);return -1;}
    int fds[3]={gate[0],ack[1],ack[1]};child_record child;int result=start(&child,0,0,fds),started=!result;
    if(started){result=receive_ack(ack[0],&child,0);child_record *old=find(child.pid);u32 state[8];
        check(!result && old && old->generation!=child.generation,"reused PID has a fresh generation");
        if(old){check(sc_job_info2(old->job,state)<0,"reaped job ticket cannot bind to reused task");
            check(sc_kill_generation((int)child.pid,old->generation)==-8 && sc_job_info2(child.job,state)==1,"old generation cannot kill new child");}}
    else check(0,"child creation succeeds after reclamation");
    sc_stream_close(gate[0],0);sc_stream_close(ack[1],0);sc_stream_close(gate[1],0);sc_stream_close(ack[0],0);
    if(started)check(reap(&child,1,6000)==0,"reused child exits and job is reaped");
    check(settle(&after,&before)==0,"reuse sidecar cleanup returns to baseline");return failures?1:0;
}
static int rollback(void)
{
    int fds[3]={0,1,2};resource_sample before,after;
    for(int i=0;i<4;i++)if(sc_spawn2("/TMP/M10-MISSING.SCX",fds,0)>=0)return 1;
    if(resources(&before)<0)return 1;int okay=1;
    for(int i=0;i<131;i++)if(sc_spawn2("/TMP/M10-MISSING.SCX",fds,0)>=0 || sc_spawn2("/TMP/M10LIFE.SCX hold 0",fds,2)!=-1)okay=0;
    if(resources(&after)<0)return 1;
    check(okay && before.owned==after.owned && before.records==after.records && before.metadata==after.metadata && before.free_pages==after.free_pages,
        "missing executable and invalid flags roll back without page growth");return failures?1:0;
}
int main(void)
{
    if(cli_parse()<0 || !cli_argc)return 2;
    if((equal(cli_argv[0],"hold") || equal(cli_argv[0],"work")) && cli_argc==2){int index;
        if(cli_integer(cli_argv[1],&index)<0 || index<0)return 2;return worker(equal(cli_argv[0],"work"),(u32)index);}
    if(equal(cli_argv[0],"rollback"))return rollback();
    int wanted=131,ticks=600;if(cli_argc>=2 && (cli_integer(cli_argv[1],&wanted)<0 || wanted<65 || wanted>16384))return 2;
    if(cli_argc>=3 && (cli_integer(cli_argv[2],&ticks)<0 || ticks<100 || ticks>60000))return 2;
    int result;
    if(equal(cli_argv[0],"many"))result=batch((u32)wanted,0,0,0);
    else if(equal(cli_argv[0],"fair"))result=batch((u32)wanted,1,(u32)ticks,0);
    else if(equal(cli_argv[0],"cycle")){result=batch((u32)wanted,0,0,0);if(!result)result=reuse();if(!result)result=batch((u32)wanted,0,0,0);}
    else if(equal(cli_argv[0],"exhaust")){result=batch(16384,0,0,1);if(!result)result=reuse();if(!result)result=batch(131,0,0,0);}
    else return 2;
    if(children)sc_free(children);if(order)sc_free(order);return result;
}
