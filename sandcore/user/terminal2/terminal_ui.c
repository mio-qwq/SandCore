static void a_status(const char *message)
{
    if(message && strcmp(message,terminal_app.message)){
        strncpy(terminal_app.message,message,sizeof(terminal_app.message)-1);
        terminal_app.message[sizeof(terminal_app.message)-1]=0;
    }
    ScUiStatusPart part;
    a_record(&part,sizeof(part));
    part.width=SC_UI_AUTO;
    part.text=a_text(terminal_app.message);
    ScUiStatusSetParts(terminal_app.ui,terminal_app.status,&part,1);
}
static void a_error(ScStatus status,const char *message)
{
    char text[256];
    snprintf(text,sizeof(text),"%s（%d）",message,status);
    if(strcmp(text,terminal_app.message))a_status(text);
}
static ScStatus a_dialog_done(ScUiContext *context,const ScUiEvent *event,void *unused)
{
    (void)context;
    (void)unused;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo info;
    a_record(&info,sizeof(info));
    ScStatus status=ScUiDialogQuery(dialog,&info);
    terminal_app.dialog=0;
    ScUiDialogRelease(dialog);
    if(!status && info.command==T_CONFIRM){
        if(terminal_app.pending_action==T_EXIT){
            terminal_app.quitting=1;
            for(TerminalSession *s=terminal_app.sessions;s;s=s->next)a_session_close(s);
        }
        else a_session_close(a_session_id(terminal_app.pending_session));
    }
    terminal_app.pending_action=terminal_app.pending_session=0;
    return 0;
}
static void a_request(ScU32 action)
{
    if(terminal_app.dialog)return;
    int live=0;
    if(action==T_EXIT){
        for(TerminalSession *s=terminal_app.sessions;s;s=s->next)if(s->job)live=1;
    }
    else live=terminal_app.active && terminal_app.active->job;
    if(!live){
        if(action==T_EXIT){
            terminal_app.quitting=1;
            for(TerminalSession *s=terminal_app.sessions;s;s=s->next)s->closing=1;
        }
        else if(terminal_app.active)a_session_close(terminal_app.active);
        return;
    }
    ScUiMenuItem buttons[2];
    a_record(buttons,sizeof(buttons[0]));
    a_record(buttons+1,sizeof(buttons[1]));
    buttons[0].command=T_CONFIRM;
    buttons[0].label=a_text("结束作业");
    buttons[1].command=T_CANCEL;
    buttons[1].label=a_text("取消");
    ScUiMessageBoxDesc desc;
    a_record(&desc,sizeof(desc));
    desc.owner=terminal_app.root;
    desc.title=a_text("关闭终端");
    desc.message=a_text(action==T_EXIT?"关闭所有会话并结束仍在运行的作业？":"关闭此会话并结束仍在运行的作业？");
    desc.buttons=buttons;
    desc.button_count=2;
    desc.default_command=desc.cancel_command=T_CANCEL;
    desc.completion=a_dialog_done;
    ScStatus status=ScUiMessageBox(terminal_app.ui,&desc,&terminal_app.dialog);
    if(status<0)a_error(status,"无法打开确认对话框");
    else {
        terminal_app.pending_action=action;
        terminal_app.pending_session=terminal_app.active?terminal_app.active->id:0;
    }
}
typedef struct {
    ScU32 command;
    const char *label;
    ScU32 key,mods;
}
TerminalCommand;
static const TerminalCommand a_commands[]={
    {
        T_NEW,"新建会话 (&N)",'N',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },
    {
        T_CLOSE,"关闭会话 (&W)",'W',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },
    {
        T_EXIT,"退出 (&X)",0,0
    },{
        T_COPY,"复制 (&C)",'C',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },
    {
        T_PASTE,"粘贴 (&V)",'V',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },{
        T_ALL,"全部选择 (&A)",'A',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },
    {
        T_CLEAR,"清屏",0,0
    },{
        T_HISTORY,"清除滚动历史",0,0
    },{
        T_RESET,"重置终端",0,0
    },
    {
        T_DENSE,"紧凑字体（12px）",0,0
    },{
        T_CODE,"常规字体（16px）",0,0
    },
    {
        T_EOF,"发送 EOF",0,0
    },{
        T_INTERRUPT,"中断前台作业",0,0
    },{
        T_ABOUT,"关于 Terminal 2",0,0
    }
};
static ScStatus a_menu_items(ScUiMenu *menu,const ScU32 *commands,ScU32 count)
{
    for(ScU32 i=0;i<count;i++){
        ScUiMenuItem item;
        a_record(&item,sizeof(item));
        if(!commands[i])item.flags=SC_UI_MENU_SEPARATOR;
        else {
            const TerminalCommand *command=a_commands+commands[i]-T_NEW;
            item.command=command->command;
            item.label=a_text(command->label);
            a_record(&item.shortcut,sizeof(item.shortcut));
            item.shortcut.key=command->key;
            item.shortcut.modifiers=command->mods;
        }
        ScU32 id=0;
        if(item.command && item.shortcut.key){
            ScUiCommandDesc binding;a_record(&binding,sizeof(binding));
            binding.command=item.command;binding.label=item.label;binding.shortcut=item.shortcut;
            ScStatus status=ScUiCommandSet(terminal_app.ui,&binding);if(status<0)return status;
        }
        ScStatus status=ScUiMenuInsert(menu,0,&item,&id);
        if(status<0)return status;
    }
    return 0;
}
static ScStatus a_widget(ScU32 kind,ScU32 dock,ScI32 height,ScUiNode *out)
{
    ScUiWidgetDesc desc;
    a_record(&desc,sizeof(desc));
    desc.parent=terminal_app.root;
    desc.kind=kind;
    desc.text=a_text("");
    ScStatus status=ScUiWidgetCreate(terminal_app.ui,&desc,out);
    if(status<0)return status;
    ScUiLayoutDesc layout;
    a_record(&layout,sizeof(layout));
    layout.mode=SC_UI_ABSOLUTE;
    layout.dock=dock;
    layout.height=height;
    layout.width=SC_UI_AUTO;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.flex_weight=dock==SC_UI_DOCK_FILL?1:0;
    layout.column_span=layout.row_span=1;
    return ScUiNodeSetLayout(terminal_app.ui,*out,&layout);
}
static ScStatus a_event(ScUiContext *context,const ScUiEvent *event,void *unused)
{
    (void)context;
    (void)unused;
    if(event->phase==SC_UI_CAPTURE_PHASE){
        TerminalSession *session=terminal_app.active;
        if(event->type==SC_UI_EVENT_KEY_DOWN && session && session->master
        && (event->modifiers&SC32_MOD_CONTROL) && !(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_ALT))
        && (event->key=='c' || event->key=='C' || event->key=='d' || event->key=='D')){
            int interrupt=event->key=='c' || event->key=='C';
            ScStatus status=a_control(session,interrupt?SC32_TERMINAL_INTERRUPT:SC32_TERMINAL_EOF);
            if(!status){
                return SC_UI_HANDLED;
            }
            if(status!=SC32_E_AGAIN){
                a_error(status,"终端控制失败");
                return SC_UI_HANDLED;
            }
        }
        return 0;
    }
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST && event->node==terminal_app.root){
        a_request(T_EXIT);
        return SC_UI_VETO;
    }
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED && event->node==terminal_app.tabs){
        ScUiNode page=a_selected();
        for(TerminalSession *s=terminal_app.sessions;s;s=s->next)if(s->page==page){
            terminal_app.active=s;
            ScUiFocus(terminal_app.ui,s->node);
            break;
        }
        return 0;
    }
    if(event->type!=SC_UI_EVENT_COMMAND)return 0;
    ScU32 command=event->command;
    TerminalSession *s=terminal_app.active;
    ScStatus status=0;
    if(command==T_NEW)status=a_session_new();
    else if(command==T_CLOSE || command==T_EXIT)a_request(command);
    else if(command==T_ABOUT)a_status("Terminal 2 · 同一 Shell、真实 PTY · Ctrl+Shift+C/V 复制粘贴 · Shift+PgUp/PgDn 滚动");
    else if(s){
        if(command==T_COPY)status=ScUiTerminalCopy(s->terminal);
        else if(command==T_PASTE)status=ScUiTerminalPaste(s->terminal);
        else if(command==T_ALL){
            ScUiTerminalInfo info;
            a_record(&info,sizeof(info));
            status=ScUiTerminalQuery(s->terminal,&info);
            if(!status){
                ScUiTerminalSelection selection;
                a_record(&selection,sizeof(selection));
                selection.anchor_row=info.first_row;
                selection.active_row=info.last_row;
                selection.active_column=info.columns;
                status=ScUiTerminalSelect(s->terminal,&selection);
            }
        }
        else if(command==T_CLEAR || command==T_HISTORY || command==T_RESET)status=ScUiTerminalClear(s->terminal,
        command==T_CLEAR?SC_UI_TERM_CLEAR_SCREEN:command==T_HISTORY?SC_UI_TERM_CLEAR_HISTORY:SC_UI_TERM_RESET);
        else if(command==T_DENSE || command==T_CODE)status=ScUiTerminalSetFont(s->terminal,command==T_DENSE?SC_UI_FONT_DENSE:SC_UI_FONT_CODE);
        else if((command==T_EOF || command==T_INTERRUPT) && s->master)status=a_control(s,command==T_EOF?SC32_TERMINAL_EOF:SC32_TERMINAL_INTERRUPT);
        ScUiFocus(terminal_app.ui,s->node);
    }
    if(status<0 && status!=SC_UI_E_NOT_FOUND)a_error(status,"终端操作失败");
    return SC_UI_HANDLED;
}
static ScStatus a_ui(void)
{
    ScUiContextDesc context;
    a_record(&context,sizeof(context));
    context.cache_budget_bytes=262144;
    context.undo_budget_bytes=65536;
    context.scheduling_quantum=8;
    context.default_font_face=context.default_font_pixels=16;
    ScStatus status=ScUiContextCreate(&context,&terminal_app.ui);
    if(status<0)return status;
    ScUiWindowDesc window;
    a_record(&window,sizeof(window));
    window.title=a_text("Terminal 2");
    window.x=84;
    window.y=54;
    window.client_width=800;
    window.client_height=540;
    window.style=SC32_STYLE_STANDARD;
    status=ScUiWindowCreate(terminal_app.ui,&window,&terminal_app.root);
    ScUiLayoutDesc layout;
    a_record(&layout,sizeof(layout));
    layout.mode=SC_UI_DOCK_LAYOUT;
    layout.width=layout.height=SC_UI_AUTO;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;
    if(!status)status=ScUiNodeSetLayout(terminal_app.ui,terminal_app.root,&layout);
    if(!status)status=a_widget(SC_UI_MENUBAR,SC_UI_DOCK_TOP,SC_UI_AUTO,&terminal_app.menu);
    if(!status)status=a_widget(SC_UI_TOOLBAR,SC_UI_DOCK_TOP,SC_UI_AUTO,&terminal_app.toolbar);
    if(!status)status=a_widget(SC_UI_STATUS,SC_UI_DOCK_BOTTOM,SC_UI_AUTO,&terminal_app.status);
    if(!status)status=a_widget(SC_UI_TAB,SC_UI_DOCK_FILL,SC_UI_AUTO,&terminal_app.tabs);
    for(ScU32 i=0;i<5 && !status;i++)status=ScUiMenuCreate(terminal_app.menus+i);
    const ScU32 file[]={
        T_NEW,T_CLOSE,0,T_EXIT
    },edit[]={
        T_COPY,T_PASTE,T_ALL,0,T_EOF,T_INTERRUPT
    },view[]={
        T_CLEAR,T_HISTORY,T_RESET,0,T_DENSE,T_CODE,0,T_ABOUT
    },toolbar[]={
        T_NEW,T_CLOSE,0,T_COPY,T_PASTE,0,T_INTERRUPT
    };
    if(!status)status=a_menu_items(terminal_app.menus[1],file,sizeof(file)/sizeof(file[0]));
    if(!status)status=a_menu_items(terminal_app.menus[2],edit,sizeof(edit)/sizeof(edit[0]));
    if(!status)status=a_menu_items(terminal_app.menus[3],view,sizeof(view)/sizeof(view[0]));
    if(!status)status=a_menu_items(terminal_app.menus[4],toolbar,sizeof(toolbar)/sizeof(toolbar[0]));
    const char *names[]={
        "会话 (&S)","编辑 (&E)","视图 (&V)"
    };
    for(ScU32 i=0;i<3 && !status;i++){
        ScUiMenuItem item;
        a_record(&item,sizeof(item));
        item.label=a_text(names[i]);
        item.submenu=terminal_app.menus[i+1];
        ScU32 id;
        status=ScUiMenuInsert(terminal_app.menus[0],0,&item,&id);
    }
    if(!status)status=ScUiMenuBind(terminal_app.ui,terminal_app.menu,terminal_app.menus[0]);
    if(!status)status=ScUiToolbarBind(terminal_app.ui,terminal_app.toolbar,terminal_app.menus[4]);
    ScUiSubscription subscription;
    a_record(&subscription,sizeof(subscription));
    subscription.procedure=a_event;
    subscription.phases=SC_UI_CAPTURE_PHASE|SC_UI_BUBBLE_PHASE|SC_UI_TARGET_PHASE;
    ScU32 token;
    if(!status)status=ScUiListen(terminal_app.ui,terminal_app.root,&subscription,&token);
    return status;
}
