#include "SCUI.H"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* 示例只使用正式系统库，模型/虚拟行/文档均是应用实际拥有的数据。 */
enum {DEMO_NEW=100,DEMO_OPEN,DEMO_SAVE,DEMO_UNDO,DEMO_REDO,DEMO_COPY,DEMO_CUT,DEMO_PASTE,
    DEMO_SELECT_ALL,DEMO_PROPERTIES,DEMO_MESSAGE,DEMO_LAYOUT_SAVE,DEMO_LAYOUT_RESTORE,DEMO_FLOAT,
    DEMO_CLASSIC,DEMO_AURORA,DEMO_STOP,DEMO_COMBO,DEMO_PROGRESS,DEMO_FIND,DEMO_REPLACE,
    DEMO_TAB_CONTROLS,DEMO_TAB_DATA,DEMO_TAB_EDITOR,DEMO_TAB_LAYOUT,DEMO_TAB_DIALOGS,DEMO_TAB_RESOURCES,DEMO_SNAPSHOT,
    DEMO_SCALE_100,DEMO_SCALE_150,DEMO_SCALE_200};
typedef struct {
    ScUiProvider *provider;
    ScU32 revision,count,*order,*rank,*work,*scratch,*candidate_rank,width,left,first,middle,last,a,b,out,sorting,descending,field,indexing,indexed;
    ScHash *edits;
    char text[64];
}
DemoRows;
typedef struct {ScUiNode node;
    ScU32 token,visibility_token;
    }
DemoRootObserver;
typedef struct {
    ScUiContext *ui;
    ScUiNode root,second,menu,toolbar,status,tree,tabs,pages[6],check,radio,spin,combo,progress,table,edit,second_edit,layout,dock,drag_source,second_exit;
    ScUiDocument *document;
    ScUiProvider *tree_provider,*combo_provider;
    ScUiSelection *tree_selection,*combo_selection,*table_selection;
    ScModel *tree_model,*combo_model;
    ScUiMenu *menus;
    ScUiIcon *icon;
    ScConfig *configuration;
    ScUiDialog *dialog;
    ScArray *root_observers;
    ScHash *root_observer_index;
    DemoRows rows;
    ScU32 sort_timer,progress_timer,drag_token;
    ScString *file;
    ScU32 commands,changes,drops,failures,snapshots;
    ScUiNode hidden_root;
    ScU32 hidden_timer,hidden_listener,hidden_phase,hidden_round;
}
Demo;
static Demo demo;
static void demo_snapshot(void);
static ScStatus demo_observe_roots(void);
static ScStatus demo_hidden_observe(void);

static ScUiText demo_text(const char *z){ScUiText t={sizeof(t),1,z,(ScU32)strlen(z)};
    return t;
}
static ScStatus demo_check(ScStatus status,const char *operation)
{
    if(status<0){ScUiErrorInfo error;
        memset(&error,0,sizeof(error));
        error.size=sizeof(error);
        error.version=1;
        if(demo.ui && ScUiErrorQuery(demo.ui,&error)==0)printf("SCUI ERROR op=%s status=%d cause=%d domain=%u raw=%d\n",operation,status,error.cause_code,error.domain,error.raw);
        else printf("SCUI ERROR op=%s status=%d\n",operation,status);
        demo.failures++;
    }
    return status;
}
static void demo_status(const char *message)
{
    if(!demo.status)return;
    char counts[96];
    snprintf(counts,sizeof(counts),"命令 %u · 修改 %u · 拖放 %u",demo.commands,demo.changes,demo.drops);
    ScUiStatusPart parts[2];
    memset(parts,0,sizeof(parts));
    for(ScU32 i=0;i<2;i++){parts[i].size=sizeof(parts[i]);
        parts[i].version=1;
        parts[i].part=i+1;
    }
    parts[0].width=560;
    parts[0].text=demo_text(message);
    parts[1].width=-1;
    parts[1].text=demo_text(counts);
    ScUiStatusSetParts(demo.ui,demo.status,parts,2);
}
static ScStatus demo_widget(ScU32 parent,ScU32 kind,const char *label,ScU32 command,ScU32 *out)
{
    ScUiWidgetDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=kind;
    d.parent=parent;
    d.text=demo_text(label);
    d.command=command;
    return demo_check(ScUiWidgetCreate(demo.ui,&d,out),"WidgetCreate");
}
static ScStatus demo_layout(ScU32 id,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex,ScU32 dock)
{
    ScUiLayoutDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.flex_weight=flex;
    d.dock=dock;
    d.gap=6;
    d.column_span=d.row_span=1;
    d.align_h=d.align_v=SC_UI_STRETCH;
    return demo_check(ScUiNodeSetLayout(demo.ui,id,&d),"NodeSetLayout");
}
static void demo_row_drop(void *item,void *context){(void)context;
    ScStringRelease(*(ScString **)item);
}
static ScU32 demo_row_at(DemoRows *rows,ScU32 index){return rows->order?rows->order[index]:index+1;
}
static void demo_row_label(ScU32 id,char *out,ScU32 bytes){snprintf(out,bytes,"Record %06u / 记录",id);
}
static ScStatus demo_row_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    DemoRows *rows=(DemoRows *)context;
    reply->revision=rows->revision;
    if(request->operation==SC_UI_DATA_COUNT){reply->total=rows->count;
        return 0;
    }
    if(request->operation==SC_UI_DATA_SORT){
        if(rows->sorting)return SC_UI_E_BUSY;
        ScU32 bytes=rows->count*4;
        ScU32 *work=(ScU32 *)malloc(bytes),*scratch=(ScU32 *)malloc(bytes),*rank=(ScU32 *)malloc(bytes+4);
        if(!work || !scratch || !rank){free(work);
            free(scratch);
            free(rank);
            return SC_UI_E_MEMORY;
        }
        for(ScU32 i=0;i<rows->count;i++)work[i]=demo_row_at(rows,i);
        rows->work=work;
        rows->scratch=scratch;
        rows->candidate_rank=rank;
        rows->indexing=rows->indexed=0;
        rows->width=1;
        rows->left=0;
        rows->a=rows->b=rows->out=0;
        rows->sorting=1;
        rows->descending=request->flags&1;
        rows->field=request->field;
        ScUiTimerDesc timer;
        memset(&timer,0,sizeof(timer));
        timer.size=sizeof(timer);
        timer.version=1;
        timer.node=demo.root;
        timer.flags=SC_UI_TIMER_BACKGROUND;
        ScMonotonicClock clock;
        memset(&clock,0,sizeof(clock));
        clock.size=sizeof(clock);
        clock.version=1;
        ScSystemMonotonic(&clock);
        timer.deadline=clock.ticks;
        timer.deadline.lo++;
        timer.period_ticks=1;
        ScStatus s=ScUiTimerSet(demo.ui,&timer,&demo.sort_timer);
        if(s<0){free(work);
            free(scratch);
            free(rank);
            rows->work=rows->scratch=rows->candidate_rank=0;
            rows->sorting=0;
            return s;
        }
        demo_status("排序中；输入与窗口仍可使用");
        return SC_UI_PENDING;
    }
    if(request->operation==SC_UI_DATA_FIND_PREFIX){
        if(reply->value.type!=SC_VALUE_UTF8)return SC_UI_E_INVALID;
        for(ScU32 step=0;step<rows->count;step++){ScU32 index=request->index<rows->count?(request->index+step)%rows->count:step,id=demo_row_at(rows,index);
            char label[64];
            demo_row_label(id,label,sizeof(label));
            if(reply->value.bytes<sizeof(label) && !memcmp(label,reply->value.data,reply->value.bytes)){reply->item=id;
                reply->total=index;
                reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
                return 0;
            }
            if((step&4095u)==4095u)ScProcessYield();
        }
        return SC_UI_E_NOT_FOUND;
    }
    ScU32 id=request->operation==SC_UI_DATA_CHILD_AT?(request->index<rows->count?demo_row_at(rows,request->index):0):request->item;
    if(!id || id>rows->count)return request->operation==SC_UI_DATA_CHILD_AT?SC_UI_END:SC_UI_E_STALE;
    reply->item=id;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){reply->total=rows->rank?rows->rank[id]:id-1;
        return 0;
    }
    if(request->operation==SC_UI_DATA_COMMIT_EDIT){
        if(rows->sorting)return SC_UI_E_BUSY;
        if(request->field!=0 || reply->value.type!=SC_VALUE_UTF8 || reply->value.bytes>63)return SC_UI_E_INVALID;
        ScString *candidate=0;
        ScStatus s=ScStringCreate(reply->value.data,reply->value.bytes,&candidate);
        if(s<0)return SC_UI_E_MEMORY;
        s=ScHashSet(rows->edits,&id,4,&candidate,0);
        if(s<0){ScStringRelease(candidate);
            return SC_UI_E_MEMORY;
        }
        rows->revision++;
        ScUiDataChange change;
        memset(&change,0,sizeof(change));
        change.size=sizeof(change);
        change.version=1;
        change.kind=SC_UI_DATA_UPDATE;
        change.revision=rows->revision;
        change.item=id;
        change.field=0;
        ScUiProviderNotify(rows->provider,&change);
        reply->revision=rows->revision;
    }
    if(request->field==0){ScHashEntry entry;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        if(ScHashGet(rows->edits,&id,4,&entry)==0){ScView view;
            memset(&view,0,sizeof(view));
            view.size=sizeof(view);
            view.version=1;
            ScStringView(*(ScString *const *)entry.value,&view);
            reply->value.data=view.data;
            reply->value.bytes=view.bytes;
        }
        else{demo_row_label(id,rows->text,sizeof(rows->text));
            reply->value.data=rows->text;
            reply->value.bytes=(ScU32)strlen(rows->text);
        }
        reply->value.type=SC_VALUE_UTF8;
    }
    else if(request->field==1){reply->value.type=SC_VALUE_U32;
        reply->value.lo=id*1024;
    }
    else if(request->field==2){reply->value.type=SC_VALUE_I32;
        reply->value.lo=(ScU32)((ScI32)(id%2001)-1000);
    }
    else if(request->field==3){reply->value.type=SC_VALUE_COUNTER;
        reply->value.lo=id*100;
        reply->value.hi=id/10000;
    }
    else{reply->value.type=SC_VALUE_U32;
        reply->value.lo=id%7;
    }
    return 0;
}
static int demo_compare(ScU32 a,ScU32 b)
{
    DemoRows *rows=&demo.rows;
    int answer;
    if(rows->field==2){ScI32 x=(ScI32)(a%2001)-1000,y=(ScI32)(b%2001)-1000;
        answer=(x>y)-(x<y);
    }
    else if(rows->field==4)answer=(a%7>b%7)-(a%7<b%7);
    else answer=(a>b)-(a<b);
    return rows->descending?-answer:answer;
}
static void demo_sort_step(void)
{
    DemoRows *r=&demo.rows;
    ScU32 budget=4096;
    if(!r->sorting)return;
    while(budget && r->width<r->count){
        if(r->out==0){r->first=r->left;
            r->middle=r->left+r->width;
            if(r->middle>r->count)r->middle=r->count;
            r->last=r->middle+r->width;
            if(r->last>r->count)r->last=r->count;
            r->a=r->first;
            r->b=r->middle;
            r->out=r->first;
        }
        while(budget && r->out<r->last){if(r->a<r->middle && (r->b>=r->last || demo_compare(r->work[r->a],r->work[r->b])<=0))r->scratch[r->out++]=r->work[r->a++];
            else r->scratch[r->out++]=r->work[r->b++];
            budget--;
        }
        if(r->out==r->last){r->left=r->last;
            r->out=0;
            if(r->left==r->count){ScU32 *swap=r->work;
                r->work=r->scratch;
                r->scratch=swap;
                r->left=0;
                r->width*=2;
            }
        }
    }
    if(r->width>=r->count){r->indexing=1;
        while(budget && r->indexed<r->count){r->candidate_rank[r->work[r->indexed]]=r->indexed;
            r->indexed++;
            budget--;
        }
        if(r->indexed<r->count)return;
        ScU32 *old=r->order,*old_rank=r->rank;
        r->order=r->work;
        r->rank=r->candidate_rank;
        r->work=r->candidate_rank=0;
        free(old);
        free(old_rank);
        free(r->scratch);
        r->scratch=0;
        r->sorting=0;
        r->revision++;
        ScUiTimerCancel(demo.ui,demo.sort_timer);
        demo.sort_timer=0;
        ScUiDataChange change;
        memset(&change,0,sizeof(change));
        change.size=sizeof(change);
        change.version=1;
        change.kind=SC_UI_DATA_SORTED;
        change.revision=r->revision;
        demo_check(ScUiProviderNotify(r->provider,&change),"ProviderNotifySorted");
        demo_status("100000 行排序已发布；选中项仍使用稳定 ID");
        printf("SCUI SORT rows=%u revision=%u\n",r->count,r->revision);
    }
}

