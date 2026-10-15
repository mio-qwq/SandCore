static ScI32 ui_relative_coordinate(ScI32 value,ScI32 origin)
{if(origin>0 && value<INT_MIN+origin)return INT_MIN;
    if(origin<0 && value>INT_MAX+origin)return INT_MAX;
    return value-origin;
    }
static void ui_event_record(ScUiEvent *e,UiNode *n,ScU32 type)
{
    memset(e,0,sizeof(*e));
    e->size=sizeof(*e);
    e->version=1;
    e->type=type;
    if(n){e->node=n->id;
        e->root=n->root;
        e->command=n->command;
        }
}
static void ui_listeners_collect(ScUiContext *c)
{
    UiListener *l=c->listeners;
    if(c->dispatching || c->listener_collecting)return;
    c->listener_collecting++;
    while(l){UiListener *next=l->next;
        if(l->retired && !l->depth){
            if(l->previous)l->previous->next=l->next;
            else c->listeners=l->next;
            if(l->next)l->next->previous=l->previous;
            if(!c->quiet && !ui_quiet && l->description.context_drop)l->description.context_drop(l->description.context,0);
            free(l);
        }
        l=next;
    }
    c->listener_collecting--;
}
static void ui_listeners_remove_node(ScUiContext *c,ScU32 id)
{for(UiListener *l=c->listeners;l;l=l->next)if(l->node==id)l->retired=1;
    ui_listeners_collect(c);
    }
