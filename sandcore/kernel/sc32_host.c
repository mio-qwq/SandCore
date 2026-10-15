#include "sc32_host.h"
#include "sc32_core.h"
#include "task.h"
#include "paging.h"
#include "auth.h"
#include "session.h"
#include "streams.h"
#include "objpool.h"
#include "timer.h"
#include "serial.h"
#include "process.h"
#include "sc32_broker.h"
#include "../user/hostapi/SC32HOSTWIRE.H"

typedef char host_launch_wire_size[(sizeof(Sc32HostLaunch)==32)?1:-1];
typedef char host_launch_result_size[(sizeof(ScLaunchInfo)==64)?1:-1];
typedef char host_info_size[(sizeof(ScShellHostInfo)==96)?1:-1];
typedef char host_prepare_size[(sizeof(ScShellHostDesc)==48)?1:-1];
typedef char host_connection_size[(sizeof(ScShellConnectionInfo)==160)?1:-1];
typedef char host_window_size[(sizeof(ScShellWindowInfo)==320)?1:-1];
typedef char host_action_size[(sizeof(ScShellWindowAction)==64)?1:-1];
typedef char host_point_size[(sizeof(ScShellHostPoint)==32)?1:-1];

typedef struct host_session host_session;
struct host_session {
    u32 session,epoch,pid,generation,endpoint,state,attempts,activated,deadline,queued;
    host_session *previous,*next;
};
typedef struct {u32 session,generation;} host_owner;
static objpool_t host_sessions,host_owners;
static host_session *host_first,*host_last;
static int host_started;

static void host_clear(void *out,u32 bytes)
{for(u32 i=0;i<bytes;i++)((u8 *)out)[i]=0;}

