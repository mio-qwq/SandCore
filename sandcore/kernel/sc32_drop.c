#include "sc32_drop.h"
#include "sc32_broker.h"
#include "sc32_core.h"
#include "wm.h"
#include "task.h"
#include "paging.h"
#include "session.h"
#include "timer.h"
#include "objpool.h"

/* 注册、票据及拥有者均用稳定稀疏池。64仅是一次格式描述预算；
 * 不是窗口、target、会话、进程或在途票据的全局数量上限。
 * 每源窗口最多一个真实拖动轮次，同一目标移动复用票/连接。
 * 所有入口在IF=0单CPU临界区，保存的只有值和内核对象，不留用户VA。 */
typedef struct {
    u32 window,target,ticket;
} drop_window;
typedef struct {
    u32 generation,targets,tickets;
} drop_owner;
typedef struct {
    u32 id,targets,tickets,closing,queued,next;
} drop_session;
typedef struct {
    u32 id,window,generation,endpoint,actions,count,formats[64];
    ScBrokerIdentity identity;
    u32 owner_previous,owner_next,session_previous,session_next;
    u32 tickets,active,queued,gc_next;
} drop_target;
typedef struct {
    u32 slot,serial,target,actions,channel,handed_off,final_point;
    ScBrokerIdentity source;
    WmDropFact source_fact,target_fact;
    i32 x,y;
    u32 expires_lo,expires_hi;
    u32 owner_previous,owner_next,session_previous,session_next;
    u32 target_previous,target_next,expiry_previous,expiry_next;
} drop_ticket;

static objpool_t drop_windows,drop_owners,drop_sessions,drop_targets,drop_tickets;
static u32 drop_started,target_serial,ticket_serial;
static u32 expiry_first,expiry_last,gc_first,gc_last,session_first,session_last;
typedef char drop_info_144[(sizeof(ScDropInfo)==144)?1:-1];
typedef char drop_register_48[(sizeof(Sc32DropRegister)==48)?1:-1];
typedef char drop_point_32[(sizeof(ScDropPoint)==32)?1:-1];

