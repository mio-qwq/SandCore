#include "SCUIPRIVATE.H"

static ScStatus ui_input(const void *p,ScU32 bytes)
{
    const ScU32 *v=(const ScU32 *)p;
    if(!p)return SC_UI_E_INVALID;
    if(v[0]!=bytes || v[1]!=1)return SC_UI_E_VERSION;
    return SC_UI_OK;
}
static ScStatus ui_output(void *p,ScU32 bytes)
{
    ScU32 *v=(ScU32 *)p;
    if(!p)return SC_UI_E_INVALID;
    if(v[0]<bytes || v[0]>65536u || v[1]!=1)return SC_UI_E_VERSION;
    return SC_UI_OK;
}
static int ui_add(ScU32 a,ScU32 b,ScU32 *out)
{if(b>UINT_MAX-a)return 0;
    *out=a+b;
    return 1;
    }
static int ui_multiply(ScU32 a,ScU32 b,ScU32 *out)
{if(a && b>UINT_MAX/a)return 0;
    *out=a*b;
    return 1;
    }
static int ui_signed_add(ScI32 a,ScI32 b,ScI32 *out)
{if((b>0 && a>INT_MAX-b) || (b<0 && a<INT_MIN-b))return 0;
    *out=a+b;
    return 1;
    }
static ScU32 ui_fraction(ScU32 extent,ScU32 part,ScU32 total)
{
    ScWideCounter product;
    ScU32 low=0,high=0,carry=0,bit;
    ScI32 shift;
    /* 32×32→双字，再除32；只有布局求比例，不假借编译器的long long。 */
    for(bit=0;bit<32;bit++)if((part>>bit)&1u){ScU32 add_low=extent<<bit,add_high=bit?extent>>(32-bit):0,old=low;
        low+=add_low;
        high+=add_high+(low<old);
        }
    product.lo=low;
    product.hi=high;
    low=0;
    for(shift=63;shift>=0;shift--){ScU32 overflow=carry>>31;
        carry=(carry<<1)|(shift>=32?((product.hi>>(shift-32))&1u):((product.lo>>shift)&1u));
        if(overflow || carry>=total){carry-=total;
            if(shift<32)low|=1u<<shift;
            }}
    return low;
}
static void ui_wide_add(ScWideCounter *v,ScU32 amount)
{ScU32 old=v->lo;
    v->lo+=amount;
    if(v->lo<old)v->hi++;
    }
static int ui_wide_compare(const ScWideCounter *a,const ScWideCounter *b)
{if(a->hi!=b->hi)return a->hi>b->hi?1:-1;
    return a->lo==b->lo?0:a->lo>b->lo?1:-1;
    }
static ScU32 ui_wide_difference(const ScWideCounter *a,const ScWideCounter *b)
{
    ScU32 lo=a->lo-b->lo,hi=a->hi-b->hi-(a->lo<b->lo);
    return hi?0x7fffffffu:lo>0x7fffffffu?0x7fffffffu:lo;
}
static ScStatus ui_now(ScWideCounter *out)
{
    ScMonotonicClock clock;
    ScStatus s;
    memset(&clock,0,sizeof(clock));
    clock.size=sizeof(clock);
    clock.version=1;
    s=ScSystemMonotonic(&clock);
    if(s<0)return s;
    *out=clock.ticks;
    return SC_UI_OK;
}
static ScStatus ui_pin(const void *address)
{return address?ScCallbackPin(address):SC_UI_OK;
    }
