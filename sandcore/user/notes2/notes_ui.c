typedef struct {const char *label;ScU32 key,modifiers;} NotesCommand;
static const NotesCommand n_commands[]={
    {"新建",'n',SC32_MOD_CONTROL},{"打开…",'o',SC32_MOD_CONTROL},
    {"保存",'s',SC32_MOD_CONTROL},{"另存为…",'s',SC32_MOD_CONTROL|SC32_MOD_SHIFT},
    {"重新加载",SC32_KEY_F1+2,0},{"退出",0,0},{"撤销",'z',SC32_MOD_CONTROL},
    {"重做",'y',SC32_MOD_CONTROL},{"剪切",'x',SC32_MOD_CONTROL},
    {"复制",'c',SC32_MOD_CONTROL},{"粘贴",'v',SC32_MOD_CONTROL},
    {"全选",'a',SC32_MOD_CONTROL},{"查找 / 替换",'f',SC32_MOD_CONTROL},
    {"下一个",'g',SC32_MOD_CONTROL},{"上一个",'g',SC32_MOD_CONTROL|SC32_MOD_SHIFT},
    {"替换当前",0,0},{"全部替换",0,0},{"收起查找",0,0},
    {"取消文件操作",0,0},{"关于 Notes 2",0,0}
};
static void n_record(void *value,ScU32 bytes)
{memset(value,0,bytes);((ScU32 *)value)[0]=bytes;((ScU32 *)value)[1]=1;}
static ScUiText n_text(const char *text)
{ScUiText value={sizeof(value),1,text,(ScU32)strlen(text)};return value;}
static ScStatus n_layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 weight,ScU32 dock)
{
    ScUiLayoutDesc desc;n_record(&desc,sizeof(desc));desc.mode=mode;desc.width=width;desc.height=height;
    desc.max_width=desc.max_height=SC_UI_AUTO;desc.flex_weight=weight;desc.dock=dock;
    desc.column_span=desc.row_span=1;desc.align_h=desc.align_v=SC_UI_STRETCH;desc.gap=6;
    return ScUiNodeSetLayout(notes.ui,node,&desc);
}
static ScStatus n_widget(ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScUiNode *out)
{
    ScUiWidgetDesc desc;n_record(&desc,sizeof(desc));desc.parent=parent;desc.kind=kind;
    desc.command=command;desc.text=n_text(label);
    if(kind==SC_UI_EDIT || kind==SC_UI_TEXTBOX || kind==SC_UI_CHECKBOX || kind==SC_UI_BUTTON)
        desc.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    if(kind==SC_UI_EDIT)desc.flags|=SC_UI_EDITOR_TAB;
    return ScUiWidgetCreate(notes.ui,&desc,out);
}
static ScStatus n_command_state(ScU32 command,ScU32 flags,ScU32 force)
{
    ScU32 index=command-N_NEW;if(index>=sizeof(n_commands)/sizeof(n_commands[0]))return SC_UI_E_RANGE;
    if(!force && notes.command_flags[index]==flags)return 0;
    ScUiCommandDesc desc;n_record(&desc,sizeof(desc));desc.command=command;desc.flags=flags;
    desc.label=n_text(n_commands[index].label);n_record(&desc.shortcut,sizeof(desc.shortcut));
    desc.shortcut.key=n_commands[index].key;desc.shortcut.modifiers=n_commands[index].modifiers;
    ScStatus status=ScUiCommandSet(notes.ui,&desc);if(status>=0)notes.command_flags[index]=flags;return status;
}
static ScStatus n_menu(ScUiMenu *menu,const ScU32 *commands,ScU32 count)
{
    for(ScU32 i=0;i<count;i++){
        ScUiMenuItem item;n_record(&item,sizeof(item));ScU32 inserted=0;
        if(!commands[i])item.flags=SC_UI_MENU_SEPARATOR;
        else{ScU32 index=commands[i]-N_NEW;item.command=commands[i];item.label=n_text(n_commands[index].label);
            n_record(&item.shortcut,sizeof(item.shortcut));item.shortcut.key=n_commands[index].key;
            item.shortcut.modifiers=n_commands[index].modifiers;}
        ScStatus status=ScUiMenuInsert(menu,0,&item,&inserted);if(status<0)return status;
    }
    return 0;
}
static ScStatus n_menus(void)
{
    ScStatus status=0;
    for(ScU32 i=0;i<5 && status>=0;i++)status=ScUiMenuCreate(notes.menus+i);
    const ScU32 file[]={N_NEW,N_OPEN,0,N_SAVE,N_SAVE_AS,N_RELOAD,0,N_EXIT};
    const ScU32 edit[]={N_UNDO,N_REDO,0,N_CUT,N_COPY,N_PASTE,N_SELECT_ALL};
    const ScU32 search[]={N_FIND,N_FIND_NEXT,N_FIND_PREVIOUS,N_REPLACE,N_REPLACE_ALL,N_FIND_CLOSE};
    const ScU32 tools[]={N_NEW,N_OPEN,N_SAVE,0,N_UNDO,N_REDO,0,N_FIND,N_CANCEL_IO};
    if(status>=0)status=n_menu(notes.menus[1],file,sizeof(file)/sizeof(file[0]));
    if(status>=0)status=n_menu(notes.menus[2],edit,sizeof(edit)/sizeof(edit[0]));
    if(status>=0)status=n_menu(notes.menus[3],search,sizeof(search)/sizeof(search[0]));
    if(status>=0)status=n_menu(notes.menus[4],tools,sizeof(tools)/sizeof(tools[0]));
    const char *labels[]={"文件 (&F)","编辑 (&E)","搜索 (&S)","帮助"};
    for(ScU32 i=0;i<4 && status>=0;i++){
        ScUiMenuItem item;n_record(&item,sizeof(item));item.label=n_text(labels[i]);
        if(i<3)item.submenu=notes.menus[i+1];else item.command=N_ABOUT;
        ScU32 inserted=0;status=ScUiMenuInsert(notes.menus[0],0,&item,&inserted);
    }
    for(ScU32 i=0;i<sizeof(n_commands)/sizeof(n_commands[0]) && status>=0;i++)status=n_command_state(N_NEW+i,0,1);
    if(status>=0)status=ScUiMenuBind(notes.ui,notes.menu,notes.menus[0]);
    if(status>=0)status=ScUiToolbarBind(notes.ui,notes.toolbar,notes.menus[4]);
    return status;
}
static void n_status(const char *message)
{
    if(message){strncpy(notes.message,message,sizeof(notes.message)-1);notes.message[sizeof(notes.message)-1]=0;}
    if(!notes.ui || !notes.edit || !notes.document)return;
    ScUiDocumentInfo document;n_record(&document,sizeof(document));
    if(ScUiDocumentQuery(notes.document,&document)<0)return;
    ScUiTextSelection selection;n_record(&selection,sizeof(selection));ScUiEditGetSelection(notes.ui,notes.edit,&selection);
    char title[160],summary[320];const char *leaf=strrchr(notes.path,'/');leaf=leaf?leaf+1:notes.path;
    snprintf(title,sizeof(title),"%s%s — Notes 2",document.flags&SC_UI_DOCUMENT_DIRTY?"* ":"",*leaf?leaf:"未命名");
    if(strcmp(title,notes.title)){strcpy(notes.title,title);ScUiText value=n_text(title);ScUiWindowSetTitle(notes.ui,notes.root,&value);}
    if(notes.io)snprintf(summary,sizeof(summary),"%s  %u / %u 字节   ·   %s",notes.io->kind==N_IO_SAVE?"保存":"读取",
        notes.io->offset,notes.io->total,notes.message);
    else snprintf(summary,sizeof(summary),"UTF-8  ·  %u 字节 / %u 行  ·  选区 %u–%u  ·  %s",
        document.bytes,document.lines,selection.anchor,selection.caret,notes.message);
    ScUiStatusPart part;n_record(&part,sizeof(part));part.width=SC_UI_AUTO;part.text=n_text(summary);
    ScUiStatusSetParts(notes.ui,notes.status,&part,1);
    /* 只有状态真正改变才重发CommandSet，避免每次输入把菜单/工具栏
     * 当作新模型全量失效；鼠标移动不进入这里。 */
    for(ScU32 i=0;i<sizeof(n_commands)/sizeof(n_commands[0]);i++){
        ScU32 command=N_NEW+i,disabled=0;
        if(command==N_UNDO)disabled=!document.can_undo;
        if(command==N_REDO)disabled=!document.can_redo;
        if(command==N_CUT || command==N_COPY)disabled=selection.anchor==selection.caret;
        if(command==N_CANCEL_IO)disabled=!notes.io;
        if(notes.io && (command==N_NEW || command==N_OPEN || command==N_SAVE || command==N_SAVE_AS
           || command==N_RELOAD || command==N_EXIT))disabled=1;
        n_command_state(command,disabled?SC_UI_MENU_DISABLED:0,0);
    }
}
static ScStatus n_build(void)
{
    ScUiContextDesc context;n_record(&context,sizeof(context));context.flags=SC_UI_STOP_LAST_ROOT;
    context.cache_budget_bytes=1048576;context.undo_budget_bytes=1048576;context.scheduling_quantum=64;
    ScStatus status=ScUiContextCreate(&context,&notes.ui);if(status<0)return status;
    ScUiWindowDesc window;n_record(&window,sizeof(window));window.title=n_text("Notes 2");
    window.x=100;window.y=80;window.client_width=1160;window.client_height=760;window.style=SC32_STYLE_STANDARD;
    status=ScUiWindowCreate(notes.ui,&window,&notes.root);
    if(status>=0)status=n_layout(notes.root,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(notes.root,SC_UI_MENUBAR,"",0,&notes.menu);
    if(status>=0)status=n_layout(notes.menu,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_TOP);
    if(status>=0)status=n_widget(notes.root,SC_UI_TOOLBAR,"",0,&notes.toolbar);
    if(status>=0)status=n_layout(notes.toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,32,0,SC_UI_DOCK_TOP);
    if(status>=0)status=n_widget(notes.root,SC_UI_STATUS,"",0,&notes.status);
    if(status>=0)status=n_layout(notes.status,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_BOTTOM);
    if(status>=0)status=n_widget(notes.root,SC_UI_PANEL,"",0,&notes.search);
    if(status>=0)status=n_layout(notes.search,SC_UI_VBOX,SC_UI_AUTO,92,0,SC_UI_DOCK_BOTTOM);
    ScUiNode row=0,label=0,node=0;
    if(status>=0)status=n_widget(notes.search,SC_UI_PANEL,"",0,&row);
    if(status>=0)status=n_layout(row,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    if(status>=0)status=n_widget(row,SC_UI_LABEL,"查找",0,&label);
    if(status>=0)status=n_layout(label,SC_UI_ABSOLUTE,48,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(row,SC_UI_TEXTBOX,"",0,&notes.needle);
    if(status>=0)status=n_layout(notes.needle,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(status>=0)status=n_widget(row,SC_UI_BUTTON,"上一个",N_FIND_PREVIOUS,&node);
    if(status>=0)status=n_layout(node,SC_UI_ABSOLUTE,80,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(row,SC_UI_BUTTON,"下一个",N_FIND_NEXT,&node);
    if(status>=0)status=n_layout(node,SC_UI_ABSOLUTE,80,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(notes.search,SC_UI_PANEL,"",0,&row);
    if(status>=0)status=n_layout(row,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    if(status>=0)status=n_widget(row,SC_UI_LABEL,"替换",0,&label);
    if(status>=0)status=n_layout(label,SC_UI_ABSOLUTE,48,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(row,SC_UI_TEXTBOX,"",0,&notes.replacement);
    if(status>=0)status=n_layout(notes.replacement,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(status>=0)status=n_widget(row,SC_UI_BUTTON,"替换",N_REPLACE,&node);
    if(status>=0)status=n_layout(node,SC_UI_ABSOLUTE,80,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(row,SC_UI_BUTTON,"全部替换",N_REPLACE_ALL,&node);
    if(status>=0)status=n_layout(node,SC_UI_ABSOLUTE,80,SC_UI_AUTO,0,0);
    if(status>=0)status=n_widget(notes.search,SC_UI_PANEL,"",0,&row);
    if(status>=0)status=n_layout(row,SC_UI_HBOX,SC_UI_AUTO,24,0,0);
    if(status>=0)status=n_widget(row,SC_UI_CHECKBOX,"忽略 ASCII 大小写",0,&notes.caseless);
    if(status>=0)status=n_layout(notes.caseless,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(status>=0)status=n_widget(row,SC_UI_BUTTON,"收起",N_FIND_CLOSE,&node);
    if(status>=0)status=n_layout(node,SC_UI_ABSOLUTE,80,SC_UI_AUTO,0,0);
    if(status>=0)status=ScUiNodeSetState(notes.ui,notes.search,SC_UI_VISIBLE,0);
    if(status>=0)status=n_widget(notes.root,SC_UI_EDIT,"",0,&notes.edit);
    if(status>=0)status=n_layout(notes.edit,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    if(status>=0)status=ScUiNodeSetFont(notes.ui,notes.edit,SC_UI_FONT_CODE);
    if(status>=0)status=n_document_empty();
    if(status>=0)status=n_menus();
    ScUiSubscription listener;n_record(&listener,sizeof(listener));
    listener.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;listener.procedure=n_event;
    ScU32 token=0;if(status>=0)status=ScUiListen(notes.ui,notes.root,&listener,&token);
    if(status>=0)status=ScUiLayout(notes.ui,notes.root);
    if(status>=0){ScUiFocus(notes.ui,notes.edit);n_status("就绪");}
    return status;
}