static ScStatus demo_load(const char *path)
{
    FILE *file=fopen(path,"rb");
    if(!file)return SC_UI_E_IO;
    if(fseek(file,0,SEEK_END) || ftell(file)<0){fclose(file);
        return SC_UI_E_IO;
    }
    long bytes=ftell(file);
    rewind(file);
    char *body=(char *)malloc(bytes?(size_t)bytes:1);
    if(!body){fclose(file);
        return SC_UI_E_MEMORY;
    }
    ScStatus s=fread(body,1,(size_t)bytes,file)==(size_t)bytes?0:SC_UI_E_IO;
    int closed=fclose(file);
    if(s==0 && closed)s=SC_UI_E_IO;
    if(s==0){ScUiText text={sizeof(text),1,body,(ScU32)bytes};
        ScUiDocumentDesc d;
        memset(&d,0,sizeof(d));
        d.size=sizeof(d);
        d.version=1;
        d.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
        d.text=text;
        ScUiDocument *candidate=0;
        ScString *name=0;
        s=ScUiDocumentCreate(&d,&candidate);
        if(s==0)s=ScStringCreate(path,(ScU32)strlen(path),&name);
        if(s==0)s=ScUiEditBind(demo.ui,demo.edit,candidate);
        if(s==0 && demo.second_edit){ScUiNodeInfo node;
            memset(&node,0,sizeof(node));
            node.size=sizeof(node);
            node.version=1;
            if(ScUiNodeQuery(demo.ui,demo.second_edit,&node)==0)s=ScUiEditBind(demo.ui,demo.second_edit,candidate);
        }
        if(s==0){ScUiDocumentRelease(demo.document);
            demo.document=candidate;
            candidate=0;
            ScStringRelease(demo.file);
            demo.file=name;
            name=0;
        }
        ScUiDocumentRelease(candidate);
        ScStringRelease(name);
    }
    free(body);
    if(s==0){ScUiTabSelect(demo.ui,demo.tabs,demo.pages[2]);
        demo_status("文件已读取；原 UTF8 / CRLF 字节保留");
    }
    return s;
}
static ScStatus demo_save(const char *path)
{
    ScUiDocumentInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScUiDocumentQuery(demo.document,&info);
    ScString *text=0;
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(s==0)s=ScUiDocumentCopy(demo.document,0,info.bytes,&text);
    if(s==0)s=ScStringView(text,&view);
    FILE *file=0;
    if(s==0){file=fopen(path,"wb");
        if(!file)s=SC_UI_E_IO;
    }
    if(s==0 && fwrite(view.data,1,view.bytes,file)!=view.bytes)s=SC_UI_E_IO;
    if(file && fclose(file) && s==0)s=SC_UI_E_IO;
    ScStringRelease(text);
    if(s==0){s=ScUiDocumentMarkSaved(demo.document);
        demo_status("文件已 flush 并关闭；保存点已更新");
        printf("SCUI SAVED bytes=%u path=%s\n",info.bytes,path);
    }
    return s;
}
static ScStatus demo_dialog_done(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    Demo *d=(Demo *)opaque;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s<0)return s;
    if(info.stage==SC_UI_DIALOG_DONE && event->command==SC_UI_DIALOG_OK){
        ScString *path=0;
        const char *z;
        ScStatus selected=ScUiDialogSelectedPath(dialog,0,&path);
        if(selected==0){ScStringCStr(path,&z);
            s=(info.flags&SC_UI_FILE_SAVE)?demo_save(z):demo_load(z);
            ScStringRelease(path);
        }
        else{ScConfig *values=0;
            if(ScUiDialogCopyValues(dialog,&values)==0){ScConfigRelease(d->configuration);
                d->configuration=values;
            }
        }
    }
    printf("SCUI DIALOG stage=%u command=%u selected=%u error=%d\n",info.stage,event->command,info.selected_count,info.error);
    if(d->dialog==dialog)d->dialog=0;
    ScUiDialogRelease(dialog);
    demo_check(s,"DialogCompletion");
    (void)context;
    return 0;
}
static ScStatus demo_property_validate(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    (void)context;
    (void)opaque;
    ScConfig *values=(ScConfig *)event->data;
    ScValue name;
    memset(&name,0,sizeof(name));
    name.size=sizeof(name);
    name.version=1;
    ScStatus s=ScConfigGet(values,"profile","name",&name);
    if(s<0 || name.type!=SC_VALUE_UTF8 || !name.bytes)return SC_UI_E_INVALID;
    printf("SCUI VALIDATE name_bytes=%u\n",name.bytes);
    return 0;
}
static ScStatus demo_property_apply(ScUiContext *context,const ScUiEvent *event,void *opaque)
{(void)context;
    (void)opaque;
    (void)event;
    printf("SCUI APPLY accepted=1\n");
    return 0;
}
static ScStatus demo_property(void)
{
    ScUiPropertySheetDesc d;
    ScUiFieldDesc fields[5];
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=demo.root;
    d.title=demo_text("属性 / Profile");
    d.initial=demo.configuration;
    d.validate=demo_property_validate;
    d.apply=demo_property_apply;
    d.completion=demo_dialog_done;
    d.context=&demo;
    memset(fields,0,sizeof(fields));
    const char *names[5]={"name","enabled","level","mode","description"};
    const char *labels[5]={"名称（必填）","启用","等级","模式","说明"};
    ScU32 kinds[5]={SC_UI_TEXTBOX,SC_UI_CHECKBOX,SC_UI_SPINBOX,SC_UI_COMBOBOX,SC_UI_TEXTBOX};
    for(ScU32 i=0;i<5;i++){fields[i].size=sizeof(fields[i]);
        fields[i].version=1;
        fields[i].field=i+1;
        fields[i].kind=kinds[i];
        fields[i].page=i==4?1:0;
        fields[i].section="profile";
        fields[i].key=names[i];
        fields[i].label=demo_text(labels[i]);
        fields[i].maximum=100;
    }
    fields[0].flags=SC_UI_FIELD_REQUIRED;
    fields[2].minimum=1;
    fields[4].flags=SC_UI_FIELD_READONLY;
    return ScUiPropertySheet(demo.ui,&d,fields,5,&demo.dialog);
}
static ScStatus demo_file_dialog(ScU32 flags)
{
    ScUiFileDialogDesc d;
    ScUiMenuItem filters[2];
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=demo.root;
    d.flags=flags;
    d.title=demo_text((flags&SC_UI_FILE_SAVE)?"保存文档":"打开文档");
    d.directory="/HOME";
    d.completion=demo_dialog_done;
    d.context=&demo;
    memset(filters,0,sizeof(filters));
    for(ScU32 i=0;i<2;i++){filters[i].size=sizeof(filters[i]);
        filters[i].version=1;
        filters[i].command=i+1;
    }
    filters[0].label=demo_text("文本与源码");
    filters[0].help=demo_text("*.TXT;*.C;*.H;*.txt;*.c;*.h");
    filters[1].label=demo_text("全部文件");
    filters[1].help=demo_text("*");
    d.filters=filters;
    d.filter_count=2;
    d.selected_filter=1;
    return ScUiFileDialog(demo.ui,&d,&demo.dialog);
}
static ScStatus demo_drop(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    Demo *d=(Demo *)opaque;
    ScUiDragData *data=(ScUiDragData *)event->data;
    if(event->type==SC_UI_EVENT_DRAG_ENTER || event->type==SC_UI_EVENT_DRAG_OVER){data->chosen_action=SC_UI_DRAG_COPY;
        return 0;
    }
    if(event->type==SC_UI_EVENT_DROP && data->phase==SC32_DRAG_TRANSFER){
        if(data->format!=SC32_CLIPBOARD_TEXT || !data->bytes || ((const char *)data->data)[data->bytes-1] || memchr(data->data,0,data->bytes-1))return SC_UI_E_INVALID;
        ScUiText text={sizeof(text),1,(const char *)data->data,data->bytes-1};
        ScUiTextSelection selection;
        memset(&selection,0,sizeof(selection));
        selection.size=sizeof(selection);
        selection.version=1;
        ScStatus s=ScUiEditGetSelection(context,d->edit,&selection);
        if(s==0){ScU32 first=selection.anchor<selection.caret?selection.anchor:selection.caret,last=selection.anchor>selection.caret?selection.anchor:selection.caret;
            s=ScUiDocumentReplace(d->document,first,last-first,&text,SC_UI_REPLACE_NEW_UNDO);
        }
        if(s==0){d->drops++;
            demo_status("跨窗口 Drop 已提交到 Document；返回真实 ACK");
            printf("SCUI DROP bytes=%u sender=%u/%u\n",data->bytes,data->sender.pid,data->sender.generation);
        }
        return s;
    }
    return 0;
}
static ScStatus demo_drag(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    Demo *d=(Demo *)opaque;
    if(event->type==SC_UI_EVENT_POINTER_DOWN && event->message && (event->message->data[3]&1u) && (event->buttons&1)){
        const char text[]="Dragged through the real broker / 拖放\n";
        ScU32 total=sizeof(ScDragOffer)+sizeof(ScDragFormat)+sizeof(text);
        unsigned char *body=(unsigned char *)malloc(total);
        if(!body)return SC_UI_E_MEMORY;
        ScDragOffer offer={sizeof(offer),1,1,total};
        ScDragFormat format={SC32_CLIPBOARD_TEXT,sizeof(offer)+sizeof(format),sizeof(text),0};
        memcpy(body,&offer,sizeof(offer));
        memcpy(body+sizeof(offer),&format,sizeof(format));
        memcpy(body+format.offset,text,sizeof(text));
        ScUiDragDesc desc;
        memset(&desc,0,sizeof(desc));
        desc.size=sizeof(desc);
        desc.version=1;
        desc.source=event->node;
        desc.actions=SC_UI_DRAG_COPY;
        desc.offer=body;
        desc.offer_bytes=total;
        desc.preview=d->icon;
        ScStatus s=ScUiDragBegin(context,&desc,&d->drag_token);
        free(body);
        return s<0?s:SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_DROP){const ScUiDragData *data=(const ScUiDragData *)event->data;
        if(data->phase==SC32_DRAG_COMPLETE){printf("SCUI DRAG COMPLETE action=%u sender=%u/%u\n",data->chosen_action,data->sender.pid,data->sender.generation);
            d->drag_token=0;
        }
    }
    return 0;
}
static ScStatus demo_display_scale(ScU32 scale)
{
    ScDisplayInfo display;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    ScStatus result=ScDisplayQuery(&display);
    if(result<0){printf("SCUI DISPLAY action=query requestedScale=%u status=%d\n",scale,result);
        return result;
        }
    /* 保留真实当前模式，只请求全局缩放；确认失败仍保留内核15秒安全回退。 */
    result=ScDisplayApply(display.width,display.height,scale,SC32_DISPLAY_PREVIEW);
    printf("SCUI DISPLAY action=preview requestedScale=%u w=%u h=%u status=%d\n",scale,display.width,display.height,result);
    if(result==0){result=ScDisplayApply(display.width,display.height,scale,SC32_DISPLAY_CONFIRM);
        printf("SCUI DISPLAY action=confirm requestedScale=%u status=%d\n",scale,result);
    }
    ScStatus observed=ScDisplayQuery(&display);
    if(observed==0)printf("SCUI DISPLAY action=observed scalePercent=%u w=%u h=%u generation=%u previewSeconds=%u status=0\n",display.scale_percent,display.width,display.height,display.generation,display.preview_seconds);
    else printf("SCUI DISPLAY action=observed status=%d\n",observed);
    if(result==0)result=observed;
    fflush(stdout);
    return result;
}
static ScStatus demo_command(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    Demo *d=(Demo *)opaque;
    ScStatus s=0;
    if(event->type==SC_UI_EVENT_TIMER && event->command==d->sort_timer){demo_sort_step();
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_VALUE_CHANGED || event->type==SC_UI_EVENT_SELECTION_CHANGED){d->changes++;
        demo_status("值与选区已更新");
        return 0;
    }
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST)return 0;
    if(event->type!=SC_UI_EVENT_COMMAND)return 0;
    d->commands++;
    switch(event->command){
    case DEMO_NEW:{ScUiText text=demo_text("");
            ScUiDocumentInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            s=ScUiDocumentQuery(d->document,&info);
            if(s==0)s=ScUiDocumentReplace(d->document,0,info.bytes,&text,SC_UI_REPLACE_NEW_UNDO);
            break;
        }
    case DEMO_OPEN:if(!d->dialog)s=demo_file_dialog(SC_UI_FILE_OPEN|SC_UI_FILE_MULTI);
        break;
    case DEMO_SAVE:if(!d->dialog)s=demo_file_dialog(SC_UI_FILE_SAVE);
        break;
    case DEMO_UNDO:s=ScUiDocumentUndo(d->document);
        break;
    case DEMO_REDO:s=ScUiDocumentRedo(d->document);
        break;
    case DEMO_COPY:s=ScUiEditCopy(context,d->edit);
        break;
    case DEMO_CUT:s=ScUiEditCut(context,d->edit);
        break;
    case DEMO_PASTE:s=ScUiEditPaste(context,d->edit);
        break;
    case DEMO_SELECT_ALL:s=ScUiEditSelectAll(context,d->edit);
        break;
    case DEMO_PROPERTIES:if(!d->dialog)s=demo_property();
        break;
    case DEMO_MESSAGE:if(!d->dialog){ScUiMessageBoxDesc message;
            memset(&message,0,sizeof(message));
            message.size=sizeof(message);
            message.version=1;
            message.owner=d->root;
            message.title=demo_text("SCUI 工作流");
            message.message=demo_text("这是同一控件框架的真实模态对话框。\nEnter 确定，Esc 取消，关闭不会结束 owner 进程。");
            message.completion=demo_dialog_done;
            message.context=d;
            s=ScUiMessageBox(context,&message,&d->dialog);
        }
        break;
    case DEMO_LAYOUT_SAVE:s=ScUiLayoutSave(context,d->root,d->configuration);
        demo_status("当前真实布局已保存到 Config");
        break;
    case DEMO_LAYOUT_RESTORE:s=ScUiLayoutRestore(context,d->root,d->configuration);
        demo_status("保存的布局已恢复");
        break;
    case DEMO_FLOAT:{ScUiWindowDesc window;
            ScU32 floating;
            memset(&window,0,sizeof(window));
            window.size=sizeof(window);
            window.version=1;
            window.title=demo_text("浮动 Dock");
            window.owner=d->root;
            window.x=240;
            window.y=160;
            window.client_width=260;
            window.client_height=180;
            window.style=SC32_STYLE_STANDARD;
            s=ScUiDockFloat(context,d->dock,&window,&floating);
            break;
        }
    case DEMO_CLASSIC:case DEMO_AURORA:s=ScThemeApply(event->command==DEMO_CLASSIC?"/SYS/THEMES/CLASSIC.CFG":"/SYS/THEMES/AURORA.CFG",SC32_THEME_TEMPORARY);
        break;
    case DEMO_STOP:ScUiContextStop(context,0);
        break;
    case DEMO_SNAPSHOT:demo_snapshot();
        break;
    case DEMO_SCALE_100:case DEMO_SCALE_150:case DEMO_SCALE_200:
        s=demo_display_scale(event->command==DEMO_SCALE_100?100:event->command==DEMO_SCALE_150?150:200);
        break;
    case DEMO_COMBO:s=ScUiComboOpen(context,d->combo,1);
        break;
    case DEMO_PROGRESS:{static ScU32 busy;
            busy=!busy;
            s=ScUiProgressSet(context,d->progress,25,100,busy?SC_UI_PROGRESS_INDETERMINATE:SC_UI_PROGRESS_DETERMINATE);
            break;
        }
    case DEMO_FIND:{ScUiFindDesc find;
            ScUiTextSelection selection;
            memset(&find,0,sizeof(find));
            find.size=sizeof(find);
            find.version=1;
            ScUiDocumentInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            ScUiDocumentQuery(d->document,&info);
            find.end=info.bytes;
            find.needle=demo_text("UTF8");
            memset(&selection,0,sizeof(selection));
            selection.size=sizeof(selection);
            selection.version=1;
            s=ScUiDocumentFind(d->document,&find,&selection);
            if(s==0)s=ScUiEditSetSelection(context,d->edit,&selection);
            break;
        }
    case DEMO_REPLACE:{ScUiFindDesc find;
            ScU32 changed;
            memset(&find,0,sizeof(find));
            find.size=sizeof(find);
            find.version=1;
            ScUiDocumentInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            ScUiDocumentQuery(d->document,&info);
            find.end=info.bytes;
            find.needle=demo_text("UTF8");
            ScUiText text=demo_text("UTF-8");
            s=ScUiDocumentReplaceAll(d->document,&find,&text,&changed);
            if(s==0)printf("SCUI REPLACE count=%u\n",changed);
            break;
        }
    default:if(event->command>=DEMO_TAB_CONTROLS && event->command<=DEMO_TAB_RESOURCES)s=ScUiTabSelect(context,d->tabs,d->pages[event->command-DEMO_TAB_CONTROLS]);
        break;
    }
    demo_check(s,"Command");
    printf("SCUI COMMAND id=%u result=%d\n",event->command,s);
    return SC_UI_HANDLED;
}