static ScStatus ui_fail(ScUiContext *c,ScStatus code,ScStatus cause,ScU32 domain,ScU32 operation,ScU32 node)
{
    if(c){
        memset(&c->error,0,sizeof(c->error));
        c->error.size=sizeof(c->error);
        c->error.version=1;
        c->error.code=code;
        c->error.cause_code=cause;
        c->error.domain=domain;
        c->error.raw=cause;
        c->error.operation=operation;
        c->error.node=node;
        c->error.revision=c->revision;
        if(cause<=SC32_E_INVALID && cause>=SC32_E_FAILED){
            ScErrorInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            if(ScErrorQuery(&info)==0){c->error.domain=info.domain;
                c->error.raw=info.raw;
                }
        }
    }
    return code;
}
static ScStatus ui_cause(ScUiContext *c,ScStatus s,ScU32 op,ScU32 node)
{
    ScStatus result=SC_UI_E_FAILED;
    if(s>=0)return s;
    if(s<=SC_UI_E_INVALID && s>=SC_UI_E_FAILED){if(c && c->error.size && c->error.code==s)return s;
        return ui_fail(c,s,s,0,op,node);
        }
    if(s==SC32_E_MEMORY || s==SC_UTIL_E_MEMORY)result=SC_UI_E_MEMORY;
    else if(s==SC32_E_RANGE || s==SC_UTIL_E_RANGE)result=SC_UI_E_RANGE;
    else if(s==SC32_E_VERSION)result=SC_UI_E_VERSION;
    else if(s==SC32_E_STALE || s==SC_UTIL_E_STALE)result=SC_UI_E_STALE;
    else if(s==SC32_E_BUSY || s==SC_UTIL_E_BUSY)result=SC_UI_E_BUSY;
    else if(s==SC32_E_INVALID || s==SC_UTIL_E_INVALID)result=SC_UI_E_INVALID;
    else if(s==SC_UTIL_E_UTF8)result=SC_UI_E_UTF8;
    else if(s==SC32_E_NOT_FOUND || s==SC_UTIL_E_NOT_FOUND)result=SC_UI_E_NOT_FOUND;
    else if(s==SC32_E_EXISTS || s==SC_UTIL_E_EXISTS)result=SC_UI_E_EXISTS;
    else if(s==SC32_E_NOT_SUPPORTED || s==SC_UTIL_E_NOT_SUPPORTED)result=SC_UI_E_NOT_SUPPORTED;
    else if(s==SC32_E_CANCELLED || s==SC_UTIL_E_CANCELLED)result=SC_UI_E_CANCELLED;
    else if(s==SC32_E_IO || s==SC_UTIL_E_IO)result=SC_UI_E_IO;
    return ui_fail(c,result,s,s<=SC_UTIL_E_INVALID?5u:SC32_DOMAIN_KERNEL,op,node);
}
static ScStatus ui_object_new(ScU32 bytes,ScU32 kind,void (*dispose)(UiObject *,int),void **out)
{
    UiObject *p;
    if(ui_stopping || !out || bytes<sizeof(*p))return SC_UI_E_INVALID;
    p=(UiObject *)calloc(1,bytes);
    if(!p)return SC_UI_E_MEMORY;
    p->kind=kind;
    p->references=1;
    p->dispose=dispose;
    p->next=ui_objects;
    if(ui_objects)ui_objects->previous=p;
    ui_objects=p;
    *out=p;
    return SC_UI_OK;
}
static ScStatus ui_object_retain(UiObject *p,ScU32 kind)
{
    if(!p || p->kind!=kind || p->closing || p->disposed)return SC_UI_E_STALE;
    if(p->references==UINT_MAX)return SC_UI_E_RANGE;
    p->references++;
    return SC_UI_OK;
}
static void ui_object_finish(UiObject *p)
{
    if(!p->disposed){p->disposed=1;
        p->dispose(p,ui_quiet);
        }
    if(p->previous)p->previous->next=p->next;
    else ui_objects=p->next;
    if(p->next)p->next->previous=p->previous;
    free(p);
}
static void ui_object_release(UiObject *p)
{
    if(!p || !p->references)return;
    if(--p->references)return;
    p->closing=1;
    if(!p->depth && !ui_finalizing && !p->disposed)ui_object_finish(p);
}
static ScStatus ui_object_enter(UiObject *p,ScU32 kind)
{
    if(ui_stopping || !p || p->kind!=kind || p->closing || p->disposed)return SC_UI_E_STALE;
    if(p->depth==UINT_MAX)return SC_UI_E_RANGE;
    p->depth++;
    return SC_UI_OK;
}
static void ui_object_leave(UiObject *p)
{
    if(p->depth)p->depth--;
    if(!p->depth && !p->references && !ui_finalizing && !p->disposed)ui_object_finish(p);
}
static ScStatus ui_text_copy(const ScUiText *text,ScString **out)
{
    ScStatus s;
    ScU32 bad;
    if(text && !text->size && !text->version && !text->data && !text->bytes)return ScStringCreate(0,0,out);
    s=ui_input(text,sizeof(*text));
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(!text->data && text->bytes)return SC_UI_E_INVALID;
    s=ScUtf8Validate(text->data,text->bytes,&bad);
    if(s<0)return SC_UI_E_UTF8;
    return ScStringCreate(text->data,text->bytes,out);
}
static void ui_text_view(ScString *string,ScUiText *text)
{
    ScView v;
    memset(text,0,sizeof(*text));
    text->size=sizeof(*text);
    text->version=1;
    if(!string)return;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    if(ScStringView(string,&v)==0){text->data=(const char *)v.data;
        text->bytes=v.bytes;
        }
}
static void ui_value_clear(UiValue *v)
{ScStringRelease(v->text);
    ScBufferRelease(v->bytes);
    memset(v,0,sizeof(*v));
    }