static drop_ticket *ticket_get(u32 low)
{return low && low<=0x7FFFFFFFu?objpool_get(&drop_tickets,(int)(low-1u)):0;}
static drop_target *target_get(u32 id)
{return id && id<=0x7FFFFFFFu?objpool_get(&drop_targets,(int)id):0;}
static drop_window *window_get(u32 window,int create)
{
    if(!window || window>0x7FFFFFFFu)return 0;
    drop_window *p=objpool_get(&drop_windows,(int)window);
    if(!p && create && !objpool_claim(&drop_windows,(int)window)){
        p=objpool_get(&drop_windows,(int)window);p->window=window;
    }
    return p;
}
static void window_trim(u32 window)
{
    drop_window *p=window_get(window,0);
    if(p && !p->target && !p->ticket)objpool_release(&drop_windows,(int)window);
}
static drop_owner *owner_get(u32 pid,u32 generation,int create)
{
    drop_owner *p=objpool_get(&drop_owners,(int)pid);
    if(!p && create && !objpool_claim(&drop_owners,(int)pid)){
        p=objpool_get(&drop_owners,(int)pid);p->generation=generation;
    }
    return p && p->generation==generation?p:0;
}
static void owner_trim(u32 pid)
{
    drop_owner *p=objpool_get(&drop_owners,(int)pid);
    if(p && !p->targets && !p->tickets)objpool_release(&drop_owners,(int)pid);
}
static drop_session *session_get_drop(u32 id,int create)
{
    drop_session *p=objpool_get(&drop_sessions,(int)id);
    if(!p && create && !objpool_claim(&drop_sessions,(int)id)){
        p=objpool_get(&drop_sessions,(int)id);p->id=id;
    }
    return p;
}
static void session_trim(u32 id)
{
    drop_session *p=session_get_drop(id,0);
    if(p && !p->targets && !p->tickets && !p->queued)objpool_release(&drop_sessions,(int)id);
}
static int identity_same(const ScBrokerIdentity *a,const ScBrokerIdentity *b)
{
    return a->pid==b->pid && a->generation==b->generation && a->session==b->session
        && a->uid==b->uid && a->gid==b->gid && a->realm==b->realm
        && a->subject_uid==b->subject_uid && a->subject_gid==b->subject_gid && a->flags==b->flags;
}
static int fact_same(const WmDropFact *a,const WmDropFact *b)
{
    return a->window==b->window && a->generation==b->generation && a->pid==b->pid
        && a->pid_generation==b->pid_generation && a->session==b->session
        && a->geometry==b->geometry && a->visibility==b->visibility
        && a->x==b->x && a->y==b->y && a->width==b->width && a->height==b->height;
}
static int publish(int pid,u32 address,const void *value,u32 bytes)
{return paging_user_copy(TASK(pid).pd,address,(void *)value,bytes,1)?SC32_E_INVALID:0;}
static void expiry_unlink(drop_ticket *p)
{
    drop_ticket *before=ticket_get(p->expiry_previous),*after=ticket_get(p->expiry_next);
    if(before)before->expiry_next=p->expiry_next;else expiry_first=p->expiry_next;
    if(after)after->expiry_previous=p->expiry_previous;else expiry_last=p->expiry_previous;
    p->expiry_previous=p->expiry_next=0;
}
static void expiry_refresh(drop_ticket *p,int linked)
{
    if(linked)expiry_unlink(p);
    u32 now[2];timer_monotonic(now);u32 ticks=timer_frequency_hz()*30u;
    p->expires_lo=now[0]+ticks;p->expires_hi=now[1]+(p->expires_lo<now[0]);
    p->expiry_previous=expiry_last;
    drop_ticket *before=ticket_get(expiry_last);if(before)before->expiry_next=p->slot;
    else expiry_first=p->slot;
    expiry_last=p->slot;
}
static int expired(drop_ticket *p)
{
    u32 now[2];timer_monotonic(now);
    return now[1]>p->expires_hi || (now[1]==p->expires_hi && now[0]>=p->expires_lo);
}
static void ticket_drop(drop_ticket *p)
{
    u32 source_pid=p->source.pid,session=p->source.session,window=p->source_fact.window;
    drop_owner *owner=owner_get(source_pid,p->source.generation,0);
    drop_session *scope=session_get_drop(session,0);drop_target *target=target_get(p->target);
    drop_ticket *before=ticket_get(p->owner_previous),*after=ticket_get(p->owner_next);
    if(before)before->owner_next=p->owner_next;else if(owner)owner->tickets=p->owner_next;
    if(after)after->owner_previous=p->owner_previous;
    before=ticket_get(p->session_previous);after=ticket_get(p->session_next);
    if(before)before->session_next=p->session_next;else if(scope)scope->tickets=p->session_next;
    if(after)after->session_previous=p->session_previous;
    before=ticket_get(p->target_previous);after=ticket_get(p->target_next);
    if(before)before->target_next=p->target_next;else if(target)target->tickets=p->target_next;
    if(after)after->target_previous=p->target_previous;
    expiry_unlink(p);
    drop_window *w=window_get(window,0);if(w && w->ticket==p->slot)w->ticket=0;
    objpool_release(&drop_tickets,(int)(p->slot-1u));
    owner_trim(source_pid);session_trim(session);window_trim(window);
}
static void target_revoke(drop_target *p)
{
    if(!p || !p->active)return;
    u32 owner_pid=p->identity.pid,session=p->identity.session,window=p->window;
    p->active=0; /* 一次值更新立即撤全部关联票权限，回收按真实链分批。 */
    drop_owner *owner=owner_get(owner_pid,p->identity.generation,0);
    drop_session *scope=session_get_drop(session,0);
    drop_target *before=target_get(p->owner_previous),*after=target_get(p->owner_next);
    if(before)before->owner_next=p->owner_next;else if(owner)owner->targets=p->owner_next;
    if(after)after->owner_previous=p->owner_previous;
    before=target_get(p->session_previous);after=target_get(p->session_next);
    if(before)before->session_next=p->session_next;else if(scope)scope->targets=p->session_next;
    if(after)after->session_previous=p->session_previous;
    p->owner_previous=p->owner_next=p->session_previous=p->session_next=0;
    drop_window *w=window_get(window,0);if(w && w->target==p->id)w->target=0;
    p->queued=1;p->gc_next=0;
    drop_target *last=target_get(gc_last);if(last)last->gc_next=p->id;else gc_first=p->id;
    gc_last=p->id;
    owner_trim(owner_pid);session_trim(session);window_trim(window);task_kernel_wake();
}
static int target_live(drop_target *p,WmDropFact *fact,ScBrokerIdentity *identity)
{
    if(!p || !p->active)return SC32_E_STALE;
    drop_session *scope=session_get_drop(p->identity.session,0);
    if(!scope || scope->closing)return SC32_E_STALE;
    int error=sc32_broker_drop_endpoint((int)p->identity.pid,p->endpoint,p->identity.session,identity);
    if(error || !identity_same(identity,&p->identity))return error?error:SC32_E_STALE;
    error=wm_sc32_drop_window((int)p->identity.pid,p->window,p->generation,fact);
    return error?error:fact->pid_generation==p->identity.generation?0:SC32_E_STALE;
}
static int ticket_live(drop_ticket *p,ScDropInfo *out)
{
    if(!p || expired(p))return SC32_E_STALE;
    drop_target *target=target_get(p->target);WmDropFact source,point;ScBrokerIdentity identity,server;
    int error=sc32_broker_identity((int)p->source.pid,&identity);
    if(error || !identity_same(&identity,&p->source))return SC32_E_STALE;
    error=wm_sc32_drop_source((int)p->source.pid,p->source_fact.window,p->source_fact.generation,0,&source);
    if(error || !fact_same(&source,&p->source_fact))return SC32_E_STALE;
    error=target_live(target,&point,&server);if(error)return error;
    if(!fact_same(&point,&p->target_fact))return SC32_E_STALE;
    error=wm_sc32_drop_point(p->x,p->y,&point);
    if(error || !fact_same(&point,&p->target_fact))return SC32_E_STALE;
    if(!p->handed_off){
        /* capture撤销不是UP。handoff前必须仍处于起始真实输入代数，
         * 失捕/INPUTSYNC/失焦不能靠重新capture让旧票复活。真正UP
         * 先冻结handoff；之后Transfer/ACK才可独立于capture完成。 */
        if(!source.captured || source.input!=p->source_fact.input){ticket_drop(p);return SC32_E_STALE;}
        if(!(source.buttons&1u))p->handed_off=1;
    }
    if(out){
        *out=(ScDropInfo){0};out->size=sizeof(*out);out->version=1;
        out->target_id=target->id;out->target_generation=target->id;
        out->window=target->window;out->generation=target->generation;out->endpoint=target->endpoint;
        out->actions=p->actions;out->client_x=p->x-point.x;out->client_y=p->y-point.y;
        out->geometry_epoch=point.geometry;out->visibility_epoch=point.visibility;out->identity=server;
        out->ticket_lo=p->slot;out->ticket_hi=p->serial;out->expires_lo=p->expires_lo;out->expires_hi=p->expires_hi;
        out->source_window=p->source_fact.window;out->source_generation=p->source_fact.generation;
        out->source_pid=p->source.pid;out->source_pid_generation=p->source.generation;
        out->flags=p->handed_off?SC32_DROP_HANDED_OFF:SC32_DROP_CAPTURED;
    }
    return 0;
}
static int register_target(int pid,u32 request,u32 output)
{
    Sc32DropRegister input;int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScDropTargetRef));
    if(error)return error;
    if(!input.actions || (input.actions&~3u) || !input.format_count || input.format_count>64u
        || input.flags || input.reserved[0] || input.reserved[1] || input.reserved[2])return SC32_E_INVALID;
    ScBrokerIdentity identity;WmDropFact fact;
    error=sc32_broker_identity(pid,&identity);if(error)return error;
    error=wm_sc32_drop_register(pid,input.window,input.generation,&fact);if(error)return error;
    error=sc32_broker_drop_endpoint(pid,input.endpoint,identity.session,&identity);if(error)return error;
    u32 formats[64];
    if(paging_user_copy(TASK(pid).pd,input.formats,formats,input.format_count*4u,0))return SC32_E_INVALID;
    for(u32 i=0;i<input.format_count;i++){
        if(!formats[i])return SC32_E_INVALID;
        for(u32 j=0;j<i;j++)if(formats[i]==formats[j])return SC32_E_INVALID;
    }
    if(target_serial==0x7FFFFFFFu)return SC32_E_RANGE;
    drop_owner *owner=owner_get((u32)pid,identity.generation,1);
    drop_session *scope=session_get_drop(identity.session,1);drop_window *w=window_get(input.window,1);
    if(!owner || !scope || !w || scope->closing){owner_trim((u32)pid);session_trim(identity.session);window_trim(input.window);return SC32_E_MEMORY;}
    u32 id=target_serial+1;
    if(objpool_claim(&drop_targets,(int)id)){owner_trim((u32)pid);session_trim(identity.session);window_trim(input.window);return SC32_E_MEMORY;}
    drop_target *p=target_get(id);p->id=id;p->identity=identity;p->window=input.window;p->generation=input.generation;
    p->endpoint=input.endpoint;p->actions=input.actions;p->count=input.format_count;
    for(u32 i=0;i<input.format_count;i++)p->formats[i]=formats[i];
    p->active=1;
    /* 所有候选资源完整取得再撤旧注册；旧票立刻失效，OOM不触旧注册。 */
    drop_target *old=target_get(w->target);
    p->owner_next=owner->targets;if(p->owner_next)target_get(p->owner_next)->owner_previous=id;owner->targets=id;
    p->session_next=scope->targets;if(p->session_next)target_get(p->session_next)->session_previous=id;scope->targets=id;
    w->target=id;target_serial=id;if(old)target_revoke(old);
    ScDropTargetRef reply={16,1,id,id};return publish(pid,output,&reply,sizeof(reply));
}
static int unregister_target(int pid,u32 request,u32 output)
{
    if(output)return SC32_E_INVALID;
    Sc32DropWindow input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    WmDropFact fact;error=wm_sc32_drop_register(pid,input.window,input.generation,&fact);if(error)return error;
    drop_window *w=window_get(input.window,0);drop_target *target=w?target_get(w->target):0;
    if(!target)return SC32_E_NOT_FOUND;
    if(target->identity.pid!=(u32)pid || target->identity.generation!=task_generation(pid))return SC32_E_ACCESS;
    target_revoke(target);return 0;
}
static int new_ticket(const ScBrokerIdentity *identity,const WmDropFact *source,const WmDropFact *point,
    drop_target *target,u32 actions,i32 x,i32 y,drop_ticket **out)
{
    if(ticket_serial==0xFFFFFFFFu)return SC32_E_RANGE;
    drop_owner *owner=owner_get(identity->pid,identity->generation,1);
    drop_session *scope=session_get_drop(identity->session,1);drop_window *w=window_get(source->window,1);
    if(!owner || !scope || !w || scope->closing){owner_trim(identity->pid);session_trim(identity->session);window_trim(source->window);return SC32_E_MEMORY;}
    int index=objpool_alloc(&drop_tickets);
    if(index<0 || index==0x7FFFFFFF){if(index>=0)objpool_release(&drop_tickets,index);
        owner_trim(identity->pid);session_trim(identity->session);window_trim(source->window);return SC32_E_MEMORY;}
    drop_ticket *p=objpool_get(&drop_tickets,index);p->slot=(u32)index+1;p->serial=++ticket_serial;
    p->target=target->id;p->actions=actions;p->source=*identity;p->source_fact=*source;p->target_fact=*point;p->x=x;p->y=y;
    p->owner_next=owner->tickets;if(p->owner_next)ticket_get(p->owner_next)->owner_previous=p->slot;owner->tickets=p->slot;
    p->session_next=scope->tickets;if(p->session_next)ticket_get(p->session_next)->session_previous=p->slot;scope->tickets=p->slot;
    p->target_next=target->tickets;if(p->target_next)ticket_get(p->target_next)->target_previous=p->slot;target->tickets=p->slot;
    u32 previous=w->ticket;w->ticket=p->slot;expiry_refresh(p,0);
    drop_ticket *old=ticket_get(previous);if(old)ticket_drop(old);
    *out=p;return 0;
}
static int at_point(int pid,u32 request,u32 output)
{
    ScDropPoint input;int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScDropInfo));
    if(error)return error;
    if(!input.actions || (input.actions&~3u) || input.flags)return SC32_E_INVALID;
    ScBrokerIdentity identity,server;WmDropFact source,point;
    error=sc32_broker_identity(pid,&identity);if(error)return error;
    error=wm_sc32_drop_source(pid,input.source_window,input.source_generation,1,&source);if(error)return error;
    if(!wm_sc32_drop_point_current(input.screen_x,input.screen_y))return SC32_E_STALE;
    error=wm_sc32_drop_point(input.screen_x,input.screen_y,&point);if(error)return error;
    if(point.session!=identity.session)return SC32_E_ACCESS;
    drop_window *target_window=window_get(point.window,0);drop_target *target=target_window?target_get(target_window->target):0;
    WmDropFact registered;error=target_live(target,&registered,&server);if(error)return target?error:SC32_E_NOT_FOUND;
    if(!fact_same(&point,&registered))return SC32_E_STALE;
    u32 actions=input.actions&target->actions;if(!actions)return SC32_E_NOT_SUPPORTED;
    drop_window *source_window=window_get(source.window,0);drop_ticket *p=source_window?ticket_get(source_window->ticket):0;
    if(p && (expired(p) || p->target!=target->id || (p->handed_off && (source.buttons&1u)) || !identity_same(&p->source,&identity)
        || !fact_same(&p->source_fact,&source) || (!p->handed_off && p->source_fact.input!=source.input)
        || !fact_same(&p->target_fact,&point) || p->actions!=actions))p=0;
    if(!p){
        if(!(source.buttons&1u))return SC32_E_ACCESS;
        error=new_ticket(&identity,&source,&point,target,actions,input.screen_x,input.screen_y,&p);if(error)return error;
    }else{
        if(p->final_point && (p->x!=input.screen_x || p->y!=input.screen_y))return SC32_E_STALE;
        p->x=input.screen_x;p->y=input.screen_y;if(!p->final_point)expiry_refresh(p,1);
    }
    if(!(source.buttons&1u)){p->handed_off=1;p->final_point=1;}
    ScDropInfo reply;error=ticket_live(p,&reply);return error?error:publish(pid,output,&reply,sizeof(reply));
}
static int validate_request(int pid,const ScDropValidateRequest *input,drop_ticket **out)
{
    drop_ticket *p=ticket_get(input->ticket_lo);
    if(!p || p->serial!=input->ticket_hi)return SC32_E_STALE;
    drop_target *target=target_get(p->target);
    if(!target || input->source_window!=p->source_fact.window || input->source_generation!=p->source_fact.generation
        || input->target_id!=target->id || input->target_generation!=target->id)return SC32_E_STALE;
    u32 generation=task_generation(pid);
    if(!((p->source.pid==(u32)pid && p->source.generation==generation)
        || (target->identity.pid==(u32)pid && target->identity.generation==generation)))return SC32_E_ACCESS;
    *out=p;return ticket_live(p,0);
}
static int validate_ticket(int pid,u32 request,u32 output,int connect)
{
    ScDropValidateRequest input;int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,connect?sizeof(Sc32BrokerToken):sizeof(ScDropInfo));
    if(error)return error;
    drop_ticket *p;error=validate_request(pid,&input,&p);if(error)return error;
    if(connect){
        if(p->source.pid!=(u32)pid || p->source.generation!=task_generation(pid))return SC32_E_ACCESS;
        if(p->channel)return SC32_E_BUSY; /* 一轮只创建一次，At移动不增长pending。 */
        drop_target *target=target_get(p->target);Sc32BrokerToken reply;
        error=sc32_broker_drop_connect(pid,(int)target->identity.pid,target->endpoint,&reply);if(error)return error;
        p->channel=reply.handle;return publish(pid,output,&reply,sizeof(reply));
    }
    ScDropInfo reply;error=ticket_live(p,&reply);return error?error:publish(pid,output,&reply,sizeof(reply));
}
static int complete_ticket(int pid,u32 request,u32 output,int cancel)
{
    if(output)return SC32_E_INVALID;
    Sc32DropComplete input={0};int error;
    if(cancel){ScDropToken token;error=sc32_core_input(pid,request,&token,sizeof(token));
        if(error)return error;
        input.ticket_lo=token.ticket_lo;input.ticket_hi=token.ticket_hi;
    }else error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    drop_ticket *p=ticket_get(input.ticket_lo);if(!p || p->serial!=input.ticket_hi)return SC32_E_STALE;
    if(p->source.pid!=(u32)pid || p->source.generation!=task_generation(pid))return SC32_E_ACCESS;
    if(!cancel){if(input.reserved || (input.action!=SC32_DROP_COPY && input.action!=SC32_DROP_MOVE))return SC32_E_INVALID;
        if(!(p->actions&input.action))return SC32_E_ACCESS;
        error=ticket_live(p,0);if(error)return error;
        if(!p->handed_off)return SC32_E_BUSY;
    }
    /* Complete只消费轮次；磁盘提交与MOVE删源必须由真实target ACK证明。 */
    ticket_drop(p);return 0;
}
int sc32_drop_init(void)
{
    if(drop_started)return 0;
    if(objpool_init(&drop_windows,sizeof(drop_window),0) || objpool_init(&drop_owners,sizeof(drop_owner),0)
        || objpool_init(&drop_sessions,sizeof(drop_session),0) || objpool_init(&drop_targets,sizeof(drop_target),0)
        || objpool_init(&drop_tickets,sizeof(drop_ticket),0))return SC32_E_MEMORY;
    drop_started=1;return 0;
}
int sc32_drop_available(void){return drop_started && sc32_broker_available();}
int sc32_drop_call(int pid,u32 operation,u32 request,u32 output)
{
    if(!sc32_drop_available())return SC32_E_NOT_SUPPORTED;
    switch(operation){
    case SC32_D_REGISTER:return register_target(pid,request,output);
    case SC32_D_UNREGISTER:return unregister_target(pid,request,output);
    case SC32_D_AT:return at_point(pid,request,output);
    case SC32_D_VALIDATE:return validate_ticket(pid,request,output,0);
    case SC32_D_CONNECT:return validate_ticket(pid,request,output,1);
    case SC32_D_COMPLETE:return complete_ticket(pid,request,output,0);
    case SC32_D_CANCEL:return complete_ticket(pid,request,output,1);
    default:return SC32_E_NOT_SUPPORTED;
    }
}
void sc32_drop_window_changed(u32 window)
{
    drop_window *w=window_get(window,0);drop_ticket *ticket=w?ticket_get(w->ticket):0;
    if(ticket)ticket_drop(ticket);
    /* 目标票的真实geometry/visibility/原点重新核对，立即拒绝旧坐标；
     * 有限期限链回收其元数据，不在高频窗口移动中全扫所有源窗口。 */
}
void sc32_drop_window_closed(u32 window)
{
    sc32_drop_window_changed(window);drop_window *w=window_get(window,0);
    drop_target *target=w?target_get(w->target):0;if(target)target_revoke(target);window_trim(window);
}
int sc32_drop_stop_step(int pid)
{
    drop_owner *owner=owner_get((u32)pid,task_generation(pid),0);if(!owner)return 1;
    for(u32 budget=0;budget<32u;budget++){
        owner=owner_get((u32)pid,task_generation(pid),0);if(!owner)return 1;
        if(owner->tickets){ticket_drop(ticket_get(owner->tickets));continue;}
        if(owner->targets){target_revoke(target_get(owner->targets));continue;}
        owner_trim((u32)pid);return 1;
    }
    return !owner_get((u32)pid,task_generation(pid),0);
}
void sc32_drop_session_discard(u32 id)
{
    drop_session *scope=session_get_drop(id,0);if(!scope)return;
    scope->closing=1;if(scope->queued)return;scope->queued=1;scope->next=0;
    drop_session *last=session_get_drop(session_last,0);if(last)last->next=id;else session_first=id;
    session_last=id;task_kernel_wake();
}
void sc32_drop_poll(void)
{
    if(!drop_started)return;
    for(u32 budget=0;budget<32u && gc_first;budget++){
        drop_target *target=target_get(gc_first);
        if(target->tickets){ticket_drop(ticket_get(target->tickets));continue;}
        gc_first=target->gc_next;if(!gc_first)gc_last=0;objpool_release(&drop_targets,(int)target->id);
    }
    for(u32 budget=0;budget<32u && session_first;budget++){
        drop_session *scope=session_get_drop(session_first,0);
        if(scope->tickets){ticket_drop(ticket_get(scope->tickets));continue;}
        if(scope->targets){target_revoke(target_get(scope->targets));continue;}
        u32 id=scope->id;session_first=scope->next;if(!session_first)session_last=0;scope->queued=0;session_trim(id);
    }
    for(u32 budget=0;budget<32u && expiry_first;budget++){
        drop_ticket *ticket=ticket_get(expiry_first);if(!expired(ticket))break;ticket_drop(ticket);
    }
    if(sc32_drop_pending())task_kernel_wake();
}
int sc32_drop_pending(void)
{return gc_first || session_first || (expiry_first && expired(ticket_get(expiry_first)));}
