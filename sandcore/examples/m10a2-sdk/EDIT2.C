#include <SCUI.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/* 两个窗口共享一个Document；应用只负责命令和文件事务，不画控件。
 * 没有绘图timer或轮询，隐藏后的caret/损伤由框架按真实可见性停用。 */

enum
{
    CMD_NEW=100,CMD_OPEN,CMD_SAVE,CMD_SAVE_AS,CMD_UNDO,CMD_REDO,
    CMD_COPY,CMD_CUT,CMD_PASTE,CMD_SELECT_ALL,CMD_FIND,CMD_EXIT,
    CHOOSE_SAVE=200,CHOOSE_DISCARD,CHOOSE_CANCEL,
    DIALOG_OPEN=1,DIALOG_SAVE,DIALOG_CLOSE
};

typedef struct
{
    ScUiNode root,editor,status,search;
} EditorView;

typedef struct
{
    ScUiContext *ui;
    ScUiDocument *document;
    ScUiMenu *menu,*toolbar;
    ScUiDialog *dialog;
    ScU32 dialog_kind,focused,closing,close_all,busy;
    ScString *path;
    EditorView views[2];
} EditorApp;

static EditorApp app;
static void text(ScUiText *out,const char *value)
{
    memset(out,0,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->data=value;
    out->bytes=(ScU32)strlen(value);
}

static int live(ScUiNode id)
{
    ScUiNodeInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    return id && ScUiNodeQuery(app.ui,id,&info)==0;
}

static void status_line(const char *value)
{
    ScUiText label;
    text(&label,value);
    for(ScU32 i=0;i<2;i++)if(live(app.views[i].status))ScUiNodeSetText(app.ui,app.views[i].status,&label);
}

static void operation_error(ScStatus code,const char *where)
{
    char line[160];
    snprintf(line,sizeof(line),"%s：未完成 (%d)",where,code);
    status_line(line);
    fprintf(stderr,"EDIT2 operation=%s status=%d errno=%d\n",where,code,errno);
}

static ScStatus framework_error(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    ScUiErrorInfo error;
    memset(&error,0,sizeof(error));
    error.size=sizeof(error);
    error.version=1;
    if(ScUiErrorQuery(ui,&error)==0)fprintf(stderr,"EDIT2 UI error=%d cause=%d domain=%u raw=%d operation=%u node=%u\n",error.code,error.cause_code,error.domain,error.raw,error.operation,error.node);
    (void)event;
    (void)context;
    return SC_UI_HANDLED;
}

static ScStatus document_info(ScUiDocumentInfo *out)
{
    memset(out,0,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    return ScUiDocumentQuery(app.document,out);
}

static void refresh_titles(void)
{
    ScUiDocumentInfo info;
    const char *path="未命名";
    char title[180],line[160];
    ScUiText caption;
    if(document_info(&info)<0)return;
    if(app.path)ScStringCStr(app.path,&path);
    for(ScU32 i=0;i<2;i++)if(live(app.views[i].root))
    {
        snprintf(title,sizeof(title),"%s%s — 编辑视图 %u",info.flags&SC_UI_DOCUMENT_DIRTY?"* ":"",path,i+1);
        /* 原生标题最多127B；长路径只影响摘要，完整路径对象仍用于文件I/O。 */
        if(strlen(title)>120)snprintf(title,sizeof(title),"%s文档 — 编辑视图 %u",info.flags&SC_UI_DOCUMENT_DIRTY?"* ":"",i+1);
        text(&caption,title);
        ScUiWindowSetTitle(app.ui,app.views[i].root,&caption);
    }
    snprintf(line,sizeof(line),"%u 字节 · %u 行 · %s · 两个视图共享 Undo",info.bytes,info.lines,info.flags&SC_UI_DOCUMENT_DIRTY?"未保存":"已保存");
    status_line(line);
}

static ScUiNode active_editor(ScUiNode root)
{
    for(ScU32 i=0;i<2;i++)if(app.views[i].root==root && live(app.views[i].editor))return app.views[i].editor;
    if(live(app.focused))return app.focused;
    for(ScU32 i=0;i<2;i++)if(live(app.views[i].editor))return app.views[i].editor;
    return 0;
}

static void finish_close(void)
{
    ScUiNode closing=app.closing;
    ScU32 all=app.close_all;
    app.closing=0;
    app.close_all=0;
    if(all)
    {
        for(ScU32 i=0;i<2;i++)if(live(app.views[i].root))ScUiNodeDestroy(app.ui,app.views[i].root);
    }
    else if(live(closing))ScUiNodeDestroy(app.ui,closing);
}

static ScStatus save_path(ScString *candidate)
{
    ScUiDocumentInfo info;
    ScString *copy=0;
    ScView view;
    const char *path=0;
    FILE *file=0;
    ScU32 retained=0;
    ScStatus s=document_info(&info);
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(s==0)s=ScStringCStr(candidate,&path);
    if(s==0)s=ScUiDocumentCopy(app.document,0,info.bytes,&copy);
    if(s==0)s=ScStringView(copy,&view);
    if(s==0)
    {
        s=ScStringRetain(candidate);
        if(s==0)retained=1;
    }
    /* 所有内存候选先准备；磁盘写失败保正文/dirty。文件自身不承诺原子
     * rename事务，失败可能留下部分目标，状态明确供用户重试。 */
    if(s==0)
    {
        file=fopen(path,"wb");
        if(!file)s=SC_UI_E_IO;
    }
    if(s==0 && fwrite(view.data,1,view.bytes,file)!=view.bytes)s=SC_UI_E_IO;
    if(file && fclose(file)!=0)s=SC_UI_E_IO;
    if(s==0)s=ScUiDocumentMarkSaved(app.document);
    if(s==0)
    {
        ScStringRelease(app.path);
        app.path=candidate;
        retained=0;
        refresh_titles();
        if(app.closing)finish_close();
    }
    if(retained)ScStringRelease(candidate);
    ScStringRelease(copy);
    return s;
}

static ScStatus open_path(ScString *candidate)
{
    const char *path=0;
    ScBuffer *buffer=0;
    ScView view;
    ScUiDocumentInfo info;
    FILE *file=0;
    ScU32 retained=0;
    ScStatus s=ScStringCStr(candidate,&path);
    char chunk[4096];
    if(s==0)s=ScBufferCreate(4096,&buffer);
    if(s==0)
    {
        file=fopen(path,"rb");
        if(!file)s=SC_UI_E_IO;
    }
    while(s==0)
    {
        size_t count=fread(chunk,1,sizeof(chunk),file);
        if(count)
        {
            s=ScBufferAppend(buffer,chunk,(ScU32)count);
            if(s<0)break;
        }
        if(count<sizeof(chunk))
        {
            if(ferror(file))s=SC_UI_E_IO;
            break;
        }
    }
    if(file && fclose(file)!=0)s=SC_UI_E_IO;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(s==0)s=ScBufferView(buffer,&view);
    ScU32 bad=0;
    if(s==0)s=ScUtf8Validate(view.data,view.bytes,&bad);
    if(s==0)s=document_info(&info);
    if(s==0)
    {
        s=ScStringRetain(candidate);
        if(s==0)retained=1;
    }
    if(s==0)
    {
        ScUiText content;
        memset(&content,0,sizeof(content));
        content.size=sizeof(content);
        content.version=1;
        content.data=(const char *)view.data;
        content.bytes=view.bytes;
        /* 替换同一个Document，不依次重新绑定两个view造成半打开状态。 */
        app.busy=1;
        s=ScUiDocumentReplace(app.document,0,info.bytes,&content,SC_UI_REPLACE_NO_UNDO);
        app.busy=0;
    }
    if(s==0)s=ScUiDocumentMarkSaved(app.document);
    if(s==0)
    {
        ScStringRelease(app.path);
        app.path=candidate;
        retained=0;
        refresh_titles();
    }
    if(retained)ScStringRelease(candidate);
    ScBufferRelease(buffer);
    return s;
}

static ScStatus file_dialog_done(ScUiContext *,const ScUiEvent *,void *);
static ScStatus close_dialog_done(ScUiContext *,const ScUiEvent *,void *);
static ScStatus file_dialog(ScUiNode owner,ScU32 save)
{
    ScUiFileDialogDesc desc;
    ScUiMenuItem filter;
    ScStatus s;
    if(app.dialog)return SC_UI_E_BUSY;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.owner=owner;
    desc.flags=save?(SC_UI_FILE_SAVE|SC_UI_FILE_OVERWRITE):SC_UI_FILE_OPEN;
    text(&desc.title,save?"保存 UTF-8 文档":"打开 UTF-8 文档");
    desc.directory="/HOME";
    desc.completion=file_dialog_done;
    desc.context=&app;
    memset(&filter,0,sizeof(filter));
    filter.size=sizeof(filter);
    filter.version=1;
    filter.command=1;
    text(&filter.label,"文本 / 源码");
    text(&filter.help,"*.TXT;*.txt;*.C;*.H;*.c;*.h");
    desc.filters=&filter;
    desc.filter_count=1;
    s=ScUiFileDialog(app.ui,&desc,&app.dialog);
    if(s==0)app.dialog_kind=save?DIALOG_SAVE:DIALOG_OPEN;
    return s;
}

static ScStatus save_document(ScUiNode owner,ScU32 choose_path)
{
    if(choose_path || !app.path)return file_dialog(owner,1);
    return save_path(app.path);
}

static ScStatus file_dialog_done(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo info;
    ScString *path=0;
    ScStatus s;
    ScU32 kind=app.dialog_kind;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.stage==SC_UI_DIALOG_DONE)s=ScUiDialogSelectedPath(dialog,0,&path);
    if(app.dialog==dialog)
    {
        app.dialog=0;
        app.dialog_kind=0;
    }
    ScUiDialogRelease(dialog);
    if(s==0 && path)s=kind==DIALOG_SAVE?save_path(path):open_path(path);
    else if(s==0)
    {
        app.closing=0;
        app.close_all=0;
        status_line("已取消；文档保留");
    }
    if(s<0)
    {
        app.closing=0;
        app.close_all=0;
        operation_error(s,kind==DIALOG_SAVE?"保存":"打开");
    }
    ScStringRelease(path);
    (void)ui;
    (void)context;
    return SC_UI_HANDLED;
}

static ScStatus ask_close(ScUiNode root,ScU32 all)
{
    ScUiDocumentInfo info;
    ScStatus s=document_info(&info);
    if(s<0)return s;
    if(!(info.flags&SC_UI_DOCUMENT_DIRTY))
    {
        app.closing=root;
        app.close_all=all;
        finish_close();
        return SC_UI_HANDLED;
    }
    if(app.dialog)return SC_UI_VETO;
    ScUiMessageBoxDesc desc;
    ScUiMenuItem buttons[3];
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.owner=root;
    text(&desc.title,"未保存的文档");
    text(&desc.message,"保存当前文档后关闭？");
    desc.completion=close_dialog_done;
    desc.context=&app;
    memset(buttons,0,sizeof(buttons));
    const char *names[3]=
    {
        "保存","放弃","取消"
    };
    for(ScU32 i=0;i<3;i++)
    {
        buttons[i].size=sizeof(buttons[i]);
        buttons[i].version=1;
        buttons[i].command=CHOOSE_SAVE+i;
        text(&buttons[i].label,names[i]);
    }
    desc.buttons=buttons;
    desc.button_count=3;
    desc.default_command=CHOOSE_SAVE;
    desc.cancel_command=CHOOSE_CANCEL;
    s=ScUiMessageBox(app.ui,&desc,&app.dialog);
    if(s==0)
    {
        app.dialog_kind=DIALOG_CLOSE;
        app.closing=root;
        app.close_all=all;
        return SC_UI_VETO;
    }
    return s;
}

static ScStatus close_dialog_done(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    ScUiNode owner=app.closing;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScU32 command=event->command;
    if(app.dialog==dialog)
    {
        app.dialog=0;
        app.dialog_kind=0;
    }
    ScUiDialogRelease(dialog);
    if(command==CHOOSE_DISCARD)finish_close();
    else if(command==CHOOSE_SAVE)
    {
        ScStatus s=save_document(owner,0);
        if(s<0)
        {
            app.closing=0;
            app.close_all=0;
            operation_error(s,"保存后关闭");
        }
    }
    else
    {
        app.closing=0;
        app.close_all=0;
        status_line("已取消关闭");
    }
    (void)ui;
    (void)context;
    return SC_UI_HANDLED;
}

static ScStatus find_text(ScUiNode root)
{
    ScUiNode edit=active_editor(root),search=0;
    ScString *needle=0;
    ScView bytes;
    ScUiDocumentInfo info;
    ScUiTextSelection selection;
    ScUiFindDesc find;
    ScStatus s;
    for(ScU32 i=0;i<2;i++)if(app.views[i].root==root)search=app.views[i].search;
    if(!search || !edit)return SC_UI_E_STALE;
    s=ScUiNodeCopyText(app.ui,search,&needle);
    memset(&bytes,0,sizeof(bytes));
    bytes.size=sizeof(bytes);
    bytes.version=1;
    memset(&info,0,sizeof(info));
    if(s==0)s=ScStringView(needle,&bytes);
    if(s==0 && !bytes.bytes)s=SC_UI_E_NOT_FOUND;
    memset(&selection,0,sizeof(selection));
    selection.size=sizeof(selection);
    selection.version=1;
    if(s==0)s=ScUiEditGetSelection(app.ui,edit,&selection);
    if(s==0)s=document_info(&info);
    memset(&find,0,sizeof(find));
    find.size=sizeof(find);
    find.version=1;
    find.flags=SC_UI_FIND_WRAP;
    find.start=selection.caret;
    find.end=info.bytes;
    find.needle.size=sizeof(find.needle);
    find.needle.version=1;
    find.needle.data=(const char *)bytes.data;
    find.needle.bytes=bytes.bytes;
    if(s==0)s=ScUiDocumentFind(app.document,&find,&selection);
    if(s==0)s=ScUiEditSetSelection(app.ui,edit,&selection);
    if(s==0)s=ScUiFocus(app.ui,edit);
    ScStringRelease(needle);
    return s;
}

static ScStatus command_query(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    ScUiCommandDesc *desc=(ScUiCommandDesc *)event->data;
    ScUiDocumentInfo info;
    ScUiTextSelection selected;
    ScUiNode edit=active_editor(0);
    if(!desc || document_info(&info)<0)return 0;
    ScU32 disabled=app.dialog!=0;
    if(event->command==CMD_UNDO)disabled|=!info.can_undo;
    if(event->command==CMD_REDO)disabled|=!info.can_redo;
    if(event->command==CMD_COPY || event->command==CMD_CUT)
    {
        memset(&selected,0,sizeof(selected));
        selected.size=sizeof(selected);
        selected.version=1;
        disabled|=!edit || ScUiEditGetSelection(app.ui,edit,&selected)<0 || selected.anchor==selected.caret;
    }
    if(disabled)desc->flags|=SC_UI_MENU_DISABLED;
    else desc->flags&=~SC_UI_MENU_DISABLED;
    (void)ui;
    (void)context;
    return 0;
}

static ScStatus commands(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    ScStatus s=0;
    ScUiNode edit=active_editor(event->root);
    ScUiDocumentInfo info;
    if(app.dialog)return SC_UI_HANDLED;
    switch(event->command)
    {
        case CMD_NEW:s=document_info(&info);
        if(s==0 && (info.flags&SC_UI_DOCUMENT_DIRTY))
        {
            status_line("请先保存或关闭当前文档，再新建");
            break;
        }
        if(s==0)
        {
            ScUiText empty;
            text(&empty,"");
            app.busy=1;
            s=ScUiDocumentReplace(app.document,0,info.bytes,&empty,SC_UI_REPLACE_NO_UNDO);
            app.busy=0;
        }
        if(s==0)
        {
            ScStringRelease(app.path);
            app.path=0;
            s=ScUiDocumentMarkSaved(app.document);
            refresh_titles();
        }
        break;
        case CMD_OPEN:s=document_info(&info);
        if(s==0 && (info.flags&SC_UI_DOCUMENT_DIRTY))status_line("请先保存当前文档，再打开其它文件");
        else if(s==0)s=file_dialog(event->root,0);
        break;
        case CMD_SAVE:s=save_document(event->root,0);
        break;
        case CMD_SAVE_AS:s=save_document(event->root,1);
        break;
        case CMD_UNDO:s=ScUiDocumentUndo(app.document);
        break;
        case CMD_REDO:s=ScUiDocumentRedo(app.document);
        break;
        case CMD_COPY:s=ScUiEditCopy(ui,edit);
        break;
        case CMD_CUT:s=ScUiEditCut(ui,edit);
        break;
        case CMD_PASTE:s=ScUiEditPaste(ui,edit);
        break;
        case CMD_SELECT_ALL:s=ScUiEditSelectAll(ui,edit);
        break;
        case CMD_FIND:s=find_text(event->root);
        break;
        case CMD_EXIT:s=ask_close(event->root,1);
        if(s==SC_UI_VETO)s=0;
        break;
        default:return 0;
    }
    if(s<0)operation_error(s,"命令");
    else if(!app.dialog)refresh_titles();
    (void)context;
    return SC_UI_HANDLED;
}

static ScStatus editor_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    if(event->type==SC_UI_EVENT_FOCUS_IN)app.focused=event->node;
    else if(event->type==SC_UI_EVENT_VALUE_CHANGED && !app.busy)refresh_titles();
    (void)ui;
    (void)context;
    return 0;
}

