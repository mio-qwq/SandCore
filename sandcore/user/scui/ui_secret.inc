#include "../SCUISECRET.H"
#include "SECRETCORE.H"
typedef struct {
    UiSecretBuffer active,pending;
    void *pages;
    ScU32 page_bytes,revision,consuming,armed,clearing,caret,first_character,timer,invalidated;
    ScWideCounter deadline;
    ScUiSecretSubmit submit;
    UiNode *node;
} UiSecret;
static UiSecret *ui_secret(UiNode *n){return n && n->kind==SC_UI_SECRET?(UiSecret *)n->secret:0;
}
static void ui_secret_notice(UiNode *n)
{
    UiSecret *s=ui_secret(n);
    if(!s)return;
    if(s->revision<UINT_MAX)s->revision++;
    if(n->value_revision<UINT_MAX)n->value_revision++;
    ui_node_damage(n);
    if(!ui_quiet && !n->owner->quiet){ScUiSecretInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        info.node=n->id;
        info.bytes=s->active.bytes;
        info.characters=ui_secret_characters(&s->active,s->active.bytes);
        info.maximum=s->active.maximum;
        info.revision=s->revision;
        info.flags=(s->armed?SC_UI_SECRET_ARMED:0)|(s->consuming?SC_UI_SECRET_CONSUMING:0)|(s->invalidated?SC_UI_SECRET_INVALIDATED:0);
        ui_event(n,SC_UI_EVENT_VALUE_CHANGED,&info,sizeof(info));
    }
}
static void ui_secret_cancel_timer(UiNode *n)
{UiSecret *s=ui_secret(n);
    if(s && s->timer){ScUiTimerCancel(n->owner,s->timer);
        s->timer=0;
}}
static void ui_secret_erase_node(UiNode *n,ScU32 notice,ScU32 invalidate)
{
    UiSecret *s=ui_secret(n);
    if(!s)return;
    if(s->consuming){s->clearing=1;
        return;
    }ScU32 changed=s->active.bytes || s->pending.bytes || s->armed;
    if(invalidate && changed)s->invalidated=1;
    else if(!invalidate)s->invalidated=0;
    ui_secret_buffer_clear(&s->active);
    ui_secret_buffer_clear(&s->pending);
    s->armed=s->first_character=0;
    ui_secret_cancel_timer(n);
    if(changed && notice)ui_secret_notice(n);
}
static void ui_secret_clear_node(UiNode *n,ScU32 notice){ui_secret_erase_node(n,notice,1);
}
static ScStatus ui_secret_allocate(UiNode *n,ScU32 maximum)
{
    UiSecret *s=ui_secret(n);
    if(!s || !maximum || maximum>4096)return SC_UI_E_RANGE;
    ScU32 region=(maximum+1u+4095u)&~4095u;
    void *pages=0;
    ScStatus status=ScMemoryAllocatePages(region*2u,&pages);
    if(status<0)return ui_cause(n->owner,status,UI_OP_NODE,n->id);
    if(s->pages){ui_secret_wipe(s->pages,s->page_bytes);
        ScMemoryFreePages(s->pages);
    }
    s->pages=pages;
    s->page_bytes=region*2u;
    memset(&s->active,0,sizeof(s->active));
    s->active.data=pages;
    s->active.maximum=maximum;
    memset(&s->pending,0,sizeof(s->pending));
    s->pending.data=(unsigned char *)pages+region;
    s->pending.maximum=maximum;
    return 0;
}
static ScStatus ui_secret_init(UiNode *n)
{
    UiSecret *s=calloc(1,sizeof(*s));
    if(!s)return SC_UI_E_MEMORY;
    n->secret=s;
    s->node=n;
    s->revision=1;
    s->caret=1;
    n->flags|=SC_UI_FOCUSABLE|SC_UI_TABSTOP|SC_UI_PASSWORD;
    ScStatus status=ui_secret_allocate(n,128);
    if(status<0){free(s);
        n->secret=0;
    }return status;
}
static void ui_secret_destroy(UiNode *n,int quiet)
{
    UiSecret *s=ui_secret(n);
    if(!s)return;
    ui_secret_cancel_timer(n);
    if(s->pages){ui_secret_wipe(s->pages,s->page_bytes);
        ScMemoryFreePages(s->pages);
        s->pages=0;
    }
    if(!quiet && s->submit.context_drop)s->submit.context_drop(s->submit.context,0);
    ui_secret_wipe(s,sizeof(*s));
    free(s);
    n->secret=0;
}
static void ui_secret_root_clear(UiNode *root)
{for(UiNode *n=root->owner->all_nodes;n;n=n->all_next)if(n->root==root->id && ui_secret(n))ui_secret_clear_node(n,0);
}
static void ui_secret_subtree_clear(UiNode *parent)
{for(UiNode *n=parent->owner->all_nodes;n;n=n->all_next)if(ui_secret(n) && ui_subtree_contains(parent,n))ui_secret_clear_node(n,0);
}
static void ui_secret_focus_changed(ScUiContext *c,UiNode *root,ScUiNode next)
{for(UiNode *n=c->all_nodes;n;n=n->all_next){UiSecret *s=ui_secret(n);
        if(s && n->root==root->id && s->armed && s->submit.button!=next)ui_secret_clear_node(n,0);
}}
static void ui_secret_schedule(UiNode *n)
{
    UiSecret *s=ui_secret(n);
    if(!s)return;
    ScUiTimerDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.node=n->id;
    if(s->armed){d.flags=SC_UI_TIMER_BACKGROUND;
        d.deadline=s->deadline;
    }
    else if(n->effective&SC_UI_FOCUSED){d.flags=SC_UI_TIMER_DRAW_ONLY;
        ui_now(&d.deadline);
        ui_wide_add(&d.deadline,n->owner->hz/2u?n->owner->hz/2u:1);
        d.period_ticks=n->owner->hz/2u?n->owner->hz/2u:1;
    }
    else{ui_secret_cancel_timer(n);
        return;
    }
    ScStatus status=s->timer?ScUiTimerChange(n->owner,s->timer,&d):ScUiTimerSet(n->owner,&d,&s->timer);
    if(status<0){s->timer=0;
        if(s->armed)ui_secret_clear_node(n,0);
    }
}
static ScStatus ui_secret_measure(UiNode *n,const ScUiSize *available,ScUiSize *out)
{(void)available;
    out->width=ui_scale(n->owner,240);
    out->height=ui_scale(n->owner,30);
    return 0;
}
static ScI32 ui_secret_cell(UiNode *n)
{ScUiText t={sizeof(t),1,"*",1};
    ScUiTextMetrics m;
    memset(&m,0,sizeof(m));
    m.size=sizeof(m);
    m.version=1;
    return ScUiMeasureText(n->owner,n->font_role,&t,0,0,&m)==0 && m.width?(ScI32)m.width:ui_scale(n->owner,8);
}
static ScStatus ui_secret_paint(UiNode *n,ScUiPainter *p)
{
    UiSecret *s=ui_secret(n);
    if(!s)return SC_UI_E_INVALID;
    ScUiRect bounds={0,0,n->rectangle.width,n->rectangle.height};
    ScI32 pad=ui_scale(n->owner,5),cell=ui_secret_cell(n);
    ui_control_surface(n,p,&bounds,SC32_COLOR_PAPER,1);
    ScUiRect clip={pad,pad,bounds.width-pad*2,bounds.height-pad*2};
    if(clip.width<=0 || clip.height<=0)return 0;
    ScU32 caret=ui_secret_characters(&s->active,s->active.caret),count=ui_secret_characters(&s->active,s->active.bytes),visible=(ScU32)(clip.width/cell);
    if(visible>256)visible=256;
    if(!visible)visible=1;
    if(caret<s->first_character)s->first_character=caret;
    if(caret>=s->first_character+visible)s->first_character=caret-visible+1;
    ScU32 begin=ui_secret_characters(&s->active,s->active.caret<s->active.anchor?s->active.caret:s->active.anchor),end=ui_secret_characters(&s->active,s->active.caret>s->active.anchor?s->active.caret:s->active.anchor);
    ScUiPainterClip(p,&clip);
    for(ScU32 i=s->first_character;i<count && i<s->first_character+visible;i++){ScUiRect r={pad+(ScI32)(i-s->first_character)*cell,pad,cell,clip.height};
        ScU32 selected=(n->effective&SC_UI_FOCUSED) && i>=begin && i<end;
        if(selected)ui_fill(p,&r,SC32_COLOR_SELECT);
        ScUiText bullet={sizeof(bullet),1,"*",1};
        ScUiPainterTextColor(p,&r,&bullet,n->font_role,0,selected?SC32_COLOR_SELECT_TEXT:SC32_COLOR_TEXT);
    }
    if((n->effective&SC_UI_FOCUSED) && s->caret){ScI32 x=pad+(ScI32)(caret-s->first_character)*cell;
        ScUiPainterLine(p,x,pad,x,bounds.height-pad-1,SC32_COLOR_TEXT);
    }
    ScUiPainterRestore(p);
    return 0;
}
static ScStatus ui_secret_event(UiNode *n,const ScUiEvent *e)
{
    UiSecret *s=ui_secret(n);
    if(!s)return 0;
    if(e->type==SC_UI_EVENT_FOCUS_IN){if(s->armed)ui_secret_clear_node(n,0);
        s->invalidated=0;
        s->caret=1;
        ui_secret_schedule(n);
        ScUiEnsureVisible(n->owner,n->id,0);
        return 0;
    }
    if(e->type==SC_UI_EVENT_FOCUS_OUT){if(!s->consuming){UiNode *root=ui_root(n->owner,n->id);
            UiNode *button=ui_node(n->owner,s->submit.button);
            if(root && root->window && button && button->root==n->root && root->window->focus==button->id && (root->window->information.state&SC32_WINDOW_FOCUSED) && s->submit.procedure){
                ui_secret_buffer_clear(&s->pending);
                UiSecretBuffer swap=s->pending;
                s->pending=s->active;
                s->active=swap;
                s->armed=1;
                s->first_character=0;
                ui_now(&s->deadline);
                ui_wide_add(&s->deadline,n->owner->hz*2u);
                ui_secret_schedule(n);
                ui_secret_notice(n);
            }else ui_secret_clear_node(n,1);
        }return 0;
    }
    if(e->type==SC_UI_EVENT_TIMER && e->command==s->timer){if(s->armed)ui_secret_clear_node(n,1);
        else{s->caret=!s->caret;
            ui_node_damage(n);
        }return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_INPUT_SYNC){ui_secret_clear_node(n,1);
        return 0;
    }
    if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e)){ScStatus status=ScUiFocus(n->owner,n->id);
        if(status<0)return status;
        ScU32 count=ui_secret_characters(&s->active,s->active.bytes);
        ScI32 x=e->x-ui_scale(n->owner,5),cell=ui_secret_cell(n);
        ScU32 wanted=s->first_character+(x>0?(ScU32)(x+cell/2)/((ScU32)cell):0);
        if(wanted>count)wanted=count;
        ScU32 at=0;
        while(wanted-- && at<s->active.bytes)at=ui_secret_next(&s->active,at);
        s->active.caret=at;
        if(!(e->modifiers&SC32_MOD_SHIFT))s->active.anchor=at;
        s->caret=1;
        ui_node_damage(n);
        return SC_UI_HANDLED;
    }
    if(s->consuming)return SC_UI_HANDLED;
    if(e->type==SC_UI_EVENT_TEXT){if(n->effective&SC_UI_ENABLED && !(n->state&SC_UI_READONLY) && e->scalar>=32 && e->scalar!=127){if(!ui_secret_insert(&s->active,e->scalar))return SC_UI_E_RANGE;
            s->invalidated=0;
            s->caret=1;
            ui_secret_notice(n);
            ui_secret_schedule(n);
        }return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_KEY_UP)return e->key==9 || e->key==10 || e->key==13 || e->key==27?0:SC_UI_HANDLED;
    if(e->type!=SC_UI_EVENT_KEY_DOWN)return 0;
    ScU32 key=e->key,control=e->modifiers&SC32_MOD_CONTROL,shift=e->modifiers&SC32_MOD_SHIFT;
    if(key==9 || key==10 || key==13)return 0;
    if(key==27){ui_secret_clear_node(n,1);
        return 0;
    }
    if(control){if(key=='a'){s->active.anchor=0;
            s->active.caret=s->active.bytes;
            ui_node_damage(n);
        }return SC_UI_HANDLED;
    }
    if(key==8 || key==SC32_KEY_DELETE){if(!(n->state&SC_UI_READONLY)){ScU32 start=s->active.caret<s->active.anchor?s->active.caret:s->active.anchor,end=s->active.caret>s->active.anchor?s->active.caret:s->active.anchor;
            if(start==end){if(key==8)start=ui_secret_previous(&s->active,start);
                else end=ui_secret_next(&s->active,end);
            }ui_secret_remove(&s->active,start,end);
            ui_secret_notice(n);
        }return SC_UI_HANDLED;
    }
    if(key==SC32_KEY_LEFT)s->active.caret=ui_secret_previous(&s->active,s->active.caret);
    else if(key==SC32_KEY_RIGHT)s->active.caret=ui_secret_next(&s->active,s->active.caret);
    else if(key==SC32_KEY_HOME)s->active.caret=0;
    else if(key==SC32_KEY_END)s->active.caret=s->active.bytes;
    else return SC_UI_HANDLED;
    if(!shift)s->active.anchor=s->active.caret;
    s->caret=1;
    ui_node_damage(n);
    return SC_UI_HANDLED;
}
/* 在普通事件监听器之前处理秘密正文；导航事件清掉原message/scalar，
 * 控件通知只发布长度，不能把字符当VALUE_CHANGING正文转给插件。 */
