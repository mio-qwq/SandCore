#include "DESKTOP.H"
#include "desktop_model.inc"
#include "desktop_view.inc"
#include "desktop_tasks.inc"
#include "desktop_actions.inc"

/* 绘制错误通过公开回调立即发布，首帧失败不能只退化为激活超时。
 * 日志只写本人账户配置目录，保留实际错误及窗口事实，不输出凭据。 */
static ScStatus desk_ui_failure;
static ScUiErrorInfo desk_ui_error_info;
static ScStatus desk_ui_error(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type==SC_UI_EVENT_ERROR && event->data && event->bytes>=sizeof(desk_ui_error_info)){
        memcpy(&desk_ui_error_info,event->data,sizeof(desk_ui_error_info));
        if(desk_ui_error_info.code<0)desk_ui_failure=desk_ui_error_info.code;
    }
    return 0;
}
static void desk_startup_log(ScStatus status)
{
    if(!desk.store_path[0])return;
    char path[68];strcpy(path,desk.store_path);char *slash=strrchr(path,'/');
    if(!slash)return;
    slash[1]=0;
    if(strlen(path)+strlen("SHELL.LOG")>=sizeof(path))return;
    strcat(path,"SHELL.LOG");
    FILE *file=fopen(path,"w");if(!file)return;
    ScUiErrorInfo error;memset(&error,0,sizeof(error));error.size=sizeof(error);error.version=1;
    if(desk.ui)ScUiErrorQuery(desk.ui,&error);
    fprintf(file,"startup=%d callback=%d ui=%d cause=%d raw=%d op=%u node=%u\n",
        status,desk_ui_failure,error.code,error.cause_code,error.raw,error.operation,error.node);
    ScUiContextInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(desk.ui && ScUiContextQuery(desk.ui,&info)==0)
        fprintf(file,"roots=%u visible=%u paints=%u:%u notification=%d\n",info.roots,info.visible_roots,
            info.paint_count.hi,info.paint_count.lo,info.notification_error);
    ScUiNode roots[2]={desk.desktop,desk.bar};
    for(ScU32 i=0;i<2;i++)if(roots[i]){
        ScWindowInfo window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
        if(ScUiWindowNative(desk.ui,roots[i],&window)==0)
            fprintf(file,"root=%u window=%u state=%u geometry=%u size=%u,%u\n",roots[i],window.window,
                window.state,window.geometry_epoch,window.client_width,window.client_height);
    }
    fclose(file);
}

static ScStatus desk_service_event(const ScShellEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_SHELL_EVENT_CHANGED){desk.epoch=event->epoch;
        /* 菜单快照已持有对象，不在库的变更callback内销正在执行的菜单。
         * 下次打开取得新快照，布局仅在没有拖动候选时应用。 */
        if(!desk.gesture){ScStatus s=desk_layout_restore();if(s==0)s=desk_index();
            if(s==0)ScUiInvalidate(desk.ui,desk.canvas,0);
            return s;}
    }
    return 0;
}
/* ThemeQuery尺寸为逻辑像素；角色工作区与原生窗口使用物理像素。
 * 同一换算同时用于初建和主题/DPI变更，避免32px逻辑任务栏触发
 * 普通窗口40px最小高度校验，以及反复32/48px调整的布局反馈。 */
