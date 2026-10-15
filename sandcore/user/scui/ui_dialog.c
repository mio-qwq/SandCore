#include "SCUIPRIVATE.H"

typedef struct {
    ScUiFieldDesc description;
    ScString *section,*key,*label;
    ScUiNode node;
    ScUiProvider *options;
    ScUiSelection *selection;
}
UiDialogField;
typedef struct {ScDirectoryEntry information;
    ScU32 id;
}
UiDialogRow;
typedef struct {ScUiNode node;
    ScU32 command;
    }
UiDialogButton;
struct ScUiDialog {
    UiObject object;
    ScUiContext *context;
    ScUiNode root,owner,list,path_edit,name_edit,status,tabs;
    ScU32 type,stage,command,flags,completed,committed,default_command,cancel_command,timer,filter;
    ScStatus error;
    ScUiEventProc validate,apply,completion;
    void *user;
    ScDropProc drop;
    ScArray *paths,*rows,*pending_rows,*buttons;
    ScString *directory,*pending_directory;
    ScDirectory directory_handle;
    ScUiMenu *filters;
    ScUiProvider *provider;
    ScUiSelection *selection;
    ScConfig *values;
    UiDialogField *fields;
    ScU32 field_count,next_row;
    ScUiDialog *overwrite;
    ScString *pending_save;
    ScHash *row_index;
};
enum {UI_DIALOG_MESSAGE=1,UI_DIALOG_FILE=2,UI_DIALOG_PROPERTY=3,UI_DIALOG_UP=4,UI_DIALOG_FOLDER=5,UI_DIALOG_FILTER=6,UI_DIALOG_SORT=7};

