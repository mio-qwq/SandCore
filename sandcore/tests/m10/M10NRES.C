#include "SCIO.H"
#include "SCNET.H"
/* 正式公开ABI下的普通身份/跨PID所有权及真实内存耗尽。数组的一
 * 百万项只限本夹具的时间预算；没有耗尽就报未完成，不能把它改
 * 成系统socket上限。资源账在同一进程中取样，不混入孩子PID历史页。 */
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static void number(const char *name,u32 value)
{cli_text(1,name);cli_number(1,(int)value);cli_text(1,"\n");}
static int exact_read(int fd,void *body,u32 bytes)
{
    u32 at=0,began=(u32)sc_tick();
    while(at<bytes){int result=sc_stream_read(fd,(u8 *)body+at,bytes-at);
        if(result>0){at+=(u32)result;continue;}
        if(result!=-6 || (u32)sc_tick()-began>2000u)return -1;
        sc_event_wait(SC_EVENT_STREAM|SC_EVENT_JOB,10);}
    return 0;
}
static int child(void)
{
    u32 request[4],reply[32],identity[8],session[32],address[4],guard[18];
    if(exact_read(0,request,sizeof(request))<0 || sc_auth_info(identity)<0
        || sc_session_page(session,32,0xFFFFFFFFu)<0)return 1;
    int wrong=0;u32 foreign=request[0];
    for(u32 i=0;i<18;i++)guard[i]=0xA10E0001;
    if(sc_socket_status(foreign,guard+1)!=-9)wrong++;
    for(u32 i=0;i<18;i++)if(guard[i]!=0xA10E0001)wrong++;
    sc_net_address(address,0x0A170001u,7007);u8 byte=0x51;
    if(sc_socket_bind(foreign,address)!=-9 || sc_socket_connect(foreign,address)!=-9
        || sc_socket_listen(foreign,1)!=-9 || sc_socket_accept(foreign,address)!=-9
        || sc_socket_send(foreign,&byte,1,address)!=-9
        || sc_socket_receive(foreign,&byte,1,address)!=-9
        || sc_socket_shutdown(foreign,1)!=-9 || sc_socket_option(foreign,SC_NET_OPT_TTL,64)!=-9
        || sc_socket_close(foreign)!=-9)wrong++;
    u32 control[32];sc_net_request(control);
    if(sc_net_control(SC_NET_RESET,control)!=-13
        || sc_net_control(SC_NET_DOWN,control)!=-13
        || sc_permissions("/SYS/CORE/CORE.SKM",-2,-2,63)>=0)wrong++;
    if(identity[1]!=1000 || identity[3]!=0 || identity[4]!=0 || !session[5] || session[5]==session[6])wrong++;
    int handle=sc_socket(SC_NET_UDP,0);
    if(handle<1)wrong++;
    else{
        sc_net_address(address,0,80);if(sc_socket_bind((u32)handle,address)!=-13)wrong++;
        sc_net_address(address,0x0A170001u,7007);
        const char *body="hidden-mio-network";u8 received[32];
        int sent=sc_socket_send((u32)handle,body,18,address),got=-11;u32 began=(u32)sc_tick();
        while(sent==18 && got==-11 && (u32)sc_tick()-began<500u){
            got=sc_socket_receive((u32)handle,received,sizeof(received),0);
            if(got==-11)sc_event_wait(SC_EVENT_NETWORK,10);}
        int matched=sent==18 && got==18;
        if(matched)for(int i=0;i<18;i++)if(received[i]!=(u8)body[i])matched=0;
        if(!matched)wrong++;
    }
    for(u32 i=0;i<32;i++)reply[i]=0;
    reply[0]=0x31524E4Du;reply[1]=(u32)wrong;reply[2]=identity[1];reply[3]=identity[3];reply[4]=identity[4];
    reply[5]=session[5];reply[6]=session[6];reply[7]=(u32)handle;
    if(cli_write(1,reply,sizeof(reply))!=(int)sizeof(reply))return 2;
    /* EOF后故意不显式close：验真正任务退出清理，不能靠探针自己
     * 先释放全部socket再称内核退出回收通过。 */
    u8 command;for(;;){int result=sc_stream_read(0,&command,1);if(result==0)return wrong?1:0;
        if(result!=-6)return 3;sc_event_wait(SC_EVENT_STREAM,10);}
}
static int permissions(void)
{
    u32 info[96],state[16],request[4],reply[32],auth[8],child_session=0;
    int input[2]={-1,-1},output[2]={-1,-1},saved[3]={0,0,0},pid=-1,ticket=-1;
    if(sc_auth_info(auth)<0 || auth[4]!=1 || sc_permissions("/TMP/M10NRES.SCX",-2,-2,63)<0)return 1;
    int handle=sc_socket(SC_NET_UDP,0);if(handle<1)return 1;
    u32 began=(u32)sc_tick();
    do{ticket=sc_auth_login("mio","");if(ticket!=-6)break;sc_event_wait(SC_EVENT_AUTH,10);}while((u32)sc_tick()-began<2000u);
    if(ticket<1)goto fail;
    for(;;){int result=sc_auth_status((u32)ticket,auth);if(!result)break;
        if(result!=1 || (u32)sc_tick()-began>2000u)goto fail;
        sc_event_wait(SC_EVENT_AUTH,10);}
    /* 某一步失败也恢复已暂存的stdio；否则错误信息会写到孩子管道，
     * 失败现场不可见，并让夹具自己的端点泄漏污染后续页账。 */
    for(int i=0;i<3;i++){if(sc_stream_dup(i,13+i)<0)goto fail;saved[i]=1;}
    if(sc_stream_pipe(input)<0 || sc_stream_pipe(output)<0)goto fail;
    if(sc_stream_dup(input[0],0)<0 || sc_stream_dup(output[1],1)<0 || sc_stream_dup(output[1],2)<0)goto fail;
    pid=sc_auth_exec((u32)ticket,"/TMP/M10NRES.SCX child",0);sc_auth_cancel((u32)ticket);ticket=-1;
    for(int i=0;i<3;i++){sc_stream_dup(13+i,i);sc_stream_close(13+i,0);saved[i]=0;}
    sc_stream_close(input[0],0);input[0]=-1;sc_stream_close(output[1],0);output[1]=-1;
    request[0]=(u32)handle;request[1]=request[2]=request[3]=0;
    if(pid<1 || cli_write(input[1],request,sizeof(request))!=(int)sizeof(request)
        || exact_read(output[0],reply,sizeof(reply))<0)goto fail;
    child_session=reply[5];
    check(reply[0]==0x31524E4Du && !reply[1] && reply[2]==1000 && !reply[3] && !reply[4],
          "ordinary mio rejects foreign sockets privileged config and CORE edit but sends real bytes");
    check(reply[5] && reply[5]!=reply[6],"hidden ordinary login completes real UDP exchange without visible desktop");
    check(sc_socket_status((u32)handle,state)==0,"foreign close leaves parent original socket live");
    check(sc_socket_status(reply[7],state)==-9,"SYSTEM parent also cannot use another PID private socket");
    sc_stream_close(input[1],0);input[1]=-1;sc_stream_close(output[0],0);output[0]=-1;
    began=(u32)sc_tick();
    do{sc_net_info(info);if(info[16]==1)break;sc_event_wait(SC_EVENT_JOB|SC_EVENT_NETWORK,10);}
    while((u32)sc_tick()-began<2000u);
    check(info[16]==1 && sc_socket_status(reply[7],state)==-9,"normal task EOF reclaims unclosed owned UDP socket");
    if(child_session)sc_session_control(child_session,1);child_session=0;
    sc_socket_close((u32)handle);
    began=(u32)sc_tick();
    do{sc_net_info(info);if(!info[16])break;sc_event_wait(SC_EVENT_NETWORK,10);}
    while((u32)sc_tick()-began<2000u);
    check(!info[16] && !info[17],"parent and child socket queues have zero remaining logical objects");
    return failures?1:0;
fail:
    for(int i=0;i<3;i++)if(saved[i]){sc_stream_dup(13+i,i);sc_stream_close(13+i,0);}
    if(ticket>0)sc_auth_cancel((u32)ticket);
    for(int i=0;i<2;i++){if(input[i]>=0)sc_stream_close(input[i],0);if(output[i]>=0)sc_stream_close(output[i],0);}
    if(child_session)sc_session_control(child_session,1);else if(pid>0)sc_kill2(pid);
    sc_socket_close((u32)handle);return 1;
}
static int exhaust(void)
{
    u32 *handles=(u32 *)sc_alloc(1048576u*sizeof(u32));void *reserve=sc_alloc(65536u);
    if(!handles || !reserve){if(handles)sc_free(handles);if(reserve)sc_free(reserve);return 1;}
    u32 original[96],end[96],memory0[48],memory1[48],count=0,began=(u32)sc_tick();int result=0;
    sc_net_info(original);sc_monitor(memory0);
    for(;count<1048576u;count++){
        result=sc_socket(SC_NET_UDP,0);if(result<1)break;handles[count]=(u32)result;
        if((count&16383u)==16383u)number("socket_progress=",count+1);
    }
    number("socket_created=",count);number("socket_failure=",(u32)result);
    check(count>131 && count<1048576u && result==-12,"socket creation reaches real resource exhaustion beyond old counts");
    sc_net_info(end);sc_monitor(memory1);u32 live=end[16],pages=memory1[4];
    int stable=1;
    for(int i=0;i<8;i++){int extra=sc_socket(SC_NET_UDP,0);u32 snapshot[96],memory[48];
        if(extra>0){sc_socket_close((u32)extra);stable=0;}else if(extra!=-12)stable=0;
        sc_net_info(snapshot);sc_monitor(memory);if(snapshot[16]!=live || memory[4]!=pages)stable=0;}
    check(stable && live==original[16]+count,"repeated real allocation failures preserve socket and physical page counts");
    u32 stale=count?handles[0]:0;int closed=1;
    /* 本轮只创建未绑定UDP，尚未加入udp_pcbs活动链，故不宣称已
     * 绑定长链任意顺序关闭的性能；逆序撤销便于完整核对这一批。
     * 关闭只排队，必须等内核实际完成释放后再读取PF基线。 */
    for(u32 i=count;i>0;i--)if(sc_socket_close(handles[i-1])<0)closed=0;
    u32 closing=(u32)sc_tick();
    do{sc_net_info(end);if(end[16]==original[16])break;sc_event_wait(SC_EVENT_NETWORK,10);}
    while((u32)sc_tick()-closing<3000u);
    sc_net_info(end);sc_monitor(memory1);
    number("socket_pf_before=",memory0[4]/4096u);number("socket_pf_after=",memory1[4]/4096u);
    number("socket_protocol_before=",original[18]);number("socket_protocol_after=",end[18]);
    check(closed && end[16]==original[16] && !end[17] && end[18]==original[18]
        && end[20]==original[20] && memory1[4]==memory0[4],"closing complete exhausted cohort restores protocol and real PF baseline");
    u32 status[16];int fresh=sc_socket(SC_NET_UDP,0);
    check(fresh>0 && (u32)fresh!=stale && sc_socket_status(stale,status)==-9,"new socket after exhaustion has distinct handle and stale handle stays invalid");
    if(fresh>0)sc_socket_close((u32)fresh);
    number("socket_exhaust_ticks=",(u32)sc_tick()-began);sc_free(handles);sc_free(reserve);return failures?1:0;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=1)return 2;
    if(equal(cli_argv[0],"child"))return child();
    int result;if(equal(cli_argv[0],"permissions"))result=permissions();
    else if(equal(cli_argv[0],"exhaust"))result=exhaust();else return 2;
    number("network_resource_failures=",(u32)failures);return result;
}
