#include "SCLIBPRIVATE.H"

typedef struct SlSubscription SlSubscription;
struct SlSubscription {
    SlSubscription *previous,*next,*retired;
    ScU32 token,type,flags,alive;
    ScI32 priority;
    ScEventProc procedure;
    void *context;
    ScDropProc context_drop;
};
struct ScEventHub {SlBase base;
    SlSubscription *first,*last,*retired;
    ScU32 count,generation,emitting;
    ScWideCounter sequence;};
static void sl_event_collect(ScEventHub *hub,int quiet)
{
    if(hub->emitting)return;
    while(hub->retired){
        SlSubscription *p=hub->retired;
        hub->retired=p->retired;
        if(!quiet && p->context_drop)p->context_drop(p->context,0);
        free(p);
    }
}
static void sl_event_unlink(ScEventHub *hub,SlSubscription *p)
{
    if(!p->alive)return;
    if(p->previous)p->previous->next=p->next;
    else hub->first=p->next;
    if(p->next)p->next->previous=p->previous;
    else hub->last=p->previous;
    p->alive=0;
    p->retired=hub->retired;
    hub->retired=p;
    hub->count--;
}
static void sl_events_dispose(SlBase *base,int quiet)
{
    ScEventHub *hub=(ScEventHub *)base;
    while(hub->first)sl_event_unlink(hub,hub->first);
    hub->emitting=0;
    sl_event_collect(hub,quiet);
}
ScStatus ScEventHubCreate(ScEventHub **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScEventHub *hub=sl_object(SL_EVENTS,sizeof(*hub),sl_events_dispose);
    if(!hub)return SC_UTIL_E_MEMORY;
    hub->generation=1;
    *out=hub;
    return 0;
}
ScStatus ScEventHubRetain(ScEventHub *hub){return sl_retain(hub,SL_EVENTS);}
void ScEventHubRelease(ScEventHub *hub){sl_release(hub,SL_EVENTS);}
ScStatus ScEventConnect(ScEventHub *hub,const ScSubscriptionDesc *desc,ScU32 *token)
{
    ScStatus status=sl_enter(hub,SL_EVENTS);
    if(status)return status;
    status=sl_mutation(&hub->base,hub->generation);
    if(!status)status=sl_input(desc,sizeof(*desc));
    if(!status && (!token || !desc->procedure || (desc->flags&~SC_EVENT_ONCE) || desc->reserved[0] || desc->reserved[1]))status=SC_UTIL_E_INVALID;
    if(status){sl_leave(hub);
        return status;}
    SlSubscription *fresh=calloc(1,sizeof(*fresh));
    if(!fresh){sl_leave(hub);
        return SC_UTIL_E_MEMORY;}
    status=sl_pin((const void *)desc->procedure);
    if(!status)status=sl_pin((const void *)desc->context_drop);
    if(!status)status=sl_identifier(&fresh->token);
    if(status){free(fresh);
        sl_leave(hub);
        return status;}
    fresh->type=desc->type;
    fresh->flags=desc->flags;
    fresh->priority=desc->priority;
    fresh->procedure=desc->procedure;
    fresh->context=desc->context;
    fresh->context_drop=desc->context_drop;
    fresh->alive=1;
    SlSubscription *next=hub->first;
    while(next && next->priority>=fresh->priority)next=next->next;
    fresh->next=next;
    fresh->previous=next?next->previous:hub->last;
    if(fresh->previous)fresh->previous->next=fresh;
    else hub->first=fresh;
    if(next)next->previous=fresh;
    else hub->last=fresh;
    hub->count++;
    hub->generation++;
    *token=fresh->token;
    sl_leave(hub);
    return 0;
}
ScStatus ScEventDisconnect(ScEventHub *hub,ScU32 token)
{
    ScStatus status=sl_enter(hub,SL_EVENTS);
    if(status)return status;
    status=sl_mutation(&hub->base,hub->generation);
    if(status){sl_leave(hub);
        return status;}
    SlSubscription *p=hub->first;
    while(p && p->token!=token)p=p->next;
    if(!p){sl_leave(hub);
        return SC_UTIL_E_STALE;}
    sl_event_unlink(hub,p);
    hub->generation++;
    hub->base.mutation=1;
    sl_event_collect(hub,0);
    hub->base.mutation=0;
    sl_leave(hub);
    return 0;
}
ScStatus ScEventEmit(ScEventHub *hub,const ScEvent *event,ScU32 flags,ScEventResult *out)
{
    ScStatus status=sl_enter(hub,SL_EVENTS);
    if(status)return status;
    status=sl_input(event,sizeof(*event));
    if(!status)status=sl_output(out,sizeof(*out));
    if(!status && ((flags&~SC_EVENT_STOP_ON_ERROR) || event->reserved[0] || event->reserved[1] || event->reserved[2] || (event->bytes && !event->data)))status=SC_UTIL_E_INVALID;
    if(!status && hub->base.mutation)status=SC_UTIL_E_BUSY;
    if(!status && hub->sequence.lo==0xFFFFFFFFu && hub->sequence.hi==0xFFFFFFFFu)status=SC_UTIL_E_RANGE;
    if(status){sl_leave(hub);
        return status;}
    ScEvent local=*event;
    sl_counter_add(&hub->sequence,1);
    local.sequence_lo=hub->sequence.lo;
    local.sequence_hi=hub->sequence.hi;
    ScEventResult result={32,1,0,0,0,{0,0,0}};
    ScU32 limit=sl_token;
    hub->emitting++;
    for(SlSubscription *p=hub->first;p;){
        SlSubscription *next=p->next;
        if(p->alive && p->token<=limit && (!p->type || p->type==event->type)){
            /* once在回调前退出活链，嵌套emit不会再次调用；context及
             * 原next仍在retired链，最外层返回后才执行drop。 */
            if(p->flags&SC_EVENT_ONCE){
                if(hub->generation==0xFFFFFFFFu){result.first_error=SC_UTIL_E_RANGE;
                    result.stopped=1;
                    break;}
                sl_event_unlink(hub,p);
                hub->generation++;
            }
            ScStatus callback=p->procedure(&local,p->context);
            result.called++;
            if(callback==SC_UTIL_STOP){result.stopped=1;
                break;}
            if(callback && !result.first_error)result.first_error=callback;
            if(callback && (flags&SC_EVENT_STOP_ON_ERROR)){result.stopped=1;
                break;}
        }
        if(hub->base.closing){result.stopped=1;
            if(!result.first_error)result.first_error=SC_UTIL_E_CANCELLED;
            break;}
        p=next;
    }
    hub->emitting--;
    hub->base.mutation=1;
    sl_event_collect(hub,0);
    hub->base.mutation=0;
    memcpy(out,&result,sizeof(result));
    status=result.first_error?result.first_error:result.stopped?SC_UTIL_STOP:0;
    sl_leave(hub);
    return status;
}
ScStatus ScEventHubQuery(const ScEventHub *hub,ScContainerInfo *out)
{
    ScStatus status=sl_valid(hub,SL_EVENTS);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,hub->count,hub->count,sizeof(ScSubscriptionDesc),hub->generation,hub->emitting!=0);
    return 0;
}
ScStatus ScEventHubClear(ScEventHub *hub)
{
    ScStatus status=sl_enter(hub,SL_EVENTS);
    if(status)return status;
    status=sl_mutation(&hub->base,hub->generation);
    if(status){sl_leave(hub);
        return status;}
    if(hub->count){while(hub->first)sl_event_unlink(hub,hub->first);
        hub->generation++;}
    hub->base.mutation=1;
    sl_event_collect(hub,0);
    hub->base.mutation=0;
    sl_leave(hub);
    return 0;
}
