static ScU32 ui_menu_flags(ScUiContext *,UiMenuEntry *);
static ScU32 ui_menu_count(ScUiMenu *menu)
{ScContainerInfo i;
    memset(&i,0,sizeof(i));
    i.size=sizeof(i);
    i.version=1;
    ScArrayQuery(menu->items,&i);
    return i.length;
    }
static UiMenuEntry *ui_menu_at(ScUiMenu *menu,ScU32 index)
{UiMenuEntry *e=0;
    ScArrayGet(menu->items,index,&e);
    return e;
    }
static UiMenuEntry *ui_menu_find(ScUiMenu *menu,ScU32 id,ScU32 *index)
{ScU32 i,count=ui_menu_count(menu);
    for(i=0;i<count;i++){UiMenuEntry *e=ui_menu_at(menu,i);
        if(e->id==id){if(index)*index=i;
            return e;
            }}
    return 0;
    }
static void ui_menu_entry_free(UiMenuEntry *e,int quiet)
{
    if(!quiet && e->description.context_drop)e->description.context_drop(e->description.context,0);
    ScStringRelease(e->label);
    ScStringRelease(e->help);
    ScStringRelease(e->caption);
    ScUiIconRelease(e->description.icon);
    ScUiMenuRelease(e->description.submenu);
    free(e);
}
static void ui_menu_dispose(UiObject *base,int quiet)
{
    ScUiMenu *m=(ScUiMenu *)base;
    ScU32 i,count=ui_menu_count(m);
    for(i=0;i<count;i++)ui_menu_entry_free(ui_menu_at(m,i),quiet);
    ScArrayRelease(m->items);
    m->items=0;
}
ScStatus ScUiMenuCreate(ScUiMenu **out)
{
    void *p=0;
    ScUiMenu *m;
    ScStatus s;
    if(!out)return SC_UI_E_INVALID;
    s=ui_object_new(sizeof(*m),UI_MENU,ui_menu_dispose,&p);
    if(s<0)return s;
    m=(ScUiMenu *)p;
    m->revision=1;
    m->next_id=1;
    s=ui_array_new(sizeof(UiMenuEntry *),&m->items);
    if(s<0){ui_object_release(&m->object);
        return ui_cause(0,s,UI_OP_MENU,0);
        }
    *out=m;
    return SC_UI_OK;
}
ScStatus ScUiMenuRetain(ScUiMenu *m){return ui_object_retain((UiObject *)m,UI_MENU);
    }
void ScUiMenuRelease(ScUiMenu *m){ui_object_release((UiObject *)m);
    }
ScStatus ScUiMenuQuery(const ScUiMenu *m,ScContainerInfo *out)
{
    ScContainerInfo v;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!m || m->object.kind!=UI_MENU || m->object.closing)return SC_UI_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.length=ui_menu_count((ScUiMenu *)m);
    v.capacity=v.length;
    v.item_bytes=sizeof(ScUiMenuItem);
    v.generation=m->revision;
    memcpy(out,&v,sizeof(v));
    return SC_UI_OK;
}
static void ui_menu_description(UiMenuEntry *e,ScUiMenuItem *out)
{*out=e->description;
    out->item=e->id;
    ui_text_view(e->label,&out->label);
    ui_text_view(e->help,&out->help);
    }
ScStatus ScUiMenuGet(const ScUiMenu *m,ScU32 id,ScUiMenuItem *out)
{
    UiMenuEntry *e;
    ScUiMenuItem d;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!m || m->object.kind!=UI_MENU || m->object.closing)return SC_UI_E_STALE;
    e=ui_menu_find((ScUiMenu *)m,id,0);
    if(!e)return SC_UI_E_NOT_FOUND;
    ui_menu_description(e,&d);
    memcpy(out,&d,sizeof(d));
    return SC_UI_OK;
}
static ScStatus ui_menu_caption(ScString *label,ScString **out,ScU32 *mnemonic)
{
    ScUiText raw;
    ScString *next=0;
    ScU32 at=0;
    ScStatus s;
    ui_text_view(label,&raw);
    *mnemonic=0;
    s=ScStringCreate(0,0,&next);
    if(s<0)return s;
    while(at<raw.bytes){ScU32 begin=at,scalar;
        s=ScUtf8Decode(raw.data,raw.bytes,&at,&scalar);
        if(s<0)goto failed;
        if(scalar=='&'){
            if(at==raw.bytes){s=SC_UI_E_INVALID;
                goto failed;
                }
            begin=at;
            s=ScUtf8Decode(raw.data,raw.bytes,&at,&scalar);
            if(s<0)goto failed;
            if(scalar!='&'){if(*mnemonic){s=SC_UI_E_INVALID;
                    goto failed;
                    }
                *mnemonic=scalar>='A' && scalar<='Z'?scalar+32:scalar;
                }
        }
        s=ScStringAppend(next,raw.data+begin,at-begin);
        if(s<0)goto failed;
    }
    *out=next;
    return SC_UI_OK;
failed:ScStringRelease(next);
    return s;
}
static ScStatus ui_menu_cycle(ScUiMenu *parent,ScUiMenu *submenu)
{
    ScArray *stack=0;
    ScHash *seen=0;
    ScStatus s;
    ScU32 count=0;
    if(!submenu)return SC_UI_OK;
    if(submenu==parent)return SC_UI_E_INVALID;
    s=ui_array_new(sizeof(ScUiMenu *),&stack);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&seen);
    if(s<0)goto done;
    s=ScArrayInsert(stack,0,&submenu,1);
    if(s<0)goto done;
    count=1;
    while(count){ScUiMenu *m;
        ScHashEntry existing;
        ScU32 i,mark=1;
        ScArrayGet(stack,--count,&m);
        ScArrayErase(stack,count,1);
        if(m==parent){s=SC_UI_E_INVALID;
            goto done;
            }
        if(!m || m->object.closing){s=SC_UI_E_STALE;
            goto done;
            }
        memset(&existing,0,sizeof(existing));
        existing.size=sizeof(existing);
        existing.version=1;
        if(ScHashGet(seen,&m,sizeof(m),&existing)==0)continue;
        s=ScHashSet(seen,&m,sizeof(m),&mark,0);
        if(s<0)goto done;
        for(i=0;i<ui_menu_count(m);i++){ScUiMenu *next=ui_menu_at(m,i)->description.submenu;
            if(next){s=ScArrayInsert(stack,count++,&next,1);
                if(s<0)goto done;
                }}
    }
    s=SC_UI_OK;