static ScU32 desk_bar_pixels(ScU32 logical,ScU32 scale)
{
    if(!logical || !scale || logical>0xffffffffu/scale)return 0;
    ScU32 pixels=logical*scale/100u;return pixels?pixels:1;
}
static ScStatus desk_root_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_COMMAND)return desk_command(event->command);
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST)return SC_UI_VETO;
    if(event->type==SC_UI_EVENT_VISIBILITY){
        ScWindowInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        ScStatus s=ScUiWindowNative(ui,desk.desktop,&info);if(s==0)desk.visible=(info.state&SC32_WINDOW_VISIBLE)!=0;
        if(desk.visible){desk.task_dirty=1;ScUiInvalidate(ui,desk.canvas,0);ScUiInvalidate(ui,desk.task_canvas,0);}
        else desk_gesture_cancel();
        return s;
    }
    if(event->type==SC_UI_EVENT_LAYOUT){
        ScUiMetrics metrics;memset(&metrics,0,sizeof(metrics));metrics.size=sizeof(metrics);metrics.version=1;
        ScStatus s=ScUiMetricsQuery(ui,desk.desktop,&metrics);if(s<0)return s;
        if(metrics.scale_percent!=desk.metrics.scale_percent || metrics.display_epoch!=desk.metrics.display_epoch
            || metrics.theme_epoch!=desk.metrics.theme_epoch || metrics.font_epoch!=desk.metrics.font_epoch){
            desk_gesture_cancel();desk.metrics=metrics;desk_index();desk.task_dirty=1;desk.refresh_pending=1;
            desk.host.size=sizeof(desk.host);desk.host.version=1;s=ScShellHostQuery(&desk.host);
            ScThemeInfo theme;memset(&theme,0,sizeof(theme));theme.size=sizeof(theme);theme.version=1;
            if(s==0)s=ScThemeQuery(&theme);
            ScU32 height=s==0?desk_bar_pixels(theme.taskbar_height,metrics.scale_percent):0;
            if(s==0 && !height)return SC32_E_RANGE;
            if(s==0 && desk.active && height!=desk.bar_height){
                s=ScShellHostWorkspace(desk.host.host_epoch,height,&desk.host);
                if(s==0)desk.bar_height=height;
            }
        }
        return s;
    }
    return 0;
}
static ScStatus desk_listen(ScUiNode node,ScUiEventProc callback)
{
    ScUiSubscription sub;memset(&sub,0,sizeof(sub));sub.size=sizeof(sub);sub.version=1;
    sub.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;sub.procedure=callback;ScU32 token;
    return ScUiListen(desk.ui,node,&sub,&token);
}
static ScStatus desk_canvas(ScUiNode root,ScUiClass kind,ScUiNode *out)
{
    ScUiLayoutDesc parent;memset(&parent,0,sizeof(parent));parent.size=sizeof(parent);parent.version=1;
    parent.mode=SC_UI_OVERLAY;parent.width=parent.height=SC_UI_AUTO;parent.max_width=parent.max_height=SC_UI_AUTO;parent.column_span=parent.row_span=1;
    ScStatus s=ScUiNodeSetLayout(desk.ui,root,&parent);if(s<0)return s;
    ScUiWidgetDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.kind=kind;desc.parent=root;desc.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;desc.text=desk_text("");
    s=ScUiWidgetCreate(desk.ui,&desc,out);
    ScUiLayoutDesc layout;memset(&layout,0,sizeof(layout));layout.size=sizeof(layout);layout.version=1;
    layout.mode=SC_UI_ABSOLUTE;layout.width=SC_UI_AUTO;layout.height=SC_UI_AUTO;
    layout.max_width=SC_UI_AUTO;layout.max_height=SC_UI_AUTO;layout.column_span=layout.row_span=1;
    if(s==0)s=ScUiNodeSetLayout(desk.ui,*out,&layout);
    return s;
}
/* 旧PathStat/Metadata的-1只有粗错误；需要区分不存在时使用现代
 * FILE的明确NOT_FOUND。只查询代数，不读取程序正文，不保留FD。 */