static ScStatus root_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST)
    {
        ScU32 other=0;
        for(ScU32 i=0;i<2;i++)if(app.views[i].root!=event->root && live(app.views[i].root))other=1;
        if(other)return 0;
        ScStatus s=ask_close(event->root,0);
        if(s<0)
        {
            operation_error(s,"关闭");
            return SC_UI_VETO;
        }
        return s;
    }
    if(event->type==SC_UI_EVENT_DESTROY)
    {
        for(ScU32 i=0;i<2;i++)if(app.views[i].root==event->root)
        {
            app.views[i].root=0;
            app.views[i].editor=0;
            app.views[i].status=0;
            app.views[i].search=0;
        }
    }
    (void)ui;
    (void)context;
    return 0;
}

static ScStatus layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex)
{
    ScUiLayoutDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.mode=mode;
    desc.width=width;
    desc.height=height;
    desc.flex_weight=flex;
    desc.gap=8;
    desc.column_span=desc.row_span=1;
    desc.align_h=SC_UI_STRETCH;
    desc.align_v=SC_UI_STRETCH;
    for(ScU32 i=0;i<2;i++)if(node==app.views[i].root)
    {
        for(ScU32 side=0;side<4;side++)desc.padding[side]=8;
    }
    return ScUiNodeSetLayout(app.ui,node,&desc);
}