static ScStatus ui_dialog_event(ScUiContext *,const ScUiEvent *,void *);
static ScStatus ui_dialog_finish(ScUiDialog *,ScU32,int);
static ScStatus ui_dialog_command(ScUiDialog *,ScU32,int live);
static ScUiText ui_dialog_text(const char *z)
{ScUiText text={sizeof(text),1,z,(ScU32)strlen(z)};
    return text;
}
static void ui_dialog_path_drop(void *item,void *context)
{(void)context;
    ScStringRelease(*(ScString **)item);
}
static ScStatus ui_dialog_paths(ScArray **out)
{
    ScArrayDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.item_bytes=sizeof(ScString *);
    d.drop=ui_dialog_path_drop;
    return ui_data_status(ScArrayCreate(&d,out));
}
static ScStatus ui_dialog_config_copy(const ScConfig *source,ScConfig **out)
{
    ScConfig *copy=0;
    ScString *text=0;
    ScView view;
    ScParseIssue issue;
    ScStatus s=ScConfigCreate(&copy);
    if(s==0 && source)s=ScConfigSerialize(source,SC_CONFIG_SORT_KEYS,&text);
    if(s==0 && text){memset(&view,0,sizeof(view));
        view.size=sizeof(view);
        view.version=1;
        s=ScStringView(text,&view);
        memset(&issue,0,sizeof(issue));
        issue.size=sizeof(issue);
        issue.version=1;
        if(s==0)s=ScConfigParse(copy,view.data,view.bytes,0,&issue);
    }
    ScStringRelease(text);
    if(s<0){ScConfigRelease(copy);
        return ui_data_status(s);
    }
    *out=copy;
    return 0;
}
static ScStatus ui_dialog_widget(ScUiDialog *d,ScUiNode parent,ScU32 kind,const ScUiText *text,ScU32 command,ScUiNode *out)
{
    ScUiWidgetDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.kind=kind;
    desc.parent=parent;
    desc.command=command;
    desc.text=text?*text:ui_dialog_text("");
    return ScUiWidgetCreate(d->context,&desc,out);
}
static void ui_dialog_layout(ScUiDialog *d,ScUiNode id,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex,ScU32 dock)
{
    UiNode *n=ui_node(d->context,id);
    if(!n)return;
    n->layout.mode=mode;
    n->layout.width=width;
    n->layout.height=height;
    n->layout.flex_weight=flex;
    n->layout.dock=dock;
    n->layout.gap=6;
    ui_node_layout_dirty(n);
}
static void ui_dialog_status(ScUiDialog *d,const char *message,ScStatus error)
{
    d->error=error;
    UiNode *n=ui_node(d->context,d->status);
    if(n){ScUiText text=ui_dialog_text(message);
        ScStatus s=ScUiNodeSetText(d->context,d->status,&text);
        if(s<0)d->context->notification_error=s;
    }
}
static void ui_dialog_dispose(UiObject *object,int quiet)
{
    ScUiDialog *d=(ScUiDialog *)object;
    if(d->directory_handle)ScDirectoryClose(d->directory_handle);
    d->directory_handle=0;
    if(d->timer && d->context && !d->context->object.disposed)ScUiTimerCancel(d->context,d->timer);
    d->timer=0;
    if(d->root && d->context && ui_node(d->context,d->root)){ScUiNode id=d->root;
        d->root=0;
        ScUiNodeDestroy(d->context,id);
    }
    ScUiDialogRelease(d->overwrite);
    d->overwrite=0;
    ScStringRelease(d->pending_save);
    ScArrayRelease(d->paths);
    ScArrayRelease(d->buttons);
    ScArrayRelease(d->rows);
    ScArrayRelease(d->pending_rows);
    ScStringRelease(d->directory);
    ScStringRelease(d->pending_directory);
    ScUiMenuRelease(d->filters);
    ScUiProviderRelease(d->provider);
    ScUiSelectionRelease(d->selection);
    ScConfigRelease(d->values);
    ScHashRelease(d->row_index);
    for(ScU32 i=0;i<d->field_count;i++){ScStringRelease(d->fields[i].section);
        ScStringRelease(d->fields[i].key);
        ScStringRelease(d->fields[i].label);
        ScUiProviderRelease(d->fields[i].options);
        ScUiSelectionRelease(d->fields[i].selection);
    }
    free(d->fields);
    if(!quiet && d->committed && d->drop)d->drop(d->user,0);
    ScUiContextRelease(d->context);
    d->context=0;
}
static ScStatus ui_dialog_new(ScUiContext *context,ScUiNode owner,ScU32 type,ScU32 flags,const ScUiText *title,
    ScU32 width,ScU32 height,ScUiDialog **out)
{
    UiNode *parent=ui_node(context,owner);
    ScUiDialog *d;
    void *candidate=0;
    ScStatus s;
    if(!parent || !parent->window)return SC_UI_E_STALE;
    s=ScUiContextRetain(context);
    if(s<0)return s;
    s=ui_object_new(sizeof(*d),UI_DIALOG,ui_dialog_dispose,&candidate);
    if(s<0){ScUiContextRelease(context);
        return s;
    }
    d=(ScUiDialog *)candidate;
    d->context=context;
    d->owner=owner;
    d->type=type;
    d->flags=flags;
    d->stage=SC_UI_DIALOG_CREATING;
    d->default_command=SC_UI_DIALOG_OK;
    d->cancel_command=SC_UI_DIALOG_CANCEL;
    d->next_row=1;
    s=ui_dialog_paths(&d->paths);
    if(s==0)s=ui_array_new(sizeof(UiDialogButton),&d->buttons);
    ScUiWindowDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.title=*title;
    desc.owner=owner;
    desc.x=parent->window->information.x+ui_scale(context,32);
    desc.y=parent->window->information.y+ui_scale(context,32);
    desc.client_width=(ScU32)ui_scale(context,(ScI32)width);
    desc.client_height=(ScU32)ui_scale(context,(ScI32)height);
    desc.style=SC32_STYLE_CLOSE|SC32_STYLE_RESIZE|SC32_STYLE_MODAL;
    if(s==0)s=ScUiWindowCreate(context,&desc,&d->root);
    if(s==0){UiNode *root=ui_node(context,d->root);
        root->layout.mode=SC_UI_VBOX;
        for(ScU32 i=0;i<4;i++)root->layout.padding[i]=8;
        root->layout.gap=8;
        ScUiSubscription sub;
        ScU32 token;
        memset(&sub,0,sizeof(sub));
        sub.size=sizeof(sub);
        sub.version=1;
        sub.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
        sub.procedure=ui_dialog_event;
        sub.context=d;
        s=ScUiListen(context,d->root,&sub,&token);
    }
    if(s<0){ScUiDialogRelease(d);
        return s;
    }
    *out=d;
    return 0;
}
static ScStatus ui_dialog_callbacks(ScUiDialog *d,ScUiEventProc validate,ScUiEventProc apply,ScUiEventProc complete,void *user,ScDropProc drop)
{
    ScStatus s=ui_pin((const void *)validate);
    if(s==0)s=ui_pin((const void *)apply);
    if(s==0)s=ui_pin((const void *)complete);
    if(s==0)s=ui_pin((const void *)drop);
    if(s==0){d->validate=validate;
        d->apply=apply;
        d->completion=complete;
        d->user=user;
        d->drop=drop;
    }
    return s;
}
static ScStatus ui_dialog_buttons(ScUiDialog *d,const ScUiMenuItem *buttons,ScU32 count)
{
    ScUiNode bar,button;
    ScStatus s=ui_dialog_widget(d,d->root,SC_UI_PANEL,0,0,&bar);
    if(s<0)return s;
    ui_dialog_layout(d,bar,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    UiNode *host=ui_node(d->context,bar);
    host->layout.align_h=SC_UI_END_ALIGN;
    ScUiMenuItem fallback[2];
    if(!count){memset(fallback,0,sizeof(fallback));
        for(ScU32 i=0;i<2;i++){fallback[i].size=sizeof(fallback[i]);
            fallback[i].version=1;
        }
        fallback[0].command=SC_UI_DIALOG_OK;
        fallback[0].label=ui_dialog_text("确定");
        fallback[1].command=SC_UI_DIALOG_CANCEL;
        fallback[1].label=ui_dialog_text("取消");
        buttons=fallback;
        count=2;
    }
    if(!buttons)return SC_UI_E_INVALID;
    if(!d->default_command)for(ScU32 i=0;i<count;i++)if(!(buttons[i].flags&SC_UI_MENU_DISABLED)){d->default_command=buttons[i].command;
        break;
        }
    if(!d->cancel_command){
        for(ScU32 i=0;i<count;i++)if(buttons[i].command==SC_UI_DIALOG_CANCEL)d->cancel_command=SC_UI_DIALOG_CANCEL;
        if(!d->cancel_command)d->cancel_command=buttons[count-1].command;
    }
    for(ScU32 i=0;i<count;i++){
        s=ui_input(buttons+i,sizeof(*buttons));
        if(s<0)return s;
        if(!buttons[i].command || buttons[i].flags&~(SC_UI_MENU_DEFAULT|SC_UI_MENU_DISABLED) || !ui_data_reserved(buttons[i].reserved,3))return SC_UI_E_INVALID;
        for(ScU32 previous=0;previous<i;previous++)if(buttons[previous].command==buttons[i].command)return SC_UI_E_EXISTS;
        s=ui_dialog_widget(d,bar,SC_UI_BUTTON,&buttons[i].label,buttons[i].command,&button);
        if(s<0)return s;
        UiDialogButton candidate={button,buttons[i].command};
        ScContainerInfo stored;
        memset(&stored,0,sizeof(stored));
        stored.size=sizeof(stored);
        stored.version=1;
        s=ScArrayQuery(d->buttons,&stored);
        if(s==0)s=ScArrayInsert(d->buttons,stored.length,&candidate,1);
        if(s<0)return ui_data_status(s);
        ui_dialog_layout(d,button,SC_UI_ABSOLUTE,SC_UI_AUTO,28,0,0);
        if(buttons[i].flags&SC_UI_MENU_DISABLED)ScUiNodeSetState(d->context,button,SC_UI_ENABLED,0);
        if(buttons[i].command==d->default_command)ScUiButtonSetDefault(d->context,d->root,button);
    }
    return 0;
}
ScStatus ScUiMessageBox(ScUiContext *context,const ScUiMessageBoxDesc *desc,ScUiDialog **out)
{
    ScStatus s=ui_input(desc,sizeof(*desc));
    ScUiDialog *d=0;
    ScUiNode label;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(desc->reserved || desc->flags)return SC_UI_E_INVALID;
    s=ui_dialog_new(context,desc->owner,UI_DIALOG_MESSAGE,0,&desc->title,420,170,&d);
    if(s<0)return s;
    d->default_command=desc->default_command;
    d->cancel_command=desc->cancel_command;
    s=ui_dialog_callbacks(d,0,0,desc->completion,desc->context,desc->context_drop);
    if(s==0)s=ui_dialog_widget(d,d->root,SC_UI_LABEL,&desc->message,0,&label);
    if(s==0){ui_dialog_layout(d,label,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
        s=ui_dialog_buttons(d,desc->buttons,desc->button_count);
    }
    if(s==0 && d->default_command && ui_dialog_command(d,d->default_command,0)<0)s=SC_UI_E_INVALID;
    if(s==0 && ui_dialog_command(d,d->cancel_command,0)<0)s=SC_UI_E_INVALID;
    if(s==0)s=ScUiLayout(context,d->root);
    if(s<0){ScUiDialogRelease(d);
        return s;
    }
    d->stage=SC_UI_DIALOG_OPEN;
    d->committed=1;
    *out=d;
    return 0;
}
ScStatus ScUiDialogRetain(ScUiDialog *d){return ui_object_retain((UiObject *)d,UI_DIALOG);
}
void ScUiDialogRelease(ScUiDialog *d)
{
    if(!d || d->object.kind!=UI_DIALOG || !d->object.references)return;
    d->object.depth++;
    if(d->object.references==1 && d->stage==SC_UI_DIALOG_OPEN && !ui_quiet)ui_dialog_finish(d,d->cancel_command,1);
    if(d->object.references)ui_object_release(&d->object);
    ui_object_leave(&d->object);
}
ScStatus ScUiDialogQuery(const ScUiDialog *d,ScUiDialogInfo *out)
{
    ScUiDialogInfo v;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!d || d->object.kind!=UI_DIALOG || d->object.disposed)return SC_UI_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.stage=d->stage;
    v.root=d->root;
    v.command=d->command;
    v.flags=d->flags;
    v.error=d->error;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(d->paths && ScArrayQuery(d->paths,&info)==0)v.selected_count=info.length;
    memcpy(out,&v,sizeof(v));
    return 0;
}
static ScStatus ui_dialog_finish(ScUiDialog *d,ScU32 command,int cancelled)
{
    if(d->completed)return 0;
    d->completed=1;
    d->command=command;
    d->stage=cancelled?SC_UI_DIALOG_CANCELLED:SC_UI_DIALOG_DONE;
    if(d->timer){ScUiTimerCancel(d->context,d->timer);
        d->timer=0;
    }
    if(d->directory_handle){ScDirectoryClose(d->directory_handle);
        d->directory_handle=0;
    }
    ScUiNode root=d->root;
    d->root=0;
    if(root && ui_node(d->context,root))ScUiNodeDestroy(d->context,root);
    if(!ui_quiet && d->completion){ScUiEvent event;
        memset(&event,0,sizeof(event));
        event.size=sizeof(event);
        event.version=1;
        event.type=SC_UI_EVENT_DIALOG_COMPLETE;
        event.root=root;
        event.command=command;
        event.flags=SC_UI_EVENT_PROGRAMMATIC;
        event.data=d;
        event.bytes=sizeof(ScUiDialogInfo);
        ScStatus s=d->completion(d->context,&event,d->user);
        if(s<0)d->context->notification_error=s;
    }
    return 0;
}
ScStatus ScUiDialogSelectedPath(ScUiDialog *d,ScU32 index,ScString **out)
{
    ScString *path=0;
    if(!d || d->object.kind!=UI_DIALOG || d->object.disposed)return SC_UI_E_STALE;
    if(!out)return SC_UI_E_INVALID;
    ScStatus s=ScArrayGet(d->paths,index,&path);
    if(s<0)return ui_data_status(s);
    s=ScStringRetain(path);
    if(s==0)*out=path;
    return ui_data_status(s);
}
ScStatus ScUiDialogCopyValues(ScUiDialog *d,ScConfig **out)
{if(!d || d->object.kind!=UI_DIALOG || d->object.disposed)return SC_UI_E_STALE;
    if(!out || !d->values)return SC_UI_E_INVALID;
    return ui_dialog_config_copy(d->values,out);
}
static void ui_dialog_node_destroy(UiNode *n,int quiet)
{
    /* 节点payload释放时header仍活，正常完成与quiet均不留下悬空root。 */
    for(UiObject *object=ui_objects;object;object=object->next)if(object->kind==UI_DIALOG){ScUiDialog *d=(ScUiDialog *)object;
        if(d->context==n->owner && d->root==n->id){d->root=0;
            if(!d->completed){d->object.depth++;
                if(quiet){d->completed=1;
                    d->stage=SC_UI_DIALOG_CANCELLED;
                    d->command=d->cancel_command;
                }
                else ui_dialog_finish(d,d->cancel_command,1);
                ui_object_leave(&d->object);
            }
            break;
        }
    }
}

/* glob只解释UTF8原字节的ASCII * / ?，?消费一个标量，不按字节拆中文。 */
static int ui_dialog_glob(const char *pattern,ScU32 bytes,const char *name,ScU32 length)
{
    ScU32 p=0,n=0,star=UINT_MAX,retry=0;
    while(n<length){
        if(p<bytes && pattern[p]=='*'){star=p++;
            retry=n;
            continue;
        }
        if(p<bytes && pattern[p]=='?'){ScU32 scalar,next=n;
            if(ScUtf8Decode(name,length,&next,&scalar)<0)return 0;
            n=next;
            p++;
            continue;
        }
        if(p<bytes && pattern[p]==name[n]){p++;
            n++;
            continue;
        }
        if(star!=UINT_MAX){ScU32 scalar,next=retry;
            if(ScUtf8Decode(name,length,&next,&scalar)<0)return 0;
            retry=n=next;
            p=star+1;
            continue;
        }
        return 0;
    }
    while(p<bytes && pattern[p]=='*')p++;
    return p==bytes;
}
static int ui_dialog_filter(ScUiDialog *d,const ScDirectoryEntry *row)
{
    if(row->kind==SC32_PATH_DIRECTORY || !d->filters || !d->filter)return 1;
    ScUiMenuItem filter;
    memset(&filter,0,sizeof(filter));
    filter.size=sizeof(filter);
    filter.version=1;
    if(ScUiMenuGet(d->filters,d->filter,&filter)<0 || !filter.help.bytes)return 1;
    ScU32 first=0;
    for(ScU32 i=0;i<=filter.help.bytes;i++)if(i==filter.help.bytes || filter.help.data[i]==';'){
        if(ui_dialog_glob(filter.help.data+first,i-first,row->name,(ScU32)strlen(row->name)))return 1;
        first=i+1;
    }
    return 0;
}
static ScStatus ui_dialog_file_data(void *opaque,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    ScUiDialog *d=(ScUiDialog *)opaque;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScArrayQuery(d->rows,&info);
    if(s<0)return ui_data_status(s);
    reply->revision=d->provider->revision;
    if(request->operation==SC_UI_DATA_COUNT){reply->total=info.length;
        return 0;
    }
    ScU32 index=request->index;
    if(request->operation!=SC_UI_DATA_CHILD_AT){ScHashEntry entry;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        s=ScHashGet(d->row_index,&request->item,4,&entry);
        if(s<0)return SC_UI_E_STALE;
        index=*(const ScU32 *)entry.value;
    }
    if(index>=info.length)return request->operation==SC_UI_DATA_CHILD_AT?SC_UI_END:SC_UI_E_STALE;
    /* 行本体由拥有数组保存，reply只借到下一次实际目录发布。 */
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    s=ScArrayView(d->rows,&view);
    if(s<0)return ui_data_status(s);
    const UiDialogRow *entry=(const UiDialogRow *)view.data+index;
    const ScDirectoryEntry *row=&entry->information;
    reply->item=entry->id;
    reply->total=index;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF)return 0;
    if(request->field==1){reply->value.type=SC_VALUE_U32;
        reply->value.lo=row->length;
    }
    else if(request->field==2){reply->value.type=SC_VALUE_U32;
        reply->value.lo=row->kind;
    }
    else{reply->value.type=SC_VALUE_UTF8;
        reply->value.data=row->name;
        reply->value.bytes=(ScU32)strlen(row->name);
    }
    return 0;
}
static ScStatus ui_dialog_directory_step(ScUiDialog *d)
{
    ScDirectoryEntry rows[64];
    ScDirectoryPage page;
    ScStatus s;
    memset(&page,0,sizeof(page));
    page.size=sizeof(page);
    page.version=1;
    if(!d->directory_handle)return 0;
    s=ScDirectoryNext(d->directory_handle,rows,64,&page);
    if(s==0)for(ScU32 i=0;i<page.count;i++)if(ui_dialog_filter(d,rows+i)){ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScArrayQuery(d->pending_rows,&info);
        if(d->next_row==UINT_MAX)s=SC_UI_E_RANGE;
        UiDialogRow row={rows[i],d->next_row++};
        if(s==0)s=ScArrayInsert(d->pending_rows,info.length,&row,1);
        if(s<0)break;
    }
    if(s<0 || (page.flags&SC32_DIRECTORY_FINISHED)){
        ScStatus close=ScDirectoryClose(d->directory_handle);
        d->directory_handle=0;
        if(s==0)s=close;
        if(d->timer){ScUiTimerCancel(d->context,d->timer);
            d->timer=0;
        }
        ScHash *index=0;
        if(s==0)s=ui_hash_new(4,&index);
        if(s==0){ScContainerInfo count;
            memset(&count,0,sizeof(count));
            count.size=sizeof(count);
            count.version=1;
            s=ScArrayQuery(d->pending_rows,&count);
            for(ScU32 i=0;s==0 && i<count.length;i++){UiDialogRow row;
                s=ScArrayGet(d->pending_rows,i,&row);
                if(s==0)s=ScHashSet(index,&row.id,4,&i,0);
            }
        }
        if(s==0){ScArray *old=d->rows;
            d->rows=d->pending_rows;
            d->pending_rows=0;
            ScArrayRelease(old);
            ScHashRelease(d->row_index);
            d->row_index=index;
            index=0;
            ScStringRelease(d->directory);
            d->directory=d->pending_directory;
            d->pending_directory=0;
            ScUiSelectionClear(d->selection);
            ScUiDataChange change;
            memset(&change,0,sizeof(change));
            change.size=sizeof(change);
            change.version=1;
            change.kind=SC_UI_DATA_RESET;
            change.revision=d->provider->revision+1;
            s=ScUiProviderNotify(d->provider,&change);
            ScUiText path;
            ui_text_view(d->directory,&path);
            ScUiNodeSetText(d->context,d->path_edit,&path);
            ui_dialog_status(d,"",s);
        }
        else{ScArrayRelease(d->pending_rows);
            d->pending_rows=0;
            ScStringRelease(d->pending_directory);
            d->pending_directory=0;
            ui_dialog_status(d,s==SC32_E_STALE?"目录已变化，请重试":"无法读取目录",ui_cause(d->context,s,UI_OP_DATA,d->list));
        }
        ScHashRelease(index);
        ScUiNodeSetState(d->context,d->list,SC_UI_BUSY,0);
    }
    return s;
}
static ScStatus ui_dialog_directory(ScUiDialog *d,const char *path)
{
    char resolved[64];
    ScU32 length;
    ScDirectory handle=0;
    ScArray *rows=0;
    ScString *directory=0;
    ScStatus s=ScPathResolve(path,resolved,sizeof(resolved),&length);
    if(s==0)s=ScDirectoryOpen(resolved,&handle);
    if(s==0)s=ui_array_new(sizeof(UiDialogRow),&rows);
    if(s==0)s=ScStringCreate(resolved,length,&directory);
    if(s<0){if(handle)ScDirectoryClose(handle);
        ScArrayRelease(rows);
        ScStringRelease(directory);
        return ui_cause(d->context,s,UI_OP_DATA,d->list);
    }
    if(d->directory_handle)ScDirectoryClose(d->directory_handle);
    ScArrayRelease(d->pending_rows);
    ScStringRelease(d->pending_directory);
    d->directory_handle=handle;
    d->pending_rows=rows;
    d->pending_directory=directory;
    ScUiNodeSetState(d->context,d->list,SC_UI_BUSY,SC_UI_BUSY);
    UiNode *root=ui_node(d->context,d->root);
    s=ui_control_timer(root,1,1,&d->timer);
    if(s<0){ScDirectoryClose(handle);
        d->directory_handle=0;
        ScArrayRelease(rows);
        d->pending_rows=0;
        ScStringRelease(directory);
        d->pending_directory=0;
    }
    ui_dialog_status(d,"读取目录…",s);
    return s;
}

