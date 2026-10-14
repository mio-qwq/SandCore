#include "sc32_gui.h"
#include "sc32_core.h"
#include "sc32_raw.h"
#include "wm.h"
#include "task.h"
#include "memory.h"
#include "paging.h"
#include "session.h"
#include "keyboard.h"
#include "mouse.h"
#include "objpool.h"
#include "timer.h"
#include "gfx.h"

#define GUI_CHUNK_MESSAGES 63u
typedef struct gui_chunk {
    struct gui_chunk *next;
    u32 begin,end,reserved;
    ScMessage messages[GUI_CHUNK_MESSAGES];
} gui_chunk_t;
typedef char gui_chunk_fits_page[(sizeof(gui_chunk_t)<=4096)?1:-1];
typedef char gui_message_keeps_wire[(sizeof(ScMessage)==64)?1:-1];
typedef char gui_window_keeps_wire[(sizeof(ScWindowInfo)==96)?1:-1];
typedef char gui_input_keeps_wire[(sizeof(ScInputSnapshot)==96)?1:-1];
typedef char gui_create_keeps_wire[(sizeof(Sc32WindowCreate)==64)?1:-1];
typedef char gui_submit_keeps_wire[(sizeof(ScSubmitDesc)==64)?1:-1];
typedef char gui_paint_keeps_wire[(sizeof(ScPaintInfo)==64)?1:-1];
typedef char gui_queue_keeps_wire[(sizeof(ScQueueInfo)==32)?1:-1];
typedef char gui_reference_keeps_wire[(sizeof(Sc32WindowRef)==16)?1:-1];
typedef char gui_place_keeps_wire[(sizeof(Sc32WindowPlace)==40)?1:-1];
typedef char gui_owner_keeps_wire[(sizeof(Sc32WindowOwner)==32)?1:-1];
typedef struct {
    u32 pid,generation,session,count,pages,sequence_lo,sequence_hi,attention;
    u32 lost_lo,lost_hi,stopping,reserving,fresh;
    gui_chunk_t *head,*tail,*last,*reservation_mark;
    ScMessage loss;
    u32 loss_pending;
    u32 input_barrier_lo,input_barrier_hi;
    u32 merge_barrier_lo,merge_barrier_hi;
} gui_owner_t;
static objpool_t owners;
static int initialized,routing,loss_broadcast;
static u32 raw_loss_previous[2];

static void zero(void *memory,u32 bytes)
{u8 *p=memory;for(u32 i=0;i<bytes;i++)p[i]=0;}
static gui_owner_t *owner(int pid)
{
    gui_owner_t *p=pid>0?objpool_get(&owners,pid):0;
    return p && task_owned(pid) && p->generation==task_generation(pid)?p:0;
}
int sc32_gui_init(void)
{
    if(initialized)return 0;
    if(objpool_init(&owners,sizeof(gui_owner_t),0))return -1;
    initialized=1;return 0;
}
int sc32_gui_prepare(int pid)
{
    if(!initialized || !task_owned(pid) || TASK(pid).state!=1)return SC32_E_INVALID;
    gui_owner_t *p=objpool_get(&owners,pid);
    if(p)return p->generation==task_generation(pid)?SC32_OK:SC32_E_STALE;
    if(objpool_claim(&owners,pid))return SC32_E_MEMORY;
    p=objpool_get(&owners,pid);p->pid=(u32)pid;p->generation=task_generation(pid);
    p->session=session_task(pid);p->fresh=1;return SC32_OK;
}
void sc32_gui_notify(int pid)
{gui_owner_t *p=owner(pid);if(p && !p->stopping)task_notify_generation(pid,p->generation,TASK_EVENT_INPUT);}
u32 sc32_gui_attention_head(int pid){gui_owner_t *p=owner(pid);return p?p->attention:0;}
void sc32_gui_attention_set(int pid,u32 head){gui_owner_t *p=owner(pid);if(p)p->attention=head;}
int sc32_gui_routing(void){return routing;}

