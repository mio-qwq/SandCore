static UiNode *ui_node(ScUiContext *c,ScUiNode id)
{
    ScHashEntry entry;
    if(!c || !id || !c->nodes)return 0;
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    if(ScHashGet(c->nodes,&id,sizeof(id),&entry)<0)return 0;
    return *(UiNode * const *)entry.value;
}
static UiNode *ui_root(ScUiContext *c,ScUiNode id)
{UiNode *n=ui_node(c,id);
    return n?ui_node(c,n->root):0;
    }
ScStatus ScUiContextRootNext(ScUiContext *c,ScUiNode after,ScUiNode *out)
{
    UiNode *n;
    ScStatus s;
    if(!out)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    /* 查询无分配/回调，当前线程返回前拓扑不变；不扫描历史编号或控件。 */
    if(!after)n=c->roots;
    else{
        n=ui_node(c,after);
        if(!n || !n->window || n->root!=n->id || (n->private_flags&UI_NODE_RETIRED)){
            s=SC_UI_E_STALE;
            goto done;
        }
        n=n->root_next;
    }
    if(n){*out=n->id;s=SC_UI_OK;}
    else s=SC_UI_END;
done:
    s=ui_cause(c,s,UI_OP_NODE,after);
    ui_object_leave(&c->object);
    return s;
}
static void ui_node_hold(UiNode *n){n->depth++;
    n->owner->object.depth++;
    }
static void ui_node_leave(UiNode *n)
{ScUiContext *c=n->owner;
    if(n->depth)n->depth--;
    if(!c->walking)ui_nodes_collect(c);
    ui_object_leave(&c->object);
    }