static int host_init(void)
{
    if(host_started)return 0;
    if(objpool_init(&host_sessions,sizeof(host_session),0))return SC32_E_MEMORY;
    if(objpool_init(&host_owners,sizeof(host_owner),0)){
        objpool_destroy(&host_sessions);return SC32_E_MEMORY;
    }
    host_started=1;return 0;
}
static host_session *host_get(u32 sid)
{return host_started?objpool_get(&host_sessions,(int)sid):0;}
static int host_scope(int pid,session_t **out)
{
    u32 identity[8];
    if(pid<=0 || !task_owned(pid) || TASK(pid).state!=1 || auth_info(pid,identity)
        || (int)identity[1]<0 || (int)identity[2]<0 || identity[3]!=AUTH_NORMAL)return SC32_E_ACCESS;
    session_t *scope=session_get(session_task(pid));
    if(!scope || scope->kind!=SESSION_NORMAL || scope->closing)return SC32_E_ACCESS;
    *out=scope;return 0;
}
int sc32_host_is_host(int pid,u32 sid)
{
    host_session *p=host_get(sid);session_t *scope;
    return p && p->state!=SC32_SHELL_STOPPING && !host_scope(pid,&scope) && scope->id==sid && p->pid==(u32)pid
        && p->generation==task_generation(pid);
}
void sc32_host_windows_changed(u32 sid)
{
    host_session *p=host_get(sid);
    if(p && p->pid)task_notify_generation((int)p->pid,p->generation,TASK_EVENT_VISIBILITY);
}
static void host_unqueue(host_session *p)
{
    if(!p->queued)return;
    if(p->previous)p->previous->next=p->next;else host_first=p->next;
    if(p->next)p->next->previous=p->previous;else host_last=p->previous;
    p->previous=p->next=0;p->queued=0;
}
static void host_queue(host_session *p,u32 delay)
{
    host_unqueue(p);p->deadline=sc_ticks+delay;
    /* 只在会话启动/故障发生时插入。热poll只看已排序队首，不每tick扫
     * 全部会话；所有重试期限不超过数秒，沿用PIT有符号差的回绕合同。 */
    host_session *after=host_first;
    while(after && (i32)(after->deadline-p->deadline)<=0)after=after->next;
    p->next=after;p->previous=after?after->previous:host_last;
    if(p->previous)p->previous->next=p;else host_first=p;
    if(after)after->previous=p;else host_last=p;
    p->queued=1;task_kernel_wake();
}
void sc32_host_session_created(u32 sid)
{
    session_t *scope=session_get(sid);
    if(!scope || scope->kind!=SESSION_NORMAL || scope->uid<0 || scope->gid<0 || host_init())return;
    if(host_get(sid) || objpool_claim(&host_sessions,(int)sid))return;
    host_session *p=host_get(sid);p->session=sid;p->epoch=1;
    host_queue(p,0);
}
void sc32_host_session_discard(u32 sid)
{
    host_session *p=host_get(sid);if(!p)return;
    host_unqueue(p);
    if(p->pid){
        wm_shell_detach((int)p->pid,p->generation);
        objpool_release(&host_owners,(int)p->pid);
    }
    objpool_release(&host_sessions,(int)sid);
}
void sc32_host_owner_stopped(int pid)
{
    if(!host_started || pid<=0)return;
    host_owner *owner=objpool_get(&host_owners,pid);
    if(!owner || owner->generation!=task_generation(pid))return;
    host_session *p=host_get(owner->session);
    objpool_release(&host_owners,pid);
    if(!p || p->pid!=(u32)pid || p->generation!=task_generation(pid))return;
    /* 独立桌面不继承管理TTY，stderr可能不可见。只对本可信绑定的
     * 意外退出向外部调试口记录实际退出码；不输出用户路径或凭据。 */
    session_t *stopping_scope=session_get(p->session);
    if((p->state!=SC32_SHELL_STOPPING || process_status(pid)!=0) && stopping_scope && !stopping_scope->closing){
        char line[]="SHELL HOST stopped pid=00000000 code=00000000\r\n";
        const char hex[]="0123456789abcdef";
        u32 code=(u32)process_status(pid);
        for(u32 i=0;i<8;i++){line[sizeof("SHELL HOST stopped pid=")-1+i]=hex[((u32)pid>>(28-i*4))&15u];line[sizeof("SHELL HOST stopped pid=00000000 code=")-1+i]=hex[(code>>(28-i*4))&15u];}
        serial_write(SERIAL_DEBUG,(const u8 *)line,sizeof(line)-1);
    }
    wm_shell_detach(pid,p->generation);
    if(p->activated && (u32)(sc_ticks-p->activated)>=6000u)p->attempts=0;
    p->pid=p->generation=p->endpoint=p->activated=0;
    if(p->epoch<0xFFFFFFFFu)p->epoch++;
    p->state=SC32_SHELL_OFFLINE;
    session_t *scope=session_get(p->session);
    if(scope && !scope->closing){
        if(p->attempts<4 && p->epoch<0xFFFFFFFFu)host_queue(p,100u);
        else p->state=SC32_SHELL_RETRY_EXHAUSTED;
    }
}
void sc32_host_poll(void)
{
    if(!host_first || (i32)(sc_ticks-host_first->deadline)<0)return;
    host_session *p=host_first;host_unqueue(p);
    session_t *scope=session_get(p->session);
    if(!scope || scope->closing || scope->kind!=SESSION_NORMAL || p->pid)return;
    if(p->attempts>=4){p->state=SC32_SHELL_RETRY_EXHAUSTED;return;}
    p->attempts++;
    /* 只有可信内核启动器能产生这项绑定。固定受保护发行路径不是普通
     * IPC名，也不接受用户给PID/身份；孩子仍是本会话普通主体，没有提权。 */
    int child=auth_session_shell_exec(p->session);
    if(child>0){
        if(!objpool_claim(&host_owners,child)){
            host_owner *owner=objpool_get(&host_owners,child);
            owner->session=p->session;owner->generation=task_generation(child);
            p->pid=(u32)child;p->generation=owner->generation;p->state=SC32_SHELL_STARTING;
            return;
        }
        task_stop(child);
    }
    p->state=p->attempts<4?SC32_SHELL_OFFLINE:SC32_SHELL_RETRY_EXHAUSTED;
    if(p->attempts<4)host_queue(p,300u);
}
static void host_value(host_session *p,ScShellHostInfo *out)
{
    host_clear(out,sizeof(*out));out->size=sizeof(*out);out->version=1;
    if(!p)return;
    out->session=out->session_generation=p->session;
    out->host_epoch=p->epoch;out->pid=p->pid;out->generation=p->generation;
    out->endpoint=p->endpoint;out->state=p->state;
    wm_shell_info(p->session,out);
}
static int host_publish(int pid,u32 output,const void *value,u32 bytes)
{return paging_user_copy(TASK(pid).pd,output,(void *)value,bytes,1)?SC32_E_INVALID:0;}
static int host_record(int pid,host_session **out,int owner)
{
    session_t *scope;int error=host_scope(pid,&scope);if(error)return error;
    host_session *p=host_get(scope->id);
    if(!p)return SC32_E_NOT_FOUND;
    if(owner && !sc32_host_is_host(pid,scope->id))return SC32_E_ACCESS;
    *out=p;return 0;
}