static ScStatus ui_value_copy(const ScValue *source,UiValue *out)
{
    UiValue next;
    ScStatus s=ui_input(source,sizeof(*source));
    ScU32 bad;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(source->flags || source->type>SC_VALUE_BYTES)return SC_UI_E_INVALID;
    memset(&next,0,sizeof(next));
    next.value=*source;
    if(source->type==SC_VALUE_UTF8){
        s=ScUtf8Validate(source->data,source->bytes,&bad);
        if(s<0)return SC_UI_E_UTF8;
        s=ScStringCreate(source->data,source->bytes,&next.text);
        if(s<0)return s;
        {ScView v;
            memset(&v,0,sizeof(v));
            v.size=sizeof(v);
            v.version=1;
            ScStringView(next.text,&v);
            next.value.data=v.data;
            }
    }
    else if(source->type==SC_VALUE_BYTES){
        s=ScBufferCreate(source->bytes,&next.bytes);
        if(s<0)return s;
        s=ScBufferAppend(next.bytes,source->data,source->bytes);
        if(s<0){ScBufferRelease(next.bytes);
            return s;
            }
        {ScView v;
            memset(&v,0,sizeof(v));
            v.size=sizeof(v);
            v.version=1;
            ScBufferView(next.bytes,&v);
            next.value.data=v.data;
            }
    }
    else if(source->data || source->bytes || (source->type!=SC_VALUE_COUNTER && source->hi)
        || (source->type==SC_VALUE_BOOL && source->lo>1) || (source->type==SC_VALUE_NULL && source->lo))return SC_UI_E_INVALID;
    *out=next;
    return SC_UI_OK;
}
static ScStatus ui_hash_new(ScU32 value_bytes,ScHash **out)
{
    ScHashDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.value_bytes=value_bytes;
    return ScHashCreate(&d,out);
}
static ScStatus ui_array_new(ScU32 item_bytes,ScArray **out)
{
    ScArrayDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.item_bytes=item_bytes;
    return ScArrayCreate(&d,out);
}
static void ui_context_dispose(UiObject *base,int quiet)
{
    ScUiContext *c=(ScUiContext *)base;
    UiNode *n;
    UiListener *listener;
    c->quiet=quiet;
    c->stopped=1;
    c->walking++;
    for(n=c->all_nodes;n;n=n->all_next){
        if(n->window && n->window->window)ScWindowDestroy(n->window->window);
        if(!(n->private_flags&UI_NODE_RETIRED))ui_node_retire(n);
    }
    c->walking--;
    ui_nodes_collect(c);
    listener=c->listeners;
    while(listener){UiListener *next=listener->next;
        if(!quiet && listener->description.context_drop)listener->description.context_drop(listener->description.context,0);
        free(listener);
        listener=next;
    }
    c->listeners=0;
    if(c->timer_index){ScIterator it;
        ScHashEntry e;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        if(ScHashIterate(c->timer_index,&it)==0){memset(&e,0,sizeof(e));
            e.size=sizeof(e);
            e.version=1;
            while(ScHashNext(&it,&e)==0)free(*(UiTimer * const *)e.value);
            }}
    ScArrayRelease(c->timers);
    ScHashRelease(c->timer_index);
    ScHashRelease(c->nodes);
    if(c->classes){ScIterator it;
        ScHashEntry e;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        if(ScHashIterate(c->classes,&it)==0){memset(&e,0,sizeof(e));
            e.size=sizeof(e);
            e.version=1;
            while(ScHashNext(&it,&e)==0){UiClassDef *item=*(UiClassDef * const *)e.value;
                if(!quiet && item->description.context_drop)item->description.context_drop(item->description.context,0);
                ScStringRelease(item->name);
                free(item);
                }}}
    ScHashRelease(c->classes);
    if(c->commands){ScIterator it;
        ScHashEntry e;
        memset(&it,0,sizeof(it));
        it.size=sizeof(it);
        it.version=1;
        if(ScHashIterate(c->commands,&it)==0){memset(&e,0,sizeof(e));
            e.size=sizeof(e);
            e.version=1;
            while(ScHashNext(&it,&e)==0){UiMenuEntry *item=*(UiMenuEntry * const *)e.value;
                ScStringRelease(item->label);
                ScStringRelease(item->help);
                ScStringRelease(item->caption);
                ScUiIconRelease(item->description.icon);
                free(item);
                }}}
    ScHashRelease(c->commands);
    ScCacheRelease(c->glyphs);
    c->timers=0;
    c->timer_index=0;
    c->nodes=0;
    c->classes=0;
    c->commands=0;
    c->glyphs=0;
}
ScStatus ScUiContextCreate(const ScUiContextDesc *description,ScUiContext **out)
{
    ScUiContext *c;
    ScStatus s=ui_input(description,sizeof(*description));
    ScU32 i;
    void *p=0;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    for(i=0;i<4;i++)if(description->reserved[i])return SC_UI_E_INVALID;
    if(description->flags&~SC_UI_STOP_LAST_ROOT || description->scale_mode>1 || description->scheduling_quantum>256
        || (description->default_font_face && description->default_font_face!=12 && description->default_font_face!=16)
        || description->default_font_pixels>64)return SC_UI_E_INVALID;
    if(description->scale_mode==1 && (description->scale_percent<50 || description->scale_percent>400))return SC_UI_E_RANGE;
    s=ui_object_new(sizeof(*c),UI_CONTEXT,ui_context_dispose,&p);
    if(s<0)return s;
    c=(ScUiContext *)p;
    c->description=*description;
    c->next_node=1;
    c->next_class=SC_UI_CLASS_FIRST;
    c->next_token=1;
    c->revision=1;
    if(!c->description.cache_budget_bytes)c->description.cache_budget_bytes=524288u;
    if(!c->description.undo_budget_bytes)c->description.undo_budget_bytes=262144u;
    if(!c->description.default_font_face)c->description.default_font_face=16;
    if(!c->description.default_font_pixels)c->description.default_font_pixels=c->description.default_font_face;
    if(!c->description.scheduling_quantum)c->description.scheduling_quantum=32;
    s=ui_hash_new(sizeof(UiNode *),&c->nodes);
    if(s==0)s=ui_hash_new(sizeof(UiClassDef *),&c->classes);
    if(s==0)s=ui_hash_new(sizeof(UiMenuEntry *),&c->commands);
    if(s==0)s=ui_hash_new(sizeof(UiTimer *),&c->timer_index);
    if(s==0)s=ui_array_new(sizeof(UiTimer *),&c->timers);
    if(s==0)s=ui_theme_refresh(c);
    if(s==0){ScMonotonicClock clock;
        memset(&clock,0,sizeof(clock));
        clock.size=sizeof(clock);
        clock.version=1;
        s=ScSystemMonotonic(&clock);
        if(s==0)c->hz=clock.hz;
        }
    if(s==0)s=ui_pin((const void *)description->error_procedure);
    if(s<0){ui_object_release(&c->object);
        return ui_cause(0,s,UI_OP_CREATE,0);
        }
    *out=c;
    return SC_UI_OK;
}
ScStatus ScUiContextRetain(ScUiContext *c){return ui_object_retain((UiObject *)c,UI_CONTEXT);
    }