static ScStatus desk_file_revision(const char *path,ScU32 *revision)
{
    ScFileOpenDesc open;memset(&open,0,sizeof(open));open.size=sizeof(open);open.version=1;
    open.path=path;open.flags=SC32_FILE_READ;ScFile file=0;
    ScStatus status=ScFileOpen(&open,&file);if(status<0)return status;
    ScFileInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    status=ScFileQuery(file,&info);ScStatus closed=ScFileClose(file);
    if(status<0)return status;
    if(closed<0)return closed;
    *revision=info.revision;return 0;
}
#include "desktop_defaults.inc"
static ScStatus desk_seeds(void)
{
    return def_seed(desk.catalog);
}
static ScStatus desk_directory(const char *path)
{
    ScPathInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus status=ScPathQuery(path,&info);
    if(status==0)return info.kind==SC32_PATH_DIRECTORY?0:SC32_E_INVALID;
    if(status!=SC32_E_NOT_FOUND && status!=SC32_E_FAILED)return status;
    /* 不把粗FAILED直接当作不存在：CreateEx只在确实缺名时发布，
     * 已有对象永不覆盖；若另一任务抢先创建则重新核类型。 */
    status=ScPathCreate(path,SC32_PATH_DIRECTORY,3);
    if(status!=SC32_E_EXISTS)return status;
    info.size=sizeof(info);info.version=1;status=ScPathQuery(path,&info);
    if(status==0 && info.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
    return status;
}
static ScStatus desk_paths(void)
{
    ScU32 length=0;char menu[68],base[68];ScStatus s=ScSessionPath("MENU.CFG",menu,sizeof(menu),&length);
    if(s<0)return s;
    int legacy_default=!strcmp(menu,"/SYS/MENU.CFG");
    s=ScSessionPath("SHELL.CAT",desk.store_path,sizeof(desk.store_path),&length);if(s<0)return s;
    /* 默认账户保留旧SYS菜单和DESK，不向root拥有的SYS目录创建新文件。
     * 新catalog/恢复记录使用该账户已有HOME内的私有配置目录；其它
     * 账户沿公开SessionPath取得自己的目录，先建父目录再建子项。 */
    if(legacy_default)strcpy(desk.store_path,"/HOME/.DESKTOP/SHELL.CAT");
    strcpy(base,desk.store_path);char *slash=strrchr(base,'/');
    if(!slash || slash==base)return SC32_E_INVALID;
    *slash=0;
    s=desk_directory(base);if(s<0)return s;
    if(legacy_default)strcpy(desk.desktop_path,"/DESK");
    else {s=ScSessionPath("DESK",desk.desktop_path,sizeof(desk.desktop_path),&length);if(s<0)return s;}
    s=desk_directory(desk.desktop_path);if(s<0)return s;
    int bytes=snprintf(desk.recovery_directory,sizeof(desk.recovery_directory),"%s/SHELLSTATE",base);
    if(bytes<0 || (ScU32)bytes>=sizeof(desk.recovery_directory))return SC32_E_RANGE;
    s=desk_directory(desk.recovery_directory);if(s<0)return s;
    s=desk_file_revision(desk.store_path,&desk.store_revision);
    if(s==SC32_E_NOT_FOUND)desk.store_revision=0;
    else if(s<0)return s;
    ScShellCatalogInfo cat;memset(&cat,0,sizeof(cat));cat.size=sizeof(cat);cat.version=1;
    s=ScShellCatalogQuery(desk.catalog,&cat);if(s<0)return s;
    ScU32 project=0;
    if(desk.store_revision){
        s=ScShellCatalogLoad(desk.catalog,desk.store_path,cat.epoch);
        /* SYSTEM记录仍沿原Load拒绝；不为默认迁移放宽文件信任层。 */
        if(s<0)fprintf(stderr,"SCSHELL configuration rejected=%d path=%s\n",s,desk.store_path);
        else project=1;
    }else{
        ScLeaseInfo pin;ScU32 eligible=0;ScStatus held=def_menu_hold(menu,&pin,&eligible);
        if(held<0)fprintf(stderr,"SCSHELL default menu check=%d path=%s\n",held,menu);
        s=ScShellCatalogImportLegacyMenu(desk.catalog,menu,cat.epoch);
        ScStatus released=def_menu_finish(&pin,&eligible);
        if(released<0)fprintf(stderr,"SCSHELL default menu release=%d path=%s\n",released,menu);
        if(s<0 && s!=SC32_E_NOT_FOUND && s!=SC_SHELL_E_NOT_FOUND)
            fprintf(stderr,"SCSHELL legacy menu rejected=%d path=%s\n",s,menu);
        if(s==0 && held==0 && released==0 && eligible)project=1;
    }
    if(project){ScU32 changed=0;s=def_project(desk.catalog,&changed);
        if(s<0)fprintf(stderr,"SCSHELL defaults projection=%d durable=0\n",s);
        else if(changed){
            /* 直接保存同一catalog/layout快照，不调用重排/布局重建。
             * 正常Store拥有exact revision与Preserving；只有后续ACK才持久。 */
            desk.layout_unsaved=desk.clock_dirty=1;
            s=ScShellCatalogStoreRevision(desk.catalog,&desk.store_revision);
            if(s==0)s=ScShellStoreBegin(desk.catalog,desk.store_path,desk.store_revision,&desk.store);
            if(s==0)desk.store_serial=desk.layout_serial;
            fprintf(stderr,"SCSHELL defaults projected=%u store=%d durable=0\n",changed,s);
        }
    }
    return 0;
}
static ScStatus desk_account(void)
{
    ScSessionPage page;memset(&page,0,sizeof(page));page.size=sizeof(page);page.version=1;page.cursor=SC32_ENUM_BEGIN;
    do{ScSessionInfo rows[4];ScStatus s=ScSessionEnumerate(&page,rows,4);if(s<0)return s;
        for(ScU32 i=0;i<page.count;i++)if(rows[i].id==desk.identity.session){
            desk_display_name(rows[i].name,(ScU32)strlen(rows[i].name),desk.account_name,sizeof(desk.account_name));return 0;}
    }while(page.cursor!=SC32_ENUM_BEGIN);
    return SC32_E_STALE;
}
static ScStatus desk_create(void)
{
    memset(&desk,0,sizeof(desk));desk.focus=desk.anchor=desk.last_click=desk.pressed=DESK_NONE;
    desk.drop_target=DESK_NONE;
    desk.loaded_icons=DESK_NONE;
    desk.grid_w=128;desk.grid_h=112;desk.last_second=0xffffffffu;desk.clock_dirty=1;
    desk.identity.size=sizeof(desk.identity);desk.identity.version=1;
    ScStatus s=ScIdentityQuery(&desk.identity);if(s<0)return s;
    s=desk_account();if(s<0)return s;
    ScProcessRef self={sizeof(self),1,0,0};s=ScProcessSelf(&self);if(s<0)return s;
    desk.host.size=sizeof(desk.host);desk.host.version=1;s=ScShellHostQuery(&desk.host);
    if(s<0)return s;
    if(desk.host.pid!=self.pid || desk.host.generation!=self.generation
        || desk.host.session!=desk.identity.session || desk.identity.realm!=SC32_BROKER_REALM_NORMAL)
        return SC32_E_ACCESS;
    ScUiContextDesc context;memset(&context,0,sizeof(context));context.size=sizeof(context);context.version=1;
    context.cache_budget_bytes=2097152;context.undo_budget_bytes=65536;context.scheduling_quantum=64;
    context.error_procedure=desk_ui_error;
    s=ScUiContextCreate(&context,&desk.ui);if(s<0)return s;
    /* 本批16张原生128px图标本来就由可见items强持，共1MiB像素。
     * 默认512KiB查找cache只能留7项，会在打开Start时重新创建仍活的
     * 同图像。2MiB是按实际可见资源留余量的上限，不预分配像素；
     * 离屏仍撤items引用，缓存按预算/主题/显示/文件代数正常失效。 */
    s=ScShellIconCacheBudget(2097152);if(s<0)return s;
    ScUiClassDesc custom;memset(&custom,0,sizeof(custom));custom.size=sizeof(custom);custom.version=1;
    custom.name="SandCore.Desktop";custom.paint=desk_paint;custom.event=desk_input;
    s=ScUiClassRegister(desk.ui,&custom,&desk.desktop_class);
    custom.name="SandCore.Taskbar";custom.paint=desk_bar_paint;custom.event=desk_bar_input;
    if(s==0)s=ScUiClassRegister(desk.ui,&custom,&desk.task_class);
    ScDisplayInfo display;memset(&display,0,sizeof(display));display.size=sizeof(display);display.version=1;
    if(s==0)s=ScDisplayQuery(&display);
    ScThemeInfo theme;memset(&theme,0,sizeof(theme));theme.size=sizeof(theme);theme.version=1;
    if(s==0)s=ScThemeQuery(&theme);
    if(s==0){desk.bar_height=desk_bar_pixels(theme.taskbar_height,display.scale_percent);if(!desk.bar_height)s=SC32_E_RANGE;}
    ScUiWindowDesc window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
    window.title=desk_text("桌面");window.client_width=display.width;window.client_height=display.height;
    window.style=SC32_STYLE_HIDDEN;
    if(s==0)s=ScUiWindowCreate(desk.ui,&window,&desk.desktop);
    window.title=desk_text("任务栏");window.client_height=desk.bar_height<40?40:desk.bar_height;
    if(s==0)s=ScUiWindowCreate(desk.ui,&window,&desk.bar);
    if(s==0)s=desk_canvas(desk.desktop,desk.desktop_class,&desk.canvas);
    if(s==0){ScUiText help=desk_text("拖动排列图标；按住 Alt 拖到文件夹或其它窗口以操作文件。");
        s=ScUiTooltipSet(desk.ui,desk.canvas,&help);}
    if(s==0)s=desk_canvas(desk.bar,desk.task_class,&desk.task_canvas);
    if(s==0)s=desk_listen(desk.desktop,desk_root_event);
    if(s==0)s=desk_listen(desk.bar,desk_root_event);
    if(s==0){desk.metrics.size=sizeof(desk.metrics);desk.metrics.version=1;s=ScUiMetricsQuery(desk.ui,desk.desktop,&desk.metrics);}
    if(s==0)s=ScShellCatalogCreate(&desk.catalog);
    if(s==0)s=desk_seeds();
    if(s==0)s=desk_paths();
    if(s==0){ScStatus loaded;
        do{loaded=desk_refresh();if(loaded==SC_UI_PENDING)ScProcessYield();}while(loaded==SC_UI_PENDING);
        if(loaded<0 && loaded!=SC32_E_NOT_FOUND)s=loaded;}
    char name[64];snprintf(name,sizeof(name),"SCSHELL.%u.%u.%u",self.pid,self.generation,desk.host.host_epoch);
    ScIpcEndpointDesc endpoint;memset(&endpoint,0,sizeof(endpoint));endpoint.size=sizeof(endpoint);endpoint.version=1;
    endpoint.name=name;endpoint.queue_budget_bytes=262144;endpoint.max_frame_bytes=65536;endpoint.pending_budget_bytes=65536;
    if(s==0)s=ScIpcCreate(&endpoint,&desk.endpoint);
    if(s==0)s=ScIpcListen(desk.endpoint);
    ScWindowInfo desktop,bar;memset(&desktop,0,sizeof(desktop));desktop.size=sizeof(desktop);desktop.version=1;
    memset(&bar,0,sizeof(bar));bar.size=sizeof(bar);bar.version=1;
    if(s==0)s=ScUiWindowNative(desk.ui,desk.desktop,&desktop);
    if(s==0)s=ScUiWindowNative(desk.ui,desk.bar,&bar);
    ScShellHostDesc host;memset(&host,0,sizeof(host));host.size=sizeof(host);host.version=1;
    host.host_epoch=desk.host.host_epoch;host.endpoint=desk.endpoint;host.desktop=desktop.window;host.desktop_generation=desktop.generation;
    host.taskbar=bar.window;host.taskbar_generation=bar.generation;host.taskbar_height=desk.bar_height;
    if(s==0)s=ScShellHostPrepare(&host,&desk.host);
    ScShellServiceDesc service;memset(&service,0,sizeof(service));service.size=sizeof(service);service.version=1;
    service.catalog=desk.catalog;service.endpoint=desk.endpoint;service.host_epoch=desk.host.host_epoch;
    service.frame_budget_bytes=65536;service.account_store=desk.store_path;service.procedure=desk_service_event;
    if(s==0)s=ScShellServiceCreate(&service,&desk.service);
    if(s<0)return s;
    /* 服务发现与显示激活分开。隐藏会话继续处理IPC/文件工作，
     * 只有真实选中后才允许两个角色首帧，并开始可见准备的超时。 */
    ScUiInvalidate(desk.ui,desk.desktop,0);ScUiInvalidate(desk.ui,desk.bar,0);
    return 0;
}
static ScMonotonicClock desk_activation_checked,desk_activation_begin;
static ScU32 desk_activation_visible,desk_activation_requested,desk_activation_dirty=1;
static ScStatus desk_activate(void)
{
    if(desk.active)return 0;
    if(desk_ui_failure<0)return desk_ui_failure;
    ScMonotonicClock now;memset(&now,0,sizeof(now));now.size=sizeof(now);now.version=1;
    ScStatus status=ScSystemMonotonic(&now);if(status<0)return status;
    if(!now.hz)return SC32_E_VERSION;
    if(desk_activation_dirty || !desk_activation_checked.hz
       || now.ticks.hi!=desk_activation_checked.ticks.hi
       || now.ticks.lo-desk_activation_checked.ticks.lo>=now.hz){
        ScSessionPage page;ScSessionInfo row;
        memset(&page,0,sizeof(page));page.size=sizeof(page);page.version=1;page.cursor=SC32_ENUM_BEGIN;
        status=ScSessionEnumerate(&page,&row,1);if(status<0)return status;
        ScU32 selected=page.visible_session==desk.identity.session;
        if(selected && !desk_activation_visible){desk_activation_begin=now;desk_activation_requested=0;}
        if(!selected)desk_activation_requested=0;
        desk_activation_visible=selected;desk_activation_checked=now;desk_activation_dirty=0;
    }
    if(!desk_activation_visible)return SC_UI_PENDING;
    status=ScShellHostActivate(desk.host.host_epoch,&desk.host);
    if(status==0){
        desk.active=1;desk.task_dirty=1;
        ScWindowInfo window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
        if(ScUiWindowNative(desk.ui,desk.desktop,&window)==0)desk.visible=(window.state&SC32_WINDOW_VISIBLE)!=0;
        ScU32 format=SC32_CLIPBOARD_FILES;ScUiDropTargetDesc drop;
        memset(&drop,0,sizeof(drop));drop.size=sizeof(drop);drop.version=1;
        drop.actions=SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE;drop.formats=&format;drop.format_count=1;drop.procedure=desk_drop;
        return ScUiDropTargetRegister(desk.ui,desk.canvas,&drop);
    }
    if(status!=SC32_E_AGAIN && status!=SC32_E_BUSY && status!=SC32_PENDING)return status;
    if(!desk_activation_requested){
        /* PREPARED的隐藏位在选中前后仍是0，普通VIS变化未必会排PAINT。
         * 只在首次可绘阶段请求仍缺首帧的surface，不每轮全帧失效。 */
        ScUiNode roots[2]={desk.desktop,desk.bar};
        for(ScU32 i=0;i<2;i++){
            ScWindowInfo window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
            ScStatus queried=ScUiWindowNative(desk.ui,roots[i],&window);if(queried<0)return queried;
            if(window.state&SC32_WINDOW_PREPARING){ScStatus requested=ScUiInvalidate(desk.ui,roots[i],0);
                if(requested<0)return requested;}
        }
        desk_activation_requested=1;
    }
    ScU32 elapsed=now.ticks.lo-desk_activation_begin.ticks.lo;
    ScU32 high=now.ticks.hi-desk_activation_begin.ticks.hi-(now.ticks.lo<desk_activation_begin.ticks.lo);
    if(high || elapsed>now.hz*5u)return SC32_E_FAILED;
    return SC_UI_PENDING;
}
static void desk_close(void)
{
    if(desk.host.host_epoch && desk.ui){ScShellHostInfo out;memset(&out,0,sizeof(out));out.size=sizeof(out);out.version=1;
        ScShellHostDeactivate(desk.host.host_epoch,&out);}
    ScShellDialogClose(desk.progress);ScShellDialogClose(desk.properties);ScShellFileOperationClose(desk.job);
    ScShellServiceClose(desk.service);desk.service=0;
    ScShellStoreClose(desk.store);desk.store=0;
    if(desk.external_drag && desk.ui)ScUiDragCancel(desk.ui,desk.external_drag);
    if(desk.endpoint)ScIpcClose(desk.endpoint);
    if(desk.refresh_directory)ScDirectoryClose(desk.refresh_directory);
    ScShellMenuRelease(desk.shell_menu);ScUiMenuRelease(desk.menu);ScUiDialogRelease(desk.dialog);
    while(desk.menu_work){DeskMenuWork *work=desk.menu_work;desk.menu_work=work->next;ScShellMenuRelease(work->menu);free(work);}
    desk_items_free(desk.items,desk.count);free(desk.cells);free(desk.tasks);free(desk.task_candidate);free(desk.drop_bytes);free(desk.drag_items);
    ScHashRelease(desk.item_index);
    desk_items_free(desk.refresh_items,desk.refresh_count);
    for(ScU32 i=0;i<desk.command_count;i++)ScShellApplicationRelease(desk.commands[i].app);
    free(desk.commands);
    ScShellCatalogRelease(desk.catalog);ScUiContextRelease(desk.ui);memset(&desk,0,sizeof(desk));
}
static ScStatus desk_clock(void)
{
    ScMonotonicClock now;memset(&now,0,sizeof(now));now.size=sizeof(now);now.version=1;
    ScStatus s=ScSystemMonotonic(&now);if(s<0)return s;
    if(!desk.clock[0] || now.ticks.lo-desk.clock_tick>=now.hz){
        ScUtcClock clock;memset(&clock,0,sizeof(clock));clock.size=sizeof(clock);clock.version=1;
        s=ScSystemClock(&clock);if(s<0)return s;desk.clock_tick=now.ticks.lo;
        ScU32 minute=clock.hour*60u+clock.minute;
        if(minute!=desk.last_second){desk.last_second=minute;desk.clock_hour=clock.hour;
            desk.clock_minute=clock.minute;desk.clock_dirty=1;}
    }
    if(desk.clock_dirty){desk.clock_dirty=0;
        snprintf(desk.clock,sizeof(desk.clock),"%02u:%02u  ·  %s%s",desk.clock_hour,desk.clock_minute,desk.account_name,
            desk.layout_unsaved?" · 未保存":"");
        if(desk.visible){ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
            if(ScUiNodeQuery(desk.ui,desk.task_canvas,&info)==0){ScI32 width=desk_scale(145);
                ScUiRect r={info.rectangle.width-width,0,width,info.rectangle.height};ScUiInvalidate(desk.ui,desk.task_canvas,&r);}}
    }
    return 0;
}
int main(void)
{
    ScStatus status=desk_create();
    /* 独立启动没有继承TTY；保留实际公共错误作为退出码，使外部
     * 可信host故障记录能够解释回退原因，而非把全部失败压成1。 */
    if(status<0){fprintf(stderr,"SCSHELL startup failed=%d\n",status);desk_startup_log(status);desk_close();return -status;}
    while(!desk.quit){
        ScU32 business_ready=0;
        status=ScUiContextStep(desk.ui,0);if(status<0)break;
        ScStatus activated=desk_activate();if(activated<0){status=activated;break;}
        status=ScShellServiceStep(desk.service,0);if(status<0 && status!=SC32_E_AGAIN)break;
        ScShellServiceInfo service;memset(&service,0,sizeof(service));service.size=sizeof(service);service.version=1;
        if(ScShellServiceQuery(desk.service,&service)==0 && (service.flags&SC_SHELL_SERVICE_READY))business_ready=1;
        if(desk.store){
            ScStatus s=ScShellStoreStep(desk.store,0);ScShellStoreInfo info;
            memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
            ScStatus queried=ScShellStoreQuery(desk.store,&info);
            if(queried<0){ScShellStoreClose(desk.store);desk.store=0;desk_error(queried,desk.store_path);}
            else if(info.state>=SC_SHELL_STORE_COMPLETE){
                if(info.state==SC_SHELL_STORE_COMPLETE){desk.store_revision=info.new_revision;
                    if(desk.store_serial==desk.layout_serial){desk.layout_unsaved=0;desk.clock_dirty=1;}}
                ScShellStoreClose(desk.store);desk.store=0;
                if(info.state!=SC_SHELL_STORE_COMPLETE){
                    if(info.status==SC_SHELL_E_STALE || info.status==SC32_E_STALE)desk_error(desk_layout_conflict(),desk.store_path);
                    else desk_error(info.status<0?info.status:s<0?s:SC_SHELL_E_UNKNOWN,desk.store_path);}
            }else if(info.flags&SC_SHELL_STORE_READY)business_ready=1;
        }
        if(desk.shell_menu){ScStatus s=ScShellMenuStep(desk.shell_menu,0);
            if(s==SC_SHELL_READY)business_ready=1;else if(s<0)desk_error(s,desk.desktop_path);}
        DeskMenuWork **slot=&desk.menu_work;
        while(*slot){DeskMenuWork *work=*slot;ScStatus s=ScShellMenuStep(work->menu,0);
            if(s==SC_SHELL_END){*slot=work->next;ScShellMenuRelease(work->menu);free(work);}
            else {if(s==SC_SHELL_READY)business_ready=1;else if(s<0)desk_error(s,desk.desktop_path);slot=&work->next;}}
        if(desk.active && (desk.task_dirty || desk.task_scanning)){ScStatus s=desk_tasks_step();if(s<0)desk_error(s,"");}
        if(desk.job){ScStatus s=desk.job_done?0:ScShellFileOperationStep(desk.job,0);
            if(s==SC_SHELL_READY)business_ready=1;
            ScStatus dialog=desk.progress?ScShellDialogStep(desk.progress):SC_SHELL_END;
            ScShellFileProgress info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
            if(ScShellFileOperationQuery(desk.job,&info)==0 && info.state>=SC_SHELL_JOB_FINISHED){
                if(!desk.job_done){ScClipboardInfo clip;memset(&clip,0,sizeof(clip));clip.size=sizeof(clip);clip.version=1;
                    ScShellClipboardReconcile(desk.job,&clip);desk.job_done=1;desk.refresh_pending=1;}
                /* 正常全成功可收起进度；部分失败/取消/不明结果留给用户
                 * 看真实已提交与未完成项，不以完成百分比吞掉错误。 */
                if(dialog==SC_SHELL_END || (info.state==SC_SHELL_JOB_FINISHED && !info.items_failed && !info.items_skipped
                    && !(info.flags&SC_SHELL_SOURCE_RETAINED))){
                    ScShellDialogClose(desk.progress);desk.progress=0;ScShellFileOperationClose(desk.job);desk.job=0;}
            }else if(s<0 && !desk.progress)desk_error(s,desk.desktop_path);
        }
        if(desk.properties)ScShellDialogStep(desk.properties);
        if(desk.refresh_pending && !desk.save_pending && desk.visible && !desk.gesture && !desk.dialog){ScStatus s=desk_refresh();
            if(s<0 && s!=SC32_E_STALE && s!=SC_SHELL_E_STALE){desk.refresh_pending=0;desk_error(s,desk.desktop_path);}}
        if(desk.visible && !desk.gesture)desk_icons_step();
        if(desk.save_pending && !desk.gesture && !desk.dialog){ScStatus s=desk_layout_save();if(s<0){desk.save_pending=0;
                if(s==SC_SHELL_E_STALE || s==SC32_E_STALE)desk_error(desk_layout_conflict(),desk.store_path);
                else desk_error(s,desk.store_path);}}
        desk_clock();
        if(desk.quit)break;
        if(business_ready || desk.task_scanning || (desk.visible && (desk.refresh_directory || desk.icons_pending))){ScProcessYield();continue;}
        ScBrokerWaitDesc wait;memset(&wait,0,sizeof(wait));wait.size=sizeof(wait);wait.version=1;
        wait.handle=desk.endpoint;wait.ready_flags=SC32_BROKER_READY_ACCEPT;
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM;wait.timeout=100;
        status=ScUiContextWaitTimeout(desk.ui,100,&wait.timeout);if(status<0)break;
        ScU32 events=0;status=ScBrokerWait(&wait,&events);if(status<0)break;
        if(events&SC32_EVENT_VISIBILITY){desk.task_dirty=1;desk_activation_dirty=1;}
    }
    if(status<0){fprintf(stderr,"SCSHELL stopped=%d\n",status);desk_startup_log(status);}
    desk_close();return status<0?-status:0;
}