static ScStatus ui_deliver(ScUiContext *c,UiNode *n,ScUiEvent *e,ScU32 phase,ScU32 watermark)
{
    UiListener *l;
    ScStatus s=0;
    e->phase=phase;
    e->current=n?n->id:0;
    for(l=c->listeners;l;l=l->next){
        if(l->retired || l->token>watermark || l->node!=(n?n->id:0) || !(l->description.phases&phase)
            || (l->description.type && l->description.type!=e->type))continue;
        if(l->description.flags&SC_UI_LISTEN_ONCE)l->retired=1;
        l->depth++;
        s=l->description.procedure(c,e,l->description.context);
        l->depth--;
        if(s)return s;
        if(c->object.closing || (n && (n->private_flags&UI_NODE_RETIRED) && e->type!=SC_UI_EVENT_DESTROY))return SC_UI_E_CANCELLED;
    }
    if(phase==SC_UI_TARGET_PHASE && n){
        if(n->kind==UI_KIND_POPUP || n->kind==SC_UI_MENUBAR || n->kind==SC_UI_TOOLBAR)s=ui_menu_event(n,e);
        else if(n->kind==SC_UI_TABLE || n->kind==SC_UI_TREE || n->kind==SC_UI_LIST)s=ui_view_event(n,e);
        else if(n->custom && n->custom->description.event){n->custom->depth++;
            s=n->custom->description.event(c,e,n->custom->description.context);
            n->custom->depth--;
            }
        else s=ui_controls_event(n,e);
    }
    return s;
}
static ScStatus ui_emit(ScUiContext *c,UiNode *target,ScUiEvent *event)
{
    ScArray *path=0;
    ScContainerInfo info;
    ScStatus s=0;
    ScU32 i,watermark=c->next_token-1;
    UiNode *n;
    ScI32 x=event->x,y=event->y;
    ScUiEvent e=*event;
    const ScUiEvent *previous_event=c->current_event;
    if(ui_quiet || c->quiet)return SC_UI_OK;
    c->object.depth++;
    c->walking++;
    c->dispatching++;
    c->current_event=&e;
    if(!target){s=ui_deliver(c,0,&e,SC_UI_TARGET_PHASE,watermark);
        goto done;
        }
    s=ui_array_new(sizeof(UiNode *),&path);
    if(s<0)goto done;
    i=0;
    for(n=target;n;n=n->parent){ui_node_hold(n);
        s=ScArrayInsert(path,i++,&n,1);
        if(s<0){ui_node_leave(n);
            goto release;
            }}
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(path,&info);
    /* node0的TARGET仍只接无目标上下文事件；CAPTURE/BUBBLE则包围
     * 完整控件路径。先持有整条路径，允许观察者销窗而不悬空target；
     * 新建popup也立即受同一上下文订阅约束，不依赖外层消息批返回。 */
    s=ui_deliver(c,0,&e,SC_UI_CAPTURE_PHASE,watermark);
    if(s)goto release;
    if(c->object.closing || (target->private_flags&UI_NODE_RETIRED)){s=SC_UI_E_CANCELLED;
        goto release;
        }
    for(i=info.length;i>1;i--){ScArrayGet(path,i-1,&n);
        e.x=ui_relative_coordinate(x,n->rectangle.x);
        e.y=ui_relative_coordinate(y,n->rectangle.y);
        s=ui_deliver(c,n,&e,SC_UI_CAPTURE_PHASE,watermark);
        if(s)goto release;
        }
    e.x=ui_relative_coordinate(x,target->rectangle.x);
    e.y=ui_relative_coordinate(y,target->rectangle.y);
    s=ui_deliver(c,target,&e,SC_UI_TARGET_PHASE,watermark);
    if(s)goto release;
    for(i=1;i<info.length;i++){ScArrayGet(path,i,&n);
        e.x=ui_relative_coordinate(x,n->rectangle.x);
        e.y=ui_relative_coordinate(y,n->rectangle.y);
        s=ui_deliver(c,n,&e,SC_UI_BUBBLE_PHASE,watermark);
        if(s)break;
        }
    if(!s){e.x=x;e.y=y;
        s=ui_deliver(c,0,&e,SC_UI_BUBBLE_PHASE,watermark);
        }
release:
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(path,&info);
    for(i=0;i<info.length;i++){ScArrayGet(path,i,&n);
        ui_node_leave(n);
        }
done:
    if(s==SC_UI_VETO && !(e.flags&SC_UI_EVENT_CANCELABLE))s=SC_UI_E_INVALID;
    c->current_event=previous_event;
    ScArrayRelease(path);
    c->dispatching--;
    c->walking--;
    ui_listeners_collect(c);
    ui_nodes_collect(c);
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
static ScStatus ui_event(UiNode *n,ScU32 type,const void *data,ScU32 bytes)
{ScUiEvent e;
    ui_event_record(&e,n,type);
    e.data=data;
    e.bytes=bytes;
    e.flags=SC_UI_EVENT_PROGRAMMATIC;
    if(type==SC_UI_EVENT_CREATE || type==SC_UI_EVENT_CLOSE_REQUEST || type==SC_UI_EVENT_OPENING || type==SC_UI_EVENT_VALUE_CHANGING || type==SC_UI_EVENT_SELECTION_CHANGING || type==SC_UI_EVENT_VALIDATE)e.flags|=SC_UI_EVENT_CANCELABLE;
    return ui_emit(n->owner,n,&e);
    }
ScStatus ScUiListen(ScUiContext *c,ScUiNode id,const ScUiSubscription *d,ScU32 *token)
{
    UiListener *l,*at;
    ScStatus s=ui_input(d,sizeof(*d));
    ScU32 i;
    if(s<0 || !token || !d->procedure)return s<0?s:SC_UI_E_INVALID;
    for(i=0;i<3;i++)if(d->reserved[i])return SC_UI_E_INVALID;
    if(!d->phases || d->phases&~7u || d->flags&~SC_UI_LISTEN_ONCE)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    if((id && !ui_node(c,id)) || c->next_token==UINT_MAX){s=id && !ui_node(c,id)?SC_UI_E_STALE:SC_UI_E_RANGE;
        goto done;
        }
    l=(UiListener *)calloc(1,sizeof(*l));
    if(!l){s=SC_UI_E_MEMORY;
        goto done;
        }
    s=ui_pin((const void *)d->procedure);
    if(s==0)s=ui_pin((const void *)d->context_drop);
    if(s<0){free(l);
        goto done;
        }
    l->description=*d;
    l->node=id;
    l->token=c->next_token++;
    at=c->listeners;
    if(!at){c->listeners=l;
        }
    else if(at->description.priority<d->priority){l->next=at;
        at->previous=l;
        c->listeners=l;
        }
    else{while(at->next && at->next->description.priority>=d->priority)at=at->next;
        l->next=at->next;
        l->previous=at;
        if(l->next)l->next->previous=l;
        at->next=l;
        }
    *token=l->token;
    s=SC_UI_OK;
done:s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiDisconnect(ScUiContext *c,ScU32 token)
{
    UiListener *l;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    for(l=c->listeners;l && l->token!=token;l=l->next){}
    if(!l || l->retired)s=SC_UI_E_STALE;
    else{l->retired=1;
        s=SC_UI_OK;
        }
    ui_listeners_collect(c);
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiHitTest(const ScUiContext *c,ScUiNode id,ScI32 x,ScI32 y,ScUiNode *out)
{
    UiNode *root,*n,*best=0;
    if(!c || !out || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_INVALID;
    root=ui_root((ScUiContext *)c,id);
    if(!root || !root->window)return SC_UI_E_STALE;
    n=root;
    while(n){if((n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){
            if(ui_rect_contains(&n->clip,x,y) && (n->custom?ui_rect_contains(&n->rectangle,x,y):ui_controls_contains(n,x,y)))best=n;
            if(n->first_child && ui_rect_contains(&n->child_clip,x,y)){n=n->first_child;
                continue;
                }}
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
        }
    if(!best)return SC_UI_E_NOT_FOUND;
    *out=best->id;
    return SC_UI_OK;
}
ScStatus ScUiFocus(ScUiContext *c,ScUiNode id)
{
    UiNode *n,*root,*old;
    ScWindowInfo info;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n){s=SC_UI_E_STALE;
        goto done;
        }
    root=ui_root(c,id);
    if((n->effective&(SC_UI_ENABLED|SC_UI_VISIBLE))!=(SC_UI_ENABLED|SC_UI_VISIBLE) || !(n->flags&SC_UI_FOCUSABLE)){s=SC_UI_E_INVALID;
        goto done;
        }
    {UiNode *native=ui_native_owner(root);
        if(!(native->window->information.state&SC32_WINDOW_FOCUSED)){memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            s=ScWindowControl(native->window->window,SC32_WINDOW_FOCUS,&info);
            if(s<0){s=ui_cause(c,s,UI_OP_EVENT,id);
                goto done;
                }
            native->window->information=info;
            if(!(info.state&SC32_WINDOW_FOCUSED)){s=SC_UI_E_BUSY;
                goto done;
                }}}
    old=ui_node(c,root->window->focus);
    if(old==n){
        if(!(n->effective&SC_UI_FOCUSED)){ui_node_hold(n);
            n->state|=SC_UI_FOCUSED;
            n->effective|=SC_UI_FOCUSED;
            ui_node_damage(n);
            ui_event(n,SC_UI_EVENT_FOCUS_IN,0,0);
            ui_node_leave(n);
            }
        s=SC_UI_OK;
        goto done;
        }
    ui_node_hold(n);
    root->window->focus=id;
    if(old){old->state&=~SC_UI_FOCUSED;
        old->effective&=~SC_UI_FOCUSED;
        ui_node_damage(old);
        ui_event(old,SC_UI_EVENT_FOCUS_OUT,0,0);
        }
    if(!(n->private_flags&UI_NODE_RETIRED)){n->state|=SC_UI_FOCUSED;
        n->effective|=SC_UI_FOCUSED;
        ui_node_damage(n);
        ui_event(n,SC_UI_EVENT_FOCUS_IN,0,0);
        s=SC_UI_OK;
        }
    else s=SC_UI_E_CANCELLED;
    ui_node_leave(n);
done:s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiFocusQuery(const ScUiContext *c,ScUiNode id,ScUiNode *out)
{UiNode *root=ui_root((ScUiContext *)c,id);
    if(!root || !root->window || !out)return SC_UI_E_STALE;
    *out=root->window->focus;
    return SC_UI_OK;
    }
ScStatus ScUiFocusNext(ScUiContext *c,ScUiNode id,ScI32 direction)
{
    UiNode *root=ui_root(c,id),*n,*first=0,*previous=0,*after=0;
    ScU32 current;
    if(!root || !root->window)return SC_UI_E_STALE;
    if(direction!=1 && direction!=-1)return SC_UI_E_INVALID;
    current=root->window->focus;
    n=root;
    while(n){if((n->flags&(SC_UI_FOCUSABLE|SC_UI_TABSTOP))==(SC_UI_FOCUSABLE|SC_UI_TABSTOP)
            && (n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){
            if(!first)first=n;
            if(previous && previous->id==current && !after)after=n;
            if(n->id==current && direction<0 && previous)return ScUiFocus(c,previous->id);
            previous=n;
            }
        if(n->first_child){n=n->first_child;
            continue;
            }
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
        }
    if(direction<0 && previous)return ScUiFocus(c,previous->id);
    if(after)return ScUiFocus(c,after->id);
    return first?ScUiFocus(c,first->id):SC_UI_E_NOT_FOUND;
}
ScStatus ScUiCapture(ScUiContext *c,ScUiNode id,ScU32 enabled)
{
    UiNode *n=ui_node(c,id),*root;
    ScStatus s;
    if(!n || enabled>1)return n?SC_UI_E_INVALID:SC_UI_E_STALE;
    root=ui_root(c,id);
    if(enabled && (n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED))return SC_UI_E_INVALID;
    {UiNode *native=ui_native_owner(root);
        s=ScPointerCapture(native->window->window,enabled);
        if(s<0)return ui_cause(c,s,UI_OP_EVENT,id);
        native->window->capture=enabled?id:0;
        root->window->capture=enabled?id:0;
        if(!enabled)native->window->cursor=UINT_MAX;
        }
    return SC_UI_OK;
}
static void ui_input_cancel(UiNode *root,ScU32 event)
{
    ScUiContext *c=root->owner;
    UiNode *target=ui_node(c,root->window->capture),*focus=ui_node(c,root->window->focus),*hover=ui_node(c,root->window->hover);
    root->window->capture=0;
    root->window->hover=0;
    if(target){target->state&=~SC_UI_PRESSED;
        ui_tree_effective(target);
        ui_node_damage(target);
        ui_event(target,event,0,0);
        }
    if(hover){hover->state&=~SC_UI_HOVER;
        ui_tree_effective(hover);
        ui_node_damage(hover);
        }
    if(focus && event==SC_UI_EVENT_FOCUS_OUT){focus->state&=~SC_UI_FOCUSED;
        focus->effective&=~SC_UI_FOCUSED;
        ui_node_damage(focus);
        ui_event(focus,event,0,0);
        }
}
static int ui_subtree_contains(UiNode *subtree,UiNode *node)
{while(node && node!=subtree)node=node->parent;
    return node==subtree;
    }
static void ui_input_subtree(UiNode *subtree)
{
    ScUiContext *c=subtree->owner;
    UiNode *root=ui_root(c,subtree->id),*target,*native;
    if(!root || !root->window)return;
    native=ui_native_owner(root);
    target=ui_node(c,root->window->capture);
    if(ui_subtree_contains(subtree,target)){ScPointerCapture(native->window->window,0);
        root->window->capture=0;
        native->window->capture=0;
        target->state&=~SC_UI_PRESSED;
        ui_tree_effective(target);
        ui_node_damage(target);
        ui_event(target,SC_UI_EVENT_CAPTURE_LOST,0,0);
        }
    target=ui_node(c,root->window->hover);
    if(ui_subtree_contains(subtree,target)){root->window->hover=0;
        target->state&=~SC_UI_HOVER;
        ui_tree_effective(target);
        ui_node_damage(target);
        ui_event(target,SC_UI_EVENT_POINTER_LEAVE,0,0);
        }
    target=ui_node(c,root->window->focus);
    if(ui_subtree_contains(subtree,target)){root->window->focus=0;
        target->state&=~SC_UI_FOCUSED;
        target->effective&=~SC_UI_FOCUSED;
        ui_node_damage(target);
        ui_event(target,SC_UI_EVENT_FOCUS_OUT,0,0);
        }
}
static void ui_cursor_target(UiNode *root,UiNode *target,ScI32 x,ScI32 y)
{
    UiNode *native=ui_native_owner(root);
    ScU32 shape=SC32_CURSOR_ARROW;
    if(!native || !native->window || native->window->capture)return;
    if(target && (target->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){
        if(target->kind==SC_UI_EDIT || target->kind==SC_UI_TEXTBOX || (target->kind==SC_UI_LABEL && (target->flags&SC_UI_LABEL_SELECTABLE)))shape=SC32_CURSOR_TEXT;
        else if(target->kind==SC_UI_SPINBOX && x<target->rectangle.width-ui_scale(target->owner,18))shape=SC32_CURSOR_TEXT;
        else if(target->kind==SC_UI_SPLITTER)shape=target->parent && target->parent->layout.mode==SC_UI_HBOX?SC32_CURSOR_RESIZE_H:SC32_CURSOR_RESIZE_V;
        else if(target->kind==SC_UI_TABLE && target->view && y>=0 && (ScU32)y<ui_view_row_height(target)){ScU32 first,past,i;
            ui_column_range(target,target->view,&first,&past);
            for(i=first;i<past;i++){UiColumn column;
                ScI32 edge,left;
                ui_column_at(target->view,i,&column);
                left=ui_column_x(target,&column);
                edge=left>INT_MAX-column.width?INT_MAX:left+column.width;
                if((ScU32)(x>=edge?(ScU32)x-(ScU32)edge:(ScU32)edge-(ScU32)x)<=(ScU32)ui_scale(target->owner,3)){shape=SC32_CURSOR_RESIZE_H;
                    break;
                    }}}
    }
    if(native->window->cursor!=shape && ScPointerSetCursor(native->window->window,shape)==0)native->window->cursor=shape;
}
static void ui_cursor_current(UiNode *root)
{
    ScInputSnapshot input;
    ScUiNode id;
    UiNode *target;
    ScI32 x,y;
    memset(&input,0,sizeof(input));
    input.size=sizeof(input);
    input.version=1;
    if(ScInputQuery(root->window->window,&input)!=0 || !input.focused)return;
    x=input.x;
    y=input.y;
    target=ui_popup_target(root,&x,&y,0);
    if(!target && ScUiHitTest(root->owner,root->id,x,y,&id)==0)target=ui_node(root->owner,id);
    ui_cursor_target(root,target,target?ui_relative_coordinate(x,target->rectangle.x):x,target?ui_relative_coordinate(y,target->rectangle.y):y);
}
static ScStatus ui_native_proc(ScWindow window,const ScMessage *m,void *context)
{
    UiNode *root=(UiNode *)context,*target;
    ScUiContext *c=root->owner;
    UiRoot *w=root->window;
    ScUiEvent e;
    ScStatus s=0;
    ScI32 x=0,y=0;
    ScU32 id=0;
    c->object.depth++;
    c->walking++;
    ui_node_hold(root);
    if(m->id==SC32_MSG_DESTROY){if(w)w->window=0;
        ui_node_retire(root);
        goto done;
        }
    if(!w || (root->private_flags&UI_NODE_RETIRED)){s=SC_UI_E_STALE;
        goto done;
        }
    w->window=window;
    if(m->id==SC32_MSG_CREATE){
        s=ui_root_surface(root);
        if(s<0)goto done;
        root->rectangle.x=0;
        root->rectangle.y=0;
        root->rectangle.width=(ScI32)w->width;
        root->rectangle.height=(ScI32)w->height;
        root->clip=root->rectangle;
        root->child_clip=root->clip;
        s=root->kind==UI_KIND_POPUP?SC_UI_OK:ui_controls_init(root);
        if(s<0)goto done;
        root->private_flags|=UI_NODE_CREATE_DELIVERED;
        s=ui_event(root,SC_UI_EVENT_CREATE,0,0);
        if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
        else if(s>0)s=0;
        if(s==0 && !(root->private_flags&UI_NODE_RETIRED) && root->kind!=UI_KIND_POPUP)s=ui_layout_root(root);
        goto done;
    }
    if(m->id==SC32_MSG_PAINT){ScContainerInfo info;
        /* 隐藏host的准备资格只在真实PAINT后重新查事实，不轮询全部hidden root。 */
        if(!w->visible){s=ui_root_surface(root);if(s<0)goto done;}
        ScUiRect r;
        w->paint_pending=1;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(w->damage,&info);
        if(!info.length){r.x=(ScI32)m->data[0];
            r.y=(ScI32)m->data[1];
            r.width=(ScI32)m->data[2];
            r.height=(ScI32)m->data[3];
            ScArrayInsert(w->damage,0,&r,1);
            }
        goto done;
        }
    if(m->id==SC32_MSG_SIZE){root->private_flags|=UI_NODE_LAYOUT_DIRTY;
        w->paint_pending=1;
        s=ui_root_surface(root);
        goto done;
        }
    if(m->id==SC32_MSG_MOVE){w->information.x=(ScI32)m->data[0];
        w->information.y=(ScI32)m->data[1];
        goto done;
        }
    if(m->id==SC32_MSG_VISIBILITY){ScU32 previous_visible=w->notified_visible;
        w->visible=m->data[0]!=0;
        /* Query可以先同步当前事实；业务通知仍以最后实际通知状态为准，
         * 不能因post-ACTIVATE查询提前置位就吞掉第一次真实VIS事件。 */
        w->notified_visible=w->visible;
        w->information.visibility_epoch=m->epoch;
        /* 隐藏消息只撤可见输入资格；PREPARING是独立的内核首帧资格。
         * 旧VIS可以晚于真实PAINT查询到达，不能抹掉刚取得的准备位；
         * 首帧前后仍由ui_root_surface重新核对实际内核状态。 */
        if(w->visible)w->information.state&=~SC32_WINDOW_PREPARING;
        if(w->visible)w->information.state|=SC32_WINDOW_VISIBLE;
        else w->information.state&=~SC32_WINDOW_VISIBLE;
        ui_timers_visibility(c,root->id,w->visible);
        ui_drag_state(root);
        if(!w->visible){ui_menu_close_owner(c,root->id);
            ui_input_cancel(root,SC_UI_EVENT_CAPTURE_LOST);
            }
        else{w->paint_pending=1;
            ui_node_damage(root);
            }
        /* 先发布真实可见性和绘图timer资格，再通知业务；后台事务可
         * 在此登记有限timer，无需隐藏后轮询或等待一次虚假的输入。 */
        if(previous_visible!=w->visible && !c->object.closing && !(root->private_flags&UI_NODE_RETIRED)){
            ui_event_record(&e,root,SC_UI_EVENT_VISIBILITY);
            e.message=m;
            e.tick=m->tick;
            e.input_epoch=m->epoch;
            e.sequence.lo=m->sequence_lo;
            e.sequence.hi=m->sequence_hi;
            e.flags=(m->flags&SC32_MSG_KERNEL)?SC_UI_EVENT_DEVICE:SC_UI_EVENT_PROGRAMMATIC;
            e.data=m->data;
            e.bytes=sizeof(m->data);
            s=ui_emit(c,root,&e);
            }
        goto done;
        }
    if(m->id==SC32_MSG_THEME || m->id==SC32_MSG_DISPLAY){UiNode *r;
        s=ui_theme_refresh(c);
        if(s<0)goto done;
        for(r=c->roots;r;r=r->root_next){r->private_flags|=UI_NODE_LAYOUT_DIRTY;
            ui_node_damage(r);
            }
        goto done;
        }
    if(m->id==SC32_MSG_CAPTURELOST || m->id==SC32_MSG_QUEUELOSS){
        if(m->id==SC32_MSG_CAPTURELOST){ScInputSnapshot current;
            memset(&current,0,sizeof(current));
            current.size=sizeof(current);
            current.version=1;
            /* 释放旧捕获会排入通知；同一UP可立即为新菜单重新捕获。
             * 仅在内核确认本窗当前仍持有另一代捕获时忽略旧通知，
             * 防止它销毁新popup。真实失去捕获及QUEUELOSS仍完整清理。
             * 不重发键鼠、不根据计时猜测，也不修改内核既有事件ABI。 */
            if(ScInputQuery(window,&current)==0 && current.captured
                && current.input_epoch!=m->epoch)goto done;
        }
        w->cursor=UINT_MAX;
        ui_input_cancel(root,SC_UI_EVENT_CAPTURE_LOST);
        ui_menu_close_owner(c,root->id);
        ui_event_record(&e,root,SC_UI_EVENT_CAPTURE_LOST);
        e.message=m;
        ui_drag_event(root,&e);
        if(m->id==SC32_MSG_QUEUELOSS)ui_event(root,SC_UI_EVENT_INPUT_SYNC,m,sizeof(*m));
        goto done;
        }
    if(m->id==SC32_MSG_FOCUS){
        if(!m->data[0]){w->cursor=UINT_MAX;
            w->information.state&=~SC32_WINDOW_FOCUSED;
            ui_input_cancel(root,SC_UI_EVENT_FOCUS_OUT);
            ui_menu_close_owner(c,root->id);
            }
        else{w->cursor=UINT_MAX;
            w->information.state|=SC32_WINDOW_FOCUSED;
            target=ui_node(c,w->focus);
            if(target)ScUiFocus(c,target->id);
            else ScUiFocusNext(c,root->id,1);
            if(!(root->private_flags&UI_NODE_RETIRED))ui_cursor_current(root);
            }
        goto done;
        }
    if(m->id==SC32_MSG_CLOSE){s=ui_event(root,SC_UI_EVENT_CLOSE_REQUEST,0,0);
        if(s==SC_UI_VETO || s==SC_UI_HANDLED){ScWindowRejectClose(window);
            s=0;
            }
        else if(s>=0)s=ScUiNodeDestroy(c,root->id);
        else ScWindowRejectClose(window);
        goto done;
        }
    ui_event_record(&e,root,0);
    e.message=m;
    e.tick=m->tick;
    e.input_epoch=m->epoch;
    e.sequence.lo=m->sequence_lo;
    e.sequence.hi=m->sequence_hi;
    e.flags=(m->flags&SC32_MSG_KERNEL)?SC_UI_EVENT_DEVICE:SC_UI_EVENT_PROGRAMMATIC;
    if(m->id==SC32_MSG_KEYDOWN || m->id==SC32_MSG_KEYUP || m->id==SC32_MSG_TEXT){
        target=ui_node(c,w->focus);
        if(!target)target=root;
        {UiNode *popup=ui_popup_target(root,&x,&y,1);
            if(popup)target=popup;
            }
        e.type=m->id==SC32_MSG_KEYDOWN?SC_UI_EVENT_KEY_DOWN:m->id==SC32_MSG_KEYUP?SC_UI_EVENT_KEY_UP:SC_UI_EVENT_TEXT;
        e.key=m->id==SC32_MSG_TEXT?0:m->data[1];
        e.scalar=m->id==SC32_MSG_TEXT?m->data[0]:m->data[2];
        e.modifiers=m->id==SC32_MSG_TEXT?m->data[1]:m->data[3];
        e.node=target->id;
        e.root=target->root;
        if(e.type==SC_UI_EVENT_KEY_DOWN && e.key==27){s=ui_drag_event(target,&e);
            if(s!=0)goto done;
            }
        s=ui_emit(c,target,&e);
        if(s==0 && m->id==SC32_MSG_KEYDOWN){
            if(e.key=='\t' && !(e.modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT)))s=ScUiFocusNext(c,root->id,(e.modifiers&SC32_MOD_SHIFT)?-1:1);
            else{s=ui_menu_key(root,e.key,e.modifiers);
                if(s==0 && ui_shortcut(c,e.key,e.modifiers,&id)==0)s=ScUiCommand(c,target->id,id);
                else if(s==0 && e.key=='\n' && w->default_button)s=ScUiCommand(c,w->default_button,ui_node(c,w->default_button)?ui_node(c,w->default_button)->command:0);
                }
        }
        goto done;
    }
    if(m->id==SC32_MSG_MOUSEMOVE || m->id==SC32_MSG_BUTTONDOWN || m->id==SC32_MSG_BUTTONUP || m->id==SC32_MSG_WHEEL){
        x=(ScI32)m->data[0];
        y=(ScI32)m->data[1];
        if(m->id==SC32_MSG_BUTTONDOWN){ui_event_record(&e,root,SC_UI_EVENT_POINTER_DOWN);
            e.message=m;
            e.x=x;
            e.y=y;
            e.buttons=m->data[2];
            e.modifiers=m->data[4];
            e.tick=m->tick;
            e.input_epoch=m->epoch;
            e.flags=(m->flags&SC32_MSG_KERNEL)?SC_UI_EVENT_DEVICE:SC_UI_EVENT_PROGRAMMATIC;
            s=ui_controls_popup_event(root,&e);
            if(s!=0)goto done;
            }
        target=ui_popup_target(root,&x,&y,0);
        if(!target && ui_menu_pointer(root,m->id,x,y,&target)){s=SC_UI_HANDLED;
            goto done;
            }
        if(!target){target=ui_node(c,w->capture);
            if(!target && ScUiHitTest(c,root->id,x,y,&id)==0)target=ui_node(c,id);
            }
        if(!target){ui_cursor_target(root,0,x,y);
            goto done;
            }
        e.node=target->id;
        e.root=target->root;
        e.x=x;
        e.y=y;
        e.type=m->id==SC32_MSG_MOUSEMOVE?SC_UI_EVENT_POINTER_MOVE:m->id==SC32_MSG_BUTTONDOWN?SC_UI_EVENT_POINTER_DOWN:m->id==SC32_MSG_BUTTONUP?SC_UI_EVENT_POINTER_UP:SC_UI_EVENT_WHEEL;
        e.buttons=m->id==SC32_MSG_WHEEL?m->data[4]:m->data[2];
        e.modifiers=m->id==SC32_MSG_WHEEL?m->data[5]:m->data[4];
        e.data=m->data;
        e.bytes=sizeof(m->data);
        s=ui_drag_event(target,&e);
        if(s!=0)goto done;
        if(m->id==SC32_MSG_MOUSEMOVE && w->hover!=target->id){UiNode *old=ui_node(c,w->hover);
            w->hover=target->id;
            if(old){ScUiEvent leave=e;
                old->state&=~SC_UI_HOVER;
                ui_tree_effective(old);
                ui_node_damage(old);
                leave.type=SC_UI_EVENT_POINTER_LEAVE;
                leave.node=old->id;
                leave.root=old->root;
                ui_emit(c,old,&leave);
                }
            ui_cursor_target(root,0,x,y);
            if(!(target->private_flags&UI_NODE_RETIRED)){ScUiEvent enter=e;
                target->state|=SC_UI_HOVER;
                ui_tree_effective(target);
                ui_node_damage(target);
                enter.type=SC_UI_EVENT_POINTER_ENTER;
                ui_emit(c,target,&enter);
                }}
        if(m->id==SC32_MSG_BUTTONDOWN && (target->flags&SC_UI_FOCUSABLE))ScUiFocus(c,target->id);
        if(!(target->private_flags&UI_NODE_RETIRED))ui_cursor_target(root,target,ui_relative_coordinate(x,target->rectangle.x),ui_relative_coordinate(y,target->rectangle.y));
        if(!(target->private_flags&UI_NODE_RETIRED))s=ui_emit(c,target,&e);
        if(!(target->private_flags&UI_NODE_RETIRED) && !(root->private_flags&UI_NODE_RETIRED))ui_cursor_target(root,target,ui_relative_coordinate(x,target->rectangle.x),ui_relative_coordinate(y,target->rectangle.y));
        goto done;
    }
    if(m->id==SC32_MSG_COMMAND){s=ScUiCommand(c,root->id,m->data[0]);
        goto done;
        }
    if(m->id>=SC32_MSG_USER){e.type=SC_UI_EVENT_USER+m->id-SC32_MSG_USER;
        e.data=m->data;
        e.bytes=sizeof(m->data);
        s=ui_emit(c,0,&e);
        }
done:c->walking--;
    ui_node_leave(root);
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
static UiTimer *ui_timer_find(ScUiContext *c,ScU32 token)
{ScHashEntry e;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    if(ScHashGet(c->timer_index,&token,sizeof(token),&e)<0)return 0;
    return *(UiTimer * const *)e.value;
    }
static ScU32 ui_timer_count(ScUiContext *c)
{ScContainerInfo i;
    memset(&i,0,sizeof(i));
    i.size=sizeof(i);
    i.version=1;
    ScArrayQuery(c->timers,&i);
    return i.length;
    }
static UiTimer *ui_timer_at(ScUiContext *c,ScU32 i){UiTimer *t=0;
    ScArrayGet(c->timers,i,&t);
    return t;
    }
static void ui_timer_swap(ScUiContext *c,ScU32 a,ScU32 b)
{UiTimer *x=ui_timer_at(c,a),*y=ui_timer_at(c,b);
    ScArraySet(c->timers,a,&y);
    ScArraySet(c->timers,b,&x);
    x->index=b;
    y->index=a;
    }
static void ui_timer_up(ScUiContext *c,ScU32 i)
{while(i){ScU32 parent=(i-1)/2;
        if(ui_wide_compare(&ui_timer_at(c,i)->description.deadline,&ui_timer_at(c,parent)->description.deadline)>=0)break;
        ui_timer_swap(c,i,parent);
        i=parent;
        }}
static void ui_timer_down(ScUiContext *c,ScU32 i)
{ScU32 count=ui_timer_count(c);
    while(i<count/2){ScU32 child=i*2+1;
        if(child+1<count && ui_wide_compare(&ui_timer_at(c,child+1)->description.deadline,&ui_timer_at(c,child)->description.deadline)<0)child++;
        if(ui_wide_compare(&ui_timer_at(c,i)->description.deadline,&ui_timer_at(c,child)->description.deadline)<=0)break;
        ui_timer_swap(c,i,child);
        i=child;
        }}
static ScStatus ui_timer_push(ScUiContext *c,UiTimer *t)
{ScU32 count=ui_timer_count(c);
    ScStatus s=ScArrayInsert(c->timers,count,&t,1);
    if(s<0)return s;
    t->index=count;
    ui_timer_up(c,count);
    return SC_UI_OK;
    }
static void ui_timer_remove(ScUiContext *c,UiTimer *t)
{
    ScU32 count=ui_timer_count(c),i=t->index;
    if(i==UINT_MAX || i>=count)return;
    if(i+1<count)ui_timer_swap(c,i,count-1);
    ScArrayErase(c->timers,count-1,1);
    t->index=UINT_MAX;
    if(i+1<count){ui_timer_up(c,i);
        ui_timer_down(c,i);
        }
}
static int ui_timer_visible(ScUiContext *c,const ScUiTimerDesc *d)
{UiNode *n,*root;
    if(!(d->flags&SC_UI_TIMER_DRAW_ONLY))return 1;
    n=ui_node(c,d->node);
    root=ui_root(c,d->node);
    return n && (n->effective&SC_UI_VISIBLE) && root && root->window && root->window->visible;
    }
static void ui_timer_detach(UiTimer *t)
{if(!t->owner)return;
    if(t->node_previous)t->node_previous->node_next=t->node_next;
    else t->owner->timers=t->node_next;
    if(t->node_next)t->node_next->node_previous=t->node_previous;
    t->owner=0;
    t->node_previous=t->node_next=0;
    }
static void ui_timer_attach(ScUiContext *c,UiTimer *t)
{UiNode *n=ui_node(c,t->description.node);
    if(!n)return;
    t->owner=n;
    t->node_next=n->timers;
    if(n->timers)n->timers->node_previous=t;
    n->timers=t;
    }
ScStatus ScUiTimerSet(ScUiContext *c,const ScUiTimerDesc *d,ScU32 *out)
{
    UiTimer *t;
    ScStatus s=ui_input(d,sizeof(*d));
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(d->reserved || d->flags&~3u || d->period_ticks>0x7fffffffu || ((d->flags&SC_UI_TIMER_DRAW_ONLY) && !d->node))return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    if((d->node && !ui_node(c,d->node)) || c->next_token==UINT_MAX){s=SC_UI_E_STALE;
        goto done;
        }
    t=(UiTimer *)calloc(1,sizeof(*t));
    if(!t){s=SC_UI_E_MEMORY;
        goto done;
        }
    t->token=c->next_token;
    t->index=UINT_MAX;
    t->description=*d;
    t->revision=1;
    s=ScArrayReserve(c->timers,ui_timer_count(c)+1);
    if(s==0)s=ScHashSet(c->timer_index,&t->token,sizeof(t->token),&t,SC_HASH_INSERT_ONLY);
    if(s<0){free(t);
        goto done;
        }
    t->paused=!ui_timer_visible(c,d);
    if(!t->paused)s=ui_timer_push(c,t);
    if(s<0){ScHashRemove(c->timer_index,&t->token,sizeof(t->token));
        free(t);
        goto done;
        }
    ui_timer_attach(c,t);
    c->next_token++;
    *out=t->token;
    s=SC_UI_OK;
done:s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiTimerChange(ScUiContext *c,ScU32 token,const ScUiTimerDesc *d)
{
    UiTimer *t;
    ScStatus s=ui_input(d,sizeof(*d));
    if(s<0)return s;
    if(d->reserved || d->flags&~3u || d->period_ticks>0x7fffffffu)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    t=ui_timer_find(c,token);
    if(!t || (d->node && !ui_node(c,d->node)))s=SC_UI_E_STALE;
    else if(t->revision==UINT_MAX)s=SC_UI_E_RANGE;
    else{s=ScArrayReserve(c->timers,ui_timer_count(c)+1);
        if(s==0){ui_timer_remove(c,t);
            ui_timer_detach(t);
            t->description=*d;
            t->revision++;
            ui_timer_attach(c,t);
            t->paused=!ui_timer_visible(c,d);
            if(!t->paused)s=ui_timer_push(c,t);
            }}
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiTimerCancel(ScUiContext *c,ScU32 token)
{
    UiTimer *t;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    t=ui_timer_find(c,token);
    if(!t)s=SC_UI_E_STALE;
    else{ui_timer_remove(c,t);
        ui_timer_detach(t);
        ScHashRemove(c->timer_index,&token,sizeof(token));
        t->retired=1;
        if(!t->firing)free(t);
        s=SC_UI_OK;
        }
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiTimerQuery(ScUiContext *c,ScU32 token,ScUiTimerDesc *out)
{UiTimer *t;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    t=ui_timer_find(c,token);
    if(!t)return SC_UI_E_STALE;
    memcpy(out,&t->description,sizeof(*out));
    return SC_UI_OK;
    }
static void ui_timers_remove_node(UiNode *node)
{
    ScUiContext *c=node->owner;
    /* 每节点只走拥有timer链，销十万节点不反复扫全context定时器表。 */
    while(node->timers){UiTimer *t=node->timers;
        ui_timer_detach(t);
        ui_timer_remove(c,t);
        ScHashRemove(c->timer_index,&t->token,sizeof(t->token));
        t->retired=1;
        if(!t->firing)free(t);
        }
}
static void ui_timers_visibility(ScUiContext *c,ScU32 root,int visible)
{
    ScIterator it;
    ScHashEntry e;
    (void)visible;
    memset(&it,0,sizeof(it));
    it.size=sizeof(it);
    it.version=1;
    if(ScHashIterate(c->timer_index,&it)<0)return;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    while(ScHashNext(&it,&e)==0){UiTimer *t=*(UiTimer * const *)e.value;
        UiNode *n=ui_root(c,t->description.node);
        if(!(t->description.flags&SC_UI_TIMER_DRAW_ONLY) || !n || n->id!=root)continue;
        if(!ui_timer_visible(c,&t->description) && !t->paused){ui_timer_remove(c,t);
            t->paused=1;
            }
        else if(ui_timer_visible(c,&t->description) && t->paused){ScWideCounter now;
            if(ui_now(&now)==0){t->description.deadline=now;
                ui_wide_add(&t->description.deadline,t->description.period_ticks?t->description.period_ticks:1);
                }
            if(ui_timer_push(c,t)==0)t->paused=0;
            }
    }
}
static ScU32 ui_wide_remainder(const ScWideCounter *n,ScU32 divisor)
{
    ScU32 remainder=0;
    ScI32 bit;
    for(bit=63;bit>=0;bit--){ScU32 value=bit>=32?((n->hi>>(bit-32))&1u):((n->lo>>bit)&1u);
        remainder=(remainder<<1)|value;
        if(remainder>=divisor)remainder-=divisor;
        }
    return remainder;
}
static void ui_timer_missed(const ScWideCounter *now,const ScWideCounter *deadline,ScU32 period,ScWideCounter *out)
{
    ScWideCounter delta;
    ScU32 remainder=0;
    ScI32 bit;
    out->lo=out->hi=0;
    if(!period)return;
    delta.lo=now->lo-deadline->lo;
    delta.hi=now->hi-deadline->hi-(now->lo<deadline->lo);
    for(bit=63;bit>=0;bit--){remainder=(remainder<<1)|(bit>=32?((delta.hi>>(bit-32))&1u):((delta.lo>>bit)&1u));
        if(remainder>=period){remainder-=period;
            if(bit>=32)out->hi|=1u<<(bit-32);
            else out->lo|=1u<<bit;
            }}
}
static ScStatus ui_timer_pump(ScUiContext *c)
{
    ScWideCounter now;
    ScU32 budget=c->description.scheduling_quantum;
    ScStatus s=ui_now(&now);
    if(s<0)return s;
    while(budget-- && ui_timer_count(c)){UiTimer *t=ui_timer_at(c,0);
        ScU32 revision=t->revision;
        UiNode *n;
        ScUiTimerEvent detail;
        ScUiEvent e;
        if(ui_wide_compare(&t->description.deadline,&now)>0)break;
        ui_timer_remove(c,t);
        t->firing=1;
        memset(&detail,0,sizeof(detail));
        detail.size=sizeof(detail);
        detail.version=1;
        detail.token=t->token;
        detail.period_ticks=t->description.period_ticks;
        ui_timer_missed(&now,&t->description.deadline,t->description.period_ticks,&detail.missed);
        detail.flags=t->description.flags;
        ui_event_record(&e,0,SC_UI_EVENT_TIMER);
        e.command=t->token;
        e.data=&detail;
        e.bytes=sizeof(detail);
        e.flags=SC_UI_EVENT_PROGRAMMATIC;
        n=t->description.node?ui_node(c,t->description.node):0;
        if(n){e.node=n->id;
            e.root=n->root;
            }
        e.tick=now.lo;
        s=ui_emit(c,n,&e);
        t->firing=0;
        if(t->retired){free(t);
            continue;
            }
        if(t->revision!=revision)continue;
        if(!t->description.period_ticks){ui_timer_detach(t);
            ScHashRemove(c->timer_index,&t->token,sizeof(t->token));
            free(t);
            }
        else{ScWideCounter delta;
            ScU32 remainder;
            delta.lo=now.lo-t->description.deadline.lo;
            delta.hi=now.hi-t->description.deadline.hi-(now.lo<t->description.deadline.lo);
            remainder=ui_wide_remainder(&delta,t->description.period_ticks);
            t->description.deadline=now;
            ui_wide_add(&t->description.deadline,t->description.period_ticks-remainder);
            t->paused=!ui_timer_visible(c,&t->description);
            if(!t->paused)ui_timer_push(c,t);
            }
        if(c->object.closing || c->stopped)break;
    }
    return s<0?s:SC_UI_OK;
}
static ScU32 ui_timer_wait(ScUiContext *c,ScU32 timeout)
{
    ScWideCounter now;
    UiNode *r;
    ScU32 nearest=timeout;
    if(ui_now(&now)<0)return 0;
    if(ui_timer_count(c)){UiTimer *t=ui_timer_at(c,0);
        ScU32 delta=ui_wide_compare(&now,&t->description.deadline)>=0?0:ui_wide_difference(&t->description.deadline,&now);
        if(nearest==SC32_INFINITE || delta<nearest)nearest=delta;
        }
    for(r=c->roots;r;r=r->root_next)if(r->window && ui_root_can_paint(r->window) && r->window->paint_pending){ScU32 delta=ui_wide_compare(&now,&r->window->retry)>=0?0:ui_wide_difference(&r->window->retry,&now);
        if(nearest==SC32_INFINITE || delta<nearest)nearest=delta;
        }
    return nearest;
}
ScStatus ScUiContextDispatch(ScUiContext *c,const ScMessage *message)
{
    ScStatus s=ui_input(message,sizeof(*message));
    if(s<0)return s;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    ui_wide_add(&c->received_messages,1);
    if(!message->window && message->id>=SC32_MSG_USER){ScUiEvent e;
        ui_event_record(&e,0,SC_UI_EVENT_USER+message->id-SC32_MSG_USER);
        e.message=message;
        e.data=message->data;
        e.bytes=sizeof(message->data);
        e.flags=SC_UI_EVENT_PROGRAMMATIC;
        s=ui_emit(c,0,&e);
        }
    else s=ScMessageDispatch(message);
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiContextWaitTimeout(ScUiContext *c,ScU32 maximum,ScU32 *out)
{
    if(!out || (maximum>0x7fffffffu && maximum!=SC32_INFINITE))return SC_UI_E_INVALID;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);if(s<0)return s;
    /* 混合GUI/IPC服务泵只借真实下一期限；不在宿主应用猜拖放的1tick
     * 或为所有空闲GUI加固定高频poll。没有改变计时/消息分派语义。 */
    ScU32 value=ui_drag_wait(c,ui_timer_wait(c,maximum));
    *out=value;ui_object_leave(&c->object);return SC_UI_OK;
}
ScStatus ScUiContextStep(ScUiContext *c,ScU32 timeout)
{
    ScMessage message;
    ScStatus s;
    ScU32 budget;
    UiNode *root;
    if(timeout>0x7fffffffu && timeout!=SC32_INFINITE)return SC_UI_E_RANGE;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    if(c->dispatching){s=SC_UI_E_BUSY;
        goto done;
        }
    if(c->stopped){s=SC_UI_END;
        goto done;
        }
    s=ui_drag_pump(c);
    if(s<0)goto done;
    budget=c->description.scheduling_quantum;
    while(budget){budget--;
        memset(&message,0,sizeof(message));
        message.size=sizeof(message);
        message.version=1;
        s=ScMessageGet(&message,0);
        if(s!=0)break;
        s=ScUiContextDispatch(c,&message);
        if(s<0 && s!=SC_UI_E_CANCELLED && s!=SC_UI_E_STALE && s!=SC32_E_STALE){c->notification_error=s;
            if(c->description.error_procedure){ScUiEvent error;
                ui_event_record(&error,0,SC_UI_EVENT_ERROR);
                error.data=&c->error;
                error.bytes=sizeof(c->error);
                c->description.error_procedure(c,&error,c->description.context);
                }
            s=SC_UI_OK;
            }
        if(c->object.closing || c->stopped)break;
        }
    if(s<0 && s!=SC_UI_E_CANCELLED && s!=SC_UI_E_STALE && s!=SC32_E_STALE)goto done;
    if(c->object.closing || c->stopped){s=SC_UI_END;
        goto done;
        }
    s=ui_timer_pump(c);
    if(s<0)goto done;
    c->walking++;
    for(root=c->roots;root;root=root->root_next){
        if(root->window && ui_root_can_paint(root->window) && root->window->paint_pending){ScStatus painted=ui_paint_root(root);
            if(painted<0 && c->description.error_procedure){ScUiEvent e;
                ui_event_record(&e,root,SC_UI_EVENT_ERROR);
                e.data=&c->error;
                e.bytes=sizeof(c->error);
                c->description.error_procedure(c,&e,c->description.context);
                }}
        if(c->object.closing || c->stopped)break;
    }
    c->walking--;
    ui_nodes_collect(c);
    if(c->stopped){s=SC_UI_END;
        goto done;
        }
    if(!budget){s=SC_UI_OK;
        goto done;
        }
    {ScBrokerWaitDesc wait;
        ScU32 events=0;
        memset(&wait,0,sizeof(wait));
        wait.size=sizeof(wait);
        wait.version=1;
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_STREAM;
        wait.timeout=ui_drag_wait(c,ui_timer_wait(c,timeout));
        if(!wait.timeout){s=SC_UI_OK;
            goto done;
            }
        s=ScBrokerWait(&wait,&events);
        if(s>=0)s=SC_UI_OK;
        else s=ui_cause(c,s,UI_OP_EVENT,0);
        }
done:s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiContextRun(ScUiContext *c,ScI32 *code)
{
    ScStatus s=SC_UI_OK;
    if(!code)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    while(!c->stopped && !c->object.closing){s=ScUiContextStep(c,SC32_INFINITE);
        if(s<0)break;
        }
    if(s>=0){*code=c->exit_code;
        s=SC_UI_OK;
        }
    s=ui_cause(c,s,UI_OP_EVENT,0);
    ui_object_leave(&c->object);
    return s;
}