static ScStatus widget(ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScU32 flags,ScUiNode *out)
{
    ScUiWidgetDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.parent=parent;
    desc.kind=kind;
    desc.command=command;
    desc.flags=flags;
    text(&desc.text,label);
    return ScUiWidgetCreate(app.ui,&desc,out);
}

static ScStatus listen(ScUiNode node,ScU32 type,ScU32 phases,ScUiEventProc procedure)
{
    ScUiSubscription desc;
    ScU32 token;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.type=type;
    desc.phases=phases;
    desc.procedure=procedure;
    desc.context=&app;
    return ScUiListen(app.ui,node,&desc,&token);
}

static ScStatus menu_command(ScUiMenu *menu,ScU32 id,const char *label,ScU32 key,ScU32 modifiers)
{
    ScUiMenuItem item;
    ScUiCommandDesc command;
    ScU32 inserted;
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.command=id;
    text(&item.label,label);
    item.shortcut.size=sizeof(item.shortcut);
    item.shortcut.version=1;
    item.shortcut.key=key;
    item.shortcut.modifiers=modifiers;
    memset(&command,0,sizeof(command));
    command.size=sizeof(command);
    command.version=1;
    command.command=id;
    text(&command.label,label);
    command.shortcut=item.shortcut;
    ScStatus s=ScUiCommandSet(app.ui,&command);
    if(s==0)s=ScUiMenuInsert(menu,0,&item,&inserted);
    return s;
}