ScStatus ScUiFileDialog(ScUiContext *context,const ScUiFileDialogDesc *desc,ScUiDialog **out)
{
    ScStatus s=ui_input(desc,sizeof(*desc));
    ScUiDialog *d=0;
    ScUiNode bar,button;
    ScUiText text;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(desc->reserved || desc->flags&~31u || !(desc->flags&(SC_UI_FILE_OPEN|SC_UI_FILE_SAVE|SC_UI_FILE_DIRECTORY))
        || ((desc->flags&SC_UI_FILE_OPEN) && (desc->flags&SC_UI_FILE_SAVE)) || ((desc->flags&SC_UI_FILE_SAVE) && (desc->flags&SC_UI_FILE_MULTI)))return SC_UI_E_INVALID;
    s=ui_dialog_new(context,desc->owner,UI_DIALOG_FILE,desc->flags,&desc->title,620,420,&d);
    if(s<0)return s;
    s=ui_dialog_callbacks(d,0,0,desc->completion,desc->context,desc->context_drop);
    if(s==0)s=ui_array_new(sizeof(UiDialogRow),&d->rows);
    if(s==0)s=ui_hash_new(4,&d->row_index);
    if(s==0 && desc->filter_count){s=ScUiMenuCreate(&d->filters);
        if(s==0)s=ScUiMenuReplace(d->filters,desc->filters,desc->filter_count);
        d->filter=desc->selected_filter;
        if(s==0 && !d->filter){ScUiMenuItem first;
            memset(&first,0,sizeof(first));
            first.size=sizeof(first);
            first.version=1;
            ScUiMenuGet(d->filters,1,&first);
            d->filter=first.item;
        }
    }
    if(s==0)s=ui_dialog_widget(d,d->root,SC_UI_PANEL,0,0,&bar);
    if(s==0){ui_dialog_layout(d,bar,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
        text=ui_dialog_text("上一级");
        s=ui_dialog_widget(d,bar,SC_UI_BUTTON,&text,UI_DIALOG_UP,&button);
    }
    if(s==0){text=ui_dialog_text(desc->directory?desc->directory:"/HOME");
        s=ui_dialog_widget(d,bar,SC_UI_TEXTBOX,&text,0,&d->path_edit);
        ui_dialog_layout(d,d->path_edit,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    }
    if(s==0){text=ui_dialog_text("新建目录");
        s=ui_dialog_widget(d,bar,SC_UI_BUTTON,&text,UI_DIALOG_FOLDER,&button);
    }
    if(s==0 && d->filters){text=ui_dialog_text("过滤");
        s=ui_dialog_widget(d,bar,SC_UI_BUTTON,&text,UI_DIALOG_FILTER,&button);
    }
    if(s==0){text=ui_dialog_text("排序");
        s=ui_dialog_widget(d,bar,SC_UI_BUTTON,&text,UI_DIALOG_SORT,&button);
    }
    if(s==0)s=ui_dialog_widget(d,d->root,SC_UI_TABLE,0,0,&d->list);
    if(s==0){ui_dialog_layout(d,d->list,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
        ScUiProviderDesc p;
        memset(&p,0,sizeof(p));
        p.size=sizeof(p);
        p.version=1;
        p.procedure=ui_dialog_file_data;
        p.context=d;
        s=ScUiProviderCreate(&p,&d->provider);
        if(s==0)s=ScUiSelectionCreate(d->provider,SC_UI_SELECTION_ALLOW_EMPTY|((desc->flags&SC_UI_FILE_MULTI)?SC_UI_SELECTION_MULTI:0),&d->selection);
        if(s==0)s=ScUiViewBind(context,d->list,d->provider,d->selection);
        ScUiColumnDesc columns[3];
        memset(columns,0,sizeof(columns));
        for(ScU32 i=0;i<3;i++){columns[i].size=sizeof(columns[i]);
            columns[i].version=1;
            columns[i].column=i+1;
            columns[i].field=i;
            columns[i].width=i?90:340;
            columns[i].min_width=40;
            columns[i].max_width=800;
        }
        columns[0].title=ui_dialog_text("名称");
        columns[1].title=ui_dialog_text("大小");
        columns[1].format=SC_UI_FORMAT_BYTES;
        columns[2].title=ui_dialog_text("类型");
        columns[2].format=SC_UI_FORMAT_U32;
        if(s==0)s=ScUiTableSetColumns(context,d->list,columns,3);
    }
    if(s==0){text=ui_dialog_text("");
        s=ui_dialog_widget(d,d->root,SC_UI_TEXTBOX,&text,0,&d->name_edit);
    }
    if(s==0){text=ui_dialog_text("");
        s=ui_dialog_widget(d,d->root,SC_UI_LABEL,&text,0,&d->status);
    }
    if(s==0)s=ui_dialog_buttons(d,0,0);
    if(s==0)s=ui_dialog_directory(d,desc->directory?desc->directory:"/HOME");
    if(s==0)s=ScUiLayout(context,d->root);
    if(s<0){ScUiDialogRelease(d);
        return s;
    }
    d->stage=SC_UI_DIALOG_OPEN;
    d->committed=1;
    *out=d;
    return 0;
}
static int ui_dialog_row_compare(const void *a,const void *b)
{
    const UiDialogRow *left=(const UiDialogRow *)a,*right=(const UiDialogRow *)b;
    if(left->information.kind!=right->information.kind)return left->information.kind==SC32_PATH_DIRECTORY?-1:1;
    return strcmp(left->information.name,right->information.name);
}
static ScStatus ui_dialog_sort(ScUiDialog *d)
{
    ScView view;
    ScArray *rows=0;
    ScHash *index=0;
    ScStatus s;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    s=ScArrayView(d->rows,&view);
    if(s==0)s=ui_array_new(sizeof(UiDialogRow),&rows);
    ScU32 count=view.bytes/sizeof(UiDialogRow);
    if(s==0 && count)s=ScArrayInsert(rows,0,view.data,count);
    if(s==0)s=ui_hash_new(4,&index);
    if(s==0){ScArrayView(rows,&view);
        qsort((void *)view.data,count,sizeof(UiDialogRow),ui_dialog_row_compare);
        const UiDialogRow *items=(const UiDialogRow *)view.data;
        for(ScU32 i=0;s==0 && i<count;i++)s=ScHashSet(index,&items[i].id,4,&i,0);
    }
    if(s==0){ScArray *old=d->rows;
        ScHash *old_index=d->row_index;
        d->rows=rows;
        d->row_index=index;
        rows=old;
        index=old_index;
        ScUiDataChange change;
        memset(&change,0,sizeof(change));
        change.size=sizeof(change);
        change.version=1;
        change.revision=d->provider->revision+1;
        change.kind=SC_UI_DATA_SORTED;
        s=ScUiProviderNotify(d->provider,&change);
    }
    ScArrayRelease(rows);
    ScHashRelease(index);
    return ui_cause(d->context,s,UI_OP_DATA,d->list);
}
static ScStatus ui_dialog_selected_row(ScUiDialog *d,ScU32 id,UiDialogRow *row)
{
    ScHashEntry entry;
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    ScStatus s=ScHashGet(d->row_index,&id,4,&entry);
    if(s<0)return SC_UI_E_STALE;
    return ui_data_status(ScArrayGet(d->rows,*(const ScU32 *)entry.value,row));
}
static ScStatus ui_dialog_join(ScUiDialog *d,const char *leaf,ScString **out)
{
    const char *directory=0;
    ScStatus s=d->directory?ScStringCStr(d->directory,&directory):SC_UI_E_BUSY;
    if(s==0)s=ScPathJoin(directory,leaf,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,out);
    if(s<0)return ui_data_status(s);
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScStringQuery(*out,&info);
    if(s==0 && info.length>=64)s=SC_UI_E_RANGE;
    if(s<0){ScStringRelease(*out);
        *out=0;
    }
    return ui_data_status(s);
}
static ScStatus ui_dialog_overwrite(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    ScUiDialog *d=(ScUiDialog *)opaque;
    (void)context;
    d->object.depth++;
    if(event->command==SC_UI_DIALOG_OK && d->stage==SC_UI_DIALOG_OPEN && d->pending_save){
        ScArray *paths=0;
        ScStatus s=ui_dialog_paths(&paths);
        if(s==0)s=ScStringRetain(d->pending_save);
        if(s==0){s=ScArrayInsert(paths,0,&d->pending_save,1);
            if(s<0)ScStringRelease(d->pending_save);
        }
        if(s==0){ScArrayRelease(d->paths);
            d->paths=paths;
            paths=0;
            ui_dialog_finish(d,SC_UI_DIALOG_OK,0);
        }
        else ui_dialog_status(d,"无法完成选择",ui_data_status(s));
        ScArrayRelease(paths);
    }
    ScStringRelease(d->pending_save);
    d->pending_save=0;
    ScUiDialog *dialog=d->overwrite;
    d->overwrite=0;
    ScUiDialogRelease(dialog);
    ui_object_leave(&d->object);
    return 0;
}
static ScStatus ui_dialog_file_accept(ScUiDialog *d)
{
    ScArray *paths=0,*ids=0;
    ScString *name=0,*path=0;
    const char *z=0;
    ScStatus s;
    if(d->directory_handle)return SC_UI_E_BUSY;
    s=ScUiNodeCopyText(d->context,d->name_edit,&name);
    if(s==0)s=ScStringCStr(name,&z);
    if(s<0)goto done;
    s=ui_dialog_paths(&paths);
    if(s<0)goto done;
    if(d->flags&SC_UI_FILE_SAVE){
        if(!*z || strchr(z,'/') || strchr(z,'\\') || strcmp(z,".")==0 || strcmp(z,"..")==0){s=SC_UI_E_INVALID;
            goto done;
        }
        s=ui_dialog_join(d,z,&path);
        if(s<0)goto done;
        const char *full;
        ScStringCStr(path,&full);
        ScPathInfo metadata;
        memset(&metadata,0,sizeof(metadata));
        metadata.size=sizeof(metadata);
        metadata.version=1;
        s=ScPathMetadata(full,&metadata);
        if(s==SC32_E_NOT_FOUND)s=0;
        else if(s==0 && metadata.kind!=SC32_PATH_FILE)s=SC_UI_E_INVALID;
        else if(s==0 && !(d->flags&SC_UI_FILE_OVERWRITE)){
            ScUiMessageBoxDesc message;
            memset(&message,0,sizeof(message));
            message.size=sizeof(message);
            message.version=1;
            message.owner=d->root;
            message.title=ui_dialog_text("确认替换");
            message.message=ui_dialog_text("同名文件已存在。替换此文件？");
            message.completion=ui_dialog_overwrite;
            message.context=d;
            s=ScUiMessageBox(d->context,&message,&d->overwrite);
            if(s==0){d->pending_save=path;
                path=0;
                s=SC_UI_PENDING;
            }
            goto done;
        }
        if(s==0){s=ScArrayInsert(paths,0,&path,1);
            if(s==0)path=0;
        }
    }
    else{
        if(*z){s=ui_dialog_join(d,z,&path);
            if(s==0){const char *full;
                ScStringCStr(path,&full);
                ScPathStatInfo stat;
                memset(&stat,0,sizeof(stat));
                stat.size=sizeof(stat);
                stat.version=1;
                s=ScPathStat(full,&stat);
                if(s==0 && stat.kind==SC32_PATH_DIRECTORY && !(d->flags&SC_UI_FILE_DIRECTORY)){s=ui_dialog_directory(d,full);
                    if(s==0)s=SC_UI_PENDING;
                    goto done;
                }
                if(s==0 && (d->flags&SC_UI_FILE_DIRECTORY) && stat.kind!=SC32_PATH_DIRECTORY)s=SC_UI_E_INVALID;
            }
            if(s==0){s=ScArrayInsert(paths,0,&path,1);
                if(s==0)path=0;
            }
        }
        else{s=ScUiSelectionCopy(d->selection,&ids);
            ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            if(s==0)s=ScArrayQuery(ids,&info);
            if(s==0 && !info.length && (d->flags&SC_UI_FILE_DIRECTORY)){s=ScStringRetain(d->directory);
                if(s==0){s=ScArrayInsert(paths,0,&d->directory,1);
                    if(s<0)ScStringRelease(d->directory);
                }
            }
            else if(s==0 && !info.length)s=SC_UI_E_NOT_FOUND;
            for(ScU32 i=0;s==0 && i<info.length;i++){ScU32 id;
                UiDialogRow row;
                s=ScArrayGet(ids,i,&id);
                if(s==0)s=ui_dialog_selected_row(d,id,&row);
                if(s==0 && row.information.kind==SC32_PATH_DIRECTORY && !(d->flags&SC_UI_FILE_DIRECTORY)){
                    if(info.length!=1){s=SC_UI_E_INVALID;
                        break;
                    }
                    s=ui_dialog_join(d,row.information.name,&path);
                    if(s==0){const char *full;
                        ScStringCStr(path,&full);
                        s=ui_dialog_directory(d,full);
                        if(s==0)s=SC_UI_PENDING;
                    }
                    break;
                }
                if(s==0 && (d->flags&SC_UI_FILE_DIRECTORY) && row.information.kind!=SC32_PATH_DIRECTORY)s=SC_UI_E_INVALID;
                if(s==0)s=ui_dialog_join(d,row.information.name,&path);
                if(s==0){s=ScArrayInsert(paths,i,&path,1);
                    if(s==0)path=0;
                }
            }
        }
    }
    if(s==0){ScArrayRelease(d->paths);
        d->paths=paths;
        paths=0;
        s=ui_dialog_finish(d,SC_UI_DIALOG_OK,0);
    }
    done:ScArrayRelease(paths);
    ScArrayRelease(ids);
    ScStringRelease(name);
    ScStringRelease(path);
    return ui_cause(d->context,s,UI_OP_DATA,d->list);
}

static ScStatus ui_dialog_field_value(ScUiDialog *d,UiDialogField *field,ScValue *value,ScString **text)
{
    UiNode *n=ui_node(d->context,field->node);
    ScStatus s;
    memset(value,0,sizeof(*value));
    value->size=sizeof(*value);
    value->version=1;
    if(!n)return SC_UI_E_STALE;
    if(field->description.kind==SC_UI_CHECKBOX || field->description.kind==SC_UI_RADIO){value->type=SC_VALUE_BOOL;
        value->lo=(n->state&SC_UI_CHECKED)!=0;
        return 0;
    }
    if(field->description.kind==SC_UI_COMBOBOX && n->view && n->view->selection){
        ScValue selected;
        memset(&selected,0,sizeof(selected));
        selected.size=sizeof(selected);
        selected.version=1;
        s=ui_provider_cell(n->view->provider,n->view->selection->current,0,&selected);
        if(s<0)return s;
        if(selected.type!=SC_VALUE_UTF8)return SC_UI_E_INVALID;
        s=ScStringCreate(selected.data,selected.bytes,text);
    }
    else s=ScUiNodeCopyText(d->context,field->node,text);
    if(s<0)return ui_data_status(s);
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    s=ScStringView(*text,&view);
    if(s<0)return ui_data_status(s);
    if(field->description.kind==SC_UI_SPINBOX){const char *z;
        char *tail;
        s=ScStringCStr(*text,&z);
        if(s<0)return ui_data_status(s);
        errno=0;
        long number=strtol(z,&tail,10);
        if(errno==ERANGE || tail==z || *tail || memchr(z,0,view.bytes) || number<field->description.minimum || number>field->description.maximum)return SC_UI_E_RANGE;
        value->type=SC_VALUE_I32;
        value->lo=(ScU32)number;
    }
    else{if((field->description.flags&SC_UI_FIELD_REQUIRED) && !view.bytes)return SC_UI_E_INVALID;
        value->type=SC_VALUE_UTF8;
        value->data=view.data;
        value->bytes=view.bytes;
    }
    return 0;
}
static ScStatus ui_dialog_property_apply(ScUiDialog *d,int close)
{
    ScConfig *candidate=0;
    ScStatus s=ui_dialog_config_copy(d->values,&candidate);
    if(s<0)return s;
    for(ScU32 i=0;s==0 && i<d->field_count;i++){
        UiDialogField *field=d->fields+i;
        if(field->description.flags&SC_UI_FIELD_READONLY)continue;
        ScValue value;
        ScString *text=0;
        const char *section,*key;
        s=ScStringCStr(field->section,&section);
        if(s==0)s=ScStringCStr(field->key,&key);
        if(s==0)s=ui_dialog_field_value(d,field,&value,&text);
        if(s==0)s=ScConfigSet(candidate,section,key,&value);
        ScStringRelease(text);
        if(s<0){ScUiNodeSetState(d->context,field->node,SC_UI_INVALID,SC_UI_INVALID);
            ScUiFocus(d->context,field->node);
            ui_dialog_status(d,"请检查标记的字段",ui_data_status(s));
        }
        else ScUiNodeSetState(d->context,field->node,SC_UI_INVALID,0);
    }
    if(s==0){d->stage=SC_UI_DIALOG_VALIDATING;
        ScUiEvent event;
        memset(&event,0,sizeof(event));
        event.size=sizeof(event);
        event.version=1;
        event.type=SC_UI_EVENT_VALIDATE;
        event.root=d->root;
        event.data=candidate;
        event.bytes=0;
        event.flags=SC_UI_EVENT_PROGRAMMATIC|SC_UI_EVENT_CANCELABLE;
        if(d->validate)s=d->validate(d->context,&event,d->user);
        if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
        else if(s>0)s=0;
        if(s==0 && (!d->root || d->object.closing))s=SC_UI_E_CANCELLED;
        if(s==0 && d->apply){event.type=SC_UI_EVENT_APPLY;
            event.flags=SC_UI_EVENT_PROGRAMMATIC;
            s=d->apply(d->context,&event,d->user);
            if(s>0)s=0;
        }
        if(s==0 && (!d->root || d->object.closing))s=SC_UI_E_CANCELLED;
        if(s==0){ScConfig *old=d->values;
            d->values=candidate;
            candidate=old;
            d->stage=SC_UI_DIALOG_OPEN;
            ui_dialog_status(d,"已应用",0);
            if(close)s=ui_dialog_finish(d,SC_UI_DIALOG_OK,0);
        }
        else{d->stage=SC_UI_DIALOG_OPEN;
            ui_dialog_status(d,"应用未完成，请检查错误",ui_data_status(s));
        }
    }
    ScConfigRelease(candidate);
    return ui_data_status(s);
}
static ScStatus ui_dialog_combo_values(ScUiDialog *d,UiDialogField *field,const ScValue *initial,const char *section,const char *key)
{
    char choice_key[128];
    int length=snprintf(choice_key,sizeof(choice_key),"%s.choices",key);
    if(length<0 || (ScU32)length>=sizeof(choice_key))return SC_UI_E_RANGE;
    ScValue choices;
    memset(&choices,0,sizeof(choices));
    choices.size=sizeof(choices);
    choices.version=1;
    ScStatus s=ScConfigGet(d->values,section,choice_key,&choices);
    if(s==SC_UTIL_E_NOT_FOUND)choices=*initial;
    else if(s<0)return ui_data_status(s);
    if(choices.type!=SC_VALUE_UTF8)return SC_UI_E_INVALID;
    ScModel *model=0;
    s=ScModelCreate(&model);
    ScU32 first=0,selected=0;
    for(ScU32 start=0,i=0;s==0 && i<=choices.bytes;i++)if(i==choices.bytes || ((const char *)choices.data)[i]==';'){
        ScU32 id=0;
        s=ScModelInsert(model,0,0,&id);
        ScValue value={sizeof(value),1,SC_VALUE_UTF8,0,0,0,(const char *)choices.data+start,i-start};
        if(s==0)s=ScModelSet(model,id,"label",&value);
        if(!first)first=id;
        if(initial->type==SC_VALUE_UTF8 && initial->bytes==value.bytes && !memcmp(initial->data,value.data,value.bytes))selected=id;
        start=i+1;
    }
    if(s==0)s=ScUiProviderFromModel(model,&field->options);
    if(s==0)s=ScUiSelectionCreate(field->options,0,&field->selection);
    if(s==0)s=ScUiViewBind(d->context,field->node,field->options,field->selection);
    if(s==0)s=ScUiSelectionSet(field->selection,selected?selected:first,SC_UI_REPLACE);
    ScModelRelease(model);
    return ui_data_status(s);
}
ScStatus ScUiPropertySheet(ScUiContext *context,const ScUiPropertySheetDesc *desc,const ScUiFieldDesc *fields,ScU32 count,ScUiDialog **out)
{
    ScStatus s=ui_input(desc,sizeof(*desc));
    ScUiDialog *d=0;
    ScHash *pages=0;
    ScU32 bytes;
    ScUiNode page=0;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(desc->flags || !ui_data_reserved(desc->reserved,2) || (!fields && count))return SC_UI_E_INVALID;
    for(ScU32 i=0;i<count;i++){s=ui_input(fields+i,sizeof(*fields));
        if(s<0)return s;
        if(fields[i].flags&~3u || !fields[i].field || !fields[i].section || !fields[i].key || !*fields[i].key || !ui_data_reserved(fields[i].reserved,2)
            || (fields[i].kind!=SC_UI_TEXTBOX && fields[i].kind!=SC_UI_CHECKBOX && fields[i].kind!=SC_UI_RADIO && fields[i].kind!=SC_UI_COMBOBOX && fields[i].kind!=SC_UI_SPINBOX)
            || fields[i].minimum>fields[i].maximum)return SC_UI_E_INVALID;
        for(ScU32 j=0;j<i;j++)if(fields[j].field==fields[i].field)return SC_UI_E_EXISTS;
    }
    if(!ui_multiply(count,sizeof(UiDialogField),&bytes))return SC_UI_E_RANGE;
    s=ui_dialog_new(context,desc->owner,UI_DIALOG_PROPERTY,0,&desc->title,560,390,&d);
    if(s<0)return s;
    if(bytes){d->fields=(UiDialogField *)calloc(1,bytes);
        if(!d->fields){s=SC_UI_E_MEMORY;
            goto done;
        }
    }
    d->field_count=count;
    s=ui_dialog_config_copy(desc->initial,&d->values);
    if(s==0)s=ui_hash_new(4,&pages);
    if(s==0)s=ui_dialog_widget(d,d->root,SC_UI_TAB,0,0,&d->tabs);
    if(s==0)ui_dialog_layout(d,d->tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,0);
    for(ScU32 i=0;s==0 && i<count;i++){
        UiDialogField *field=d->fields+i;
        field->description=fields[i];
        s=ScStringCreate(fields[i].section,(ScU32)strlen(fields[i].section),&field->section);
        if(s==0)s=ScStringCreate(fields[i].key,(ScU32)strlen(fields[i].key),&field->key);
        if(s==0)s=ui_text_copy(&fields[i].label,&field->label);
        if(s<0)break;
        ScHashEntry entry;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        if(ScHashGet(pages,&fields[i].page,4,&entry)==0)page=*(const ScUiNode *)entry.value;
        else{char title[32];
            snprintf(title,sizeof(title),"设置 %u",fields[i].page+1);
            ScUiText caption=ui_dialog_text(title);
            s=ui_dialog_widget(d,d->tabs,SC_UI_PROPERTY_PAGE,0,0,&page);
            if(s==0){ui_dialog_layout(d,page,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0);
                s=ScUiTabAdd(context,d->tabs,page,&caption);
            }
            if(s==0)s=ScHashSet(pages,&fields[i].page,4,&page,0);
            if(s<0)break;
        }
        ScUiNode row,label;
        ScUiText caption;
        ui_text_view(field->label,&caption);
        if(fields[i].kind==SC_UI_RADIO){s=ui_dialog_widget(d,page,SC_UI_RADIO,&caption,0,&field->node);
        }
        else{s=ui_dialog_widget(d,page,SC_UI_PANEL,0,0,&row);
            if(s==0){ui_dialog_layout(d,row,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
                s=ui_dialog_widget(d,row,SC_UI_LABEL,&caption,0,&label);
            }
            if(s==0){ui_dialog_layout(d,label,SC_UI_ABSOLUTE,140,SC_UI_AUTO,0,0);
                s=ui_dialog_widget(d,row,fields[i].kind,0,0,&field->node);
            }
            if(s==0){ui_dialog_layout(d,field->node,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
                ScUiLabelAssociate(context,label,field->node);
            }
        }
        if(s<0)break;
        ScValue initial;
        memset(&initial,0,sizeof(initial));
        initial.size=sizeof(initial);
        initial.version=1;
        s=ScConfigGet(d->values,fields[i].section,fields[i].key,&initial);
        if(s==SC_UTIL_E_NOT_FOUND){s=0;
            initial.type=fields[i].kind==SC_UI_TEXTBOX || fields[i].kind==SC_UI_COMBOBOX?SC_VALUE_UTF8:fields[i].kind==SC_UI_SPINBOX?SC_VALUE_I32:SC_VALUE_BOOL;
            initial.data=initial.type==SC_VALUE_UTF8?"":0;
            initial.lo=fields[i].kind==SC_UI_SPINBOX?(ScU32)fields[i].minimum:0;
        }
        if(s<0)break;
        if(fields[i].kind==SC_UI_COMBOBOX)s=ui_dialog_combo_values(d,field,&initial,fields[i].section,fields[i].key);
        else if(initial.type==SC_VALUE_UTF8){ScUiText value={sizeof(value),1,(const char *)initial.data,initial.bytes};
            s=ScUiNodeSetText(context,field->node,&value);
        }
        else{if(fields[i].kind==SC_UI_SPINBOX)s=ScUiSpinRange(context,field->node,fields[i].minimum,fields[i].maximum,1);
            if(s==0)s=ScUiNodeSetValue(context,field->node,&initial);
        }
        if(s==0 && (fields[i].flags&SC_UI_FIELD_READONLY))ScUiNodeSetState(context,field->node,SC_UI_READONLY,SC_UI_READONLY);
    }
    if(s==0){ScUiText empty=ui_dialog_text("");
        s=ui_dialog_widget(d,d->root,SC_UI_LABEL,&empty,0,&d->status);
    }
    if(s==0){ScUiMenuItem buttons[3];
        memset(buttons,0,sizeof(buttons));
        for(ScU32 i=0;i<3;i++){buttons[i].size=sizeof(buttons[i]);
            buttons[i].version=1;
            buttons[i].command=i+1;
        }
        buttons[0].label=ui_dialog_text("确定");
        buttons[1].label=ui_dialog_text("取消");
        buttons[2].label=ui_dialog_text("应用");
        s=ui_dialog_buttons(d,buttons,3);
    }
    if(s==0)s=ui_dialog_callbacks(d,desc->validate,desc->apply,desc->completion,desc->context,desc->context_drop);
    if(s==0)s=ScUiLayout(context,d->root);
    done:ScHashRelease(pages);
    if(s<0){ScUiDialogRelease(d);
        return s;
    }
    d->stage=SC_UI_DIALOG_OPEN;
    d->committed=1;
    *out=d;
    return 0;
}
static ScStatus ui_dialog_command(ScUiDialog *d,ScU32 command,int live)
{
    if(!command)return SC_UI_E_INVALID;
    if(d->type==UI_DIALOG_FILE)return command==SC_UI_DIALOG_OK || command==SC_UI_DIALOG_CANCEL?0:SC_UI_E_INVALID;
    if(d->type==UI_DIALOG_PROPERTY)return command==SC_UI_DIALOG_OK || command==SC_UI_DIALOG_CANCEL || command==SC_UI_DIALOG_APPLY?0:SC_UI_E_INVALID;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus status=ScArrayQuery(d->buttons,&info);
    if(status<0)return ui_data_status(status);
    for(ScU32 i=0;i<info.length;i++){UiDialogButton button;
        status=ScArrayGet(d->buttons,i,&button);
        if(status<0)return ui_data_status(status);
        if(button.command!=command)continue;
        if(!live)return 0;
        UiNode *node=ui_node(d->context,button.node);
        if(!node || node->kind!=SC_UI_BUTTON || node->root!=d->root || node->command!=command)return SC_UI_E_STALE;
        if((node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED))return SC_UI_E_CANCELLED;
        return 0;
    }
    return SC_UI_E_INVALID;
}
ScStatus ScUiDialogClose(ScUiDialog *d,ScU32 command)
{
    ScStatus s=ui_object_enter((UiObject *)d,UI_DIALOG);
    if(s<0)return s;
    s=ui_dialog_command(d,command,!d->completed);
    if(s<0){ui_object_leave(&d->object);
        return s;
        }
    if(d->stage==SC_UI_DIALOG_VALIDATING)s=SC_UI_E_BUSY;
    else if(d->completed)s=0;
    else if(command==d->cancel_command)s=ui_dialog_finish(d,command,1);
    else if(d->type==UI_DIALOG_FILE && command==SC_UI_DIALOG_OK)s=ui_dialog_file_accept(d);
    else if(d->type==UI_DIALOG_PROPERTY && (command==SC_UI_DIALOG_OK || command==SC_UI_DIALOG_APPLY))s=ui_dialog_property_apply(d,command==SC_UI_DIALOG_OK);
    else s=ui_dialog_finish(d,command,0);
    ui_object_leave(&d->object);
    return s;
}
static ScStatus ui_dialog_event(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    ScUiDialog *d=(ScUiDialog *)opaque;
    ScStatus s=0;
    if(d->completed || d->object.closing)return 0;
    d->object.depth++;
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST){s=ScUiDialogClose(d,d->cancel_command);
        if(s<0 && d->root)ui_dialog_status(d,"取消动作当前不可用",s);
        s=SC_UI_VETO;
    }
    else if(event->type==SC_UI_EVENT_KEY_DOWN && event->key==27){s=ScUiDialogClose(d,d->cancel_command);
        if(s>=0)s=SC_UI_HANDLED;
    }
    else if(event->type==SC_UI_EVENT_TIMER && event->command==d->timer)s=ui_dialog_directory_step(d);
    else if(event->type==SC_UI_EVENT_COMMAND){
        if(d->type==UI_DIALOG_FILE && (event->command==UI_DIALOG_UP || event->command==UI_DIALOG_FOLDER)){
            ScString *name=0,*path=0;
            const char *z=0;
            s=event->command==UI_DIALOG_UP?ui_dialog_join(d,"..",&path):ScUiNodeCopyText(context,d->name_edit,&name);
            if(s==0 && name)s=ScStringCStr(name,&z);
            if(s==0 && event->command==UI_DIALOG_FOLDER){if(!z || !*z || strchr(z,'/') || strchr(z,'\\') || !strcmp(z,".") || !strcmp(z,".."))s=SC_UI_E_INVALID;
                else s=ui_dialog_join(d,z,&path);
            }
            if(s==0)s=ScStringCStr(path,&z);
            if(s==0 && event->command==UI_DIALOG_FOLDER)s=ScPathCreate(z,SC32_PATH_DIRECTORY,3);
            if(s==0){if(event->command==UI_DIALOG_FOLDER)ScStringCStr(d->directory,&z);
                s=ui_dialog_directory(d,z);
            }
            ScStringRelease(name);
            ScStringRelease(path);
        }
        else if(d->type==UI_DIALOG_FILE && event->command==UI_DIALOG_FILTER){ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            s=ScUiMenuQuery(d->filters,&info);
            if(s==0 && info.length){d->filter=d->filter%info.length+1;
                const char *z;
                ScStringCStr(d->directory,&z);
                s=ui_dialog_directory(d,z);
            }
        }
        else if(d->type==UI_DIALOG_FILE && event->command==UI_DIALOG_SORT)s=ui_dialog_sort(d);
        else if(ui_dialog_command(d,event->command,0)==0)s=ScUiDialogClose(d,event->command);
        else{ui_object_leave(&d->object);
            return 0;
            }
        if(s>=0)s=SC_UI_HANDLED;
    }
    else if(d->type==UI_DIALOG_FILE && event->type==SC_UI_EVENT_KEY_DOWN && event->key=='\n'){
        if(event->node==d->path_edit){ScString *path=0;
            const char *z;
            s=ScUiNodeCopyText(context,d->path_edit,&path);
            if(s==0)s=ScStringCStr(path,&z);
            if(s==0)s=ui_dialog_directory(d,z);
            ScStringRelease(path);
        }
        else s=ScUiDialogClose(d,SC_UI_DIALOG_OK);
        if(s>=0)s=SC_UI_HANDLED;
    }
    if(s<0 && d->root)ui_dialog_status(d,"操作未完成，请检查错误",s);
    ui_object_leave(&d->object);
    return s;
}