static ScStatus ui_secret_dispatch(UiNode *n,ScUiEvent *e)
{
    if(!ui_secret(n))return 0;
    ui_node_hold(n);
    ScStatus s=ui_secret_event(n,e);
    ui_secret_wipe(&e->scalar,sizeof(e->scalar));
    e->message=0;
    e->data=0;
    e->bytes=0;
    ui_node_leave(n);
    return s;
}
static void ui_secret_event_finished(ScUiContext *c,UiNode *target,const ScUiEvent *e)
{
    if(!target || (e->type!=SC_UI_EVENT_POINTER_UP && e->type!=SC_UI_EVENT_CAPTURE_LOST && e->type!=SC_UI_EVENT_INPUT_SYNC))return;
    for(UiNode *n=c->all_nodes;n;n=n->all_next){UiSecret *s=ui_secret(n);
        if(s && s->armed && s->submit.button==target->id)ui_secret_clear_node(n,1);
    }
}
ScStatus ScUiSecretCreate(ScUiContext *c,const ScUiSecretDesc *d,ScUiNode *out)
{
    ScStatus status=ui_input(d,sizeof(*d));
    if(status<0 || !out)return status<0?status:SC_UI_E_INVALID;
    if(d->flags || d->reserved[0] || d->reserved[1] || d->reserved[2] || !d->max_bytes || d->max_bytes>4096)return SC_UI_E_INVALID;
    ScUiWidgetDesc widget;
    memset(&widget,0,sizeof(widget));
    widget.size=sizeof(widget);
    widget.version=1;
    widget.parent=d->parent;
    widget.kind=SC_UI_SECRET;
    widget.text.size=sizeof(ScUiText);
    widget.text.version=1;
    widget.text.data="";
    widget.text.bytes=0;
    ScUiNode node=0;
    status=ScUiWidgetCreate(c,&widget,&node);
    if(status<0)return status;
    UiNode *n=ui_node(c,node);
    if(d->max_bytes!=128)status=ui_secret_allocate(n,d->max_bytes);
    if(status<0){ScUiNodeDestroy(c,node);
        return status;
    }*out=node;
    return 0;
}
ScStatus ScUiSecretQuery(ScUiContext *c,ScUiNode node,ScUiSecretInfo *out)
{
    ScStatus status=ui_output(out,sizeof(*out));
    if(status<0)return status;
    UiNode *n=ui_node(c,node);
    UiSecret *s=ui_secret(n);
    if(!s)return SC_UI_E_STALE;
    ScUiSecretInfo i;
    memset(&i,0,sizeof(i));
    i.size=sizeof(i);
    i.version=1;
    i.node=node;
    i.bytes=s->active.bytes;
    i.characters=ui_secret_characters(&s->active,s->active.bytes);
    i.maximum=s->active.maximum;
    i.revision=s->revision;
    i.flags=(s->armed?1u:0)|(s->consuming?2u:0)|(s->invalidated?4u:0);
    memcpy(out,&i,sizeof(i));
    return 0;
}
ScStatus ScUiSecretClear(ScUiContext *c,ScUiNode node)
{UiNode *n=ui_node(c,node);
    if(!ui_secret(n))return SC_UI_E_STALE;
    ui_node_hold(n);
    ui_secret_clear_node(n,1);
    ui_node_leave(n);
    return 0;
}
ScStatus ScUiSecretConsume(ScUiContext *c,ScUiNode node,ScUiSecretProc procedure,void *context)
{
    UiNode *n=ui_node(c,node);
    UiSecret *s=ui_secret(n);
    if(!s || !procedure)return SC_UI_E_INVALID;
    if(s->consuming)return SC_UI_E_BUSY;
    ScStatus status=ui_pin((const void *)procedure);
    if(status<0)return status;
    ui_node_hold(n);
    s->consuming=1;
    UiSecretBuffer *buffer=s->armed?&s->pending:&s->active;
    if(s->armed){ScWideCounter now;
        if(ui_now(&now)<0 || ui_wide_compare(&now,&s->deadline)>=0){s->consuming=0;
            ui_secret_clear_node(n,1);
            ui_node_leave(n);
            return SC_UI_E_CANCELLED;
    }}
    status=procedure(c,node,(const char *)buffer->data,buffer->bytes,context);
    s->consuming=0;
    s->clearing=0;
    ui_secret_erase_node(n,1,0);
    ui_node_leave(n);
    return status;
}
ScStatus ScUiSecretBindSubmit(ScUiContext *c,ScUiNode node,const ScUiSecretSubmit *d)
{
    ScStatus status=ui_input(d,sizeof(*d));
    if(status<0)return status;
    UiNode *n=ui_node(c,node);
    UiSecret *s=ui_secret(n);
    if(!s)return SC_UI_E_STALE;
    if(d->flags || d->reserved[0] || d->reserved[1] || d->reserved[2] || (!d->button!=!d->procedure))return SC_UI_E_INVALID;
    if(s->consuming)return SC_UI_E_BUSY;
    UiNode *button=d->button?ui_node(c,d->button):0;
    if(d->button && (!button || button->kind!=SC_UI_BUTTON || button->root!=n->root))return SC_UI_E_INVALID;
    for(UiNode *other=c->all_nodes;other;other=other->all_next){UiSecret *o=ui_secret(other);
        if(o && other!=n && d->button && o->submit.button==d->button)return SC_UI_E_EXISTS;
    }
    status=ui_pin((const void *)d->procedure);
    if(status==0)status=ui_pin((const void *)d->context_drop);
    if(status<0)return status;
    ui_node_hold(n);
    ui_secret_clear_node(n,0);
    ScUiSecretSubmit old=s->submit;
    s->submit=*d;
    if(old.context_drop)old.context_drop(old.context,0);
    ui_node_leave(n);
    return 0;
}
static ScStatus ui_secret_submit_command(ScUiContext *c,UiNode *button)
{
    for(UiNode *n=c->all_nodes;n;n=n->all_next){UiSecret *s=ui_secret(n);
        if(s && n->root==button->root && s->submit.button==button->id && s->submit.procedure){if(s->invalidated && !s->active.bytes && !s->armed)return SC_UI_E_CANCELLED;
            ScUiSecretProc proc=s->submit.procedure;
            void *context=s->submit.context;
            return ScUiSecretConsume(c,n->id,proc,context);
    }}
    return 0;
}