static ScStatus create_menus(void)
{
    ScUiMenu *file=0,*edit=0;
    ScStatus s=ScUiMenuCreate(&app.menu);
    if(s==0)s=ScUiMenuCreate(&app.toolbar);
    if(s==0)s=ScUiMenuCreate(&file);
    if(s==0)s=ScUiMenuCreate(&edit);
    const char *labels[12]=
    {
        "New","Open…","Save","Save as…","Undo","Redo","Copy","Cut","Paste","Select all","Find","Exit"
    };
    const char keys[12]=
    {
        'n','o','s',0,'z','y','c','x','v','a','f',0
    };
    for(ScU32 i=0;s==0 && i<12;i++)s=menu_command(i<4 || i==11?file:edit,CMD_NEW+i,labels[i],(ScU32)keys[i],keys[i]?SC32_MOD_CONTROL:0);
    if(s==0)
    {
        ScUiMenuItem item;
        ScU32 id;
        memset(&item,0,sizeof(item));
        item.size=sizeof(item);
        item.version=1;
        text(&item.label,"&File");
        item.submenu=file;
        s=ScUiMenuInsert(app.menu,0,&item,&id);
        if(s==0)
        {
            text(&item.label,"&Edit");
            item.submenu=edit;
            s=ScUiMenuInsert(app.menu,0,&item,&id);
        }
    }
    for(ScU32 i=0;s==0 && i<3;i++)
    {
        ScUiMenuItem item;
        ScU32 id;
        memset(&item,0,sizeof(item));
        item.size=sizeof(item);
        item.version=1;
        item.command=CMD_OPEN+i;
        text(&item.label,i==0?"Open":i==1?"Save":"Save as");
        s=ScUiMenuInsert(app.toolbar,0,&item,&id);
    }
    ScUiMenuRelease(file);
    ScUiMenuRelease(edit);
    return s;
}