static gui_chunk_t *new_chunk(gui_owner_t *p)
{
    gui_chunk_t *chunk=(gui_chunk_t *)pframe_alloc();if(!chunk)return 0;
    zero(chunk,sizeof(*chunk));p->pages++;return chunk;
}
static void drop_chunk(gui_owner_t *p,gui_chunk_t *chunk)
{pframe_free((u32)chunk);p->pages--;}
int sc32_gui_reserve(int pid,u32 messages)
{
    int error=sc32_gui_prepare(pid);if(error)return error;gui_owner_t *p=owner(pid);
    if(p->reserving)return SC32_E_BUSY;
    p->reservation_mark=p->last;p->reserving=1;
    if(messages>0xFFFFFFFFu-p->count){sc32_gui_reservation_end(pid,0);return SC32_E_RANGE;}
    u32 space=p->tail?GUI_CHUNK_MESSAGES-p->tail->end:0;
    for(gui_chunk_t *q=p->tail?p->tail->next:0;q;q=q->next)space+=GUI_CHUNK_MESSAGES;
    while(space<messages){
        gui_chunk_t *q=new_chunk(p);
        if(!q){sc32_gui_reservation_end(pid,0);return SC32_E_MEMORY;}
        if(p->last)p->last->next=q;else p->head=p->tail=q;
        p->last=q;space+=GUI_CHUNK_MESSAGES;
    }
    return SC32_OK;
}
void sc32_gui_reservation_end(int pid,int committed)
{
    gui_owner_t *p=owner(pid);if(!p || !p->reserving)return;
    if(!committed){
        gui_chunk_t *q=p->reservation_mark?p->reservation_mark->next:p->head;
        if(p->reservation_mark)p->reservation_mark->next=0;else p->head=p->tail=0;
        while(q){gui_chunk_t *next=q->next;drop_chunk(p,q);q=next;}
        p->last=p->reservation_mark;
        if(!p->tail)p->tail=p->last;
    }else{
        /* 成功后只保留当前尾部，空队列最多缓存一页归本人寿命。
         * 63是页内分配粒度，不是队列上限；消费再填不每消息借还PF。 */
        if(p->tail){gui_chunk_t *q=p->tail->next;p->tail->next=0;p->last=p->tail;
            while(q){gui_chunk_t *next=q->next;drop_chunk(p,q);q=next;}}
        p->fresh=0;
    }
    p->reserving=0;p->reservation_mark=0;
    if(!committed && p->fresh && !p->count && !p->attention && !wm_sc32_owner_has_windows(pid))
        objpool_release(&owners,pid);
}
void sc32_gui_stamp(int pid,ScMessage *message)
{
    gui_owner_t *p=owner(pid);if(!p)return;
    message->size=64;message->version=1;
    if(++p->sequence_lo==0)p->sequence_hi++;
    message->sequence_lo=p->sequence_lo;message->sequence_hi=p->sequence_hi;
    message->tick=sc_ticks;
    if(!(message->flags&SC32_MSG_APPLICATION))message->flags|=SC32_MSG_KERNEL;
    if(message->id!=SC32_MSG_MOUSEMOVE){p->merge_barrier_lo=p->sequence_lo;p->merge_barrier_hi=p->sequence_hi;}
}
static int earlier(const ScMessage *a,const ScMessage *b)
{return a->sequence_hi<b->sequence_hi || (a->sequence_hi==b->sequence_hi && a->sequence_lo<b->sequence_lo);}
static void lost(gui_owner_t *p,u32 reason,u32 lo,u32 hi)
{
    int first=!p->loss_pending;
    if(first){
        zero(&p->loss,sizeof(p->loss));p->loss.id=SC32_MSG_QUEUELOSS;
        p->loss.flags=SC32_MSG_DEFERRED;sc32_gui_stamp((int)p->pid,&p->loss);
        p->loss_pending=1;
        p->input_barrier_lo=p->loss.sequence_lo;p->input_barrier_hi=p->loss.sequence_hi;
        /* 先挂损失屏障，再取消输入/capture；撤销通知即使再次OOM也
         * 不递归重置整棵窗口图。这里没有伪造任何KEYUP或按钮松开。 */
        if(reason==2)wm_sc32_loss_epoch();
        wm_sc32_loss_owner((int)p->pid);
    }
    if(reason==1){p->loss.flags|=SC32_MSG_RAWLOSS;p->loss.data[3]=lo;p->loss.data[4]=hi;}
    else if(reason==2){if(++p->lost_lo==0)p->lost_hi++;}
    p->loss.data[0]=p->lost_lo;p->loss.data[1]=p->lost_hi;p->loss.data[2]|=reason;
    p->loss.epoch=wm_sc32_route_epoch();p->loss.data[5]=p->loss.epoch;
    sc32_gui_notify((int)p->pid);
    if(first && reason==2 && !loss_broadcast){
        /* 软件OOM撤销了共享raw旧epoch，别的窗也可能有尚未路由的
         * 通断码。稀有失败路径明确广播重同步，bit4不增加任何虚假
         * 丢失计数；正常输入仍沿单owner链，不扫描全部GUI进程。 */
        loss_broadcast=1;
        for(int id=objpool_next(&owners,-1);id>=0;id=objpool_next(&owners,id)){
            gui_owner_t *other=owner(id);
            if(other && other!=p && !other->stopping && other->session==session_active())lost(other,4,0,0);
        }
        loss_broadcast=0;
    }
}
void sc32_gui_emit(int pid,ScMessage *message)
{
    gui_owner_t *p=owner(pid);if(!p || p->stopping || TASK(pid).state==2)return;
    if(!message->sequence_lo && !message->sequence_hi)sc32_gui_stamp(pid,message);
    int device=message->id==SC32_MSG_KEYDOWN || message->id==SC32_MSG_KEYUP ||
        message->id==SC32_MSG_TEXT || message->id==SC32_MSG_MOUSEMOVE ||
        message->id==SC32_MSG_BUTTONDOWN || message->id==SC32_MSG_BUTTONUP;
    if(device && p->loss_pending){wm_sc32_input_revoked(pid,message);lost(p,2,0,0);return;}
    if(message->id==SC32_MSG_MOUSEMOVE && p->tail && p->tail->end>p->tail->begin){
        ScMessage *last=&p->tail->messages[p->tail->end-1];
        if(last->id==message->id && last->window==message->window && last->generation==message->generation
            && last->epoch==message->epoch && last->data[2]==message->data[2] && last->data[4]==message->data[4]
            && (last->sequence_hi>p->merge_barrier_hi || (last->sequence_hi==p->merge_barrier_hi
                && last->sequence_lo>=p->merge_barrier_lo))){
            /* 保存最新事实屏障，而非只看最早pending：旧PAINT之前
             * 可能还有一个较新的CLOSE。O(1)水位能阻止跨任何按钮/
             * 焦点/虚拟待办合并，热路径不扫描全部attention窗口。 */
            u32 lo=last->sequence_lo,hi=last->sequence_hi;*last=*message;
            last->sequence_lo=lo;last->sequence_hi=hi;last->flags|=SC32_MSG_COALESCED;
            sc32_gui_notify(pid);return;
        }
    }
    if(!p->tail || p->tail->end==GUI_CHUNK_MESSAGES){
        gui_chunk_t *q=p->tail?p->tail->next:0;
        if(!q){q=new_chunk(p);if(!q){lost(p,2,0,0);return;}
            if(p->last)p->last->next=q;else p->head=q;p->last=q;}
        p->tail=q;
    }
    if(p->count==0xFFFFFFFFu){lost(p,2,0,0);return;}
    p->tail->messages[p->tail->end++]=*message;p->count++;sc32_gui_notify(pid);
}
static void pop(gui_owner_t *p)
{
    gui_chunk_t *q=p->head;if(!q || q->begin==q->end)return;
    q->begin++;p->count--;
    if(q->begin==q->end){
        if(q!=p->tail){p->head=q->next;drop_chunk(p,q);}
        else q->begin=q->end=0;
    }
}
static int next_message(int pid,ScMessage *result,int consume)
{
    gui_owner_t *p=owner(pid);if(!p || p->stopping)return 0;
    while(p->head && p->head->begin<p->head->end){
        ScMessage *m=&p->head->messages[p->head->begin];
        int device=m->id==SC32_MSG_KEYDOWN || m->id==SC32_MSG_KEYUP || m->id==SC32_MSG_TEXT
            || m->id==SC32_MSG_MOUSEMOVE || m->id==SC32_MSG_BUTTONDOWN || m->id==SC32_MSG_BUTTONUP;
        int revoked=device && (m->sequence_hi<p->input_barrier_hi ||
            (m->sequence_hi==p->input_barrier_hi && m->sequence_lo<p->input_barrier_lo));
        if(!revoked && wm_sc32_message_valid(pid,m))break;
        /* 丢失屏障前已缓存的输入也撤销，不能在QUEUELOSS重同步前
         * 重放旧KEYDOWN复活按住/拖动。状态/FOCUS等真实事实仍保留。 */
        pop(p);
    }
    ScMessage *queued=p->head && p->head->begin<p->head->end?&p->head->messages[p->head->begin]:0;
    ScMessage attention;int has=wm_sc32_pending(pid,&attention,0),source=0;
    const ScMessage *chosen=queued;
    if(has && (!chosen || earlier(&attention,chosen))){chosen=&attention;source=1;}
    if(p->loss_pending && (!chosen || earlier(&p->loss,chosen))){chosen=&p->loss;source=2;}
    if(!chosen)return 0;
    *result=*chosen;
    if(consume){if(source==1)(void)wm_sc32_pending(pid,&attention,1);
        else if(source==2)p->loss_pending=0;else pop(p);}
    return 1;
}
static int message_query(int pid,u32 request,u32 output,int consume)
{
    int error=sc32_core_output(pid,output,64);if(error)return error;if(request)return SC32_E_INVALID;
    ScMessage message;if(!next_message(pid,&message,0))return SC32_PENDING;
    if(paging_user_copy(TASK(pid).pd,output,&message,64,1))return SC32_E_INVALID;
    if(consume)(void)next_message(pid,&message,1);
    return SC32_OK;
}
int sc32_gui_wait_check(int pid,u32 request,u32 output,u32 *timeout)
{
    if(output || !timeout)return SC32_E_INVALID;
    Sc32MessageWait input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(input.reserved || (input.timeout>0x7FFFFFFFu && input.timeout!=SC32_INFINITE))return SC32_E_INVALID;
    *timeout=input.timeout;ScMessage message;
    if(next_message(pid,&message,0))return SC32_OK;
    return input.timeout?SC32_PENDING:SC32_TIMEOUT;
}
int sc32_gui_wait_result(int pid,u32 events)
{ScMessage message;if(next_message(pid,&message,0))return SC32_OK;
 return events&TASK_EVENT_TIMEOUT?SC32_TIMEOUT:SC32_PENDING;}