void ScUiContextRelease(ScUiContext *c){ui_object_release((UiObject *)c);
    }
ScStatus ScUiContextQuery(const ScUiContext *c,ScUiContextInfo *out)
{
    ScUiContextInfo v;
    UiNode *n;
    UiListener *l;
    ScCacheInfo cache;
    ScContainerInfo timers;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.nodes=c->live_nodes;
    v.roots=c->live_roots;
    v.layout_revision=c->revision;
    v.notification_error=c->notification_error;
    for(n=c->all_nodes;n;n=n->all_next)if(n->window && !(n->private_flags&UI_NODE_RETIRED)){
        v.surface_bytes+=n->window->bytes;
        v.visible_roots+=n->window->visible!=0;
        v.dirty_roots+=n->window->paint_pending!=0;
        }
    for(l=c->listeners;l;l=l->next)if(!l->retired)v.listeners++;
    memset(&timers,0,sizeof(timers));
    timers.size=sizeof(timers);
    timers.version=1;
    if(ScHashQuery(c->timer_index,&timers)==0)v.timers=timers.length;
    memset(&cache,0,sizeof(cache));
    cache.size=sizeof(cache);
    cache.version=1;
    if(ScCacheQuery(c->glyphs,&cache)==0)v.cache_bytes=cache.used_bytes;
    v.received_messages=c->received_messages;
    v.paint_count=c->paint_count;
    v.submitted_bytes=c->submitted_bytes;
    memcpy(out,&v,sizeof(v));
    return SC_UI_OK;
}
ScStatus ScUiErrorQuery(const ScUiContext *c,ScUiErrorInfo *out)
{
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    if(!c->error.size){ScUiErrorInfo v;
        memset(&v,0,sizeof(v));
        v.size=sizeof(v);
        v.version=1;
        memcpy(out,&v,sizeof(v));
        }
    else memcpy(out,&c->error,sizeof(*out));
    return SC_UI_OK;
}
ScStatus ScUiContextStop(ScUiContext *c,ScI32 code)
{if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    c->stopped=1;
    c->exit_code=code;
    return SC_UI_OK;
    }
void ScUiFinalize(void)
{
    UiObject *p;
    ui_stopping=1;
    ui_quiet=1;
    ui_finalizing=1;
    ui_drag_finish();
    /* 两遍保留所有header，解除交叉引用时不读已经free的对象。 */
    for(p=ui_objects;p;p=p->next)if(!p->disposed){p->closing=1;
        p->disposed=1;
        p->dispose(p,1);
        }
    p=ui_objects;
    while(p){UiObject *next=p->next;
        free(p);
        p=next;
        }
    ui_objects=0;
    ui_unicode_finish();
    ui_finalizing=0;
}