/* 显式字节数包括唯一末尾 NUL。先验证完整 RO 范围，再复制可信局部值；
 * 不逐字读取用户指针，也不把参数地址留给之后运行的子进程。 */
static int host_text(int pid,u32 address,u32 bytes,char *out,u32 capacity)
{
    if(!bytes || bytes>capacity || !address)return SC32_E_INVALID;
    if(paging_user_copy(TASK(pid).pd,address,out,bytes,0))return SC32_E_INVALID;
    for(u32 i=0;i+1<bytes;i++)if(!out[i])return SC32_E_INVALID;
    return out[bytes-1]?SC32_E_INVALID:SC32_OK;
}

static int host_launch(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScLaunchInfo));
    if(error)return error;
    Sc32HostLaunch input;
    error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(input.flags || input.reserved)return SC32_E_INVALID;
    char path[128],arguments[1024];
    error=host_text(pid,input.path,input.path_bytes,path,sizeof(path));
    if(error)return error;
    error=host_text(pid,input.arguments,input.argument_bytes,arguments,sizeof(arguments));
    if(error)return error;
    int child=streams_launch_session(pid,path,arguments);
    if(child<1)return child==-2?SC32_E_NOT_FOUND:child==-4?SC32_E_MEMORY:
        child==-5?SC32_E_ACCESS:SC32_E_INVALID;
    ScLaunchInfo result;
    host_clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=1;
    result.pid=(u32)child;result.generation=task_generation(child);
    result.session=session_task(child);result.uid=auth_uid(child);result.gid=auth_gid(child);
    u32 identity[8];
    if(auth_info(child,identity)){
        task_stop(child);
        return SC32_E_FAILED;
    }
    result.realm=identity[3];
    /* 输出已在任何 spawn 前验完整声明范围；同 IF=0 期间本人页表没有
     * 用户执行机会。防御性复制失败仍撤未运行孩子，不能留下未知启动。 */
    if(paging_user_copy(TASK(pid).pd,output,&result,sizeof(result),1)){
        task_stop(child);
        return SC32_E_INVALID;
    }
    return SC32_OK;
}