int sc32_gui_stop_step(int pid)
{
    gui_owner_t *p=objpool_get(&owners,pid);if(!p)return 1;
    if(!p->stopping){wm_sc32_owner_stopped(pid);p->stopping=1;}
    for(u32 budget=0;budget<32;budget++){
        if(p->attention){(void)wm_sc32_attention_stop(pid);continue;}
        if(!p->head)break;
        gui_chunk_t *q=p->head;p->head=q->next;drop_chunk(p,q);
    }
    if(p->head || p->attention)return 0;
    objpool_release(&owners,pid);return 1;
}
void sc32_gui_pump(void)
{
    if(task_pid()!=0 || !initialized)return;
    if(!sc32_raw_active()){
        if(keyboard_legacy_pending() || mouse_events_pending())return;
        sc32_raw_start();sc32_raw_loss(raw_loss_previous);return;
    }
    routing=1;u32 losses[2];sc32_raw_loss(losses);
    if(losses[0]!=raw_loss_previous[0] || losses[1]!=raw_loss_previous[1]){
        raw_loss_previous[0]=losses[0];raw_loss_previous[1]=losses[1];
        /* raw缺口属于共享硬件流，不声称每个窗各丢了这么多输入。
         * 本会话各GUI收到明确RAW_SCOPE并重同步，旧epoch记录随后丢弃。 */
        wm_sc32_loss_epoch();
        for(int id=objpool_next(&owners,-1);id>=0;id=objpool_next(&owners,id)){
            gui_owner_t *p=owner(id);if(p && p->session==session_active())lost(p,1,losses[0],losses[1]);
        }
    }
    Sc32RawEvent event;
    for(u32 budget=0;budget<256 && sc32_raw_peek(&event);budget++){
        if(event.session!=wm_sc32_session_id() || event.epoch!=wm_sc32_route_epoch())sc32_raw_pop();
        else if(wm_sc32_route(&event))sc32_raw_pop();else break;
    }
    routing=0;
}