static ScStatus demo_listen(ScUiNode node,ScUiEventProc procedure,ScU32 phases)
{
    ScUiSubscription s;
    ScU32 token;
    memset(&s,0,sizeof(s));
    s.size=sizeof(s);
    s.version=1;
    s.phases=phases;
    s.procedure=procedure;
    s.context=&demo;
    return demo_check(ScUiListen(demo.ui,node,&s,&token),"Listen");
}
static ScStatus demo_menu(void)
{
    static const char *labels[5]={"&File","&Edit","&View","&Tools","&Help"};
    ScUiMenu *sub[5]={0,0,0,0,0};
    ScStatus s=ScUiMenuCreate(&demo.menus);
    const char *captions[31]={"New","Open…","Save…","Undo","Redo","Copy","Cut","Paste","Select all","Properties…","Message…","Save layout","Restore layout","Float dock","Classic","Aurora","Exit","Open combo","Progress","Find UTF8","Replace UTF8","Controls","Data","Editor","Layout","Dialogs","Resources","Snapshot","Scale 100%","Scale 150%","Scale 200%"};
    for(ScU32 i=0;s==0 && i<5;i++)s=ScUiMenuCreate(sub+i);
    for(ScU32 i=0;s==0 && i<31;i++){
        ScUiMenuItem item;
        ScU32 inserted;
        memset(&item,0,sizeof(item));
        item.size=sizeof(item);
        item.version=1;
        item.command=DEMO_NEW+i;
        item.label=demo_text(captions[i]);
        item.help=demo_text(captions[i]);
        item.shortcut.size=sizeof(item.shortcut);
        item.shortcut.version=1;
        if(i<9){const char keys[9]={'n','o','s','z','y','c','x','v','a'};
            item.shortcut.key=(ScU32)keys[i];
            item.shortcut.modifiers=SC32_MOD_CONTROL;
        }
        if(i>=21 && i<=27){item.shortcut.key=SC32_KEY_F1+5+(i-21);
            item.shortcut.modifiers=0;
            }
        ScU32 group=i<=2 || i==16?0:i<=8 || i==19 || i==20?1:i>=21 || i==14 || i==15?2:i>=9 && i<=13?3:4;
        s=ScUiMenuInsert(sub[group],0,&item,&inserted);
        ScUiCommandDesc command;
        memset(&command,0,sizeof(command));
        command.size=sizeof(command);
        command.version=1;
        command.command=item.command;
        command.label=item.label;
        command.shortcut=item.shortcut;
        command.icon=demo.icon;
        if(s==0)s=ScUiCommandSet(demo.ui,&command);
    }
    for(ScU32 i=0;s==0 && i<5;i++){ScUiMenuItem item;
        ScU32 id;
        memset(&item,0,sizeof(item));
        item.size=sizeof(item);
        item.version=1;
        item.label=demo_text(labels[i]);
        item.submenu=sub[i];
        s=ScUiMenuInsert(demo.menus,0,&item,&id);
    }
    if(s==0)s=ScUiMenuBind(demo.ui,demo.menu,demo.menus);
    ScUiMenu *tools=0;
    if(s==0)s=ScUiMenuCreate(&tools);
    const ScU32 commands[8]={DEMO_NEW,DEMO_OPEN,DEMO_SAVE,DEMO_UNDO,DEMO_REDO,DEMO_COPY,DEMO_PASTE,DEMO_PROPERTIES};
    for(ScU32 i=0;s==0 && i<8;i++){ScUiMenuItem item;
        ScU32 id;
        memset(&item,0,sizeof(item));
        item.size=sizeof(item);
        item.version=1;
        item.command=commands[i];
        item.label=demo_text(captions[commands[i]-DEMO_NEW]);
        item.icon=demo.icon;
        s=ScUiMenuInsert(tools,0,&item,&id);
    }
    if(s==0)s=ScUiToolbarBind(demo.ui,demo.toolbar,tools);
    ScUiMenuRelease(tools);
    for(ScU32 i=0;i<5;i++)ScUiMenuRelease(sub[i]);
    return demo_check(s,"Menu/Command/Toolbar");
}
static ScStatus demo_model(void)
{
    ScStatus s=ScModelCreate(&demo.tree_model);
    const char *names[6]={"Controls","Data","Editor","Layout","Dialogs","Resources"};
    for(ScU32 i=0;s==0 && i<6;i++){ScU32 id,child;
        ScValue text={sizeof(text),1,SC_VALUE_UTF8,0,0,0,names[i],(ScU32)strlen(names[i])};
        s=ScModelInsert(demo.tree_model,0,0,&id);
        if(s==0)s=ScModelSet(demo.tree_model,id,"label",&text);
        if(s==0)s=ScModelInsert(demo.tree_model,id,0,&child);
        text.data="Keyboard + pointer";
        text.bytes=18;
        if(s==0)s=ScModelSet(demo.tree_model,child,"label",&text);
    }
    if(s==0)s=ScUiProviderFromModel(demo.tree_model,&demo.tree_provider);
    if(s==0)s=ScUiSelectionCreate(demo.tree_provider,SC_UI_SELECTION_ALLOW_EMPTY,&demo.tree_selection);
    if(s==0)s=ScUiViewBind(demo.ui,demo.tree,demo.tree_provider,demo.tree_selection);
    if(s==0)s=ScModelCreate(&demo.combo_model);
    const char *options[3]={"Classic","Aurora","用户选择"};
    ScU32 first=0;
    for(ScU32 i=0;s==0 && i<3;i++){ScU32 id;
        ScValue value={sizeof(value),1,SC_VALUE_UTF8,0,0,0,options[i],(ScU32)strlen(options[i])};
        s=ScModelInsert(demo.combo_model,0,0,&id);
        if(s==0)s=ScModelSet(demo.combo_model,id,"label",&value);
        if(!first)first=id;
    }
    if(s==0)s=ScUiProviderFromModel(demo.combo_model,&demo.combo_provider);
    if(s==0)s=ScUiSelectionCreate(demo.combo_provider,0,&demo.combo_selection);
    if(s==0)s=ScUiViewBind(demo.ui,demo.combo,demo.combo_provider,demo.combo_selection);
    if(s==0)s=ScUiSelectionSet(demo.combo_selection,first,SC_UI_REPLACE);
    return demo_check(s,"Models/ViewBind");
}
static ScStatus demo_controls(void)
{
    ScUiNode group,row,node,r2,r3,label;
    ScStatus s=demo_widget(demo.pages[0],SC_UI_GROUPBOX,"Controls / 输入与状态",0,&group);
    if(s==0)s=demo_layout(group,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(s==0)s=demo_widget(group,SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"主动作",DEMO_MESSAGE,&node);
    if(s==0)ScUiButtonSetDefault(demo.ui,demo.root,node);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"Disabled",0,&node);
    if(s==0)ScUiNodeSetState(demo.ui,node,SC_UI_ENABLED,0);
    ScUiWidgetDesc checkbox;
    memset(&checkbox,0,sizeof(checkbox));
    checkbox.size=sizeof(checkbox);
    checkbox.version=1;
    checkbox.kind=SC_UI_CHECKBOX;
    checkbox.parent=group;
    checkbox.flags=SC_UI_THREE_STATE;
    checkbox.text=demo_text("三态：未选 / 已选 / 混合");
    if(s==0)s=ScUiWidgetCreate(demo.ui,&checkbox,&demo.check);
    if(s==0)s=ScUiCheckSet(demo.ui,demo.check,2);
    if(s==0)s=demo_widget(group,SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    if(s==0)s=demo_widget(row,SC_UI_RADIO,"A",0,&demo.radio);
    if(s==0)s=demo_widget(row,SC_UI_RADIO,"B",0,&r2);
    if(s==0)s=demo_widget(row,SC_UI_RADIO,"C",0,&r3);
    if(s==0)s=ScUiRadioSelect(demo.ui,row,demo.radio);
    if(s==0)s=demo_widget(group,SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    if(s==0)s=demo_widget(row,SC_UI_LABEL,"名称",0,&label);
    if(s==0)s=demo_widget(row,SC_UI_TEXTBOX,"中文 / UTF8",0,&node);
    if(s==0){demo_layout(node,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
        ScUiLabelAssociate(demo.ui,label,node);
        ScUiTooltipSet(demo.ui,node,&checkbox.text);
    }
    if(s==0)s=demo_widget(group,SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    if(s==0)s=demo_widget(row,SC_UI_COMBOBOX,"Classic",0,&demo.combo);
    if(s==0)s=demo_widget(row,SC_UI_SPINBOX,"25",0,&demo.spin);
    if(s==0)s=ScUiSpinRange(demo.ui,demo.spin,-100,100,5);
    if(s==0)s=demo_widget(group,SC_UI_PROGRESS,"",0,&demo.progress);
    if(s==0)s=ScUiProgressSet(demo.ui,demo.progress,25,100,SC_UI_PROGRESS_DETERMINATE);
    if(s==0)s=demo_widget(group,SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"下拉",DEMO_COMBO,&node);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"忙碌 / 确定",DEMO_PROGRESS,&node);
    ScUiWidgetDesc password=checkbox;
    password.kind=SC_UI_TEXTBOX;
    password.parent=group;
    password.flags=SC_UI_PASSWORD;
    password.text=demo_text("Secret密码");
    if(s==0)s=ScUiWidgetCreate(demo.ui,&password,&node);
    return demo_check(s,"ControlsPage");
}
static ScStatus demo_data_page(void)
{
    ScStatus s=demo_widget(demo.pages[1],SC_UI_LABEL,"100000 virtual rows · 表头排序 / F2 编辑 / Shift 与 Ctrl 选择",0,&demo.table);
    if(s==0)s=demo_widget(demo.pages[1],SC_UI_TABLE,"",0,&demo.table);
    if(s==0)s=demo_layout(demo.table,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    DemoRows *rows=&demo.rows;
    rows->count=100000;
    rows->revision=1;
    ScHashDesc hash;
    memset(&hash,0,sizeof(hash));
    hash.size=sizeof(hash);
    hash.version=1;
    hash.value_bytes=sizeof(ScString *);
    hash.drop=demo_row_drop;
    if(s==0)s=ScHashCreate(&hash,&rows->edits);
    ScUiProviderDesc provider;
    memset(&provider,0,sizeof(provider));
    provider.size=sizeof(provider);
    provider.version=1;
    provider.flags=SC_UI_PROVIDER_SORT|SC_UI_PROVIDER_EDIT|SC_UI_PROVIDER_ASYNC;
    provider.procedure=demo_row_data;
    provider.context=rows;
    if(s==0)s=ScUiProviderCreate(&provider,&rows->provider);
    if(s==0)s=ScUiSelectionCreate(rows->provider,SC_UI_SELECTION_MULTI|SC_UI_SELECTION_ALLOW_EMPTY,&demo.table_selection);
    if(s==0)s=ScUiViewBind(demo.ui,demo.table,rows->provider,demo.table_selection);
    ScUiColumnDesc columns[5];
    memset(columns,0,sizeof(columns));
    const char *labels[5]={"Name / 名称","Bytes","Signed","Counter","Group"};
    const ScU32 formats[5]={SC_UI_FORMAT_TEXT,SC_UI_FORMAT_BYTES,SC_UI_FORMAT_I32,SC_UI_FORMAT_COUNTER,SC_UI_FORMAT_U32};
    for(ScU32 i=0;i<5;i++){columns[i].size=sizeof(columns[i]);
        columns[i].version=1;
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].title=demo_text(labels[i]);
        columns[i].width=i?100:220;
        columns[i].min_width=40;
        columns[i].max_width=1000;
        columns[i].flags=SC_UI_COLUMN_SORTABLE|(i?SC_UI_COLUMN_RIGHT:SC_UI_COLUMN_EDITABLE);
        columns[i].format=formats[i];
        columns[i].sort_kind=i==2?SC_UI_SORT_SIGNED:i==3?SC_UI_SORT_COUNTER:i?SC_UI_SORT_UNSIGNED:SC_UI_SORT_TEXT;
    }
    if(s==0)s=ScUiTableSetColumns(demo.ui,demo.table,columns,5);
    return demo_check(s,"VirtualTable");
}
static ScStatus demo_editor(void)
{
    ScUiNode tools,node;
    ScStatus s=demo_widget(demo.pages[2],SC_UI_PANEL,"",0,&tools);
    if(s==0)s=demo_layout(tools,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    if(s==0)s=demo_widget(tools,SC_UI_BUTTON,"Find UTF8",DEMO_FIND,&node);
    if(s==0)s=demo_widget(tools,SC_UI_BUTTON,"Replace all",DEMO_REPLACE,&node);
    if(s==0)s=demo_widget(demo.pages[2],SC_UI_EDIT,"",0,&demo.edit);
    if(s==0)s=demo_layout(demo.edit,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    ScUiDocumentDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
    d.text=demo_text("SCUI document / UTF8\r\n中文与 ASCII 使用真实 Phoenix 度量。\nCombining: e\xcc\x81; emoji: \xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb; flag: \xf0\x9f\x87\xa8\xf0\x9f\x87\xb3\nTab:\tline; undo / redo 保留保存点。\n");
    if(s==0)s=ScUiDocumentCreate(&d,&demo.document);
    if(s==0)s=ScUiEditBind(demo.ui,demo.edit,demo.document);
    if(s==0)s=ScUiNodeSetFont(demo.ui,demo.edit,SC_UI_FONT_CODE);
    ScUiStyleSpan span={sizeof(span),1,0,19,SC32_COLOR_ACCENT,SC32_COLOR_PAPER,SC_UI_STYLE_UNDERLINE,1};
    if(s==0)s=ScUiEditSetSpans(demo.ui,demo.edit,&span,1);
    ScUiDropTargetDesc drop;
    ScU32 format=SC32_CLIPBOARD_TEXT;
    memset(&drop,0,sizeof(drop));
    drop.size=sizeof(drop);
    drop.version=1;
    drop.actions=SC_UI_DRAG_COPY;
    drop.formats=&format;
    drop.format_count=1;
    drop.procedure=demo_drop;
    drop.context=&demo;
    if(s==0)s=ScUiDropTargetRegister(demo.ui,demo.edit,&drop);
    return demo_check(s,"Editor/DropTarget");
}

static ScStatus demo_other_pages(void)
{
    ScUiNode row,label,node,split,grid;
    ScStatus s=demo_widget(demo.pages[3],SC_UI_LABEL,"HBox / VBox / Grid / Flow / Dock 与可保存布局",0,&label);
    if(s==0)s=demo_widget(demo.pages[3],SC_UI_PANEL,"",0,&demo.layout);
    if(s==0)s=demo_layout(demo.layout,SC_UI_HBOX,SC_UI_AUTO,180,0,0);
    if(s==0)s=demo_widget(demo.layout,SC_UI_DOCK,"",0,&demo.dock);
    if(s==0)s=demo_layout(demo.dock,SC_UI_VBOX,220,SC_UI_AUTO,0,0);
    if(s==0)s=demo_widget(demo.dock,SC_UI_LABEL,"Dock：可浮动，值与子树仍保留",0,&label);
    if(s==0)s=demo_widget(demo.dock,SC_UI_BUTTON,"Float dock",DEMO_FLOAT,&node);
    if(s==0)s=demo_widget(demo.layout,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=demo_widget(demo.layout,SC_UI_GRID,"",0,&grid);
    if(s==0)s=demo_layout(grid,SC_UI_GRID_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0);
    ScUiLayoutDesc layout;
    memset(&layout,0,sizeof(layout));
    layout.size=sizeof(layout);
    layout.version=1;
    if(s==0)s=ScUiNodeGetLayout(demo.ui,grid,&layout);
    layout.columns=2;
    if(s==0)s=ScUiNodeSetLayout(demo.ui,grid,&layout);
    for(ScU32 i=0;s==0 && i<6;i++){char text[32];
        snprintf(text,sizeof(text),"Grid %u",i+1);
        s=demo_widget(grid,SC_UI_BUTTON,text,0,&node);
        if(s==0){layout.size=sizeof(layout);
            layout.version=1;
            s=ScUiNodeGetLayout(demo.ui,node,&layout);
            layout.row=i/2;
            layout.column=i%2;
            if(s==0)s=ScUiNodeSetLayout(demo.ui,node,&layout);
        }
    }
    if(s==0)s=demo_widget(demo.pages[3],SC_UI_PANEL,"",0,&row);
    if(s==0)s=demo_layout(row,SC_UI_FLOW,SC_UI_AUTO,60,0,0);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"Save layout",DEMO_LAYOUT_SAVE,&node);
    if(s==0)s=demo_widget(row,SC_UI_BUTTON,"Restore layout",DEMO_LAYOUT_RESTORE,&node);
    if(s==0)s=demo_widget(demo.pages[4],SC_UI_LABEL,"真实模态窗口 · 文件分页 · 表单验证与 Apply 保存点",0,&label);
    if(s==0)s=demo_widget(demo.pages[4],SC_UI_BUTTON,"Message / Enter / Esc",DEMO_MESSAGE,&node);
    if(s==0)s=demo_widget(demo.pages[4],SC_UI_BUTTON,"Open / 多选 / 目录",DEMO_OPEN,&node);
    if(s==0)s=demo_widget(demo.pages[4],SC_UI_BUTTON,"Save / Overwrite",DEMO_SAVE,&node);
    if(s==0)s=demo_widget(demo.pages[4],SC_UI_BUTTON,"Properties / Apply / Cancel",DEMO_PROPERTIES,&node);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_LABEL,"Phoenix 12 / 16 · Classic / Aurora · 命名角色 · 有预算缓存",0,&label);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_LABEL,"Dense: 中文 ABC 0123",0,&label);
    if(s==0)s=ScUiNodeSetFont(demo.ui,label,SC_UI_FONT_DENSE);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_LABEL,"Heading: 应用平台",0,&label);
    if(s==0)s=ScUiNodeSetFont(demo.ui,label,SC_UI_FONT_HEADING);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_BUTTON,"Classic",DEMO_CLASSIC,&node);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_BUTTON,"Aurora",DEMO_AURORA,&node);
    if(s==0)s=demo_widget(demo.pages[5],SC_UI_LABEL,"自有像素图标；颜色来自当前 Theme24",0,&label);
    if(s==0)s=ScUiNodeSetIcon(demo.ui,label,demo.icon);
    return demo_check(s,"Layout/Dialogs/Resources");
}
static ScStatus demo_configuration(void)
{
    ScStatus s=ScConfigCreate(&demo.configuration);
    const char *name="SandCore user",*mode="Classic",*choices="Classic;Aurora;Custom",*description="只读初始值";
    ScValue value={sizeof(value),1,SC_VALUE_UTF8,0,0,0,name,(ScU32)strlen(name)};
    if(s==0)s=ScConfigSet(demo.configuration,"profile","name",&value);
    value.data=mode;
    value.bytes=(ScU32)strlen(mode);
    if(s==0)s=ScConfigSet(demo.configuration,"profile","mode",&value);
    value.data=choices;
    value.bytes=(ScU32)strlen(choices);
    if(s==0)s=ScConfigSet(demo.configuration,"profile","mode.choices",&value);
    value.data=description;
    value.bytes=(ScU32)strlen(description);
    if(s==0)s=ScConfigSet(demo.configuration,"profile","description",&value);
    value.type=SC_VALUE_BOOL;
    value.data=0;
    value.bytes=0;
    value.lo=1;
    if(s==0)s=ScConfigSet(demo.configuration,"profile","enabled",&value);
    value.type=SC_VALUE_I32;
    value.lo=25;
    if(s==0)s=ScConfigSet(demo.configuration,"profile","level",&value);
    return demo_check(s,"Config");
}
static ScStatus demo_ready(void)
{
    ScStatus s;
    ScUiContextDesc context;
    memset(&context,0,sizeof(context));
    context.size=sizeof(context);
    context.version=1;
    context.flags=SC_UI_STOP_LAST_ROOT;
    context.default_font_face=16;
    context.default_font_pixels=16;
    s=ScUiContextCreate(&context,&demo.ui);
    if(s<0)return s;
    ScArrayDesc observers;
    ScHashDesc observer_index;
    memset(&observers,0,sizeof(observers));
    observers.size=sizeof(observers);
    observers.version=1;
    observers.item_bytes=sizeof(DemoRootObserver);
    memset(&observer_index,0,sizeof(observer_index));
    observer_index.size=sizeof(observer_index);
    observer_index.version=1;
    observer_index.value_bytes=4;
    s=ScArrayCreate(&observers,&demo.root_observers);
    if(s==0)s=ScHashCreate(&observer_index,&demo.root_observer_index);
    if(s<0)return s;
    ScThemeInfo theme;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    s=ScThemeQuery(&theme);
    if(s<0)return s;
    ScU32 pixels[256];
    for(ScU32 y=0;y<16;y++)for(ScU32 x=0;x<16;x++)pixels[y*16+x]=(x>2 && x<13 && y>2 && y<13)?0xff000000u|theme.colors[(x+y<16)?SC32_COLOR_ACCENT:SC32_COLOR_ACCENT_DEEP]:0;
    s=ScUiIconCreateArgb(16,16,64,pixels,&demo.icon);
    if(s<0)return s;
    s=demo_configuration();
    if(s<0)return s;
    ScUiWindowDesc window;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    window.title=demo_text("SCUI / Controls & Workflows");
    window.x=60;
    window.y=50;
    window.client_width=900;
    window.client_height=600;
    window.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(demo.ui,&window,&demo.root);
    if(s<0)return s;
    demo_layout(demo.root,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,0,0);
    s=demo_widget(demo.root,SC_UI_MENUBAR,"",0,&demo.menu);
    if(s==0)s=demo_layout(demo.menu,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_TOP);
    if(s==0)s=demo_widget(demo.root,SC_UI_TOOLBAR,"",0,&demo.toolbar);
    if(s==0)s=demo_layout(demo.toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,32,0,SC_UI_DOCK_TOP);
    if(s==0)s=demo_widget(demo.root,SC_UI_STATUS,"",0,&demo.status);
    if(s==0)s=demo_layout(demo.status,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_BOTTOM);
    ScUiNode body,split;
    const char *pages[6]={"Controls","Data","Editor","Layout","Dialogs","Resources"};
    if(s==0)s=demo_widget(demo.root,SC_UI_PANEL,"",0,&body);
    if(s==0)s=demo_layout(body,SC_UI_HBOX,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    if(s==0)s=demo_widget(body,SC_UI_TREE,"",0,&demo.tree);
    if(s==0)s=demo_layout(demo.tree,SC_UI_ABSOLUTE,192,SC_UI_AUTO,0,0);
    if(s==0)s=demo_widget(body,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=demo_widget(body,SC_UI_TAB,"",0,&demo.tabs);
    if(s==0)s=demo_layout(demo.tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,0);
    for(ScU32 i=0;s==0 && i<6;i++){s=demo_widget(demo.tabs,SC_UI_PROPERTY_PAGE,"",0,demo.pages+i);
        if(s==0)s=demo_layout(demo.pages[i],SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0);
        ScUiText title=demo_text(pages[i]);
        if(s==0)s=ScUiTabAdd(demo.ui,demo.tabs,demo.pages[i],&title);
    }
    if(s==0)s=demo_controls();
    if(s==0)s=demo_model();
    if(s==0)s=demo_data_page();
    if(s==0)s=demo_editor();
    if(s==0)s=demo_other_pages();
    if(s==0)s=demo_menu();
    if(s==0)s=demo_listen(demo.root,demo_command,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE);
    window.title=demo_text("SCUI / Shared document & drag");
    window.x=1020;
    window.y=100;
    window.client_width=360;
    window.client_height=280;
    window.owner=0;
    if(s==0)s=ScUiWindowCreate(demo.ui,&window,&demo.second);
    if(s==0)s=demo_layout(demo.second,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0,0);
    ScUiNode label=0,source=0;
    if(s==0)s=demo_widget(demo.second,SC_UI_LABEL,"拖动下面图标到主窗 Editor。\n同窗口与跨窗口都走真实 OS ticket / IPC。",0,&label);
    if(s==0)s=demo_widget(demo.second,SC_UI_BUTTON,"拖放文本 / Drag",0,&source);
    demo.drag_source=source;
    if(s==0)s=ScUiNodeSetIcon(demo.ui,source,demo.icon);
    if(s==0)s=demo_listen(source,demo_drag,SC_UI_TARGET_PHASE);
    if(s==0)s=demo_listen(demo.second,demo_command,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE);
    ScUiWidgetDesc shared;
    memset(&shared,0,sizeof(shared));
    shared.size=sizeof(shared);
    shared.version=1;
    shared.kind=SC_UI_EDIT;
    shared.parent=demo.second;
    shared.flags=SC_UI_WRAP;
    shared.text=demo_text("");
    if(s==0)s=ScUiWidgetCreate(demo.ui,&shared,&demo.second_edit);
    if(s==0)s=demo_layout(demo.second_edit,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(s==0)s=ScUiEditBind(demo.ui,demo.second_edit,demo.document);
    if(s==0)s=demo_widget(demo.second,SC_UI_BUTTON,"Exit",DEMO_STOP,&label);
    demo.second_exit=label;
    if(s==0)s=ScUiLayout(demo.ui,demo.root);
    if(s==0)s=ScUiLayout(demo.ui,demo.second);
    demo_status("就绪；菜单与工具栏共享 Command，Tab 可遍历全部主操作");
    return s;
}
static void demo_hidden_reset(void)
{
    if(demo.ui){
        if(demo.hidden_timer)ScUiTimerCancel(demo.ui,demo.hidden_timer);
        if(demo.hidden_listener)ScUiDisconnect(demo.ui,demo.hidden_listener);
    }
    demo.hidden_timer=0;
    demo.hidden_listener=0;
    demo.hidden_root=0;
    demo.hidden_phase=0;
}
static ScStatus demo_hidden_arm(ScU32 delay,const ScMonotonicClock *clock)
{
    ScUiTimerDesc timer;
    memset(&timer,0,sizeof(timer));
    timer.size=sizeof(timer);
    timer.version=1;
    timer.node=demo.hidden_root;
    timer.flags=SC_UI_TIMER_BACKGROUND;
    timer.deadline=clock->ticks;
    timer.deadline.lo+=delay;
    if(timer.deadline.lo<clock->ticks.lo){
        if(timer.deadline.hi==0xffffffffu)return SC32_E_RANGE;
        timer.deadline.hi++;
    }
    return ScUiTimerSet(demo.ui,&timer,&demo.hidden_timer);
}
static ScStatus demo_hidden_timer(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    (void)context;
    (void)opaque;
    if(event->type!=SC_UI_EVENT_TIMER || event->command!=demo.hidden_timer)return 0;
    /* oneshot已经进入回调，不撤当前firing对象；后续仅留下一次明确期限。 */
    demo.hidden_timer=0;
    ScUiContextInfo info;
    ScMonotonicClock clock;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    memset(&clock,0,sizeof(clock));
    clock.size=sizeof(clock);
    clock.version=1;
    ScStatus status=ScUiContextQuery(demo.ui,&info);
    if(status==0 && (!info.roots || info.visible_roots)){
        demo_hidden_reset();
        return SC_UI_HANDLED;
    }
    if(status==0)status=ScSystemMonotonic(&clock);
    if(status<0){
        demo_hidden_reset();
        demo.hidden_phase=3;
        return demo_check(status,"HiddenObservation");
    }
    printf("SCUI HIDDEN sample=%u round=%u tick=%u tickHi=%u visible=%u paints=%u:%u submitted=%u:%u\n",
        demo.hidden_phase,demo.hidden_round,clock.ticks.lo,clock.ticks.hi,info.visible_roots,
        info.paint_count.hi,info.paint_count.lo,info.submitted_bytes.hi,info.submitted_bytes.lo);
    fflush(stdout);
    if(demo.hidden_phase==1){
        demo.hidden_phase=2;
        status=demo_hidden_arm(200,&clock);
        if(status<0){
            demo_hidden_reset();
            demo.hidden_phase=3;
            return demo_check(status,"HiddenTimer");
        }
    }
    else{
        ScU32 listener=demo.hidden_listener;
        demo.hidden_listener=0;
        demo.hidden_phase=3;
        if(listener)ScUiDisconnect(demo.ui,listener);
    }
    return SC_UI_HANDLED;
}
static ScStatus demo_hidden_observe(void)
{
    ScUiContextInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus status=ScUiContextQuery(demo.ui,&info);
    if(status<0)return status;
    if(!info.roots || info.visible_roots){
        demo_hidden_reset();
        return 0;
    }
    if(demo.hidden_phase)return 0;
    if(demo.hidden_round==0xffffffffu)return SC32_E_RANGE;
    ScUiNode root=0;
    status=ScUiContextRootNext(demo.ui,0,&root);
    if(status!=0)return status==SC_UI_END?0:status;
    ScMonotonicClock clock;
    memset(&clock,0,sizeof(clock));
    clock.size=sizeof(clock);
    clock.version=1;
    status=ScSystemMonotonic(&clock);
    if(status<0)return status;
    ScUiSubscription subscription;
    memset(&subscription,0,sizeof(subscription));
    subscription.size=sizeof(subscription);
    subscription.version=1;
    subscription.type=SC_UI_EVENT_TIMER;
    subscription.phases=SC_UI_TARGET_PHASE;
    subscription.priority=1000;
    subscription.procedure=demo_hidden_timer;
    status=ScUiListen(demo.ui,root,&subscription,&demo.hidden_listener);
    if(status<0)return status;
    demo.hidden_root=root;
    demo.hidden_phase=1;
    demo.hidden_round++;
    /* 真实隐藏通知后的2PIT让当前消息批次收束；不绘画或改变任何损伤。 */
    status=demo_hidden_arm(2,&clock);
    if(status<0){
        demo_hidden_reset();
        demo.hidden_phase=3;
    }
    return status;
}
static void demo_release(void)
{
    demo_hidden_reset();
    ScUiDialogRelease(demo.dialog);
    demo.dialog=0;
    if(demo.ui){if(demo.sort_timer)ScUiTimerCancel(demo.ui,demo.sort_timer);
        if(demo.drag_token)ScUiDragCancel(demo.ui,demo.drag_token);
        if(demo.second)ScUiNodeDestroy(demo.ui,demo.second);
        if(demo.root)ScUiNodeDestroy(demo.ui,demo.root);
    }
    ScUiSelectionRelease(demo.tree_selection);
    ScUiSelectionRelease(demo.combo_selection);
    ScUiSelectionRelease(demo.table_selection);
    ScUiProviderRelease(demo.tree_provider);
    ScUiProviderRelease(demo.combo_provider);
    ScUiProviderRelease(demo.rows.provider);
    ScModelRelease(demo.tree_model);
    ScModelRelease(demo.combo_model);
    ScUiDocumentRelease(demo.document);
    ScUiMenuRelease(demo.menus);
    ScUiIconRelease(demo.icon);
    ScConfigRelease(demo.configuration);
    ScStringRelease(demo.file);
    ScHashRelease(demo.rows.edits);
    ScArrayRelease(demo.root_observers);
    ScHashRelease(demo.root_observer_index);
    free(demo.rows.order);
    free(demo.rows.rank);
    free(demo.rows.work);
    free(demo.rows.scratch);
    free(demo.rows.candidate_rank);
    ScUiContextRelease(demo.ui);
    demo.ui=0;
}
static void demo_geometry(ScUiNode node,const char *role)
{
    ScUiNodeInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(!node || ScUiNodeQuery(demo.ui,node,&info)<0)return;
    ScString *name=0;
    ScView text;
    memset(&text,0,sizeof(text));
    text.size=sizeof(text);
    text.version=1;
    ScStatus named=ScUiAccessibleText(demo.ui,node,SC_UI_ACCESSIBLE_NAME,&name);
    if(named==0)named=ScStringView(name,&text);
    if(named==0 && !text.bytes){ScStringRelease(name);
        name=0;
        named=ScUiNodeCopyText(demo.ui,node,&name);
        if(named==0)named=ScStringView(name,&text);
        }
    if(named<0)text.bytes=0;
    printf("SCUI NODE role=%s id=%u root=%u parent=%u kind=%u x=%d y=%d w=%d h=%d state=%u textBytes=%u textStatus=%d textHex=",role,node,info.root,info.parent,info.kind,info.rectangle.x,info.rectangle.y,info.rectangle.width,info.rectangle.height,info.state,text.bytes,named);
    if(!text.bytes)printf("-");
    else for(ScU32 i=0;i<text.bytes;i++)printf("%02x",(ScU32)((const unsigned char *)text.data)[i]);
    printf("\n");
    ScStringRelease(name);
}
static void demo_window_geometry(ScUiNode node,const char *role)
{
    ScUiNodeInfo n;
    ScWindowInfo window;
    memset(&n,0,sizeof(n));
    n.size=sizeof(n);
    n.version=1;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    if(ScUiNodeQuery(demo.ui,node,&n)==0 && ScWindowQuery(n.native_window,&window)==0)
    printf("SCUI WINDOW role=%s root=%u window=%u generation=%u pid=%u pidgen=%u session=%u x=%d y=%d w=%d h=%d cw=%u ch=%u state=%u geometry=%u visibility=%u\n",role,n.root,window.window,window.generation,window.owner_pid,window.owner_generation,window.session,window.x,window.y,window.width,window.height,window.client_width,window.client_height,window.state,window.geometry_epoch,window.visibility_epoch);
}
static void demo_metrics(ScUiNode root,const char *role)
{
    ScUiMetrics metrics;
    memset(&metrics,0,sizeof(metrics));
    metrics.size=sizeof(metrics);
    metrics.version=1;
    ScStatus status=ScUiMetricsQuery(demo.ui,root,&metrics);
    if(status==0)printf("SCUI METRICS role=%s root=%u scalePercent=%u fontFace=%u fontPixels=%u fontEpoch=%u themeEpoch=%u displayEpoch=%u rowHeight=%u menuHeight=%u toolbarHeight=%u statusHeight=%u\n",role,root,metrics.scale_percent,metrics.font_face,metrics.font_pixels,metrics.font_epoch,metrics.theme_epoch,metrics.display_epoch,metrics.row_height,metrics.menu_height,metrics.toolbar_height,metrics.status_height);
    else printf("SCUI QUERY_ERROR kind=metrics root=%u status=%d\n",root,status);
}
static void demo_edit_selection(ScUiNode node,const char *role)
{
    ScUiTextSelection selection;
    ScUiNodeInfo info;
    memset(&selection,0,sizeof(selection));
    selection.size=sizeof(selection);
    selection.version=1;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(ScUiNodeQuery(demo.ui,node,&info)==0 && ScUiEditGetSelection(demo.ui,node,&selection)==0)
        printf("SCUI EDIT_SELECTION role=%s id=%u root=%u anchor=%u caret=%u affinity=%u revision=%u flags=%u\n",role,node,info.root,selection.anchor,selection.caret,selection.affinity,selection.revision,selection.flags);
}
static void demo_root_tree(ScUiNode root,const char *prefix)
{
    /* 沿公共的孩子/兄弟/父ID走树，不解释opaque对象或扫描猜测的ID范围。 */
    ScUiNode current=root;
    while(current){ScUiNodeInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScStatus status=ScUiNodeQuery(demo.ui,current,&info);
        if(status<0){printf("SCUI QUERY_ERROR kind=tree id=%u status=%d\n",current,status);
            return;
            }
        char role[40];
        snprintf(role,sizeof(role),"%s-%u",prefix,current);
        demo_geometry(current,role);
        if(info.first_child){current=info.first_child;
            continue;
            }
        while(current!=root && !info.next_sibling){current=info.parent;
            if(!current)return;
            info.size=sizeof(info);
            info.version=1;
            status=ScUiNodeQuery(demo.ui,current,&info);
            if(status<0){printf("SCUI QUERY_ERROR kind=tree id=%u status=%d\n",current,status);
                return;
                }
            }
        if(current==root)return;
        current=info.next_sibling;
    }
}
static void demo_other_roots(void)
{
    ScUiNode after=0,root=0;
    for(;;){ScStatus status=ScUiContextRootNext(demo.ui,after,&root);
        if(status==SC_UI_END)return;
        if(status<0){printf("SCUI QUERY_ERROR kind=roots after=%u status=%d\n",after,status);
            return;
            }
        after=root;
        if(root==demo.root || root==demo.second)continue;
        ScUiNodeInfo node;
        ScWindowInfo window;
        memset(&node,0,sizeof(node));
        node.size=sizeof(node);
        node.version=1;
        memset(&window,0,sizeof(window));
        window.size=sizeof(window);
        window.version=1;
        status=ScUiNodeQuery(demo.ui,root,&node);
        if(status==0)status=ScWindowQuery(node.native_window,&window);
        if(status<0){printf("SCUI QUERY_ERROR kind=window root=%u status=%d\n",root,status);
            continue;
            }
        const char *prefix=(window.state&SC32_WINDOW_MODAL)?"dialog":"root";
        char role[40];
        snprintf(role,sizeof(role),"%s-%u",prefix,root);
        demo_window_geometry(root,role);
        demo_metrics(root,role);
        demo_root_tree(root,prefix);
    }
}
static ScStatus demo_root_key(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    (void)context;
    (void)opaque;
    /* 锁定键描述真实状态，不应阻断无Shift/Ctrl/Alt的诊断快捷键。 */
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->key==SC32_KEY_F1+11 &&
        !(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL|SC32_MOD_ALT))){demo_snapshot();
        return SC_UI_HANDLED;
        }
    return 0;
}
static ScStatus demo_root_visibility(ScUiContext *context,const ScUiEvent *event,void *opaque)
{
    (void)context;
    (void)opaque;
    if(event->type!=SC_UI_EVENT_VISIBILITY)return 0;
    /* 先补实际新root的观察，再依据本次已发布可见性核对全窗口资格。 */
    ScStatus status=demo_observe_roots();
    if(status==0)status=demo_hidden_observe();
    return demo_check(status,"VisibilityObservation");
}
static ScStatus demo_observe_roots(void)
{
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus status=ScArrayQuery(demo.root_observers,&info);
    if(status<0)return status;
    for(ScU32 i=info.length;i;i--){DemoRootObserver observer;
        status=ScArrayGet(demo.root_observers,i-1,&observer);
        if(status<0)return status;
        ScUiNodeInfo node;
        memset(&node,0,sizeof(node));
        node.size=sizeof(node);
        node.version=1;
        status=ScUiNodeQuery(demo.ui,observer.node,&node);
        if(status==SC_UI_E_STALE){ScHashRemove(demo.root_observer_index,&observer.node,4);
            ScArrayErase(demo.root_observers,i-1,1);
            }
        else if(status<0)return status;
    }
    ScUiNode after=0,root;
    for(;;){status=ScUiContextRootNext(demo.ui,after,&root);
        if(status==SC_UI_END)return 0;
        if(status<0)return status;
        after=root;
        ScHashEntry found;
        memset(&found,0,sizeof(found));
        found.size=sizeof(found);
        found.version=1;
        if(ScHashGet(demo.root_observer_index,&root,4,&found)==0)continue;
        /* 新modal/root先登记真正键盘观察者，使F12不会被内部dialog当未知完成命令。 */
        ScUiSubscription subscription;
        memset(&subscription,0,sizeof(subscription));
        subscription.size=sizeof(subscription);
        subscription.version=1;
        subscription.type=SC_UI_EVENT_KEY_DOWN;
        subscription.phases=SC_UI_CAPTURE_PHASE|SC_UI_TARGET_PHASE;
        subscription.priority=1000;
        subscription.procedure=demo_root_key;
        DemoRootObserver observer={root,0,0};
        if(root!=demo.root && root!=demo.second){
            status=ScUiListen(demo.ui,root,&subscription,&observer.token);
            if(status<0)return status;
        }
        subscription.type=SC_UI_EVENT_VISIBILITY;
        subscription.phases=SC_UI_TARGET_PHASE;
        subscription.procedure=demo_root_visibility;
        status=ScUiListen(demo.ui,root,&subscription,&observer.visibility_token);
        if(status<0){
            if(observer.token)ScUiDisconnect(demo.ui,observer.token);
            return status;
        }
        info.size=sizeof(info);
        info.version=1;
        status=ScArrayQuery(demo.root_observers,&info);
        int inserted=0;
        if(status==0)status=ScArrayInsert(demo.root_observers,info.length,&observer,1);
        if(status==0)inserted=1;
        if(status==0)status=ScHashSet(demo.root_observer_index,&root,4,&observer.visibility_token,0);
        if(status<0){
            if(observer.token)ScUiDisconnect(demo.ui,observer.token);
            ScUiDisconnect(demo.ui,observer.visibility_token);
            if(inserted)ScArrayErase(demo.root_observers,info.length,1);
            return status;
            }
    }
}
static void demo_snapshot(void)
{
    ScU32 count=++demo.snapshots;
    printf("SCUI SNAPSHOT BEGIN count=%u\n",count);
    demo_window_geometry(demo.root,"main");
    demo_window_geometry(demo.second,"second");
    demo_geometry(demo.root,"main");
    demo_geometry(demo.second,"second");
    demo_geometry(demo.menu,"menu");
    demo_geometry(demo.toolbar,"toolbar");
    demo_geometry(demo.status,"status");
    demo_geometry(demo.tabs,"tabs");
    demo_geometry(demo.table,"table");
    demo_geometry(demo.check,"check");
    demo_geometry(demo.spin,"spin");
    demo_geometry(demo.combo,"combo");
    demo_geometry(demo.edit,"edit");
    demo_geometry(demo.second_edit,"shared-edit");
    demo_geometry(demo.drag_source,"drag-source");
    demo_geometry(demo.second_exit,"exit");
    demo_metrics(demo.root,"main");
    demo_metrics(demo.second,"second");
    demo_edit_selection(demo.edit,"edit");
    demo_edit_selection(demo.second_edit,"shared-edit");
    ScThemeInfo theme;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    if(ScThemeQuery(&theme)==0)printf("SCUI THEME style=%u generation=%u radius=%u border=%u titleHeight=%u taskbarHeight=%u\n",theme.style,theme.generation,theme.radius,theme.border,theme.title_height,theme.taskbar_height);
    ScDisplayInfo display;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    if(ScDisplayQuery(&display)==0)printf("SCUI DISPLAY action=snapshot scalePercent=%u w=%u h=%u backend=%u generation=%u previewSeconds=%u\n",display.scale_percent,display.width,display.height,display.backend,display.generation,display.preview_seconds);
    if(demo.dialog){ScUiDialogInfo dialog;
        memset(&dialog,0,sizeof(dialog));
        dialog.size=sizeof(dialog);
        dialog.version=1;
        if(ScUiDialogQuery(demo.dialog,&dialog)==0)printf("SCUI DIALOG root=%u stage=%u command=%u selected=%u flags=%u error=%d\n",dialog.root,dialog.stage,dialog.command,dialog.selected_count,dialog.flags,dialog.error);
        }
    demo_other_roots();
    ScUiDocumentInfo document;
    memset(&document,0,sizeof(document));
    document.size=sizeof(document);
    document.version=1;
    if(ScUiDocumentQuery(demo.document,&document)==0)printf("SCUI DOCUMENT bytes=%u lines=%u revision=%u saved=%u flags=%u undo=%u redo=%u canUndo=%u canRedo=%u\n",document.bytes,document.lines,document.revision,document.saved_revision,document.flags,document.undo_bytes,document.redo_bytes,document.can_undo,document.can_redo);
    ScUiSelectionInfo selection;
    memset(&selection,0,sizeof(selection));
    selection.size=sizeof(selection);
    selection.version=1;
    if(ScUiSelectionQuery(demo.table_selection,&selection)==0)printf("SCUI SELECTION count=%u current=%u anchor=%u revision=%u\n",selection.count,selection.current,selection.anchor,selection.revision);
    ScUiContextInfo context;
    memset(&context,0,sizeof(context));
    context.size=sizeof(context);
    context.version=1;
    if(ScUiContextQuery(demo.ui,&context)==0)printf("SCUI CONTEXT roots=%u nodes=%u timers=%u listeners=%u visible=%u surface=%u cache=%u messages=%u:%u paints=%u:%u submitted=%u:%u error=%d\n",context.roots,context.nodes,context.timers,context.listeners,context.visible_roots,context.surface_bytes,context.cache_bytes,context.received_messages.hi,context.received_messages.lo,context.paint_count.hi,context.paint_count.lo,context.submitted_bytes.hi,context.submitted_bytes.lo,context.notification_error);
    printf("SCUI SNAPSHOT END count=%u\n",count);
    fflush(stdout);
}
int main(void)
{
    memset(&demo,0,sizeof(demo));
    ScStatus s=demo_ready();
    if(s<0){demo_check(s,"Startup");
        demo_release();
        return 1;
    }
    ScUiContextInfo context;
    ScUiMetrics metrics;
    memset(&context,0,sizeof(context));
    context.size=sizeof(context);
    context.version=1;
    memset(&metrics,0,sizeof(metrics));
    metrics.size=sizeof(metrics);
    metrics.version=1;
    ScUiContextQuery(demo.ui,&context);
    ScUiMetricsQuery(demo.ui,demo.root,&metrics);
    printf("SCUI READY roots=%u nodes=%u virtual=%u font=%u/%u scale=%u\n",context.roots,context.nodes,demo.rows.count,metrics.font_face,metrics.font_pixels,metrics.scale_percent);
    demo_snapshot();
    fflush(stdout);
    ScI32 code=0;
    /* 公开Step保留库的真实等待/绘制；步间补新root观察，无固定100Hz轮询。 */
    for(;;){s=demo_observe_roots();
        if(s<0)break;
        s=ScUiContextStep(demo.ui,SC32_INFINITE);
        if(s==SC_UI_END || s<0)break;
        }
    if(s>=0)s=ScUiContextRun(demo.ui,&code);
    if(s<0)demo_check(s,"ContextRun");
    printf("SCUI EXIT commands=%u changes=%u drops=%u failures=%u status=%d code=%d\n",demo.commands,demo.changes,demo.drops,demo.failures,s,code);
    demo_release();
    return s<0 || demo.failures?1:code;
}