static ScStatus create_view(ScU32 index,ScI32 x,ScI32 y,ScU32 width,ScU32 height)
{
    EditorView *view=app.views+index;
    ScUiWindowDesc window;
    ScUiNode bar=0,menu=0,toolbar=0,label=0,button=0;
    ScStatus s;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    text(&window.title,index?"共享视图 — UTF-8 Editor":"文档 — UTF-8 Editor");
    window.x=x;
    window.y=y;
    window.client_width=width;
    window.client_height=height;
    window.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(app.ui,&window,&view->root);
    if(s==0)s=layout(view->root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0);
    if(s==0)s=listen(view->root,SC_UI_EVENT_COMMAND,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE,commands);
    if(s==0)s=listen(view->root,0,SC_UI_TARGET_PHASE,root_event);
    if(s==0)s=widget(view->root,SC_UI_MENUBAR,"",0,0,&menu);
    if(s==0)s=layout(menu,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0);
    if(s==0)s=ScUiMenuBind(app.ui,menu,app.menu);
    if(s==0)s=widget(view->root,SC_UI_TOOLBAR,"",0,0,&toolbar);
    if(s==0)s=layout(toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,30,0);
    if(s==0)s=ScUiToolbarBind(app.ui,toolbar,app.toolbar);
    if(s==0)s=widget(view->root,SC_UI_PANEL,"",0,0,&bar);
    if(s==0)s=layout(bar,SC_UI_HBOX,SC_UI_AUTO,30,0);
    if(s==0)s=widget(bar,SC_UI_LABEL,"查找",0,0,&label);
    if(s==0)s=layout(label,SC_UI_ABSOLUTE,44,SC_UI_AUTO,0);
    if(s==0)s=widget(bar,SC_UI_TEXTBOX,"",0,0,&view->search);
    if(s==0)s=layout(view->search,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1);
    if(s==0)s=ScUiLabelAssociate(app.ui,label,view->search);
    if(s==0)s=widget(bar,SC_UI_BUTTON,"下一个",CMD_FIND,0,&button);
    if(s==0)s=layout(button,SC_UI_ABSOLUTE,92,SC_UI_AUTO,0);
    if(s==0)s=widget(view->root,SC_UI_EDIT,"",0,index?SC_UI_WRAP:0,&view->editor);
    if(s==0)s=layout(view->editor,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1);
    if(s==0)s=ScUiNodeSetFont(app.ui,view->editor,SC_UI_FONT_CODE);
    if(s==0)s=ScUiEditBind(app.ui,view->editor,app.document);
    if(s==0)s=listen(view->editor,0,SC_UI_TARGET_PHASE,editor_event);
    if(s==0)s=widget(view->root,SC_UI_STATUS,"",0,0,&view->status);
    if(s==0)s=layout(view->status,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0);
    if(s==0)s=ScUiTooltipSet(app.ui,view->editor,&window.title);
    if(s==0)s=ScUiLayout(app.ui,view->root);
    return s;
}

