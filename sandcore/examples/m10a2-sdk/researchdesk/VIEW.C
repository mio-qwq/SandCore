#include "RDAPP.H"
static ScU32 command_state[24];
static int dirty(RdDoc *d)
{
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    return d && !ScUiDocumentQuery(d->document,&info) && (info.flags&SC_UI_DOCUMENT_DIRTY);
}
void RdStatus(const char *message)
{
    if(message){
        strncpy(rd.message,message,sizeof(rd.message)-1);
        rd.message[sizeof(rd.message)-1]=0;
    }
    if(!rd.ui || !rd.status)return;
    ScUiStatusPart part;
    RdRecord(&part,sizeof(part));
    part.width=SC_UI_AUTO;
    part.text=RdText(rd.message);
    ScUiStatusSetParts(rd.ui,rd.status,&part,1);
}
void RdError(ScStatus code,const char *message)
{
    char text[256];
    snprintf(text,sizeof(text),"%s（%d）",message,code);
    RdStatus(text);
}
static ScStatus layout(ScUiNode node,ScU32 mode,ScU32 dock,ScI32 width,ScI32 height,ScU32 flex)
{
    ScUiLayoutDesc d;
    RdRecord(&d,sizeof(d));
    d.mode=mode;
    d.dock=dock;
    d.width=width;
    d.height=height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.column_span=d.row_span=1;
    d.flex_weight=flex;
    return ScUiNodeSetLayout(rd.ui,node,&d);
}
static ScStatus widget(ScU32 kind,ScUiNode parent,ScU32 command,const char *text,ScUiNode *out)
{
    ScUiWidgetDesc d;
    RdRecord(&d,sizeof(d));
    d.kind=kind;
    d.parent=parent;
    d.command=command;
    d.text=RdText(text);
    return ScUiWidgetCreate(rd.ui,&d,out);
}
ScStatus RdDocumentCreate(const char *path,const void *bytes,ScU32 count,ScU32 disk,RdDoc **out)
{
    if(count>RD_MAX_BYTES || memchr(bytes,0,count) || ScUtf8Validate(bytes,count,0)<0)return SC_UI_E_UTF8;
    if(rd.next_doc>=UINT_MAX-1)return SC_UI_E_RANGE;
    RdDoc *d=(RdDoc *)calloc(1,sizeof(*d));
    if(!d)return SC_UI_E_MEMORY;
    d->id=++rd.next_doc;
    d->disk_revision=disk;
    if(path && *path){
        if(strlen(path)>64){
            free(d);
            return SC32_E_RANGE;
        }
        strcpy(d->path,path);
    }
    ScUiDocumentDesc desc;
    RdRecord(&desc,sizeof(desc));
    desc.undo_budget_bytes=1048576;
    RdRecord(&desc.text,sizeof(desc.text));desc.text.data=(const char *)bytes;desc.text.bytes=count;
    ScStatus status=ScUiDocumentCreate(&desc,&d->document);
    if(!status)status=widget(SC_UI_PANEL,rd.tabs,0,"",&d->page);
    if(!status)status=layout(d->page,SC_UI_DOCK_LAYOUT,SC_UI_DOCK_FILL,SC_UI_AUTO,SC_UI_AUTO,1);
    if(!status)status=widget(SC_UI_EDIT,d->page,0,"",&d->edit);
    if(!status)status=layout(d->edit,SC_UI_ABSOLUTE,SC_UI_DOCK_FILL,SC_UI_AUTO,SC_UI_AUTO,1);
    if(!status)status=ScUiEditBind(rd.ui,d->edit,d->document);
    if(!status)status=ScUiNodeSetFont(rd.ui,d->edit,SC_UI_FONT_CODE);
    snprintf(d->label,sizeof(d->label),"%s",d->path[0]?d->path:"未命名");
    ScUiText title=RdText(d->label);
    if(!status)status=ScUiTabAdd(rd.ui,rd.tabs,d->page,&title);
    if(status<0){
        if(d->page)ScUiNodeDestroy(rd.ui,d->page);
        ScUiDocumentRelease(d->document);
        free(d);
        return status;
    }
    d->next=rd.docs;
    rd.docs=d;
    rd.active=d;
    ScUiTabSelect(rd.ui,rd.tabs,d->page);
    ScUiFocus(rd.ui,d->edit);
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    ScUiDocumentQuery(d->document,&info);
    d->revision=info.revision;
    RdNotify(rd.tree_provider,SC_UI_DATA_RESET);
    ScUiTreeExpand(rd.ui,rd.tree,1,1);
    RdRefresh();
    if(out)*out=d;
    return 0;
}
ScStatus RdDocumentClose(RdDoc *d)
{
    if(!d)return SC_UI_E_NOT_FOUND;
    if(rd.io.kind && rd.io.doc==d->id)return SC32_E_BUSY;
    if(rd.parse.doc==d->id)RdParseCancel();
    if(rd.source_doc==d->id){
        rd.source_doc=0;
        rd.stats_valid=0;
        rd.computing=0;
    }
    ScStatus status=ScUiTabRemove(rd.ui,rd.tabs,d->page);
    if(status<0)return status;
    RdDoc **link=&rd.docs;
    while(*link && *link!=d)link=&(*link)->next;
    if(*link)*link=d->next;
    if(rd.active==d)rd.active=0;
    ScUiDocumentRelease(d->document);
    free(d);
    ScValue value;
    RdRecord(&value,sizeof(value));
    if(!ScUiNodeGetValue(rd.ui,rd.tabs,&value))for(RdDoc *p=rd.docs;p;p=p->next)if(p->page==value.lo)rd.active=p;
    RdNotify(rd.tree_provider,SC_UI_DATA_RESET);
    RdRefresh();
    return 0;
}
typedef struct {
    ScU32 id;
    const char *label;
    ScU32 key,modifiers;
}
Command;
static const Command commands[]={
    {
        RD_NEW,"新建 (&N)",'N',SC32_MOD_CONTROL
    },{
        RD_OPEN,"打开 (&O)",'O',SC32_MOD_CONTROL
    },{
        RD_SAVE,"保存 (&S)",'S',SC32_MOD_CONTROL
    },
    {
        RD_SAVE_AS,"另存为",'S',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },{
        RD_CLOSE,"关闭文档",'W',SC32_MOD_CONTROL
    },{
        RD_EXIT,"退出 (&X)",0,0
    },
    {
        RD_UNDO,"撤销",'Z',SC32_MOD_CONTROL
    },{
        RD_REDO,"重做",'Y',SC32_MOD_CONTROL
    },{
        RD_CUT,"剪切",'X',SC32_MOD_CONTROL
    },
    {
        RD_COPY,"复制",'C',SC32_MOD_CONTROL
    },{
        RD_PASTE,"粘贴",'V',SC32_MOD_CONTROL
    },{
        RD_ALL,"全选",'A',SC32_MOD_CONTROL
    },
    {
        RD_PARSE,"读取数据并统计",SC32_KEY_F1+4,0
    },{
        RD_PLUGIN,"连接统计插件",0,0
    },{
        RD_RELEASE,"释放插件引用",0,0
    },
    {
        RD_CANCEL,"取消后台操作",0,0
    },{
        RD_FIND,"查找当前文本",'F',SC32_MOD_CONTROL
    },{
        RD_FIND_NEXT,"查找下一处",SC32_KEY_F1+2,0
    },
    {
        RD_FONT_SMALL,"紧凑字体",0,0
    },{
        RD_FONT_NORMAL,"常规字体",0,0
    },{
        RD_ABOUT,"关于课题台",0,0
    }
};
void RdRefresh(void)
{
    if(!rd.ui)return;
    int tree_changed=0;
    for(RdDoc *d=rd.docs;d;d=d->next){
        char label[96];
        snprintf(label,sizeof(label),"%s%s",dirty(d)?"* ":"",d->path[0]?d->path:"未命名");
        if(strcmp(label,d->label)){
            strcpy(d->label,label);
            ScUiText text=RdText(label);
            ScUiTabSetText(rd.ui,rd.tabs,d->page,&text);
            tree_changed=1;
        }
    }
    if(tree_changed)RdNotify(rd.tree_provider,SC_UI_DATA_RESET);
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    if(rd.active)ScUiDocumentQuery(rd.active->document,&info);
    for(ScU32 i=0;i<sizeof(commands)/sizeof(commands[0]);i++){
        ScU32 id=commands[i].id,disabled=0;
        if(id>=RD_SAVE && id<=RD_CLOSE)disabled=!rd.active || rd.io.kind;
        if(id>=RD_UNDO && id<=RD_ALL)disabled=!rd.active;
        if(id==RD_UNDO)disabled=!rd.active || !info.can_undo;
        if(id==RD_REDO)disabled=!rd.active || !info.can_redo;
        if(id==RD_PARSE)disabled=!rd.active || rd.parse.phase || rd.computing;
        if(id==RD_PLUGIN || id==RD_RELEASE)disabled=rd.computing;
        if(id==RD_CANCEL)disabled=!rd.io.kind && !rd.parse.phase && !rd.computing;
        ScU32 flags=disabled?SC_UI_MENU_DISABLED:0;
        if(command_state[i]==flags+1)continue;
        command_state[i]=flags+1;
        ScUiCommandDesc command;
        RdRecord(&command,sizeof(command));
        command.command=id;
        command.flags=flags;
        command.label=RdText(commands[i].label);
        RdRecord(&command.shortcut,sizeof(command.shortcut));
        command.shortcut.key=commands[i].key;
        command.shortcut.modifiers=commands[i].modifiers;
        ScUiCommandSet(rd.ui,&command);
    }
    char title[192];
    snprintf(title,sizeof(title),"%s — ResearchDesk",rd.active?rd.active->label:"课题台");
    ScUiText text=RdText(title);
    ScUiWindowSetTitle(rd.ui,rd.root,&text);
    if(rd.stats_valid){
        char message[256];
        snprintf(message,sizeof(message),"%u条数据 · 总和 %s · 均值 %d · 最小 %d · 最大 %d",rd.result.count,rd.result.sum,rd.result.mean,rd.result.minimum,rd.result.maximum);
        RdStatus(message);
    }
    ScUiInvalidate(rd.ui,rd.chart,0);
}
static ScStatus menu_items(ScUiMenu *menu,const ScU32 *ids,ScU32 count)
{
    for(ScU32 i=0;i<count;i++){
        ScUiMenuItem item;
        RdRecord(&item,sizeof(item));
        if(!ids[i])item.flags=SC_UI_MENU_SEPARATOR;
        else {
            const Command *command=commands+ids[i]-RD_NEW;
            item.command=command->id;
            item.label=RdText(command->label);
            RdRecord(&item.shortcut,sizeof(item.shortcut));
            item.shortcut.key=command->key;
            item.shortcut.modifiers=command->modifiers;
        }
        ScU32 id;
        ScStatus status=ScUiMenuInsert(menu,0,&item,&id);
        if(status<0)return status;
    }
    return 0;
}
static ScU32 scaled(ScU32 count,ScU32 maximum,ScU32 width)
{
    ScU32 used=0,remainder=0;
    if(!maximum)return 0;
    /* 三根柱最多遍历可见像素宽；count由本次受限文档行数验证，不乘溢出。 */
    for(ScU32 x=0;x<width;x++){
        remainder+=count;
        if(remainder>=maximum){
            used++;
            remainder-=maximum;
        }
    }
    return used;
}
static ScStatus graph(ScUiContext *ui,ScUiNode node,ScUiPainter *p,void *unused)
{
    (void)ui;
    (void)node;
    (void)unused;
    ScUiRect bounds;
    ScStatus status=ScUiPainterBounds(p,&bounds);
    if(status<0)return status;
    ScUiPainterFill(p,&bounds,SC32_COLOR_PAPER);
    ScUiRect label={
        8,6,bounds.width-16,40
    };
    ScUiText title=RdText(rd.stats_valid?"整数样本分布":"选择文档，再读取数据并统计。旧结果若源已修改则不作当前结果。");
    ScUiPainterText(p,&label,&title,SC_UI_FONT_BODY,SC_UI_TEXT_WRAP);
    if(!rd.stats_valid)return 0;
    ScU32 counts[3]={
        rd.result.positive,rd.result.zero,rd.result.negative
    },colors[3]={
        SC32_COLOR_GREEN,SC32_COLOR_GOLD,SC32_COLOR_ALERT
    };
    const char *names[]={
        "正值","零值","负值"
    };
    ScU32 maximum=0;
    for(ScU32 i=0;i<3;i++)if(counts[i]>maximum)maximum=counts[i];
    for(ScU32 i=0;i<3;i++){
        ScI32 top=52+(ScI32)i*44;
        char text[64];
        snprintf(text,sizeof(text),"%s %u",names[i],counts[i]);
        label.x=8;label.y=top;label.width=bounds.width-16;label.height=20;
        title=RdText(text);
        ScUiPainterText(p,&label,&title,SC_UI_FONT_BODY,0);
        ScU32 width=bounds.width>24?(ScU32)bounds.width-24:0;
        ScUiRect bar={
            8,top+24,(ScI32)scaled(counts[i],maximum,width),12
        };
        ScUiPainterFill(p,&bar,colors[i]);
    }
    return 0;
}
static ScStatus dialog_done(ScUiContext *ui,const ScUiEvent *event,void *unused)
{
    (void)ui;
    (void)unused;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo info;
    RdRecord(&info,sizeof(info));
    ScStatus status=ScUiDialogQuery(dialog,&info);
    rd.dialog=0;
    if(status<0 || info.stage==SC_UI_DIALOG_CANCELLED || info.command==RD_KEEP || info.command==SC_UI_DIALOG_CANCEL){
        rd.dialog_action=rd.dialog_doc=0;
        ScUiDialogRelease(dialog);
        return 0;
    }
    if(rd.dialog_kind==3){
        RdDoc *doc=RdDocument(rd.dialog_doc);
        ScU32 action=rd.dialog_action;
        if(info.command==RD_DISCARD){
            if(doc)RdDocumentClose(doc);
            rd.dialog_action=rd.dialog_doc=0;
            if(action==RD_EXIT)RdRequest(RD_EXIT);
        }
        else if(info.command==RD_DO_SAVE && doc){
            if(doc->path[0])status=RdIoStart(RD_IO_SAVE,doc->path,doc);
            else {
                rd.active=doc;
                ScUiDialogRelease(dialog);
                rd.dialog_kind=0;
                RdRequest(RD_SAVE_AS);
                return 0;
            }
        }
    }
    else {
        ScString *path=0;
        const char *name=0;
        status=ScUiDialogSelectedPath(dialog,0,&path);
        if(!status)status=ScStringCStr(path,&name);
        if(!status)status=RdIoStart(rd.dialog_kind==1?RD_IO_OPEN:RD_IO_SAVE,name,RdDocument(rd.dialog_doc));
        ScStringRelease(path);
    }
    if(status<0){
        rd.dialog_action=rd.dialog_doc=0;
        RdError(status,"无法开始文件操作");
    }
    ScUiDialogRelease(dialog);
    rd.dialog_kind=0;
    return 0;
}
void RdRequest(ScU32 action)
{
    if(rd.dialog)return;
    if(rd.io.kind){
        RdStatus("文件事务进行中，可先取消后台操作再关闭。");
        return;
    }
    if(action==RD_OPEN || action==RD_SAVE_AS){
        ScUiFileDialogDesc d;
        RdRecord(&d,sizeof(d));
        d.owner=rd.root;
        d.flags=action==RD_OPEN?SC_UI_FILE_OPEN:SC_UI_FILE_SAVE|SC_UI_FILE_OVERWRITE;
        d.title=RdText(action==RD_OPEN?"打开课题数据或笔记":"条件保存");
        d.directory="/HOME";
        d.completion=dialog_done;
        rd.dialog_kind=action==RD_OPEN?1:2;
        rd.dialog_doc=rd.active?rd.active->id:0;
        ScStatus status=ScUiFileDialog(rd.ui,&d,&rd.dialog);
        if(status<0)RdError(status,"文件对话框失败");
        return;
    }
    RdDoc *doc=rd.active;
    if(action==RD_EXIT){
        doc=rd.docs;
        while(doc && !dirty(doc))doc=doc->next;
    }
    if((action==RD_CLOSE || action==RD_EXIT) && doc && dirty(doc)){
        ScUiMenuItem buttons[3];
        const char *labels[]={
            "保存","丢弃并继续","取消"
        };
        ScU32 ids[]={
            RD_DO_SAVE,RD_DISCARD,RD_KEEP
        };
        for(ScU32 i=0;i<3;i++){
            RdRecord(buttons+i,sizeof(buttons[i]));
            buttons[i].command=ids[i];
            buttons[i].label=RdText(labels[i]);
        }
        ScUiMessageBoxDesc d;
        RdRecord(&d,sizeof(d));
        d.owner=rd.root;
        d.title=RdText(doc->label);
        d.message=RdText("文档尚未保存。如何继续？");
        d.buttons=buttons;
        d.button_count=3;
        d.default_command=RD_DO_SAVE;
        d.cancel_command=RD_KEEP;
        d.completion=dialog_done;
        rd.dialog_action=action;
        rd.dialog_doc=doc->id;
        rd.dialog_kind=3;
        ScStatus status=ScUiMessageBox(rd.ui,&d,&rd.dialog);
        if(status<0)RdError(status,"未保存确认失败");
        return;
    }
    if(action==RD_CLOSE && doc)RdDocumentClose(doc);
    else if(action==RD_EXIT){
        rd.quitting=1;
        RdParseCancel();
    }
}
ScStatus RdFind(void)
{
    if(!rd.active)return SC_UI_E_NOT_FOUND;
    ScString *needle=0;
    ScStatus status=ScUiNodeCopyText(rd.ui,rd.find_box,&needle);
    if(status<0)return status;
    ScView view;
    RdRecord(&view,sizeof(view));
    ScStringView(needle,&view);
    if(!view.bytes){
        ScStringRelease(needle);
        ScUiFocus(rd.ui,rd.find_box);
        return 0;
    }
    ScUiTextSelection selection;
    RdRecord(&selection,sizeof(selection));
    ScUiEditGetSelection(rd.ui,rd.active->edit,&selection);
    ScUiFindDesc desc;
    RdRecord(&desc,sizeof(desc));
    desc.flags=SC_UI_FIND_WRAP;
    desc.start=selection.caret;
    ScUiDocumentInfo information;RdRecord(&information,sizeof(information));
    status=ScUiDocumentQuery(rd.active->document,&information);if(status<0){ScStringRelease(needle);return status;}
    desc.end=information.bytes;if(desc.start>desc.end)desc.start=0;
    RdRecord(&desc.needle,sizeof(desc.needle));desc.needle.data=(const char *)view.data;desc.needle.bytes=view.bytes;
    ScUiTextSelection found;
    RdRecord(&found,sizeof(found));
    status=ScUiDocumentFind(rd.active->document,&desc,&found);
    if(!status)status=ScUiEditSetSelection(rd.ui,rd.active->edit,&found);
    ScStringRelease(needle);
    return status;
}
static void navigate(void)
{
    ScUiSelectionInfo selected;
    RdRecord(&selected,sizeof(selected));
    if(ScUiSelectionQuery(rd.table_selection,&selected)<0 || !rd.count)return;
    if(selected.current<rd.rows[0].id || selected.current-rd.rows[0].id>=rd.count)return;
    RdRow *row=rd.rows+selected.current-rd.rows[0].id;
    RdDoc *doc=RdDocument(rd.source_doc);
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    if(!doc || ScUiDocumentQuery(doc->document,&info)<0 || info.revision!=rd.source_revision){
        RdStatus("表格来自旧修订，请重新读取数据。");
        return;
    }
    rd.active=doc;
    ScUiTabSelect(rd.ui,rd.tabs,doc->page);
    ScUiTextSelection selection;
    RdRecord(&selection,sizeof(selection));
    selection.anchor=selection.caret=row->offset;
    selection.revision=info.revision;
    ScUiEditSetSelection(rd.ui,doc->edit,&selection);
    ScUiFocus(rd.ui,doc->edit);
}
ScStatus RdAction(ScUiContext *ui,const ScUiEvent *event,void *unused)
{
    (void)ui;
    (void)unused;
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST && event->node==rd.root){
        RdRequest(RD_EXIT);
        return SC_UI_VETO;
    }
    if(event->type==SC_UI_EVENT_VALUE_CHANGED){
        for(RdDoc *d=rd.docs;d;d=d->next)if(event->node==d->edit){
            RdChanged(d);
            break;
        }
        return 0;
    }
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED){
        if(event->node==rd.tabs && event->bytes==4){
            ScU32 page=*(const ScU32 *)event->data;
            for(RdDoc *d=rd.docs;d;d=d->next)if(d->page==page)rd.active=d;
            RdRefresh();
        }
        else if(event->node==rd.tree){
            ScUiSelectionInfo selected;
            RdRecord(&selected,sizeof(selected));
            if(!ScUiSelectionQuery(rd.tree_selection,&selected) && selected.current>1){
                RdDoc *doc=RdDocument(selected.current-1);
                if(doc){
                    rd.active=doc;
                    ScUiTabSelect(rd.ui,rd.tabs,doc->page);
                    ScUiFocus(rd.ui,doc->edit);
                    RdRefresh();
                }
            }
        }
        return 0;
    }
    if(event->type!=SC_UI_EVENT_COMMAND)return 0;
    ScU32 command=event->command;
    RdDoc *doc=rd.active;
    ScStatus status=0;
    if(command==1401){
        navigate();
        return SC_UI_HANDLED;
    }
    if(command==RD_NEW)status=RdDocumentCreate("","",0,0,0);
    else if(command==RD_OPEN || command==RD_SAVE_AS || command==RD_CLOSE || command==RD_EXIT)RdRequest(command);
    else if(command==RD_SAVE && doc){
        if(doc->path[0])status=RdIoStart(RD_IO_SAVE,doc->path,doc);
        else RdRequest(RD_SAVE_AS);
    }
    else if(command==RD_PARSE)status=RdParseStart();
    else if(command==RD_PLUGIN)status=RdPluginLoad();
    else if(command==RD_RELEASE)status=RdPluginRelease();
    else if(command==RD_CANCEL){
        RdIoCancel();
        RdParseCancel();
        RdStatus("已请求取消，文件操作等待真实终态。");
    }
    else if(command==RD_FIND || command==RD_FIND_NEXT)status=RdFind();
    else if(command==RD_ABOUT)RdStatus("ResearchDesk · 四个独立TU + RDSTATS.SCL · 只用公开SDK。数据行：标签<TAB>32位整数。");
    else if(doc){
        if(command==RD_UNDO)status=ScUiDocumentUndo(doc->document);
        else if(command==RD_REDO)status=ScUiDocumentRedo(doc->document);
        else if(command==RD_CUT)status=ScUiEditCut(rd.ui,doc->edit);
        else if(command==RD_COPY)status=ScUiEditCopy(rd.ui,doc->edit);
        else if(command==RD_PASTE)status=ScUiEditPaste(rd.ui,doc->edit);
        else if(command==RD_ALL)status=ScUiEditSelectAll(rd.ui,doc->edit);
        else if(command==RD_FONT_SMALL || command==RD_FONT_NORMAL)status=ScUiNodeSetFont(rd.ui,doc->edit,command==RD_FONT_SMALL?SC_UI_FONT_DENSE:SC_UI_FONT_CODE);
    }
    if(status<0)RdError(status,"操作失败，当前作品保留");
    RdRefresh();
    return SC_UI_HANDLED;
}
ScStatus RdUiCreate(void)
{
    rd.tree_revision=rd.table_revision=1;
    ScUiContextDesc context;
    RdRecord(&context,sizeof(context));
    context.default_font_face=context.default_font_pixels=16;
    context.cache_budget_bytes=262144;
    context.undo_budget_bytes=1048576;
    context.scheduling_quantum=8;
    ScStatus status=ScUiContextCreate(&context,&rd.ui);
    if(status<0)return status;
    ScUiWindowDesc window;
    RdRecord(&window,sizeof(window));
    window.title=RdText("ResearchDesk — 课题台");
    window.x=64;
    window.y=44;
    window.client_width=960;
    window.client_height=650;
    window.style=SC32_STYLE_STANDARD;
    status=ScUiWindowCreate(rd.ui,&window,&rd.root);
    if(status<0)return status;
    status=layout(rd.root,SC_UI_DOCK_LAYOUT,0,SC_UI_AUTO,SC_UI_AUTO,0);
    if(!status)status=widget(SC_UI_MENUBAR,rd.root,0,"",&rd.menu);
    if(!status)status=layout(rd.menu,SC_UI_ABSOLUTE,SC_UI_DOCK_TOP,SC_UI_AUTO,SC_UI_AUTO,0);
    if(!status)status=widget(SC_UI_TOOLBAR,rd.root,0,"",&rd.toolbar);
    if(!status)status=layout(rd.toolbar,SC_UI_ABSOLUTE,SC_UI_DOCK_TOP,SC_UI_AUTO,SC_UI_AUTO,0);
    if(!status)status=widget(SC_UI_TEXTBOX,rd.root,RD_FIND,"查找文本",&rd.find_box);
    if(!status)status=layout(rd.find_box,SC_UI_ABSOLUTE,SC_UI_DOCK_TOP,SC_UI_AUTO,28,0);
    if(!status)status=widget(SC_UI_STATUS,rd.root,0,"",&rd.status);
    if(!status)status=layout(rd.status,SC_UI_ABSOLUTE,SC_UI_DOCK_BOTTOM,SC_UI_AUTO,SC_UI_AUTO,0);
    if(!status)status=widget(SC_UI_SPLITTER,rd.root,0,"",&rd.left_split);
    if(!status)status=layout(rd.left_split,SC_UI_HBOX,SC_UI_DOCK_FILL,SC_UI_AUTO,SC_UI_AUTO,1);
    if(!status)status=widget(SC_UI_TREE,rd.left_split,0,"",&rd.tree);
    if(!status)status=layout(rd.tree,SC_UI_ABSOLUTE,0,180,SC_UI_AUTO,0);
    if(!status)status=widget(SC_UI_SPLITTER,rd.left_split,0,"",&rd.body_split);
    if(!status)status=layout(rd.body_split,SC_UI_HBOX,0,SC_UI_AUTO,SC_UI_AUTO,1);
    if(!status)status=widget(SC_UI_TAB,rd.body_split,0,"",&rd.tabs);
    if(!status)status=layout(rd.tabs,SC_UI_ABSOLUTE,0,SC_UI_AUTO,SC_UI_AUTO,2);
    if(!status)status=widget(SC_UI_SPLITTER,rd.body_split,0,"",&rd.right);
    if(!status)status=layout(rd.right,SC_UI_VBOX,0,320,SC_UI_AUTO,1);
    if(!status)status=widget(SC_UI_TABLE,rd.right,1401,"",&rd.table);
    if(!status)status=layout(rd.table,SC_UI_ABSOLUTE,0,SC_UI_AUTO,300,1);
    ScUiClassDesc klass;
    RdRecord(&klass,sizeof(klass));
    klass.name="ResearchDesk.Distribution";
    klass.paint=graph;
    if(!status)status=ScUiClassRegister(rd.ui,&klass,&rd.chart_class);
    if(!status)status=widget(rd.chart_class,rd.right,0,"",&rd.chart);
    if(!status)status=layout(rd.chart,SC_UI_ABSOLUTE,0,SC_UI_AUTO,210,1);
    ScUiProviderDesc provider;
    RdRecord(&provider,sizeof(provider));
    provider.flags=SC_UI_PROVIDER_TREE;
    provider.procedure=RdTreeData;
    if(!status)status=ScUiProviderCreate(&provider,&rd.tree_provider);
    provider.flags=SC_UI_PROVIDER_SORT;
    provider.procedure=RdTableData;
    if(!status)status=ScUiProviderCreate(&provider,&rd.table_provider);
    if(!status)status=ScUiSelectionCreate(rd.tree_provider,SC_UI_SELECTION_ALLOW_EMPTY,&rd.tree_selection);
    if(!status)status=ScUiSelectionCreate(rd.table_provider,SC_UI_SELECTION_ALLOW_EMPTY,&rd.table_selection);
    if(!status)status=ScUiViewBind(rd.ui,rd.tree,rd.tree_provider,rd.tree_selection);
    if(!status)status=ScUiViewBind(rd.ui,rd.table,rd.table_provider,rd.table_selection);
    ScUiColumnDesc columns[3];
    const char *titles[]={
        "标签","数值","源行"
    };
    for(ScU32 i=0;i<3;i++){
        RdRecord(columns+i,sizeof(columns[i]));
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].title=RdText(titles[i]);
        columns[i].width=i?84:180;
        columns[i].min_width=48;
        columns[i].max_width=1000;
        columns[i].flags=SC_UI_COLUMN_SORTABLE;
        columns[i].format=i==1?SC_UI_FORMAT_I32:i==2?SC_UI_FORMAT_U32:SC_UI_FORMAT_TEXT;
        columns[i].sort_kind=i==1?SC_UI_SORT_SIGNED:i==2?SC_UI_SORT_UNSIGNED:SC_UI_SORT_TEXT;
    }
    if(!status)status=ScUiTableSetColumns(rd.ui,rd.table,columns,3);
    for(ScU32 i=0;i<6 && !status;i++)status=ScUiMenuCreate(rd.menus+i);
    const ScU32 file[]={
        RD_NEW,RD_OPEN,RD_SAVE,RD_SAVE_AS,RD_CLOSE,0,RD_EXIT
    },edit[]={
        RD_UNDO,RD_REDO,0,RD_CUT,RD_COPY,RD_PASTE,RD_ALL,0,RD_FIND,RD_FIND_NEXT
    },analysis[]={
        RD_PARSE,RD_PLUGIN,RD_RELEASE,RD_CANCEL
    },view[]={
        RD_FONT_SMALL,RD_FONT_NORMAL,RD_ABOUT
    },tools[]={
        RD_NEW,RD_OPEN,RD_SAVE,0,RD_UNDO,RD_REDO,0,RD_PARSE,RD_CANCEL
    };
    if(!status)status=menu_items(rd.menus[1],file,sizeof(file)/4);
    if(!status)status=menu_items(rd.menus[2],edit,sizeof(edit)/4);
    if(!status)status=menu_items(rd.menus[3],analysis,sizeof(analysis)/4);
    if(!status)status=menu_items(rd.menus[4],view,sizeof(view)/4);
    if(!status)status=menu_items(rd.menus[5],tools,sizeof(tools)/4);
    const char *names[]={
        "文件 (&F)","编辑 (&E)","分析 (&A)","视图 (&V)"
    };
    for(ScU32 i=0;i<4 && !status;i++){
        ScUiMenuItem item;
        RdRecord(&item,sizeof(item));
        item.label=RdText(names[i]);
        item.submenu=rd.menus[i+1];
        ScU32 id;
        status=ScUiMenuInsert(rd.menus[0],0,&item,&id);
    }
    if(!status)status=ScUiMenuBind(rd.ui,rd.menu,rd.menus[0]);
    if(!status)status=ScUiToolbarBind(rd.ui,rd.toolbar,rd.menus[5]);
    ScUiSubscription subscription;
    RdRecord(&subscription,sizeof(subscription));
    subscription.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    subscription.procedure=RdAction;
    ScU32 token;
    if(!status)status=ScUiListen(rd.ui,rd.root,&subscription,&token);
    if(!status)status=ScUiTreeExpand(rd.ui,rd.tree,1,1);
    return status;
}