static int publish(int pid,u32 output,void *value,u32 bytes)
{return paging_user_copy(TASK(pid).pd,output,value,bytes,1)?SC32_E_INVALID:SC32_OK;}
static int no_output(u32 output)
{return output?SC32_E_INVALID:SC32_OK;}
static int title_read(int pid,u32 address,char text[128])
{
    for(u32 i=0;i<128;i++){
        if(paging_user_copy(TASK(pid).pd,address+i,text+i,1,0))return SC32_E_INVALID;
        if(!text[i])return gfx_utf8_valid(text)?SC32_OK:SC32_E_INVALID;
        if((u8)text[i]<32)return SC32_E_INVALID;
    }
    return SC32_E_RANGE;
}
static int reserved_zero(const u32 *values,u32 count)
{for(u32 i=0;i<count;i++)if(values[i])return 0;return 1;}
static int changes_begin(int pid,u32 count,int *other)
{
    if(count>0xFFFFFFFFu/8-2)return SC32_E_RANGE;
    int error=sc32_gui_reserve(pid,count*8+16);if(error)return error;
    *other=wm_sc32_focus_pid();
    if(*other>0 && *other!=pid){error=sc32_gui_reserve(*other,8);
        if(error){sc32_gui_reservation_end(pid,0);return error;}}
    else *other=0;
    return SC32_OK;
}
static void changes_end(int pid,int other,int committed)
{sc32_gui_reservation_end(pid,committed);if(other)sc32_gui_reservation_end(other,committed);}
static int queue_query(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,32);if(error)return error;if(request)return SC32_E_INVALID;
    gui_owner_t *p=owner(pid);ScQueueInfo info={0};info.size=32;info.version=1;
    if(p){info.messages=p->count+wm_sc32_pending_count(pid)+(p->loss_pending!=0);info.pages=p->pages;info.lost_lo=p->lost_lo;info.lost_hi=p->lost_hi;
        info.flags=p->loss_pending?1u:0u;}
    info.input_epoch=wm_sc32_route_epoch();
    return publish(pid,output,&info,sizeof(info));
}
static int post_message(int pid,u32 request,u32 output)
{
    if(output)return SC32_E_INVALID;
    ScMessage message;
    int error=sc32_core_input(pid,request,&message,64);if(error)return error;
    if((message.id<SC32_MSG_USER && message.id!=SC32_MSG_COMMAND) || message.flags || message.epoch
        || message.sequence_lo || message.sequence_hi || message.tick)return SC32_E_INVALID;
    if(message.window){ScWindowInfo info;error=wm_sc32_query(pid,message.window,message.generation,&info);if(error)return error;}
    else if(message.generation || message.id==SC32_MSG_COMMAND)return SC32_E_INVALID;
    error=sc32_gui_reserve(pid,1);if(error)return error;
    message.flags=SC32_MSG_APPLICATION;sc32_gui_emit(pid,&message);sc32_gui_reservation_end(pid,1);
    return SC32_OK;
}
int sc32_gui_call(int pid,u32 operation,u32 request,u32 output)
{
    /* helper完成本人/IF/整区前检。有effects的输出首先预验，再读取
     * request和保留位，所有候选资源先留齐，绝不让坏out变成R3盲写。 */
    int error=0,other=0;Sc32WindowRef reference;
    switch(operation){
    case SC32_GUI_CREATE:{
        error=sc32_core_output(pid,output,16);if(error)return error;Sc32WindowCreate input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(!reserved_zero(input.reserved,6))return SC32_E_INVALID;
        char title[128];
        error=title_read(pid,input.title,title);if(error)return error;
        error=changes_begin(pid,1,&other);if(error)return error;
        error=wm_sc32_create(pid,&input,title,&reference);changes_end(pid,other,!error);
        return error?error:publish(pid,output,&reference,16);
    }
    case SC32_GUI_QUERY:{
        error=sc32_core_output(pid,output,96);if(error)return error;
        error=sc32_core_input(pid,request,&reference,16);if(error)return error;ScWindowInfo info;
        error=wm_sc32_query(pid,reference.window,reference.generation,&info);
        return error?error:publish(pid,output,&info,96);
    }
    case SC32_GUI_ACTIVATE:case SC32_GUI_DESTROY:case SC32_GUI_CLOSE_CANCEL:{
        if(no_output(output))return SC32_E_INVALID;
        error=sc32_core_input(pid,request,&reference,16);if(error)return error;
        ScWindowInfo valid;error=wm_sc32_query(pid,reference.window,reference.generation,&valid);if(error)return error;
        if(operation==SC32_GUI_CLOSE_CANCEL)return wm_sc32_cancel_close(pid,reference.window,reference.generation);
        u32 count=wm_sc32_change_count(pid,reference.window,reference.generation);
        if(!count)return SC32_E_STALE;
        error=changes_begin(pid,count,&other);
        if(error && operation==SC32_GUI_DESTROY){
            /* 释放本身不需要新资源。CREATE被用户过程拒绝或fini关闭
             * 时，不能因通知队列OOM留下候选窗/画布；真实通知缺口
             * 走QUEUELOSS，三环显式Destroy事实仍可同步清理自己的表。 */
            return wm_sc32_destroy(pid,reference.window,reference.generation);
        }
        if(error)return error;
        if(operation==SC32_GUI_ACTIVATE)error=wm_sc32_activate(pid,reference.window,reference.generation);
        else if(operation==SC32_GUI_DESTROY)error=wm_sc32_destroy(pid,reference.window,reference.generation);
        else error=wm_sc32_cancel_close(pid,reference.window,reference.generation);
        changes_end(pid,other,!error);return error;
    }
    case SC32_GUI_PLACE:{
        error=sc32_core_output(pid,output,96);if(error)return error;Sc32WindowPlace input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(!reserved_zero(input.reserved,2))return SC32_E_INVALID;
        error=changes_begin(pid,wm_sc32_change_count(pid,input.window,input.generation),&other);if(error)return error;
        ScWindowInfo info;error=wm_sc32_place(pid,&input,&info);changes_end(pid,other,!error);
        return error?error:publish(pid,output,&info,96);
    }
    case SC32_GUI_CONTROL:{
        error=sc32_core_output(pid,output,96);if(error)return error;Sc32WindowAction input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        error=changes_begin(pid,wm_sc32_change_count(pid,input.window,input.generation),&other);if(error)return error;
        ScWindowInfo info;error=wm_sc32_control(pid,&input,&info);changes_end(pid,other,!error);
        return error?error:publish(pid,output,&info,96);
    }
    case SC32_GUI_OWNER:{
        error=sc32_core_output(pid,output,96);if(error)return error;Sc32WindowOwner input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        error=changes_begin(pid,wm_sc32_change_count(pid,input.window,input.generation),&other);if(error)return error;
        ScWindowInfo info;error=wm_sc32_owner(pid,&input,&info);changes_end(pid,other,!error);
        return error?error:publish(pid,output,&info,96);
    }
    case SC32_GUI_TITLE:{
        if(output)return SC32_E_INVALID;
        Sc32WindowTitle input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        char title[128];error=title_read(pid,input.title,title);if(error)return error;
        return wm_sc32_title(pid,input.window,input.generation,title);
    }
    case SC32_GUI_PEEK:return message_query(pid,request,output,0);
    case SC32_GUI_GET:return message_query(pid,request,output,1);
    case SC32_GUI_WAIT:{u32 timeout;return sc32_gui_wait_check(pid,request,output,&timeout);}
    case SC32_GUI_POST:return post_message(pid,request,output);
    case SC32_GUI_INPUT:{
        error=sc32_core_output(pid,output,96);if(error)return error;
        error=sc32_core_input(pid,request,&reference,16);if(error)return error;ScInputSnapshot info;
        error=wm_sc32_input(pid,reference.window,reference.generation,&info);
        return error?error:publish(pid,output,&info,96);
    }
    case SC32_GUI_CAPTURE:{
        if(output)return SC32_E_INVALID;
        Sc32WindowAction input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(input.reserved || input.action>1)return SC32_E_INVALID;
        if(!input.action)return wm_sc32_capture(pid,input.window,input.generation,0);
        error=changes_begin(pid,1,&other);if(error)return error;
        error=wm_sc32_capture(pid,input.window,input.generation,input.action);changes_end(pid,other,!error);return error;
    }
    case SC32_GUI_INVALIDATE:{
        if(output)return SC32_E_INVALID;
        Sc32Invalidate input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(input.reserved)return SC32_E_INVALID;
        return wm_sc32_invalidate(pid,&input);
    }
    case SC32_GUI_PAINT_BEGIN:{
        error=sc32_core_output(pid,output,64);if(error)return error;
        error=sc32_core_input(pid,request,&reference,16);if(error)return error;ScPaintInfo info;
        error=wm_sc32_paint_begin(pid,reference.window,reference.generation,&info);
        return error?error:publish(pid,output,&info,64);
    }
    case SC32_GUI_PAINT_END:{
        if(output)return SC32_E_INVALID;
        Sc32PaintEnd input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        return wm_sc32_paint_end(pid,&input);
    }
    case SC32_GUI_SUBMIT:{
        if(output)return SC32_E_INVALID;
        ScSubmitDesc input;
        error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
        if(!reserved_zero(input.reserved,4) || !input.rectangle_count || input.rectangle_count>SC32_GUI_RECT_LIMIT)
            return SC32_E_INVALID;
        if(!paging_user_range(TASK(pid).pd,(u32)input.pixels,input.bytes))return SC32_E_INVALID;
        ScSubmitRect rectangles[SC32_GUI_RECT_LIMIT];
        if(paging_user_copy(TASK(pid).pd,(u32)input.rectangles,rectangles,input.rectangle_count*sizeof(ScSubmitRect),0))return SC32_E_INVALID;
        return wm_sc32_submit(pid,&input,rectangles);
    }
    case SC32_GUI_QUEUE:return queue_query(pid,request,output);
    default:return SC32_E_NOT_SUPPORTED;
    }
}