static void release_app(void)
{
    /* 显式关闭还可调用正常用户callback；库late fini不再进入已释放model。 */
    ScUiDialog *dialog=app.dialog;
    app.dialog=0;
    ScUiDialogRelease(dialog);
    if(app.ui)
    {
        for(ScU32 i=0;i<2;i++)if(live(app.views[i].root))ScUiNodeDestroy(app.ui,app.views[i].root);
    }
    ScUiContextRelease(app.ui);
    app.ui=0;
    ScUiDocumentRelease(app.document);
    ScStringRelease(app.path);
    ScUiMenuRelease(app.menu);
    ScUiMenuRelease(app.toolbar);
}

int main(void)
{
    ScUiContextDesc context;
    ScUiDocumentDesc document;
    ScStatus s;
    ScI32 code=0;
    memset(&context,0,sizeof(context));
    context.size=sizeof(context);
    context.version=1;
    context.flags=SC_UI_STOP_LAST_ROOT;
    context.default_font_face=16;
    context.default_font_pixels=16;
    context.error_procedure=framework_error;
    context.context=&app;
    s=ScUiContextCreate(&context,&app.ui);
    memset(&document,0,sizeof(document));
    document.size=sizeof(document);
    document.version=1;
    document.undo_budget_bytes=1048576;
    text(&document.text,"UTF-8 Editor\n同一个 Document，两个独立视图。\n菜单、Undo 和系统剪贴板由 SCUI 提供。\n");
    if(s==0)s=ScUiDocumentCreate(&document,&app.document);
    if(s==0)s=ScUiDocumentMarkSaved(app.document);
    if(s==0)s=create_menus();
    if(s==0)s=listen(0,SC_UI_EVENT_COMMAND_QUERY,SC_UI_TARGET_PHASE,command_query);
    if(s==0)s=create_view(0,40,50,700,520);
    if(s==0)s=create_view(1,770,80,460,440);
    if(s==0)
    {
        app.focused=app.views[0].editor;
        refresh_titles();
        s=ScUiFocus(app.ui,app.focused);
    }
    if(s==0)s=ScUiContextRun(app.ui,&code);
    if(s<0)fprintf(stderr,"EDIT2 stopped status=%d\n",s);
    release_app();
    return s<0?1:code;
}