static int host_query(int pid,u32 request,u32 output,int restart)
{
    if(request)return SC32_E_INVALID;
    int error=sc32_core_output(pid,output,sizeof(ScShellHostInfo));
    if(error)return error;
    host_session *p;
    error=host_record(pid,&p,0);
    if(error==SC32_E_NOT_FOUND && restart){
        session_t *scope;error=host_scope(pid,&scope);
        if(error)return error;
        sc32_host_session_created(scope->id);error=host_record(pid,&p,0);
        if(error==SC32_E_NOT_FOUND)return SC32_E_MEMORY;
    }
    if(error)return error;
    if(restart){
        if(p->pid)return SC32_E_BUSY;
        if(p->epoch==0xFFFFFFFFu)return SC32_E_RANGE;
        p->attempts=0;p->state=SC32_SHELL_OFFLINE;host_queue(p,0);
    }
    ScShellHostInfo result;host_value(p,&result);
    return host_publish(pid,output,&result,sizeof(result));
}
static int host_prepare(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScShellHostInfo));
    if(error)return error;
    ScShellHostDesc input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.flags || input.reserved[0] || input.reserved[1] || !input.endpoint
        || !input.desktop || !input.desktop_generation || !input.taskbar
        || !input.taskbar_generation || input.desktop==input.taskbar)return SC32_E_INVALID;
    host_session *p;
    error=host_record(pid,&p,1);if(error)return error;
    if(input.host_epoch!=p->epoch)return SC32_E_STALE;
    if(p->state==SC32_SHELL_ACTIVE)return SC32_E_BUSY;
    ScBrokerIdentity identity;
    error=sc32_broker_drop_endpoint(pid,input.endpoint,p->session,&identity);if(error)return error;
    error=wm_shell_prepare(pid,input.desktop,input.desktop_generation,
        input.taskbar,input.taskbar_generation,input.taskbar_height);
    if(error)return error;
    p->endpoint=input.endpoint;p->state=SC32_SHELL_PREPARED;
    ScShellHostInfo result;host_value(p,&result);
    return host_publish(pid,output,&result,sizeof(result));
}
static int host_control(int pid,u32 operation,u32 request,u32 output)
{
    int connect=operation==SC32_H_CONNECT;
    int error=sc32_core_output(pid,output,connect?sizeof(ScShellConnectionInfo):sizeof(ScShellHostInfo));
    if(error)return error;
    Sc32HostControl input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.flags || input.reserved[0] || input.reserved[1] || input.reserved[2]
        || (operation!=SC32_H_WORKSPACE && input.value))return SC32_E_INVALID;
    host_session *p;
    int deactivate=operation==SC32_H_DEACTIVATE;
    error=host_record(pid,&p,!connect && !deactivate);if(error)return error;
    if((!connect || input.host_epoch) && input.host_epoch!=p->epoch)return SC32_E_STALE;
    if(deactivate){
        if(p->pid!=(u32)pid || p->generation!=task_generation(pid))return SC32_E_ACCESS;
        if(p->state!=SC32_SHELL_STOPPING){
            wm_shell_detach(pid,p->generation);
            p->endpoint=0;p->state=SC32_SHELL_STOPPING;
        }
        /* PID/gen保留到真实EXIT，Restart仍BUSY；正常stdio/fini与return
         * 能完整执行，不因销角色窗被强行提前Stop或丢掉退出码。 */
        ScShellHostInfo result;host_value(p,&result);
        return host_publish(pid,output,&result,sizeof(result));
    }
    if(connect){
        if((p->state!=SC32_SHELL_PREPARED && p->state!=SC32_SHELL_ACTIVE) || !p->pid)return SC32_E_AGAIN;
        ScShellConnectionInfo result;host_clear(&result,sizeof(result));
        result.size=sizeof(result);result.version=1;host_value(p,&result.host);
        error=sc32_broker_drop_endpoint((int)p->pid,p->endpoint,p->session,&result.peer);
        if(error)return error;
        Sc32BrokerToken token;
        error=sc32_broker_drop_connect(pid,(int)p->pid,p->endpoint,&token);if(error)return error;
        result.channel=token.handle;
        error=host_publish(pid,output,&result,sizeof(result));
        if(error)(void)sc32_broker_kernel_close(pid,token.handle);
        return error;
    }
    ScBrokerIdentity identity;
    error=sc32_broker_drop_endpoint(pid,p->endpoint,p->session,&identity);if(error)return error;
    if(operation==SC32_H_ACTIVATE){
        if(p->state!=SC32_SHELL_PREPARED)return SC32_E_BUSY;
        error=wm_shell_activate(pid);if(error)return error;
        p->state=SC32_SHELL_ACTIVE;p->activated=sc_ticks;
    }else{
        if(p->state!=SC32_SHELL_ACTIVE)return SC32_E_BUSY;
        error=wm_shell_workspace(pid,input.value);if(error)return error;
    }
    ScShellHostInfo result;host_value(p,&result);
    return host_publish(pid,output,&result,sizeof(result));
}
static int host_windows(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScShellWindowPage));if(error)return error;
    Sc32HostWindows input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.reserved || !input.capacity || input.capacity>64u || !input.rows
        || input.after==0xFFFFFFFFu)return SC32_E_INVALID;
    host_session *p;
    error=host_record(pid,&p,1);if(error)return error;
    if(input.host_epoch!=p->epoch)return SC32_E_STALE;
    if(p->state!=SC32_SHELL_ACTIVE)return SC32_E_BUSY;
    u32 bytes=input.capacity*sizeof(ScShellWindowInfo);
    if(!paging_user_write_range(TASK(pid).pd,input.rows,bytes))return SC32_E_INVALID;
    /* 一个320B局部record重复使用，绝不在4KiB内核栈开64行数组。窗口
     * 总量动态；64只是一陷入的拷贝预算，cursor/finished给真实后续状态。 */
    ScShellWindowPage page;host_clear(&page,sizeof(page));page.size=sizeof(page);page.version=1;
    page.cursor=input.after;
    for(u32 i=0;i<input.capacity && page.examined<64u;i++){
        ScShellWindowInfo row;u32 cursor,epoch,examined;int finished;
        error=wm_shell_window_next(pid,page.cursor,64u-page.examined,&row,&cursor,&epoch,&examined,&finished);
        if(error)return error;
        if(input.expected_epoch && input.expected_epoch!=epoch)return SC32_E_STALE;
        page.epoch=epoch;page.cursor=cursor;page.examined+=examined;
        if(finished){page.flags|=1u;break;}
        if(!row.window.window)break;
        if(paging_user_copy(TASK(pid).pd,input.rows+i*sizeof(row),&row,sizeof(row),1))return SC32_E_INVALID;
        page.count++;
    }
    return host_publish(pid,output,&page,sizeof(page));
}
static int host_window_action(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScWindowInfo));if(error)return error;
    ScShellWindowAction input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.flags || input.reserved[0] || input.reserved[1] || input.reserved[2]
        || input.action>SC32_SHELL_ACTION_CLOSE)return SC32_E_INVALID;
    host_session *p;
    error=host_record(pid,&p,1);if(error)return error;
    if(input.host_epoch!=p->epoch)return SC32_E_STALE;
    if(p->state!=SC32_SHELL_ACTIVE)return SC32_E_BUSY;
    ScWindowInfo result;
    error=wm_shell_window_action(pid,&input,&result);if(error)return error;
    return host_publish(pid,output,&result,sizeof(result));
}
static int host_window_at(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScWindowInfo));if(error)return error;
    ScShellHostPoint input;error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.flags || input.reserved[0] || input.reserved[1])return SC32_E_INVALID;
    host_session *p;error=host_record(pid,&p,1);if(error)return error;
    if(input.host_epoch!=p->epoch)return SC32_E_STALE;
    if(p->state!=SC32_SHELL_ACTIVE)return SC32_E_BUSY;
    ScWindowInfo result;error=wm_shell_window_at(pid,input.x,input.y,&result);if(error)return error;
    return host_publish(pid,output,&result,sizeof(result));
}
int sc32_host_call(int pid,u32 operation,u32 request,u32 output)
{
    switch(operation){
    case SC32_H_LAUNCH:return host_launch(pid,request,output);
    case SC32_H_QUERY:return host_query(pid,request,output,0);
    case SC32_H_PREPARE:return host_prepare(pid,request,output);
    case SC32_H_ACTIVATE:case SC32_H_CONNECT:case SC32_H_WORKSPACE:case SC32_H_DEACTIVATE:
        return host_control(pid,operation,request,output);
    case SC32_H_WINDOWS:return host_windows(pid,request,output);
    case SC32_H_WINDOW_ACTION:return host_window_action(pid,request,output);
    case SC32_H_WINDOW_AT:return host_window_at(pid,request,output);
    case SC32_H_RESTART:return host_query(pid,request,output,1);
    default:return SC32_E_NOT_SUPPORTED;
    }
}