static void ui_node_clear(UiNode *n)
{
    int quiet=n->owner->quiet || ui_quiet;
    if(n->private_flags&UI_NODE_CLEARED)return;
    n->private_flags|=UI_NODE_CLEARED;
    ui_drag_node_destroy(n,quiet);
    ui_controls_destroy(n,quiet);
    ui_dialog_node_destroy(n,quiet);
    ui_view_destroy(n);
    ui_popup_node_destroy(n);
    if(!quiet && (n->private_flags&UI_NODE_CONTEXT_COMMITTED) && n->context_drop)n->context_drop(n->context,0);
    n->context_drop=0;
    n->context=0;
    ScStringRelease(n->text);
    ScStringRelease(n->accessible_name);
    ScStringRelease(n->tooltip);
    ScUiIconRelease(n->icon);
    ui_value_clear(&n->value);
    n->text=0;
    n->accessible_name=0;
    n->tooltip=0;
    n->icon=0;
    if(n->custom && n->custom->live_nodes)n->custom->live_nodes--;
    if(n->window){free(n->window->pixels);
        ScArrayRelease(n->window->damage);
        free(n->window);
        n->window=0;
        }
    if(n->retire_parent){UiNode *parent=n->retire_parent;
        n->retire_parent=0;
        if(parent->depth)parent->depth--;
        if(parent->retire_children)parent->retire_children--;
        }
}
static void ui_node_destroy_notice(UiNode *n)
{
    if(n->retire_children || (n->private_flags&UI_NODE_DESTROY_SENT))return;
    n->private_flags|=UI_NODE_DESTROY_SENT;
    if((n->private_flags&UI_NODE_CREATE_DELIVERED) && !n->owner->quiet && !ui_quiet)ui_event(n,SC_UI_EVENT_DESTROY,0,0);
    ui_listeners_remove_node(n->owner,n->id);
}
static void ui_nodes_collect(ScUiContext *c)
{
    UiNode *n,**link;
    if(c->walking || !c->retired)return;
    c->walking++;
    for(n=c->retired;n;n=n->retired_next){UiNode *ready=n;
        /* 内容先叶后父；header整遍结束才free，回调仍可保存已退役路径。 */
        while(ready && !ready->retire_children && !(ready->private_flags&UI_NODE_CLEARED)){
            UiNode *parent=ready->retire_parent;
            ui_node_destroy_notice(ready);
            if(ready->depth)break;
            ui_node_clear(ready);
            ready=parent && (parent->private_flags&UI_NODE_RETIRED)?parent:0;
        }
    }
    c->walking--;
    link=&c->retired;
    while(*link){n=*link;
        if(!n->depth && (n->private_flags&UI_NODE_CLEARED)){
            *link=n->retired_next;
            if(n->all_previous)n->all_previous->all_next=n->all_next;
            else c->all_nodes=n->all_next;
            if(n->all_next)n->all_next->all_previous=n->all_previous;
            free(n);
        }
        else link=&n->retired_next;
    }
}
static void ui_detach_node(UiNode *n)
{
    if(n->previous)n->previous->next=n->next;
    else if(n->parent)n->parent->first_child=n->next;
    if(n->next)n->next->previous=n->previous;
    else if(n->parent)n->parent->last_child=n->previous;
    n->parent=0;
    n->previous=0;
    n->next=0;
}
static void ui_append_node(UiNode *parent,UiNode *n,UiNode *before)
{
    n->parent=parent;
    if(before){n->next=before;
        n->previous=before->previous;
        before->previous=n;
        if(n->previous)n->previous->next=n;
        else parent->first_child=n;
        }
    else{n->previous=parent->last_child;
        if(n->previous)n->previous->next=n;
        else parent->first_child=n;
        parent->last_child=n;
        }
}
static ScStatus ui_node_new(ScUiContext *c,ScU32 kind,UiNode *parent,UiNode **out)
{
    UiNode *n;
    UiClassDef *custom=0;
    ScStatus s;
    if(c->next_node==UINT_MAX || c->revision==UINT_MAX)return SC_UI_E_RANGE;
    if(kind>=SC_UI_CLASS_FIRST){ScHashEntry e;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        if(ScHashGet(c->classes,&kind,sizeof(kind),&e)<0)return SC_UI_E_NOT_FOUND;
        custom=*(UiClassDef * const *)e.value;
        if(custom->retired)return SC_UI_E_STALE;
    }
    else if(!kind || kind>UI_KIND_POPUP)return SC_UI_E_INVALID;
    n=(UiNode *)calloc(1,sizeof(*n));
    if(!n)return SC_UI_E_MEMORY;
    n->owner=c;
    n->id=c->next_node;
    n->generation=n->id;
    n->kind=kind;
    n->custom=custom;
    n->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    n->effective=n->state;
    n->root=parent?parent->root:n->id;
    n->value_revision=1;
    n->layout_revision=1;
    n->scroll_step_x=16;
    n->scroll_step_y=20;
    n->menu_hot=n->menu_pressed=UINT_MAX;
    n->layout.size=sizeof(n->layout);
    n->layout.version=1;
    n->layout.mode=SC_UI_ABSOLUTE;
    n->layout.width=SC_UI_AUTO;
    n->layout.height=SC_UI_AUTO;
    n->layout.max_width=SC_UI_AUTO;
    n->layout.max_height=SC_UI_AUTO;
    n->layout.column_span=1;
    n->layout.row_span=1;
    n->layout.flags=UI_LAYOUT_CLIP;
    n->value.value.size=sizeof(ScValue);
    n->value.value.version=1;
    s=ScHashSet(c->nodes,&n->id,sizeof(n->id),&n,SC_HASH_INSERT_ONLY);
    if(s<0){free(n);
        return ui_cause(c,s,UI_OP_NODE,0);
        }
    c->next_node++;
    c->revision++;
    c->live_nodes++;
    n->all_next=c->all_nodes;
    if(c->all_nodes)c->all_nodes->all_previous=n;
    c->all_nodes=n;
    if(parent)ui_append_node(parent,n,0);
    if(custom)custom->live_nodes++;
    *out=n;
    return SC_UI_OK;
}
static ScStatus ui_tree_effective(UiNode *root)
{
    UiNode *n=root;
    while(n){ScU32 effective=n->state;
        if(n->parent){effective&=~(SC_UI_VISIBLE|SC_UI_ENABLED);
            effective|=(n->state&n->parent->effective&(SC_UI_VISIBLE|SC_UI_ENABLED));
            }
        n->effective=effective;
        if(n->first_child){n=n->first_child;
            continue;
            }
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
    }
    ui_drag_state(root);
    return SC_UI_OK;
}
static void ui_node_layout_dirty(UiNode *n)
{
    UiNode *root=ui_root(n->owner,n->id);
    while(n){n->private_flags|=UI_NODE_LAYOUT_DIRTY;
        n=n->parent;
        }
    if(root){root->private_flags|=UI_NODE_LAYOUT_DIRTY;
        root->owner->layout_pending=1;
        ui_node_damage(root);
        }
}
static void ui_node_damage(UiNode *n)
{
    UiNode *root=ui_node(n->owner,n->root);
    ScUiRect area;
    if(!root || !root->window)return;
    area=n->rectangle;
    ui_damage(root,&area);
}
static void ui_node_retire(UiNode *root)
{
    ScUiContext *c=root->owner;
    UiNode *n,*work=0;
    if(root->private_flags&UI_NODE_RETIRED)return;
    c->walking++;
    /* 标记时先绑父依赖，逆序工作链无分配。重入销祖先跳过已退役
     * 分支，父通知等子通知/内容回收后再执行，不重复接入同一header。 */
    n=root;
    while(n){
        if(n->private_flags&UI_NODE_RETIRED)goto mark_next;
        n->private_flags|=UI_NODE_RETIRED|UI_NODE_DESTROYING;
        n->destroy_next=work;
        work=n;
        {UiNode *parent=n->parent;
            if(!parent && n->window && n->window->owner)parent=ui_node(c,n->window->owner);
            if(parent){parent->depth++;
                parent->retire_children++;
                n->retire_parent=parent;
                }}
        n->retired_next=c->retired;
        c->retired=n;
        ScHashRemove(c->nodes,&n->id,sizeof(n->id));
        ui_timers_remove_node(n);
        if(c->live_nodes)c->live_nodes--;
        if(n->window && c->live_roots){
            c->live_roots--;
            if(n->root_previous)n->root_previous->root_next=n->root_next;
            else c->roots=n->root_next;
            if(n->root_next)n->root_next->root_previous=n->root_previous;
            n->root_previous=0;
            n->root_next=0;
        }
        if(n->first_child){n=n->first_child;
            continue;
            }
mark_next:
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
    }
    while(work){UiNode *next=work->destroy_next;
        work->destroy_next=0;
        ui_node_destroy_notice(work);
        ui_detach_node(work);
        if(!work->depth && (work->private_flags&UI_NODE_DESTROY_SENT))ui_node_clear(work);
        work=next;
    }
    c->walking--;
    if(c->revision<UINT_MAX)c->revision++;
    if(!c->live_roots && (c->description.flags&SC_UI_STOP_LAST_ROOT))c->stopped=1;
    if(!c->walking)ui_nodes_collect(c);
}
static ScStatus ui_node_text(UiNode *n,const ScUiText *text)
{
    ScString *next=0;
    ScStatus s;
    ScUiEvent e;
    ScUiText candidate;
    ScU32 revision=n->value_revision;
    if(n->value_revision==UINT_MAX || n->owner->revision==UINT_MAX)return SC_UI_E_RANGE;
    s=ui_text_copy(text,&next);
    if(s<0)return ui_cause(n->owner,s,UI_OP_NODE,n->id);
    ui_text_view(next,&candidate);
    ui_event_record(&e,n,SC_UI_EVENT_VALUE_CHANGING);
    e.flags=SC_UI_EVENT_PROGRAMMATIC|SC_UI_EVENT_CANCELABLE;
    e.data=&candidate;
    e.bytes=sizeof(candidate);
    s=ui_emit(n->owner,n,&e);
    if(s<0 || s==SC_UI_VETO || (n->private_flags&UI_NODE_RETIRED)){ScStringRelease(next);
        return s<0?s:SC_UI_E_CANCELLED;
        }
    if(n->value_revision!=revision){ScStringRelease(next);
        return SC_UI_E_STALE;
        }
    s=ui_controls_set_text(n,&candidate);
    if(s<0 || (n->private_flags&UI_NODE_RETIRED)){ScStringRelease(next);
        return s<0?s:SC_UI_E_CANCELLED;
        }
    ScStringRelease(n->text);
    n->text=next;
    n->value_revision++;
    n->owner->revision++;
    ui_node_layout_dirty(n);
    s=ui_event(n,SC_UI_EVENT_VALUE_CHANGED,0,0);
    if(s<0)n->owner->notification_error=s;
    return SC_UI_OK;
}
ScStatus ScUiWidgetCreate(ScUiContext *c,const ScUiWidgetDesc *description,ScUiNode *out)
{
    UiNode *parent,*n=0;
    ScString *text=0;
    ScStatus s;
    ScU32 i;
    s=ui_input(description,sizeof(*description));
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    for(i=0;i<4;i++)if(description->reserved[i])return SC_UI_E_INVALID;
    if(description->kind==SC_UI_WINDOW || description->kind==UI_KIND_POPUP)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    parent=ui_node(c,description->parent);
    if(!parent){s=SC_UI_E_STALE;
        goto done;
        }
    s=ui_text_copy(&description->text,&text);
    if(s<0)goto done;
    s=ui_pin((const void *)description->context_drop);
    if(s<0)goto done;
    s=ui_node_new(c,description->kind,parent,&n);
    if(s<0)goto done;
    ui_node_hold(n);
    n->text=text;
    text=0;
    n->flags=description->flags;
    n->command=description->command;
    n->context=description->context;
    n->context_drop=description->context_drop;
    s=ui_controls_init(n);
    if(s==0){n->private_flags|=UI_NODE_CREATE_DELIVERED;
        s=ui_event(n,SC_UI_EVENT_CREATE,0,0);
        }
    if(s<0 || s==SC_UI_VETO || (n->private_flags&UI_NODE_RETIRED)){
        if(!(n->private_flags&UI_NODE_RETIRED))ui_node_retire(n);
        s=s<0?s:SC_UI_E_CANCELLED;
    }
    else{n->private_flags|=UI_NODE_CONTEXT_COMMITTED;
        ui_tree_effective(n);
        ui_timers_visibility(c,n->root,1);
        ui_node_layout_dirty(n);
        *out=n->id;
        s=SC_UI_OK;
        }
    ui_node_leave(n);
done:ScStringRelease(text);
    if(s<0)ui_fail(c,s,s,0,UI_OP_CREATE,description->parent);
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiWindowCreate(ScUiContext *c,const ScUiWindowDesc *description,ScUiNode *out)
{
    UiNode *n=0,*owner;
    ScWindowDesc d;
    ScWindow window=0;
    ScString *title=0;
    const char *z=0;
    ScStatus s;
    s=ui_input(description,sizeof(*description));
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(description->reserved[0] || description->reserved[1])return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    owner=description->owner?ui_node(c,description->owner):0;
    if(description->owner && (!owner || !owner->window)){s=SC_UI_E_STALE;
        goto done;
        }
    s=ui_text_copy(&description->title,&title);
    if(s<0)goto done;
    s=ScStringCStr(title,&z);
    if(s<0)goto done;
    if(strlen(z)!=description->title.bytes || description->title.bytes>127){s=SC_UI_E_INVALID;
        goto done;
        }
    s=ui_pin((const void *)description->context_drop);
    if(s<0)goto done;
    s=ui_node_new(c,SC_UI_WINDOW,0,&n);
    if(s<0)goto done;
    ui_node_hold(n);
    n->text=title;
    title=0;
    n->context=description->context;
    n->context_drop=description->context_drop;
    n->window=(UiRoot *)calloc(1,sizeof(*n->window));
    if(!n->window){s=SC_UI_E_MEMORY;
        goto rollback;
        }
    n->private_flags|=UI_ROOT_CREATING;
    n->window->owner=description->owner;
    n->window->id=n->id;
    c->live_roots++;
    n->root_next=c->roots;
    if(c->roots)c->roots->root_previous=n;
    c->roots=n;
    s=ui_array_new(sizeof(ScUiRect),&n->window->damage);
    if(s<0)goto rollback;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.title=z;
    d.x=description->x;
    d.y=description->y;
    d.client_width=description->client_width;
    d.client_height=description->client_height;
    d.style=description->style;
    if(owner){d.owner_window=owner->window->window;
        d.owner_generation=owner->window->information.generation;
        }
    d.procedure=ui_native_proc;
    d.context=n;
    c->dispatching++;
    s=ScWindowCreate(&d,&window);
    c->dispatching--;
    if(s<0 || (n->private_flags&UI_NODE_RETIRED)){s=s<0?ui_cause(c,s,UI_OP_CREATE,n->id):SC_UI_E_CANCELLED;
        goto rollback;
        }
    n->window->window=window;
    n->private_flags&=~UI_ROOT_CREATING;
    /* CREATE过程看到的是尚未ACTIVATE的隐藏候选。成功返回前再查一次
     * 已提交的真实窗口，避免已排队键盘先于VIS通知时仍按旧可见性路由。 */
    s=ui_root_surface(n);
    if(s<0)goto rollback;
    n->private_flags|=UI_NODE_CONTEXT_COMMITTED;
    *out=n->id;
    s=SC_UI_OK;
    ui_node_leave(n);
    goto done;
rollback:if(!(n->private_flags&UI_NODE_RETIRED))ui_node_retire(n);
    ui_node_leave(n);
done:ScStringRelease(title);
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeDestroy(ScUiContext *c,ScUiNode id)
{
    UiNode *n;
    ScU32 root_id;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n){s=SC_UI_E_STALE;
        goto done;
        }
    if(n->private_flags&UI_NODE_DESTROYING){s=SC_UI_OK;
        goto done;
        }
    root_id=n->root;
    ui_node_hold(n);
    c->walking++;
    n->private_flags|=UI_NODE_DESTROYING;
    ui_input_subtree(n);
    c->walking--;
    if(n->window && n->window->window){s=ScWindowDestroy(n->window->window);
        if(s<0 && s!=SC32_E_STALE){n->private_flags&=~UI_NODE_DESTROYING;
            s=ui_cause(c,s,UI_OP_NODE,id);
            ui_node_leave(n);
            goto done;
            }}
    if(!(n->private_flags&UI_NODE_RETIRED))ui_node_retire(n);
    ui_node_leave(n);
    n=ui_node(c,root_id);
    if(n && n->window && !n->window->focus)ScUiFocusNext(c,root_id,1);
    s=SC_UI_OK;
done:s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeQuery(const ScUiContext *c,ScUiNode id,ScUiNodeInfo *out)
{
    UiNode *n;
    ScUiNodeInfo v;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    n=ui_node((ScUiContext *)c,id);
    if(!n)return SC_UI_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.node=id;
    v.generation=n->generation;
    v.kind=n->kind;
    v.parent=n->parent?n->parent->id:0;
    v.first_child=n->first_child?n->first_child->id:0;
    v.next_sibling=n->next?n->next->id:0;
    v.root=n->root;
    v.native_window=ui_root(n->owner,id)->window->window;
    v.state=n->effective;
    v.flags=n->flags;
    v.rectangle=n->rectangle;
    v.clip=n->clip;
    v.value_revision=n->value_revision;
    v.layout_revision=n->layout_revision;
    v.font_role=n->font_role;
    memcpy(out,&v,sizeof(v));
    return SC_UI_OK;
}
ScStatus ScUiWindowNative(ScUiContext *c,ScUiNode id,ScWindowInfo *out)
{UiNode *r=ui_root(c,id);
    if(!r || !r->window)return SC_UI_E_STALE;
    return ui_cause(c,ScWindowQuery(r->window->window,out),UI_OP_NODE,id);
    }
ScStatus ScUiWindowShow(ScUiContext *c,ScUiNode id,ScU32 action)
{
    UiNode *r;
    ScWindowInfo info;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    r=ui_root(c,id);
    if(!r || !r->window)s=SC_UI_E_STALE;
    else{memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScWindowControl(r->window->window,action,&info);
        if(s==0){r->window->information=info;
            r->window->visible=(info.state&SC32_WINDOW_VISIBLE)!=0;
            ui_timers_visibility(c,r->id,r->window->visible);
            ui_drag_state(r);
            }
        else s=ui_cause(c,s,UI_OP_NODE,id);
        }
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiWindowSetTitle(ScUiContext *c,ScUiNode id,const ScUiText *text)
{
    UiNode *r=ui_root(c,id);
    ScString *next=0;
    const char *z;
    ScStatus s;
    if(!r || !r->window)return SC_UI_E_STALE;
    s=ui_text_copy(text,&next);
    if(s<0)return s;
    s=ScStringCStr(next,&z);
    if(s==0 && (text->bytes>127 || strlen(z)!=text->bytes))s=SC_UI_E_INVALID;
    if(s==0)s=ScWindowSetTitle(r->window->window,z);
    if(s==0){ScStringRelease(r->text);
        r->text=next;
        }
    else ScStringRelease(next);
    return ui_cause(c,s,UI_OP_NODE,id);
}
ScStatus ScUiNodeMove(ScUiContext *c,ScUiNode id,ScUiNode parent_id,ScUiNode before_id)
{
    UiNode *n,*parent,*before,*walk;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    parent=ui_node(c,parent_id);
    before=before_id?ui_node(c,before_id):0;
    if(!n || !parent || (before_id && !before))s=SC_UI_E_STALE;
    else if(n->window || (before && before->parent!=parent))s=SC_UI_E_INVALID;
    else{for(walk=parent;walk && walk!=n;walk=walk->parent){}
        if(walk==n)s=SC_UI_E_INVALID;
        else if(before==n)s=SC_UI_OK;
        else if(c->revision==UINT_MAX)s=SC_UI_E_RANGE;
        else{UiNode *old_parent=n->parent,*old_root=ui_root(c,n->id);
            ScU32 old_id=n->root;
            ui_node_hold(n);
            c->walking++;
            ui_node_damage(n);
            if(old_id!=parent->root && old_root && old_root->window){ui_input_subtree(n);
                ui_menu_close_owner(c,old_id);
            }
            if((n->private_flags&UI_NODE_RETIRED) || (parent->private_flags&UI_NODE_RETIRED) || (before && (before->private_flags&UI_NODE_RETIRED))){s=SC_UI_E_CANCELLED;
                goto move_done;
                }
            ui_detach_node(n);
            ui_append_node(parent,n,before);
            if(old_id!=parent->root){UiNode *current=n;
                while(current){current->root=parent->root;
                    if(current->first_child){current=current->first_child;
                        continue;
                        }
                    while(current!=n && !current->next)current=current->parent;
                    if(current==n)break;
                    current=current->next;
                    }}
            c->revision++;
            ui_tree_effective(n);
            ui_timers_visibility(c,parent->root,1);
            if(old_parent && !(old_parent->private_flags&UI_NODE_RETIRED))ui_node_layout_dirty(old_parent);
            ui_node_layout_dirty(parent);
            if(old_id!=parent->root && old_root && !(old_root->private_flags&UI_NODE_RETIRED) && !old_root->window->focus)ScUiFocusNext(c,old_id,1);
            s=SC_UI_OK;
move_done:c->walking--;
            ui_node_leave(n);
            }}
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeSetState(ScUiContext *c,ScUiNode id,ScU32 mask,ScU32 value)
{
    UiNode *n,*r;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    ScU32 allowed=SC_UI_VISIBLE|SC_UI_ENABLED|SC_UI_READONLY|SC_UI_CHECKED|SC_UI_MIXED|SC_UI_SELECTED|SC_UI_INVALID|SC_UI_EXPANDED|SC_UI_BUSY;
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n)s=SC_UI_E_STALE;
    else if(mask&~allowed || value&~mask)s=SC_UI_E_INVALID;
    else if(c->revision==UINT_MAX)s=SC_UI_E_RANGE;
    else{ui_node_hold(n);
        c->walking++;
        n->state=(n->state&~mask)|value;
        c->revision++;
        ui_tree_effective(n);
        ui_node_damage(n);
        if(mask&SC_UI_VISIBLE){ui_timers_visibility(c,n->root,1);
            ui_node_layout_dirty(n);
            }
        r=ui_root(c,id);
        if((n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED)){ui_input_subtree(n);
            if(r && r->window && !(r->private_flags&UI_NODE_RETIRED) && !r->window->focus)ScUiFocusNext(c,r->id,1);
            }
        s=(n->private_flags&UI_NODE_RETIRED)?SC_UI_E_CANCELLED:SC_UI_OK;
        c->walking--;
        ui_node_leave(n);
        }
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeSetText(ScUiContext *c,ScUiNode id,const ScUiText *text)
{UiNode *n;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n)s=SC_UI_E_STALE;
    else{ui_node_hold(n);
        s=ui_node_text(n,text);
        ui_node_leave(n);
        }
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
    }
ScStatus ScUiNodeCopyText(ScUiContext *c,ScUiNode id,ScString **out)
{
    UiNode *n=ui_node(c,id);
    ScUiText text;
    ScStatus s;
    if(!n || !out)return n?SC_UI_E_INVALID:SC_UI_E_STALE;
    if(n->kind==SC_UI_EDIT || n->kind==SC_UI_TEXTBOX || n->kind==SC_UI_SPINBOX || (n->kind==SC_UI_LABEL && (n->flags&SC_UI_LABEL_SELECTABLE)))return ui_cause(c,ui_controls_copy_text(n,out),UI_OP_NODE,id);
    ui_text_view(n->text,&text);
    s=ScStringCreate(text.data,text.bytes,out);
    return ui_cause(c,s,UI_OP_NODE,id);
}
ScStatus ScUiNodeSetValue(ScUiContext *c,ScUiNode id,const ScValue *value)
{
    UiNode *n;
    UiValue next;
    ScUiEvent e;
    ScU32 revision;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,id);
    memset(&next,0,sizeof(next));
    if(!n){s=SC_UI_E_STALE;
        goto done;
        }
    if(n->value_revision==UINT_MAX || c->revision==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    s=ui_value_copy(value,&next);
    if(s<0)goto done;
    revision=n->value_revision;
    ui_node_hold(n);
    ui_event_record(&e,n,SC_UI_EVENT_VALUE_CHANGING);
    e.flags=SC_UI_EVENT_PROGRAMMATIC|SC_UI_EVENT_CANCELABLE;
    e.data=&next.value;
    e.bytes=sizeof(next.value);
    s=ui_emit(c,n,&e);
    if(s==SC_UI_VETO || (n->private_flags&UI_NODE_RETIRED))s=SC_UI_E_CANCELLED;
    if(s>=0 && n->value_revision!=revision)s=SC_UI_E_STALE;
    if(s>=0)s=ui_controls_set_value(n,&next.value);
    if(s>=0 && !(n->private_flags&UI_NODE_RETIRED)){ui_value_clear(&n->value);
        n->value=next;
        memset(&next,0,sizeof(next));
        n->value_revision++;
        c->revision++;
        if(n->kind==SC_UI_CHECKBOX || n->kind==SC_UI_RADIO){n->state&=~(SC_UI_CHECKED|SC_UI_MIXED);
            if(n->value.value.lo==1)n->state|=SC_UI_CHECKED;
            else if(n->value.value.lo==2)n->state|=SC_UI_MIXED;
            ui_tree_effective(n);
            }
        ui_node_damage(n);
        if(ui_event(n,SC_UI_EVENT_VALUE_CHANGED,0,0)<0)c->notification_error=SC_UI_E_FAILED;
        s=SC_UI_OK;
        }
    else if(s>=0)s=SC_UI_E_CANCELLED;
    ui_node_leave(n);
done:ui_value_clear(&next);
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiNodeGetValue(ScUiContext *c,ScUiNode id,ScValue *out)
{UiNode *n;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    n=ui_node(c,id);
    if(!n)return SC_UI_E_STALE;
    memcpy(out,&n->value.value,sizeof(*out));
    return SC_UI_OK;
    }
ScStatus ScUiNodeSetIcon(ScUiContext *c,ScUiNode id,ScUiIcon *icon)
{
    UiNode *n=ui_node(c,id);
    ScStatus s;
    if(!n)return SC_UI_E_STALE;
    if(icon){s=ScUiIconRetain(icon);
        if(s<0)return s;
        }
    ScUiIconRelease(n->icon);
    n->icon=icon;
    ui_node_layout_dirty(n);
    return SC_UI_OK;
}
ScStatus ScUiNodeSetFont(ScUiContext *c,ScUiNode id,ScU32 role)
{UiNode *n=ui_node(c,id);
    if(!n)return SC_UI_E_STALE;
    if(role>SC_UI_FONT_CODE)return SC_UI_E_INVALID;
    n->font_role=role;
    ui_node_layout_dirty(n);
    return SC_UI_OK;
    }
ScStatus ScUiClassRegister(ScUiContext *c,const ScUiClassDesc *description,ScUiClass *out)
{
    UiClassDef *item;
    ScStatus s=ui_input(description,sizeof(*description));
    ScU32 i;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    for(i=0;i<6;i++)if(description->reserved[i])return SC_UI_E_INVALID;
    if(!description->name || !*description->name || strlen(description->name)>127)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    if(c->next_class==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    item=(UiClassDef *)calloc(1,sizeof(*item));
    if(!item){s=SC_UI_E_MEMORY;
        goto done;
        }
    item->description=*description;
    item->id=c->next_class;
    s=ScStringCreate(description->name,strlen(description->name),&item->name);
    if(s==0)s=ui_pin((const void *)description->measure);
    if(s==0)s=ui_pin((const void *)description->arrange);
    if(s==0)s=ui_pin((const void *)description->paint);
    if(s==0)s=ui_pin((const void *)description->event);
    if(s==0)s=ui_pin((const void *)description->context_drop);
    if(s==0)s=ScHashSet(c->classes,&item->id,sizeof(item->id),&item,SC_HASH_INSERT_ONLY);
    if(s<0){ScStringRelease(item->name);
        free(item);
        }
    else{c->next_class++;
        *out=item->id;
        s=SC_UI_OK;
        }
done:s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiClassUnregister(ScUiContext *c,ScUiClass id)
{
    ScHashEntry e;
    UiClassDef *item;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    s=ScHashGet(c->classes,&id,sizeof(id),&e);
    if(s<0)s=SC_UI_E_NOT_FOUND;
    else{item=*(UiClassDef * const *)e.value;
        if(item->live_nodes || item->depth)s=SC_UI_E_BUSY;
        else{ScHashRemove(c->classes,&id,sizeof(id));
            if(item->description.context_drop)item->description.context_drop(item->description.context,0);
            ScStringRelease(item->name);
            free(item);
            s=SC_UI_OK;
            }}
    s=ui_cause(c,s,UI_OP_NODE,0);
    ui_object_leave(&c->object);
    return s;
}
