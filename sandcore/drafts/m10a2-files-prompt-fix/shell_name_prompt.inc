/* 单个名字不是配置属性页。只组合公开标准控件，尺寸与字体仍服从同一SCUI context。 */
static const char sh_menu_name_rule[]="名称不能含空格或路径分隔符（最多63字节）。";
static ScStatus sh_menu_name_close(ScShellMenu *m) {
    ScUiNode root=m->prompt;
    ScStatus s=0;
    if(root)s=ScUiNodeDestroy(m->desc.ui,root);
    if(s<0&&s!=SC_UI_E_STALE)return s;
    if(m->prompt_listener)ScUiDisconnect(m->desc.ui,m->prompt_listener);
    m->prompt=m->prompt_name=m->prompt_status=0;
    m->prompt_listener=m->prompt_intent=m->prompt_kind=0;
    return 0;
}
static ScStatus sh_menu_name_event(ScUiContext *ui,const ScUiEvent *event,void *context) {
    ScShellMenu *m=(ScShellMenu *)context;
    ScU32 handled=0;
    ScStatus s=0;
    if(!sh_valid(m,SH_MENU)||sh_quiet)return 0;
    sh_enter(&m->base);
    if(event->type==SC_UI_EVENT_DESTROY&&event->node==m->prompt) {
        m->prompt=m->prompt_name=m->prompt_status=0;
        m->prompt_listener=m->prompt_intent=m->prompt_kind=0;
    } else if(event->type==SC_UI_EVENT_CLOSE_REQUEST ||
        (event->type==SC_UI_EVENT_KEY_DOWN&&event->key==27)) {
        m->prompt_intent=SC_UI_DIALOG_CANCEL;
        handled=1;
    } else if(event->type==SC_UI_EVENT_COMMAND&&
        (event->command==SC_UI_DIALOG_OK||event->command==SC_UI_DIALOG_CANCEL)) {
        /* 只锁存一次意图；取消优先，实际事务留到现有MenuStep，避免输入回调重入磁盘操作。 */
        if(!m->prompt_intent||event->command==SC_UI_DIALOG_CANCEL)m->prompt_intent=event->command;
        handled=1;
    } else if(event->type==SC_UI_EVENT_VALUE_CHANGED&&event->node==m->prompt_name) {
        ScUiText text;
        sh_ui_text(sh_menu_name_rule,&text);
        s=ScUiNodeSetText(ui,m->prompt_status,&text);
        if(s>=0)s=ScUiNodeSetState(ui,m->prompt_name,SC_UI_INVALID,0);
    }
    sh_leave(&m->base);
    return s<0?s:(handled?SC_UI_HANDLED:0);
}
static ScStatus sh_menu_name_layout(ScShellMenu *m,ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex) {
    ScUiLayoutDesc layout;
    memset(&layout,0,sizeof(layout));
    layout.size=sizeof(layout);layout.version=1;
    layout.mode=mode;layout.width=width;layout.height=height;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;
    layout.align_h=layout.align_v=SC_UI_STRETCH;
    layout.flex_weight=flex;layout.gap=8;
    if(node==m->prompt)layout.padding[0]=layout.padding[1]=layout.padding[2]=layout.padding[3]=12;
    return ScUiNodeSetLayout(m->desc.ui,node,&layout);
}
static ScStatus sh_menu_name_open(ScShellMenu *m,ScU32 command,const char *name) {
    ScUiWindowDesc window;
    ScUiSubscription listener;
    ScUiMetrics metrics;
    ScWindowInfo owner;
    ScDisplayInfo display;
    ScUiNode label=0,bar=0,spacer=0,accept=0,cancel=0;
    ScU32 scale,w,h;
    ScStatus s;
    memset(&metrics,0,sizeof(metrics));metrics.size=sizeof(metrics);metrics.version=1;
    memset(&owner,0,sizeof(owner));owner.size=sizeof(owner);owner.version=1;
    memset(&display,0,sizeof(display));display.size=sizeof(display);display.version=1;
    memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
    window.owner=m->dialog&&m->dialog->root?m->dialog->root:m->desc.owner;
    s=ScUiMetricsQuery(m->desc.ui,window.owner,&metrics);
    if(s>=0)s=ScUiWindowNative(m->desc.ui,window.owner,&owner);
    if(s>=0)s=ScDisplayQuery(&display);
    if(s<0)return sh_cause(s,106,0);
    scale=metrics.scale_percent?metrics.scale_percent:100;
    w=(320*scale+50)/100;h=(170*scale+50)/100;
    if(display.width<96||display.height<80)return SC_SHELL_E_RANGE;
    if(w>display.width-2)w=display.width-2;
    if(h>display.height-40)h=display.height-40;
    window.client_width=w;window.client_height=h;
    window.x=owner.x+(owner.width-(ScI32)w)/2;
    window.y=owner.y+(owner.height-(ScI32)h)/2;
    if(window.x<0)window.x=0;
    if(window.y<0)window.y=0;
    if(window.x>(ScI32)(display.width-w-2))window.x=(ScI32)(display.width-w-2);
    if(window.y>(ScI32)(display.height-h-40))window.y=(ScI32)(display.height-h-40);
    window.style=SC32_STYLE_CLOSE|SC32_STYLE_MODAL;
    sh_ui_text(command==SC_SHELL_COMMAND_NEW_DIRECTORY?"新建文件夹":"重命名",&window.title);
    s=ScUiWindowCreate(m->desc.ui,&window,&m->prompt);
    if(s<0)return sh_cause(s,106,0);
    m->prompt_kind=command;m->prompt_intent=0;
    s=sh_menu_name_layout(m,m->prompt,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0);
    if(s>=0)s=sh_widget(m->desc.ui,m->prompt,SC_UI_LABEL,"名称",0,20,&label);
    if(s>=0)s=sh_widget(m->desc.ui,m->prompt,SC_UI_TEXTBOX,name,0,30,&m->prompt_name);
    if(s>=0)s=ScUiLabelAssociate(m->desc.ui,label,m->prompt_name);
    if(s>=0)s=sh_widget(m->desc.ui,m->prompt,SC_UI_LABEL,sh_menu_name_rule,0,36,&m->prompt_status);
    if(s>=0)s=sh_widget(m->desc.ui,m->prompt,SC_UI_PANEL,"",0,30,&bar);
    if(s>=0)s=sh_widget(m->desc.ui,bar,SC_UI_LABEL,"",0,30,&spacer);
    if(s>=0)s=sh_menu_name_layout(m,spacer,SC_UI_ABSOLUTE,0,30,1);
    if(s>=0)s=sh_widget(m->desc.ui,bar,SC_UI_BUTTON,command==SC_SHELL_COMMAND_NEW_DIRECTORY?"新建":"重命名",SC_UI_DIALOG_OK,30,&accept);
    if(s>=0)s=sh_menu_name_layout(m,accept,SC_UI_ABSOLUTE,88,30,0);
    if(s>=0)s=sh_widget(m->desc.ui,bar,SC_UI_BUTTON,"取消",SC_UI_DIALOG_CANCEL,30,&cancel);
    if(s>=0)s=sh_menu_name_layout(m,cancel,SC_UI_ABSOLUTE,88,30,0);
    if(s>=0)s=ScUiButtonSetDefault(m->desc.ui,m->prompt,accept);
    memset(&listener,0,sizeof(listener));listener.size=sizeof(listener);listener.version=1;
    listener.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    listener.procedure=sh_menu_name_event;listener.context=m;
    if(s>=0)s=ScUiListen(m->desc.ui,m->prompt,&listener,&m->prompt_listener);
    if(s>=0)s=ScUiLayout(m->desc.ui,m->prompt);
    if(s>=0)s=ScUiFocus(m->desc.ui,m->prompt_name);
    if(s>=0)s=ScUiEditSelectAll(m->desc.ui,m->prompt_name);
    if(s<0)sh_menu_name_close(m);
    return sh_cause(s,106,0);
}
static ScStatus sh_menu_name_prompt(ScShellMenu *m,ScU32 command) {
    const char *name="新建文件夹";
    if(command==SC_SHELL_COMMAND_RENAME) {
        name=strrchr(m->items[0].source,'/');
        name=name?name+1:m->items[0].source;
    } else if(command==0x7300) {
        name=strrchr(m->conflict_snapshot.destination,'/');
        name=name?name+1:m->conflict_snapshot.destination;
    }
    return sh_menu_name_open(m,command,name);
}
static void sh_menu_name_error(ScShellMenu *m,ScStatus status) {
    char message[120];
    ScUiText text;
    if(status==SC_SHELL_E_INVALID)sh_ui_text("请输入1-63字节名称；不含空格、斜杠或冒号。",&text);
    else if(status==SC_SHELL_E_RANGE)sh_ui_text("完整路径过长，请缩短名称。",&text);
    else {snprintf(message,sizeof(message),"未能继续（错误 %d）；请核对任务或修改名称。",status);sh_ui_text(message,&text);}
    ScUiNodeSetText(m->desc.ui,m->prompt_status,&text);
    ScUiNodeSetState(m->desc.ui,m->prompt_name,SC_UI_INVALID,SC_UI_INVALID);
    ScUiFocus(m->desc.ui,m->prompt_name);
}