done:ScHashRelease(seen);
    ScArrayRelease(stack);
    return s;
}
static ScStatus ui_menu_entry_new(ScUiMenu *m,const ScUiMenuItem *d,ScU32 *next_id,UiMenuEntry **out)
{
    UiMenuEntry *e;
    ScStatus s=ui_input(d,sizeof(*d));
    ScU32 i;
    if(s<0)return s;
    for(i=0;i<3;i++)if(d->reserved[i])return SC_UI_E_INVALID;
    if(d->flags&~31u || (!(d->flags&SC_UI_MENU_SEPARATOR) && !d->command && !d->submenu))return SC_UI_E_INVALID;
    if(*next_id==UINT_MAX || d->item==UINT_MAX)return SC_UI_E_RANGE;
    if(d->item && d->item<*next_id && !ui_menu_find(m,d->item,0))return SC_UI_E_STALE;
    s=ui_menu_cycle(m,d->submenu);
    if(s<0)return s;
    e=(UiMenuEntry *)calloc(1,sizeof(*e));
    if(!e)return SC_UI_E_MEMORY;
    e->description=*d;
    e->description.icon=0;
    e->description.submenu=0;
    e->id=d->item?d->item:*next_id;
    if(e->id>=*next_id)*next_id=e->id+1;
    s=ui_text_copy(&d->label,&e->label);
    if(s==0)s=ui_text_copy(&d->help,&e->help);
    if(s==0)s=ui_menu_caption(e->label,&e->caption,&e->mnemonic);
    if(s==0 && d->icon){s=ScUiIconRetain(d->icon);
        if(s==0)e->description.icon=d->icon;
        }
    if(s==0 && d->submenu){s=ScUiMenuRetain(d->submenu);
        if(s==0)e->description.submenu=d->submenu;
        }
    if(s==0)s=ui_pin((const void *)d->context_drop);
    if(s<0){ui_menu_entry_free(e,1);
        return s;
        }
    e->description.size=sizeof(e->description);
    e->description.version=1;
    e->description.item=e->id;
    *out=e;
    return SC_UI_OK;
}
static void ui_menu_invalidate(ScUiMenu *menu)
{
    UiObject *object;
    for(object=ui_objects;object;object=object->next)if(object->kind==UI_CONTEXT && !object->closing){ScUiContext *c=(ScUiContext *)object;
        UiNode *n;
        for(n=c->all_nodes;n;n=n->all_next)if(!(n->private_flags&UI_NODE_RETIRED) && n->menu==menu){n->menu_pressed=UINT_MAX;
            n->state&=~SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_layout_dirty(n);
            }
        {UiPopup *p;
            for(p=c->popups;p;p=p->next)if(p->menu==menu){UiNode *node=ui_node(c,p->node);
                p->hot=UINT_MAX;
                if(p->hover_timer){ScUiTimerCancel(c,p->hover_timer);
                    p->hover_timer=0;
                    }
                if(node)ui_node_damage(node);
                }}
    }
}
ScStatus ScUiMenuReplace(ScUiMenu *m,const ScUiMenuItem *items,ScU32 count)
{
    ScArray *candidate=0,*old;
    ScHash *ids=0;
    ScU32 i,next,mark=1;
    ScStatus s=ui_object_enter((UiObject *)m,UI_MENU);
    if(s<0)return s;
    if((count && !items) || count>UINT_MAX/sizeof(*items)){s=SC_UI_E_INVALID;
        goto done;
        }
    if(m->revision==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    next=m->next_id;
    s=ui_array_new(sizeof(UiMenuEntry *),&candidate);
    if(s==0)s=ui_hash_new(sizeof(ScU32),&ids);
    if(s<0)goto done;
    s=ScArrayReserve(candidate,count);
    if(s<0)goto done;
    for(i=0;i<count;i++){UiMenuEntry *e=0;
        s=ui_menu_entry_new(m,&items[i],&next,&e);
        if(s<0)goto done;
        s=ScHashSet(ids,&e->id,sizeof(e->id),&mark,SC_HASH_INSERT_ONLY);
        if(s==0)s=ScArrayInsert(candidate,i,&e,1);
        if(s<0){ui_menu_entry_free(e,1);
            goto done;
            }}
    old=m->items;
    m->items=candidate;
    candidate=old;
    m->next_id=next;
    m->revision++;
    ui_menu_invalidate(m);
    {ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(candidate,&info);
        for(i=0;i<info.length;i++){UiMenuEntry *e=0;
            ScArrayGet(candidate,i,&e);
            ui_menu_entry_free(e,ui_quiet);
            }
        ScArrayClear(candidate);
        }
    s=SC_UI_OK;
done:
    if(candidate){ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(candidate,&info);
        for(i=0;i<info.length;i++){UiMenuEntry *e=0;
            ScArrayGet(candidate,i,&e);
            ui_menu_entry_free(e,1);
            }}
    ScArrayRelease(candidate);
    ScHashRelease(ids);
    s=ui_cause(0,s,UI_OP_MENU,0);
    ui_object_leave(&m->object);
    return s;
}
ScStatus ScUiMenuInsert(ScUiMenu *m,ScU32 before,const ScUiMenuItem *d,ScU32 *out)
{
    UiMenuEntry *e=0;
    ScU32 index,next;
    ScStatus s=ui_object_enter((UiObject *)m,UI_MENU);
    if(s<0)return s;
    if(!out){s=SC_UI_E_INVALID;
        goto done;
        }
    if(m->revision==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    index=ui_menu_count(m);
    if(before && !ui_menu_find(m,before,&index)){s=SC_UI_E_NOT_FOUND;
        goto done;
        }
    next=m->next_id;
    s=ui_menu_entry_new(m,d,&next,&e);
    if(s<0)goto done;
    if(ui_menu_find(m,e->id,0)){s=SC_UI_E_EXISTS;
        goto done;
        }
    s=ScArrayInsert(m->items,index,&e,1);
    if(s==0){m->revision++;
        m->next_id=next;
        *out=e->id;
        e=0;
        ui_menu_invalidate(m);
        }
done:if(e)ui_menu_entry_free(e,1);
    s=ui_cause(0,s,UI_OP_MENU,0);
    ui_object_leave(&m->object);
    return s;
}
ScStatus ScUiMenuRemove(ScUiMenu *m,ScU32 id)
{
    UiMenuEntry *e;
    ScU32 index;
    ScStatus s=ui_object_enter((UiObject *)m,UI_MENU);
    if(s<0)return s;
    e=ui_menu_find(m,id,&index);
    if(!e)s=SC_UI_E_NOT_FOUND;
    else if(m->revision==UINT_MAX)s=SC_UI_E_RANGE;
    else{s=ScArrayErase(m->items,index,1);
        if(s==0){m->revision++;
            ui_menu_invalidate(m);
            ui_menu_entry_free(e,ui_quiet);
            }}
    s=ui_cause(0,s,UI_OP_MENU,0);
    ui_object_leave(&m->object);
    return s;
}
ScStatus ScUiMenuSetState(ScUiMenu *m,ScU32 id,ScU32 mask,ScU32 state)
{
    UiMenuEntry *e;
    ScStatus s=ui_object_enter((UiObject *)m,UI_MENU);
    if(s<0)return s;
    e=ui_menu_find(m,id,0);
    if(!e)s=SC_UI_E_NOT_FOUND;
    else if(mask&~31u || state&~mask)s=SC_UI_E_INVALID;
    else if(m->revision==UINT_MAX)s=SC_UI_E_RANGE;
    else{e->description.flags=(e->description.flags&~mask)|state;
        m->revision++;
        ui_menu_invalidate(m);
        s=SC_UI_OK;
        }
    s=ui_cause(0,s,UI_OP_MENU,0);
    ui_object_leave(&m->object);
    return s;
}
ScStatus ScUiMenuSetText(ScUiMenu *m,ScU32 id,const ScUiText *text)
{
    UiMenuEntry *e;
    ScString *label=0,*caption=0;
    ScU32 mnemonic=0;
    ScStatus s=ui_object_enter((UiObject *)m,UI_MENU);
    if(s<0)return s;
    e=ui_menu_find(m,id,0);
    if(!e){s=SC_UI_E_NOT_FOUND;
        goto done;
        }
    if(m->revision==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    s=ui_text_copy(text,&label);
    if(s==0)s=ui_menu_caption(label,&caption,&mnemonic);
    if(s==0){ScStringRelease(e->label);
        ScStringRelease(e->caption);
        e->label=label;
        e->caption=caption;
        e->mnemonic=mnemonic;
        label=caption=0;
        m->revision++;
        ui_menu_invalidate(m);
        }
done:ScStringRelease(label);
    ScStringRelease(caption);
    s=ui_cause(0,s,UI_OP_MENU,0);
    ui_object_leave(&m->object);
    return s;
}
static ScStatus ui_menu_bind(ScUiContext *c,ScUiNode id,ScUiMenu *menu,ScU32 kind)
{
    UiNode *n=ui_node(c,id);
    ScUiMenu *old;
    ScStatus s;
    if(!n || n->kind!=kind)return SC_UI_E_INVALID;
    if(menu){s=ScUiMenuRetain(menu);
        if(s<0)return s;
        }
    ui_node_hold(n);
    old=n->menu;
    n->menu=menu;
    n->menu_hot=n->menu_pressed=UINT_MAX;
    if(kind==SC_UI_TOOLBAR)n->flags|=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScUiMenuRelease(old);
    if(n->private_flags&UI_NODE_RETIRED)s=SC_UI_E_CANCELLED;
    else{ui_node_layout_dirty(n);
        s=SC_UI_OK;
        }
    ui_node_leave(n);
    return s;
}
ScStatus ScUiMenuBind(ScUiContext *c,ScUiNode id,ScUiMenu *menu){return ui_menu_bind(c,id,menu,SC_UI_MENUBAR);
    }
ScStatus ScUiToolbarBind(ScUiContext *c,ScUiNode id,ScUiMenu *menu){return ui_menu_bind(c,id,menu,SC_UI_TOOLBAR);
    }
static UiNode *ui_native_owner(UiNode *root)
{
    ScU32 guard=0;
    ScUiContext *c=root->owner;
    while(root->window && root->window->popup && root->window->owner){UiNode *owner=ui_node(c,root->window->owner);
        if(!owner || ++guard>c->live_roots)break;
        root=owner;
        }
    return root;
}
static ScStatus ui_popup_root(UiNode *owner,ScU32 kind,const ScUiRect *anchor,ScU32 width,ScU32 height,ScU32 flags,UiNode **out)
{
    ScUiContext *c=owner->owner;
    UiNode *base=ui_root(c,owner->id),*n=0;
    ScPopupDesc d;
    ScWindow window;
    ScStatus s;
    if(!base || !base->window || !out || !anchor)return SC_UI_E_INVALID;
    s=ui_node_new(c,kind==SC32_POPUP_MENU?UI_KIND_POPUP:SC_UI_PANEL,0,&n);
    if(s<0)return s;
    ui_node_hold(n);
    n->window=(UiRoot *)calloc(1,sizeof(*n->window));
    if(!n->window){s=SC_UI_E_MEMORY;
        goto failed;
        }
    n->window->id=n->id;
    n->window->owner=base->id;
    n->window->popup=kind;
    n->window->popup_flags=flags;
    n->private_flags|=UI_ROOT_CREATING;
    n->root_next=c->roots;
    if(c->roots)c->roots->root_previous=n;
    c->roots=n;
    c->live_roots++;
    s=ui_array_new(sizeof(ScUiRect),&n->window->damage);
    if(s<0)goto failed;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner_window=base->window->window;
    d.owner_generation=base->window->information.generation;
    if(anchor->width<0 || anchor->height<0 || !ui_signed_add(owner->rectangle.x,anchor->x,&d.anchor_x) || !ui_signed_add(owner->rectangle.y,anchor->y,&d.anchor_y)){s=SC_UI_E_RANGE;
        goto failed;
        }
    d.anchor_width=(ScU32)anchor->width;
    d.anchor_height=(ScU32)anchor->height;
    d.preferred_width=width;
    d.preferred_height=height;
    d.kind=kind;
    d.flags=flags;
    d.procedure=ui_native_proc;
    d.context=n;
    c->dispatching++;
    s=ScPopupCreate(&d,&window);
    c->dispatching--;
    if(s<0 || (n->private_flags&UI_NODE_RETIRED)){s=s<0?ui_cause(c,s,UI_OP_MENU,owner->id):SC_UI_E_CANCELLED;
        goto failed;
        }
    n->private_flags&=~UI_ROOT_CREATING;
    n->window->window=window;
    *out=n;
    ui_node_leave(n);
    return SC_UI_OK;
failed:if(!(n->private_flags&UI_NODE_RETIRED))ui_node_retire(n);
    ui_node_leave(n);
    return s;
}
static void ui_client_origin(const ScWindowInfo *w,ScI32 *x,ScI32 *y)
{ScI32 border=(w->width-(ScI32)w->client_width)/2;
    *x=w->x+border;
    *y=w->y+w->height-(ScI32)w->client_height-border;
    }
static UiNode *ui_popup_target(UiNode *owner,ScI32 *x,ScI32 *y,int keyboard)
{
    ScUiContext *c=owner->owner;
    UiNode *r;
    ScI32 ox,oy;
    ui_client_origin(&owner->window->information,&ox,&oy);
    for(r=c->roots;r;r=r->root_next)if(r->window && r->window->popup && r->window->visible
        && !(r->window->popup_flags&SC32_POPUP_HIT_TRANSPARENT) && ui_native_owner(r)==ui_native_owner(owner)){
        if(keyboard){UiNode *focus=ui_node(c,r->window->focus);
            return focus?focus:r;
            }
        else{ScI32 rx,ry,local_x,local_y;
            ScUiNode hit;
            ui_client_origin(&r->window->information,&rx,&ry);
            local_x=ox+*x-rx;
            local_y=oy+*y-ry;
            if(local_x>=0 && local_y>=0 && (ScU32)local_x<r->window->information.client_width && (ScU32)local_y<r->window->information.client_height){
                *x=local_x;
                *y=local_y;
                if(ScUiHitTest(c,r->id,local_x,local_y,&hit)==0)return ui_node(c,hit);
                return r;
                }}
    }
    return 0;
}
static void ui_popup_node_destroy(UiNode *n)
{
    ScUiContext *c=n->owner;
    UiPopup *p=c->popups;
    while(p){if(p->node==n->id){if(p->previous)p->previous->next=p->next;
            else c->popups=p->next;
            if(p->next)p->next->previous=p->previous;
            ScUiMenuRelease(p->menu);
            free(p);
            break;
            }
        p=p->next;
        }
    ScUiMenuRelease(n->menu);
    n->menu=0;
}
static void ui_menu_close_owner(ScUiContext *c,ScU32 owner)
{
    UiPopup *p;
    ScU32 watermark=c->next_token-1;
    int closed=0;
    c->walking++;
    /* Destroy会进用户代码，重新从活链寻找；不能借下一popup跨回调。 */
    for(;;){for(p=c->popups;p;p=p->next)if(p->owner==owner && !p->retired && p->token<=watermark)break;
        if(!p)break;
        p->retired=1;
        closed=1;
        ScUiNodeDestroy(c,p->node);
        }
    if(closed){UiNode *root=ui_node(c,owner);
        if(root && root->window && root->window->capture==owner){UiNode *native=ui_native_owner(root);
            ScPointerCapture(native->window->window,0);
            root->window->capture=0;
            if(native->window->capture==owner)native->window->capture=0;
            }}
    c->walking--;
    ui_nodes_collect(c);
}
static int ui_menu_pointer(UiNode *root,ScU32 type,ScI32 x,ScI32 y,UiNode **out)
{
    UiPopup *p;
    ScUiNode hit;
    ScUiContext *c=root->owner;
    for(p=c->popups;p;p=p->next)if(!p->retired && p->owner==root->id)break;
    if(!p)return 0;
    if(ScUiHitTest(c,root->id,x,y,&hit)==0){UiNode *n=ui_node(c,hit);
        if(n && n->kind==SC_UI_MENUBAR && n->menu){*out=n;
            return 0;
            }}
    if(type==SC32_MSG_BUTTONDOWN)ui_menu_close_owner(c,root->id);
    return 1;
}
ScStatus ScUiMenuClose(ScUiContext *c,ScU32 token)
{
    UiPopup *p,*current;
    UiNode *node;
    ScU32 owner_id;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    for(p=c->popups;p && p->token!=token;p=p->next){}
    if(!p || p->retired){s=SC_UI_E_STALE;
        goto done;
        }
    node=ui_node(c,p->node);
    if(!node){s=SC_UI_E_STALE;
        goto done;
        }
    owner_id=p->owner;
    ui_node_hold(node);
    c->walking++;
    current=p;
    p->retired=1;
    if(ui_event(node,SC_UI_EVENT_CLOSING,0,0)<0)c->notification_error=SC_UI_E_FAILED;
    /* 只沿真实popup chain叶先关闭，不让多级菜单吃掉用户栈。 */
    while(current->child_popup){UiPopup *child;
        for(child=c->popups;child && child->token!=current->child_popup;child=child->next){}
        if(!child || child->retired){current->child_popup=0;
            break;
            }
        current=child;
        }
    for(;;){ScU32 parent_token=current->parent_popup,is_target=current==p;
        current->retired=1;
        s=ScUiNodeDestroy(c,current->node);
        if(s<0 && s!=SC_UI_E_STALE)break;
        if(is_target){s=SC_UI_OK;
            break;
            }
        for(current=c->popups;current && current->token!=parent_token;current=current->next){}
        if(!current){s=SC_UI_OK;
            break;
            }
        current->child_popup=0;
    }
    if(p->parent_popup){UiPopup *parent;
        for(parent=c->popups;parent;parent=parent->next)if(parent->token==p->parent_popup){parent->child_popup=0;
            break;
            }}
    {UiPopup *left;
        int active=0;
        for(left=c->popups;left;left=left->next)if(!left->retired && left->owner==owner_id){active=1;
            break;
            }
        if(!active){UiNode *owner=ui_node(c,owner_id);
            if(owner && owner->window && owner->window->capture==owner_id){UiNode *native=ui_native_owner(owner);
                ScPointerCapture(native->window->window,0);
                owner->window->capture=0;
                if(native->window->capture==owner_id)native->window->capture=0;
                }}}
    c->walking--;
    ui_node_leave(node);
done:s=ui_cause(c,s,UI_OP_MENU,0);
    ui_object_leave(&c->object);
    return s;
}
static void ui_shortcut_text(const ScUiShortcut *shortcut,char *buffer,ScU32 capacity)
{
    const char *control=(shortcut->modifiers&SC32_MOD_CONTROL)?"Ctrl+":"",*alt=(shortcut->modifiers&SC32_MOD_ALT)?"Alt+":"",*shift=(shortcut->modifiers&SC32_MOD_SHIFT)?"Shift+":"";
    if(!shortcut->key){if(capacity)buffer[0]=0;
        return;
        }
    if(shortcut->key>=SC32_KEY_F1 && shortcut->key<SC32_KEY_F1+12)snprintf(buffer,capacity,"%s%s%sF%u",control,alt,shift,shortcut->key-SC32_KEY_F1+1);
    else snprintf(buffer,capacity,"%s%s%s%c",control,alt,shift,(int)(shortcut->key>='a' && shortcut->key<='z'?shortcut->key-32:shortcut->key));
}
static ScStatus ui_menu_query_commands(ScUiContext *c,ScUiMenu *menu)
{
    ScArray *stack=0,*commands=0;
    ScHash *seen=0,*ids=0;
    ScU32 depth=0,count=0,mark=1,i;
    ScStatus s;
    s=ui_array_new(sizeof(ScUiMenu *),&stack);
    if(s==0)s=ui_array_new(sizeof(ScU32),&commands);
    if(s==0)s=ui_hash_new(4,&seen);
    if(s==0)s=ui_hash_new(4,&ids);
    if(s<0)goto done;
    s=ScArrayInsert(stack,depth++,&menu,1);
    if(s<0)goto done;
    /* 先收唯一命令值，再进应用Query；菜单在回调中重建不使路径悬空。 */
    while(depth){ScUiMenu *current;
        ScHashEntry found;
        ScArrayGet(stack,--depth,&current);
        ScArrayErase(stack,depth,1);
        memset(&found,0,sizeof(found));
        found.size=sizeof(found);
        found.version=1;
        if(ScHashGet(seen,&current,sizeof(current),&found)==0)continue;
        s=ScHashSet(seen,&current,sizeof(current),&mark,0);
        if(s<0)goto done;
        for(i=0;i<ui_menu_count(current);i++){UiMenuEntry *entry=ui_menu_at(current,i);
            ScU32 command=entry->description.command;
            if(command && ScHashGet(ids,&command,4,&found)!=0){s=ScHashSet(ids,&command,4,&mark,0);
                if(s==0)s=ScArrayInsert(commands,count++,&command,1);
                if(s<0)goto done;
                }
            if(entry->description.submenu){ScUiMenu *child=entry->description.submenu;
                s=ScArrayInsert(stack,depth++,&child,1);
                if(s<0)goto done;
                }}}
    for(i=0;i<count;i++){ScU32 command;
        ScUiCommandDesc state;
        ScArrayGet(commands,i,&command);
        memset(&state,0,sizeof(state));
        state.size=sizeof(state);
        state.version=1;
        s=ScUiCommandQuery(c,command,&state);
        if(s<0 && s!=SC_UI_E_NOT_FOUND)goto done;
        if(c->object.closing){s=SC_UI_E_CANCELLED;
            goto done;
            }}
    s=SC_UI_OK;
done:ScHashRelease(ids);
    ScHashRelease(seen);
    ScArrayRelease(commands);
    ScArrayRelease(stack);
    return s;
}
ScStatus ScUiMenuOpen(ScUiContext *c,ScUiNode owner_id,ScUiMenu *menu,ScI32 x,ScI32 y,ScU32 flags,ScU32 *out)
{
    UiNode *owner=ui_root(c,owner_id),*node;
    UiPopup *p=0;
    ScUiRect anchor;
    ScStatus s;
    ScU32 i,count,width=80,height;
    ScUiMetrics metrics;
    if(!owner || !out || flags || !menu || menu->object.closing)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    ui_node_hold(owner);
    s=ScUiMenuRetain(menu);
    if(s<0)goto done;
    p=(UiPopup *)calloc(1,sizeof(*p));
    if(!p){ScUiMenuRelease(menu);
        s=SC_UI_E_MEMORY;
        goto done;
        }
    memset(&metrics,0,sizeof(metrics));
    metrics.size=sizeof(metrics);
    metrics.version=1;
    ScUiMetricsQuery(c,owner->id,&metrics);
    p->context=c;
    p->owner=owner->id;
    p->menu=menu;
    p->hot=UINT_MAX;
    p->row_height=(ScI32)metrics.row_height;
    if(c->next_token==UINT_MAX){s=SC_UI_E_RANGE;
        goto failed;
        }
    p->token=c->next_token++;
    count=ui_menu_count(menu);
    if(!count){s=SC_UI_E_INVALID;
        goto failed;
        }
    s=ui_event(owner,SC_UI_EVENT_OPENING,menu,0);
    if(s<0 || s==SC_UI_VETO || (owner->private_flags&UI_NODE_RETIRED)){if(s>=0)s=SC_UI_E_CANCELLED;
        goto failed;
        }
    s=ui_menu_query_commands(c,menu);
    if(s<0 || (owner->private_flags&UI_NODE_RETIRED)){if(s>=0)s=SC_UI_E_CANCELLED;
        goto failed;
        }
    count=ui_menu_count(menu);
    if(!count){s=SC_UI_E_INVALID;
        goto failed;
        }
    for(i=0;i<count;i++){UiMenuEntry *item=ui_menu_at(menu,i);
        ScUiText text;
        ScUiTextMetrics m;
        char shortcut[48];
        ScU32 need;
        ui_text_view(item->caption,&text);
        s=ui_text_measure(c,SC_UI_FONT_MENU,&text,SC_UI_AUTO,0,&m);
        if(s<0)goto failed;
        if(m.width>(ScU32)INT_MAX){s=SC_UI_E_RANGE;
            goto failed;
            }
        if((ScI32)m.width>p->caption_width)p->caption_width=(ScI32)m.width;
        ui_shortcut_text(&item->description.shortcut,shortcut,sizeof(shortcut));
        text.data=shortcut;
        text.bytes=strlen(shortcut);
        s=ui_text_measure(c,SC_UI_FONT_MENU,&text,SC_UI_AUTO,0,&m);
        if(s<0)goto failed;
        if(m.width>(ScU32)INT_MAX){s=SC_UI_E_RANGE;
            goto failed;
            }
        if((ScI32)m.width>p->shortcut_width)p->shortcut_width=(ScI32)m.width;
        if(!ui_add((ScU32)p->caption_width,(ScU32)p->shortcut_width,&need) || !ui_add(need,(ScU32)ui_scale(c,68),&need)){s=SC_UI_E_RANGE;
            goto failed;
            }
        if(need>width)width=need;
        }
    if(!ui_multiply(count,(ScU32)p->row_height,&height) || height>UINT_MAX-8){s=SC_UI_E_RANGE;
        goto failed;
        }
    height+=8;
    if(width>c->display.width)width=c->display.width;
    {ScU32 available=c->display.height>c->theme.taskbar_height?c->display.height-c->theme.taskbar_height:1;
        if(height>available)height=available;
        }
    anchor.x=x;
    anchor.y=y;
    anchor.width=1;
    anchor.height=1;
    s=ui_popup_root(owner,SC32_POPUP_MENU,&anchor,width,height,SC32_POPUP_NO_ACTIVATE,&node);
    if(s<0)goto failed;
    p->node=node->id;
    p->next=c->popups;
    if(c->popups)c->popups->previous=p;
    c->popups=p;
    count=ui_menu_count(menu);
    for(i=0;i<count;i++)if(!(ui_menu_flags(c,ui_menu_at(menu,i))&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR))){p->hot=i;
        break;
        }
    ui_node_damage(node);
    s=ScUiCapture(c,owner->id,1);
    if(s<0){ScUiMenuClose(c,p->token);
        goto done;
        }
    *out=p->token;
    s=SC_UI_OK;
    goto done;
failed:ScUiMenuRelease(p->menu);
    free(p);
done:s=ui_cause(c,s,UI_OP_MENU,0);
    ui_node_leave(owner);
    ui_object_leave(&c->object);
    return s;
}
static UiPopup *ui_popup(UiNode *node)
{UiPopup *p;
    for(p=node->owner->popups;p;p=p->next)if(p->node==node->id)return p;
    return 0;
    }
static ScU32 ui_menu_flags(ScUiContext *c,UiMenuEntry *item)
{
    ScUiCommandDesc d;
    ScU32 flags=item->description.flags;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    if(item->description.command && ui_command_state(c,item->description.command,&d)==0)flags=(flags&~(SC_UI_MENU_DISABLED|SC_UI_MENU_CHECKED))|(d.flags&(SC_UI_MENU_DISABLED|SC_UI_MENU_CHECKED));
    return flags;
}
static ScStatus ui_menu_item_width(UiNode *n,UiMenuEntry *entry,ScI32 *out)
{
    ScUiText text;
    ScUiTextMetrics m;
    ScU32 width,pad=(ScU32)ui_scale(n->owner,16);
    ScStatus s;
    if(n->kind==SC_UI_TOOLBAR && (entry->description.flags&SC_UI_MENU_SEPARATOR)){*out=ui_scale(n->owner,10);
        return SC_UI_OK;
        }
    ui_text_view(entry->caption,&text);
    s=ui_text_measure(n->owner,SC_UI_FONT_MENU,&text,SC_UI_AUTO,0,&m);
    if(s<0)return s;
    if(n->kind==SC_UI_TOOLBAR && entry->description.icon){if(!ui_add(pad,(ScU32)ui_scale(n->owner,22),&pad))return SC_UI_E_RANGE;
        }
    if(!ui_add(m.width,pad,&width) || width>(ScU32)INT_MAX)return SC_UI_E_RANGE;
    *out=(ScI32)width;
    return SC_UI_OK;
}
static ScStatus ui_menu_measure(UiNode *n,ScUiSize *out)
{
    ScU32 i,width=0;
    ScUiMetrics m;
    ScStatus s;
    memset(&m,0,sizeof(m));
    m.size=sizeof(m);
    m.version=1;
    s=ScUiMetricsQuery(n->owner,n->root,&m);
    if(s<0)return s;
    out->height=(ScI32)(n->kind==SC_UI_TOOLBAR?m.toolbar_height:m.menu_height);
    out->width=0;
    if(n->window){out->width=(ScI32)n->window->width;
        out->height=(ScI32)n->window->height;
        return SC_UI_OK;
        }
    if(!n->menu)return SC_UI_OK;
    for(i=0;i<ui_menu_count(n->menu);i++){ScI32 item_width;
        s=ui_menu_item_width(n,ui_menu_at(n->menu,i),&item_width);
        if(s<0)return s;
        if(!ui_add(width,(ScU32)item_width,&width) || width>(ScU32)INT_MAX)return SC_UI_E_RANGE;
        }
    out->width=(ScI32)width;
    return SC_UI_OK;
}
static ScStatus ui_menu_paint(UiNode *n,ScUiPainter *paint)
{
    ScUiContext *c=n->owner;
    ScUiMenu *m=n->menu;
    UiPopup *popup=ui_popup(n);
    ScU32 i,first=0,count;
    ScI32 x=0,row_height;
    ScUiRect whole;
    if(popup){m=popup->menu;
        first=popup->first_row;
        row_height=popup->row_height;
        }
    else row_height=n->rectangle.height;
    whole.x=0;
    whole.y=0;
    whole.width=n->rectangle.width;
    whole.height=n->rectangle.height;
    ui_fill(paint,&whole,SC32_COLOR_FACE);
    if(popup)ui_border(paint,&whole,0);
    if(!m)return SC_UI_OK;
    m->object.depth++;
    count=ui_menu_count(m);
    for(i=first;i<count;i++){UiMenuEntry *item=ui_menu_at(m,i);
        ScUiRect row,text_rect;
        ScUiText label;
        ScUiTextMetrics measure;
        ScU32 state=ui_menu_flags(c,item);
        ScStatus s;
        row.x=popup?2:x;
        row.y=popup?2+(ScI32)(i-first)*row_height:0;
        row.height=row_height;
        row.width=popup?whole.width-4:0;
        if(row.y>=whole.height)break;
        ui_text_view(item->caption,&label);
        s=ui_text_measure(c,SC_UI_FONT_MENU,&label,SC_UI_AUTO,0,&measure);
        if(s<0){s=ui_cause(0,s,UI_OP_MENU,0);
            ui_object_leave(&m->object);
            return s;
            }
        if(!popup){s=ui_menu_item_width(n,item,&row.width);
            if(s<0){ui_object_leave(&m->object);
                return s;
                }}
        if(!(n->effective&SC_UI_ENABLED))state|=SC_UI_MENU_DISABLED;
        if(state&SC_UI_MENU_SEPARATOR){ScUiRect line=row;
            line.y+=row.height/2;
            line.height=1;
            line.x+=ui_scale(c,4);
            line.width-=ui_scale(c,8);
            ui_fill(paint,&line,SC32_COLOR_LINE);
            }
        else{
            int hot=(popup?popup->hot:n->menu_hot)==i;
            if(n->kind==SC_UI_TOOLBAR){int pressed=(n->menu_pressed==item->id && hot) || (state&SC_UI_MENU_CHECKED);
                if(hot || pressed)ui_border(paint,&row,pressed);
                }
            else if(hot)ui_fill(paint,&row,SC32_COLOR_SELECT);
            paint->text_role=(state&SC_UI_MENU_DISABLED)?SC32_COLOR_MUTED:hot && n->kind!=SC_UI_TOOLBAR?SC32_COLOR_SELECT_TEXT:SC32_COLOR_TEXT;
            text_rect=row;
            text_rect.x+=ui_scale(c,popup?28:8);
            text_rect.y+=(row.height-(ScI32)measure.height)/2;
            text_rect.width-=ui_scale(c,popup?56:16);
            if(n->kind==SC_UI_TOOLBAR && item->description.icon){ScUiRect icon;
                icon.x=row.x+ui_scale(c,5);
                icon.y=row.y+(row.height-ui_scale(c,16))/2;
                icon.width=icon.height=ui_scale(c,16);
                s=ScUiPainterIcon(paint,&icon,item->description.icon,(state&SC_UI_MENU_DISABLED)?0:SC_UI_ENABLED);
                if(s<0){ui_object_leave(&m->object);
                    return s;
                    }
                text_rect.x+=ui_scale(c,22);
                text_rect.width-=ui_scale(c,22);
                }
            s=ScUiPainterText(paint,&text_rect,&label,SC_UI_FONT_MENU,UI_TEXT_ELLIPSIS);
            if(s<0){s=ui_cause(0,s,UI_OP_MENU,0);
                ui_object_leave(&m->object);
                return s;
                }
            if(popup){char shortcut[48];
                ScUiText st;
                ScUiRect sr=row;
                ui_shortcut_text(&item->description.shortcut,shortcut,sizeof(shortcut));
                st.size=sizeof(st);
                st.version=1;
                st.data=shortcut;
                st.bytes=strlen(shortcut);
                sr.x=whole.width-popup->shortcut_width-ui_scale(c,24);
                sr.width=popup->shortcut_width;
                sr.y=text_rect.y;
                s=ScUiPainterText(paint,&sr,&st,SC_UI_FONT_MENU,0);
                if(s<0){s=ui_cause(0,s,UI_OP_MENU,0);
                    ui_object_leave(&m->object);
                    return s;
                    }
                if(item->description.icon){ScUiRect icon;
                    icon.x=ui_scale(c,5);
                    icon.y=row.y+(row.height-ui_scale(c,16))/2;
                    icon.width=icon.height=ui_scale(c,16);
                    ScUiPainterIcon(paint,&icon,item->description.icon,(state&SC_UI_MENU_DISABLED)?0:SC_UI_ENABLED);
                    }
                if(state&SC_UI_MENU_CHECKED){ScUiPainterLine(paint,ui_scale(c,8),row.y+row.height/2,ui_scale(c,11),row.y+row.height/2+ui_scale(c,3),paint->text_role);
                    ScUiPainterLine(paint,ui_scale(c,11),row.y+row.height/2+ui_scale(c,3),ui_scale(c,18),row.y+row.height/2-ui_scale(c,4),paint->text_role);
                    }
                if(item->description.submenu){ScI32 ax=whole.width-ui_scale(c,12),ay=row.y+row.height/2,d;
                    for(d=0;d<ui_scale(c,4);d++)ScUiPainterLine(paint,ax+d,ay-d,ax+d,ay+d,paint->text_role);
                    }
            }
        }
        if(!ui_signed_add(x,row.width,&x)){ui_object_leave(&m->object);
            return SC_UI_E_RANGE;
            }
    }
    ui_object_leave(&m->object);
    return SC_UI_OK;
}
static ScStatus ui_menu_activate(UiNode *n,UiMenuEntry *entry,ScU32 row)
{
    ScUiContext *c=n->owner;
    UiPopup *p=ui_popup(n);
    ScU32 token,entry_id=entry->id;
    ScUiNode owner=p?p->owner:n->root;
    if(ui_menu_flags(c,entry)&(SC_UI_MENU_SEPARATOR|SC_UI_MENU_DISABLED))return SC_UI_HANDLED;
    if(entry->description.submenu){ScUiMenu *submenu=entry->description.submenu;
        ScI32 x=n->rectangle.x,y;
        ScStatus s=ScUiMenuRetain(submenu);
        if(s<0)return s;
        if(!ui_signed_add(n->rectangle.y,n->rectangle.height,&y)){ScUiMenuRelease(submenu);
            return SC_UI_E_RANGE;
            }
        if(p){ScI32 ox,oy,px,py;
            UiNode *base=ui_node(c,p->owner);
            ui_client_origin(&base->window->information,&ox,&oy);
            ui_client_origin(&n->window->information,&px,&py);
            x=px-ox+n->rectangle.width-2;
            y=py-oy+2+(ScI32)(row-p->first_row)*p->row_height;
            }
        else{ScU32 i;
            for(i=0;i<row;i++){ScI32 width;
                s=ui_menu_item_width(n,ui_menu_at(n->menu,i),&width);
                if(s<0 || !ui_signed_add(x,width,&x)){ScUiMenuRelease(submenu);
                    return s<0?s:SC_UI_E_RANGE;
                    }}}
        if(p && p->child_popup)ScUiMenuClose(c,p->child_popup);
        else if(!p)ui_menu_close_owner(c,owner);
        if(n->private_flags&UI_NODE_RETIRED)s=SC_UI_E_CANCELLED;
        else s=ScUiMenuOpen(c,owner,submenu,x,y,0,&token);
        if(s==0){UiPopup *child;
            for(child=c->popups;child;child=child->next)if(child->token==token){
                if(p){child->parent_popup=p->token;
                    child->source=p->source;
                    child->source_item=p->source_item;
                    p->child_popup=token;
                    }
                else if(n->kind==SC_UI_MENUBAR){child->source=n->id;
                    child->source_item=entry_id;
                    n->menu_hot=row;
                    }
                break;
                }}
        ScUiMenuRelease(submenu);
        return s<0?s:SC_UI_HANDLED;
        }
    {ScU32 command=entry->description.command;
        ScUiNode source=p?owner:n->id;
        ui_menu_close_owner(c,owner);
        return ScUiCommand(c,source,command);
        }
}
static ScStatus ui_menu_switch(UiNode *n,UiPopup *p,ScI32 direction)
{
    UiNode *bar=ui_node(n->owner,p->source);
    ScU32 index,count,i;
    UiMenuEntry *entry;
    if(!bar || !bar->menu)return SC_UI_HANDLED;
    count=ui_menu_count(bar->menu);
    entry=ui_menu_find(bar->menu,p->source_item,&index);
    if(!entry || !count)return SC_UI_HANDLED;
    for(i=0;i<count;i++){index=direction>0?(index+1)%count:index?index-1:count-1;
        entry=ui_menu_at(bar->menu,index);
        if(!(ui_menu_flags(bar->owner,entry)&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR)))return ui_menu_activate(bar,entry,index);
        }
    return SC_UI_HANDLED;
}
static ScStatus ui_menu_event(UiNode *n,const ScUiEvent *e)
{
    UiPopup *p=ui_popup(n);
    ScUiMenu *m=p?p->menu:n->menu;
    ScUiContext *c=n->owner;
    ScU32 count,index=UINT_MAX,i;
    if(!m || e->type==SC_UI_EVENT_CREATE || e->type==SC_UI_EVENT_DESTROY)return SC_UI_OK;
    count=ui_menu_count(m);
    if(!count)return SC_UI_OK;
    if(e->type==SC_UI_EVENT_POINTER_MOVE || e->type==SC_UI_EVENT_POINTER_DOWN || e->type==SC_UI_EVENT_POINTER_UP){
        ScU32 left=!e->message || (e->message->data[3]&1u);
        if(p && e->x>=0 && e->x<n->rectangle.width && e->y>=2 && e->y<n->rectangle.height)index=p->first_row+(ScU32)(e->y-2)/(ScU32)p->row_height;
        else if(!p && e->y>=0 && e->y<n->rectangle.height){ScI32 x=0;
            for(i=0;i<count;i++){ScI32 width,end;
                ScStatus s=ui_menu_item_width(n,ui_menu_at(m,i),&width);
                if(s<0)return s;
                if(!ui_signed_add(x,width,&end))return SC_UI_E_RANGE;
                if(e->x>=x && e->x<end){index=i;
                    break;
                    }
                x=end;
                }}
        if(index>=count)index=UINT_MAX;
        if(p){if(p->hot!=index){p->hot=index;
                ui_node_damage(n);
                if(p->hover_timer){ScUiTimerCancel(c,p->hover_timer);
                    p->hover_timer=0;
                    }
                if(p->child_popup)ScUiMenuClose(c,p->child_popup);
                if(n->private_flags&UI_NODE_RETIRED)return SC_UI_E_CANCELLED;
                count=ui_menu_count(m);
                if(index>=count)index=UINT_MAX;
                if(index<count && ui_menu_at(m,index)->description.submenu){ScUiTimerDesc timer;
                    memset(&timer,0,sizeof(timer));
                    timer.size=sizeof(timer);
                    timer.version=1;
                    timer.node=n->id;
                    timer.flags=SC_UI_TIMER_DRAW_ONLY;
                    if(ui_now(&timer.deadline)==0){ui_wide_add(&timer.deadline,c->hz/5?c->hz/5:1);
                        ScUiTimerSet(c,&timer,&p->hover_timer);
                        }}}}
        else if(n->menu_hot!=index){n->menu_hot=index;
            ui_node_damage(n);
            }
        if(n->kind==SC_UI_TOOLBAR){
            if(e->type==SC_UI_EVENT_POINTER_DOWN && left && index!=UINT_MAX && !(ui_menu_flags(c,ui_menu_at(m,index))&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR))){ScStatus s=ScUiCapture(c,n->id,1);
                if(s<0)return s;
                n->menu_pressed=ui_menu_at(m,index)->id;
                n->state|=SC_UI_PRESSED;
                ui_tree_effective(n);
                ui_node_damage(n);
                return SC_UI_HANDLED;
                }
            if(e->type==SC_UI_EVENT_POINTER_UP && left && n->menu_pressed!=UINT_MAX){ScU32 pressed=n->menu_pressed;
                n->menu_pressed=UINT_MAX;
                n->state&=~SC_UI_PRESSED;
                ui_tree_effective(n);
                ScUiCapture(c,n->id,0);
                ui_node_damage(n);
                if(index!=UINT_MAX && ui_menu_at(m,index)->id==pressed){ScStatus s=ui_menu_activate(n,ui_menu_at(m,index),index);
                    return s<0?s:SC_UI_HANDLED;
                    }
                return SC_UI_HANDLED;
                }
        }
        else if(index!=UINT_MAX && left && ((p && e->type==SC_UI_EVENT_POINTER_UP) || (!p && e->type==SC_UI_EVENT_POINTER_DOWN))){ScStatus s=ui_menu_activate(n,ui_menu_at(m,index),index);
            return s<0?s:SC_UI_HANDLED;
            }
        if(!p && n->kind==SC_UI_MENUBAR && e->type==SC_UI_EVENT_POINTER_MOVE && index!=UINT_MAX){UiPopup *open;
            for(open=c->popups;open;open=open->next)if(!open->retired && open->source==n->id && !open->parent_popup)break;
            if(open && open->source_item!=ui_menu_at(m,index)->id)return ui_menu_activate(n,ui_menu_at(m,index),index);
            }
        return p?SC_UI_HANDLED:SC_UI_OK;
    }
    if(e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_FOCUS_OUT || e->type==SC_UI_EVENT_INPUT_SYNC){n->menu_pressed=UINT_MAX;
        n->menu_key=0;
        n->state&=~SC_UI_PRESSED;
        ui_tree_effective(n);
        ui_node_damage(n);
        return SC_UI_OK;
        }
    if(n->kind==SC_UI_TOOLBAR && e->type==SC_UI_EVENT_FOCUS_IN && n->menu_hot>=count){for(i=0;i<count;i++)if(!(ui_menu_flags(c,ui_menu_at(m,i))&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR))){n->menu_hot=i;
            ui_node_damage(n);
            break;
            }
        return SC_UI_OK;
        }
    if(e->type==SC_UI_EVENT_POINTER_LEAVE && !p){n->menu_hot=UINT_MAX;
        ui_node_damage(n);
        return SC_UI_OK;
        }
    if(p && e->type==SC_UI_EVENT_WHEEL && e->message){ScI32 delta=(ScI32)e->message->data[2];
        if(delta>0)p->first_row=p->first_row>(ScU32)delta?p->first_row-(ScU32)delta:0;
        else if(delta<0 && p->first_row<count-1){ScU32 amount=0u-(ScU32)delta;
            p->first_row=amount>count-1-p->first_row?count-1:p->first_row+amount;
            }
        ui_node_damage(n);
        return SC_UI_HANDLED;
        }
    if(p && e->type==SC_UI_EVENT_KEY_DOWN){
        if(e->key==27){ScUiMenuClose(c,p->token);
            return SC_UI_HANDLED;
            }
        if(e->key==SC32_KEY_LEFT && p->parent_popup){ScUiMenuClose(c,p->token);
            return SC_UI_HANDLED;
            }
        if(e->key==SC32_KEY_LEFT && p->source)return ui_menu_switch(n,p,-1);
        if(e->key==SC32_KEY_RIGHT && p->source && (p->hot>=count || !ui_menu_at(m,p->hot)->description.submenu))return ui_menu_switch(n,p,1);
        if(e->key=='\n' || e->key==SC32_KEY_RIGHT){if(p->hot<count && (e->key=='\n' || ui_menu_at(m,p->hot)->description.submenu)){ScStatus s=ui_menu_activate(n,ui_menu_at(m,p->hot),p->hot);
                return s<0?s:SC_UI_HANDLED;
                }
            return SC_UI_HANDLED;
            }
        if(e->key==SC32_KEY_UP || e->key==SC32_KEY_DOWN || e->key==SC32_KEY_HOME || e->key==SC32_KEY_END){ScI32 step=e->key==SC32_KEY_UP?-1:1;
            index=p->hot;
            if(e->key==SC32_KEY_HOME)index=count-1;
            if(e->key==SC32_KEY_END){index=0;
                step=-1;
                }
            for(i=0;i<count;i++){if(index==UINT_MAX)index=step>0?0:count-1;
                else index=step>0?(index+1)%count:index?index-1:count-1;
                if(!(ui_menu_flags(c,ui_menu_at(m,index))&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR)))break;
                }
            if(i==count)index=UINT_MAX;
            p->hot=index;
            if(index!=UINT_MAX){if(index<p->first_row)p->first_row=index;
                else{ScU32 rows=(ScU32)n->rectangle.height/(ScU32)p->row_height;
                    if(!rows)rows=1;
                    if(index-p->first_row>=rows)p->first_row=index-rows+1;
                    }}
            ui_node_damage(n);
            return SC_UI_HANDLED;
            }
        for(i=0;i<count;i++)if(ui_menu_at(m,i)->mnemonic==e->key){ScStatus s=ui_menu_activate(n,ui_menu_at(m,i),i);
            return s<0?s:SC_UI_HANDLED;
            }
    }
    if(p && e->type==SC_UI_EVENT_TIMER && e->command==p->hover_timer){p->hover_timer=0;
        if(p->hot<count){ScStatus s=ui_menu_activate(n,ui_menu_at(m,p->hot),p->hot);
            return s<0?s:SC_UI_HANDLED;
            }
        return SC_UI_HANDLED;
        }
    if(n->kind==SC_UI_TOOLBAR && e->type==SC_UI_EVENT_KEY_DOWN){
        if(e->key==SC32_KEY_LEFT || e->key==SC32_KEY_RIGHT){ScI32 direction=e->key==SC32_KEY_RIGHT?1:-1;
            index=n->menu_hot;
            for(i=0;i<count;i++){if(index==UINT_MAX)index=direction>0?0:count-1;
                else index=direction>0?(index+1)%count:index?index-1:count-1;
                if(!(ui_menu_flags(c,ui_menu_at(m,index))&(SC_UI_MENU_SEPARATOR|SC_UI_MENU_DISABLED)))break;
                }
            n->menu_hot=i==count?UINT_MAX:index;
            ui_node_damage(n);
            return SC_UI_HANDLED;
            }
        if((e->key==' ' || e->key=='\n') && !(e->modifiers&(SC32_MOD_ALT|SC32_MOD_CONTROL)) && n->menu_hot<count){n->menu_pressed=ui_menu_at(m,n->menu_hot)->id;
            n->menu_key=e->key;
            n->state|=SC_UI_PRESSED;
            ui_tree_effective(n);
            ui_node_damage(n);
            return SC_UI_HANDLED;
            }
    }
    if(n->kind==SC_UI_TOOLBAR && e->type==SC_UI_EVENT_KEY_UP && n->menu_key==e->key){ScU32 pressed=n->menu_pressed;
        n->menu_pressed=UINT_MAX;
        n->menu_key=0;
        n->state&=~SC_UI_PRESSED;
        ui_tree_effective(n);
        ui_node_damage(n);
        if(n->menu_hot<count && ui_menu_at(m,n->menu_hot)->id==pressed){ScStatus s=ui_menu_activate(n,ui_menu_at(m,n->menu_hot),n->menu_hot);
            return s<0?s:SC_UI_HANDLED;
            }
        return SC_UI_HANDLED;
        }
    return SC_UI_OK;
}
static ScStatus ui_menu_key(UiNode *root,ScU32 key,ScU32 modifiers)
{
    UiNode *n=root;
    while(n){if(n->kind==SC_UI_MENUBAR && n->menu && (n->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){ScU32 i,count=ui_menu_count(n->menu);
            for(i=0;i<count;i++)if((modifiers&SC32_MOD_ALT) && ui_menu_at(n->menu,i)->mnemonic==key)return ui_menu_activate(n,ui_menu_at(n->menu,i),i);
            if(key==SC32_KEY_F1+9)for(i=0;i<count;i++)if(!(ui_menu_flags(n->owner,ui_menu_at(n->menu,i))&(SC_UI_MENU_DISABLED|SC_UI_MENU_SEPARATOR)))return ui_menu_activate(n,ui_menu_at(n->menu,i),i);
            }
        if(n->first_child){n=n->first_child;
            continue;
            }
        while(n!=root && !n->next)n=n->parent;
        if(n==root)break;
        n=n->next;
        }
    return SC_UI_OK;
}
static ScStatus ui_command_state(ScUiContext *c,ScU32 command,ScUiCommandDesc *out)
{
    ScHashEntry e;
    UiMenuEntry *entry;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing)return SC_UI_E_STALE;
    if(ScHashGet(c->commands,&command,sizeof(command),&e)<0)return SC_UI_E_NOT_FOUND;
    entry=*(UiMenuEntry * const *)e.value;
    memset(out,0,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->command=command;
    out->flags=entry->description.flags;
    ui_text_view(entry->label,&out->label);
    out->shortcut=entry->description.shortcut;
    out->icon=entry->description.icon;
    return SC_UI_OK;
}
ScStatus ScUiCommandSet(ScUiContext *c,const ScUiCommandDesc *d)
{
    UiMenuEntry *next=0,*old=0;
    ScHashEntry previous;
    ScStatus s=ui_input(d,sizeof(*d));
    ScUiMenuItem item;
    ScU32 i;
    if(s<0 || !d->command)return s<0?s:SC_UI_E_INVALID;
    for(i=0;i<3;i++)if(d->reserved[i])return SC_UI_E_INVALID;
    if(d->flags&~(SC_UI_MENU_DISABLED|SC_UI_MENU_CHECKED|SC_UI_MENU_RADIO|SC_UI_MENU_DEFAULT) || d->shortcut.modifiers&~(SC32_MOD_SHIFT|SC32_MOD_CONTROL|SC32_MOD_ALT))return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    if(c->next_token==UINT_MAX){s=SC_UI_E_RANGE;
        goto done;
        }
    next=(UiMenuEntry *)calloc(1,sizeof(*next));
    if(!next){s=SC_UI_E_MEMORY;
        goto done;
        }
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.command=d->command;
    item.flags=d->flags;
    item.label=d->label;
    item.shortcut=d->shortcut;
    next->description=item;
    s=ui_text_copy(&d->label,&next->label);
    if(s==0)s=ui_menu_caption(next->label,&next->caption,&next->mnemonic);
    if(s==0 && d->icon){s=ScUiIconRetain(d->icon);
        if(s==0)next->description.icon=d->icon;
        }
    memset(&previous,0,sizeof(previous));
    previous.size=sizeof(previous);
    previous.version=1;
    if(ScHashGet(c->commands,&d->command,sizeof(d->command),&previous)==0)old=*(UiMenuEntry * const *)previous.value;
    if(s==0)s=ScHashSet(c->commands,&d->command,sizeof(d->command),&next,0);
    if(s==0){next->id=c->next_token++;
        if(old)ui_menu_entry_free(old,1);
        next=0;
        {UiNode *n;
            for(n=c->all_nodes;n;n=n->all_next)if(n->command==d->command || n->menu)ui_node_damage(n);
            }
        s=SC_UI_OK;
        }
done:if(next)ui_menu_entry_free(next,1);
    s=ui_cause(c,s,UI_OP_MENU,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiCommandQuery(ScUiContext *c,ScU32 id,ScUiCommandDesc *out)
{
    ScUiCommandDesc d,current,expected;
    ScString *label=0;
    ScUiIcon *icon=0;
    ScHashEntry found;
    UiMenuEntry *entry;
    ScU32 stamp=0;
    ScStatus s=ui_output(out,sizeof(*out));
    ScUiEvent event;
    if(s<0)return s;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    s=ui_command_state(c,id,&d);
    if(s<0)goto done;
    if(!c->command_query_depth){memset(&found,0,sizeof(found));
        found.size=sizeof(found);
        found.version=1;
        ScHashGet(c->commands,&id,4,&found);
        entry=*(UiMenuEntry * const *)found.value;
        stamp=entry->id;
        s=ScStringRetain(entry->label);
        if(s<0)goto done;
        label=entry->label;
        if(entry->description.icon){s=ScUiIconRetain(entry->description.icon);
            if(s<0)goto done;
            icon=entry->description.icon;
            }
        expected=d;
        ui_event_record(&event,0,SC_UI_EVENT_COMMAND_QUERY);
        event.command=id;
        event.flags=SC_UI_EVENT_PROGRAMMATIC;
        event.data=&d;
        event.bytes=sizeof(d);
        c->command_query_depth++;
        s=ui_emit(c,0,&event);
        c->command_query_depth--;
        if(s<0)goto done;
        if(c->object.closing){s=SC_UI_E_CANCELLED;
            goto done;
            }
        if(d.size!=sizeof(d) || d.version!=1 || d.command!=id || d.flags&~(SC_UI_MENU_DISABLED|SC_UI_MENU_CHECKED|SC_UI_MENU_RADIO|SC_UI_MENU_DEFAULT)){s=SC_UI_E_INVALID;
            goto done;
            }
        if(d.label.size!=expected.label.size || d.label.version!=expected.label.version || d.label.data!=expected.label.data || d.label.bytes!=expected.label.bytes
            || d.shortcut.key!=expected.shortcut.key || d.shortcut.modifiers!=expected.shortcut.modifiers || d.icon!=expected.icon || d.reserved[0] || d.reserved[1] || d.reserved[2]){s=SC_UI_E_INVALID;
            goto done;
            }
        if(ScHashGet(c->commands,&id,4,&found)==0){entry=*(UiMenuEntry * const *)found.value;
            if(entry->id==stamp && entry->description.flags!=d.flags){entry->description.flags=d.flags;
                for(UiNode *n=c->all_nodes;n;n=n->all_next)if(!(n->private_flags&UI_NODE_RETIRED) && (n->command==id || n->menu || n->kind==UI_KIND_POPUP))ui_node_damage(n);
                }}
    }
    s=ui_command_state(c,id,&current);
    if(s==0)memcpy(out,&current,sizeof(current));
done:ScStringRelease(label);
    ScUiIconRelease(icon);
    s=ui_cause(c,s,UI_OP_MENU,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiCommandRemove(ScUiContext *c,ScU32 id)
{
    ScHashEntry e;
    UiMenuEntry *entry;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    if(ScHashGet(c->commands,&id,sizeof(id),&e)<0)s=SC_UI_E_NOT_FOUND;
    else{entry=*(UiMenuEntry * const *)e.value;
        s=ScHashRemove(c->commands,&id,sizeof(id));
        if(s==0){ui_menu_entry_free(entry,1);
            for(UiNode *n=c->all_nodes;n;n=n->all_next)if(n->command==id || n->menu)ui_node_damage(n);
            }}
    s=ui_cause(c,s,UI_OP_MENU,0);
    ui_object_leave(&c->object);
    return s;
}
static ScStatus ui_shortcut(ScUiContext *c,ScU32 key,ScU32 modifiers,ScU32 *out)
{
    ScIterator it;
    ScHashEntry e;
    memset(&it,0,sizeof(it));
    it.size=sizeof(it);
    it.version=1;
    if(ScHashIterate(c->commands,&it)<0)return SC_UI_E_NOT_FOUND;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    modifiers&=SC32_MOD_SHIFT|SC32_MOD_CONTROL|SC32_MOD_ALT;
    while(ScHashNext(&it,&e)==0){UiMenuEntry *entry=*(UiMenuEntry * const *)e.value;
        if(entry->description.shortcut.key==key && entry->description.shortcut.modifiers==modifiers && !(entry->description.flags&SC_UI_MENU_DISABLED)){*out=entry->description.command;
            return SC_UI_OK;
            }}
    return SC_UI_E_NOT_FOUND;
}
ScStatus ScUiCommand(ScUiContext *c,ScUiNode source,ScU32 command)
{
    UiNode *n;
    ScUiEvent event;
    ScUiCommandDesc state;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    n=ui_node(c,source);
    if(!n || !command){s=n?SC_UI_E_INVALID:SC_UI_E_STALE;
        goto done;
        }
    memset(&state,0,sizeof(state));
    state.size=sizeof(state);
    state.version=1;
    s=ScUiCommandQuery(c,command,&state);
    if(s<0 && s!=SC_UI_E_NOT_FOUND)goto done;
    if(s==0 && (state.flags&SC_UI_MENU_DISABLED)){s=SC_UI_E_CANCELLED;
        goto done;
        }
    n=ui_node(c,source);
    if(!n || c->object.closing){s=SC_UI_E_CANCELLED;
        goto done;
        }
    ui_event_record(&event,n,SC_UI_EVENT_COMMAND);
    event.command=command;
    event.flags=SC_UI_EVENT_PROGRAMMATIC;
    if(c->current_event){event.flags=c->current_event->flags&~SC_UI_EVENT_CANCELABLE;
        event.message=c->current_event->message;
        event.tick=c->current_event->tick;
        event.input_epoch=c->current_event->input_epoch;
        event.sequence=c->current_event->sequence;
        event.modifiers=c->current_event->modifiers;
        }
    s=ui_emit(c,n,&event);
    if(s>=0)s=SC_UI_OK;
done:s=ui_cause(c,s,UI_OP_MENU,0);
    ui_object_leave(&c->object);
    return s;
}
