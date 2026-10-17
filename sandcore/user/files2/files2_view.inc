static ScStatus f2_layout_node(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex)
{
    ScUiLayoutDesc layout;memset(&layout,0,sizeof(layout));layout.size=sizeof(layout);layout.version=1;
    layout.mode=mode;layout.width=width;layout.height=height;layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;layout.align_h=layout.align_v=SC_UI_STRETCH;
    layout.flex_weight=flex;layout.gap=5;return ScUiNodeSetLayout(f2.ui,node,&layout);
}
static ScStatus f2_widget(ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScI32 width,ScI32 height,ScU32 flex,ScUiNode *out)
{
    ScUiWidgetDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.kind=kind;desc.parent=parent;desc.command=command;desc.text=f2_text(label);
    if(kind==SC_UI_BUTTON || kind==SC_UI_TEXTBOX || kind==SC_UI_TABLE || kind==SC_UI_TREE)desc.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScStatus s=ScUiWidgetCreate(f2.ui,&desc,out);if(s<0)return s;
    return f2_layout_node(*out,kind==SC_UI_PANEL?SC_UI_HBOX:SC_UI_ABSOLUTE,width,height,flex);
}
static ScStatus f2_listen(ScUiNode node,ScU32 phases,ScUiEventProc procedure)
{
    ScUiSubscription subscription;memset(&subscription,0,sizeof(subscription));subscription.size=sizeof(subscription);subscription.version=1;
    subscription.phases=phases;subscription.procedure=procedure;ScU32 token;return ScUiListen(f2.ui,node,&subscription,&token);
}
static void f2_status(const char *message)
{
    if(message && message!=f2.message)snprintf(f2.message,sizeof(f2.message),"%s",message);
    if(!f2.status)return;
    ScUiSelectionInfo selected;memset(&selected,0,sizeof(selected));selected.size=sizeof(selected);selected.version=1;
    if(f2.selection)ScUiSelectionQuery(f2.selection,&selected);
    char count[128];snprintf(count,sizeof(count),"%u 项 | 已选 %u%s",f2.current?f2.current->visible:0,selected.count,
        f2.bookmark_serial!=f2.saved_serial?" | 书签未保存":"");
    ScUiStatusPart parts[2];memset(parts,0,sizeof(parts));
    for(ScU32 i=0;i<2;i++){parts[i].size=sizeof(parts[i]);parts[i].version=1;parts[i].part=i+1;}
    parts[0].width=-1;parts[0].text=f2_text(f2.message);parts[1].width=248;parts[1].text=f2_text(count);
    ScUiStatusSetParts(f2.ui,f2.status,parts,2);
}
static ScStatus f2_menu_add(ScUiMenu *menu,const char *label,ScU32 command,ScUiMenu *submenu,ScU32 flags,ScU32 key,ScU32 modifiers)
{
    ScUiMenuItem item;memset(&item,0,sizeof(item));item.size=sizeof(item);item.version=1;
    item.command=command;item.label=f2_text(label);item.submenu=submenu;item.flags=flags;
    item.shortcut.size=sizeof(item.shortcut);item.shortcut.version=1;item.shortcut.key=key;item.shortcut.modifiers=modifiers;
    if(command && key){
        /* 菜单中的键帽是描述，键盘分派还须登记同一公开Command。 */
        ScUiCommandDesc binding;memset(&binding,0,sizeof(binding));binding.size=sizeof(binding);binding.version=1;
        binding.command=command;binding.flags=flags&~SC_UI_MENU_SEPARATOR;binding.label=item.label;binding.shortcut=item.shortcut;
        ScStatus status=ScUiCommandSet(f2.ui,&binding);if(status<0)return status;
    }
    ScU32 id;return ScUiMenuInsert(menu,0,&item,&id);
}
static ScStatus f2_places_refresh(void)
{
    if(!f2.places)return 0;
    if(f2.bookmark_count>UINT_MAX/sizeof(ScUiMenuItem)-3)return SC_UI_E_RANGE;
    ScU32 count=f2.bookmark_count+3;ScUiMenuItem *items=calloc(count,sizeof(*items));if(!items)return SC_UI_E_MEMORY;
    for(ScU32 i=0;i<count;i++){items[i].size=sizeof(*items);items[i].version=1;}
    for(ScU32 i=0;i<f2.bookmark_count;i++){items[i].command=F2_BOOKMARK_FIRST+i;items[i].label=f2_text(f2.bookmarks[i].path);
        if(f2.current && !f2_compare_text(f2.current->path,f2.bookmarks[i].path))items[i].flags=SC_UI_MENU_CHECKED;}
    items[count-3].flags=SC_UI_MENU_SEPARATOR;items[count-3].label=f2_text("");
    items[count-2].command=F2_BOOKMARK_ADD;items[count-2].label=f2_text("添加当前目录");
    items[count-1].command=F2_BOOKMARK_REMOVE;items[count-1].label=f2_text("移除当前目录书签");
    ScStatus s=ScUiMenuReplace(f2.places,items,count);free(items);return s;
}
static ScStatus f2_history_refresh(void)
{
    if(!f2.history_menu)return 0;
    if(f2.history_count>UINT_MAX/sizeof(ScUiMenuItem)-1)return SC_UI_E_RANGE;
    ScU32 count=f2.history_count?f2.history_count:1;ScUiMenuItem *items=calloc(count,sizeof(*items));if(!items)return SC_UI_E_MEMORY;
    for(ScU32 i=0;i<count;i++){items[i].size=sizeof(*items);items[i].version=1;
        items[i].command=f2.history_count?F2_HISTORY_FIRST+i:0;items[i].label=f2_text(f2.history_count?f2.history[i].path:"暂无导航历史");
        items[i].flags=f2.history_count?(i==f2.history_index?SC_UI_MENU_CHECKED:0):SC_UI_MENU_DISABLED;}
    ScStatus s=ScUiMenuReplace(f2.history_menu,items,count);free(items);return s;
}
static ScStatus f2_menus_create(void)
{
    ScUiMenu *file=0,*edit=0,*view=0,*help=0,*tools=0;ScStatus s=ScUiMenuCreate(&f2.menus);
    if(s==0)s=ScUiMenuCreate(&file);
    if(s==0)s=ScUiMenuCreate(&edit);
    if(s==0)s=ScUiMenuCreate(&view);
    if(s==0)s=ScUiMenuCreate(&f2.places);
    if(s==0)s=ScUiMenuCreate(&f2.history_menu);
    if(s==0)s=ScUiMenuCreate(&help);
    if(s==0)s=ScUiMenuCreate(&tools);
    if(s==0)s=f2_menu_add(file,"打开",SC_SHELL_COMMAND_OPEN,0,0,'\n',0);
    if(s==0)s=f2_menu_add(file,"打开方式…",F2_OPEN_WITH,0,0,0,0);
    if(s==0)s=f2_menu_add(file,"定位文件…",F2_OPEN_FILE,0,0,'o',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(file,"选择目录…",F2_BROWSE,0,0,0,0);
    if(s==0)s=f2_menu_add(file,"",0,0,SC_UI_MENU_SEPARATOR,0,0);
    if(s==0)s=f2_menu_add(file,"新建文件",F2_NEW_FILE,0,0,'n',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(file,"新建目录",SC_SHELL_COMMAND_NEW_DIRECTORY,0,0,'n',SC32_MOD_CONTROL|SC32_MOD_SHIFT);
    if(s==0)s=f2_menu_add(file,"重命名",SC_SHELL_COMMAND_RENAME,0,0,SC32_KEY_F1+1,0);
    if(s==0)s=f2_menu_add(file,"删除…",SC_SHELL_COMMAND_DELETE,0,0,SC32_KEY_DELETE,0);
    if(s==0)s=f2_menu_add(file,"属性",SC_SHELL_COMMAND_PROPERTIES,0,0,'\n',SC32_MOD_ALT);
    if(s==0)s=f2_menu_add(file,"",0,0,SC_UI_MENU_SEPARATOR,0,0);
    if(s==0)s=f2_menu_add(file,"关闭",F2_CLOSE,0,0,'w',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"复制",SC_SHELL_COMMAND_COPY,0,0,'c',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"剪切",SC_SHELL_COMMAND_CUT,0,0,'x',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"粘贴",SC_SHELL_COMMAND_PASTE,0,0,'v',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"",0,0,SC_UI_MENU_SEPARATOR,0,0);
    if(s==0)s=f2_menu_add(edit,"全选",F2_SELECT_ALL,0,0,'a',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"取消选择",F2_SELECT_NONE,0,0,0,0);
    if(s==0)s=f2_menu_add(edit,"在 Studio 编辑",F2_EDIT,0,0,'d',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(edit,"在 Debug 调试",F2_DEBUG,0,0,'d',SC32_MOD_CONTROL|SC32_MOD_SHIFT);
    if(s==0)s=f2_menu_add(view,"后退",F2_BACK,0,0,SC32_KEY_LEFT,SC32_MOD_ALT);
    if(s==0)s=f2_menu_add(view,"前进",F2_FORWARD,0,0,SC32_KEY_RIGHT,SC32_MOD_ALT);
    if(s==0)s=f2_menu_add(view,"上级目录",F2_UP,0,0,SC32_KEY_UP,SC32_MOD_ALT);
    if(s==0)s=f2_menu_add(view,"刷新",F2_REFRESH,0,0,SC32_KEY_F1+4,0);
    if(s==0)s=f2_menu_add(view,"地址栏",F2_ADDRESS,0,0,'l',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(view,"过滤",F2_FILTER,0,0,'f',SC32_MOD_CONTROL);
    if(s==0)s=f2_menu_add(view,"取消搜索",F2_CANCEL_SEARCH,0,0,27,0);
    if(s==0)s=f2_menu_add(help,"关于 Files 2",F2_ABOUT,0,0,SC32_KEY_F1,0);
    if(s==0)s=f2_menu_add(f2.menus,"文件",0,file,0,0,0);
    if(s==0)s=f2_menu_add(f2.menus,"编辑",0,edit,0,0,0);
    if(s==0)s=f2_menu_add(f2.menus,"查看",0,view,0,0,0);
    if(s==0)s=f2_menu_add(f2.menus,"书签",0,f2.places,0,0,0);
    if(s==0)s=f2_menu_add(f2.menus,"历史",0,f2.history_menu,0,0,0);
    if(s==0)s=f2_menu_add(f2.menus,"帮助",0,help,0,0,0);
    static const char *labels[]={"后退","前进","上级","刷新","新建","复制","粘贴","书签"};
    static const ScU32 commands[]={F2_BACK,F2_FORWARD,F2_UP,F2_REFRESH,SC_SHELL_COMMAND_NEW_DIRECTORY,SC_SHELL_COMMAND_COPY,SC_SHELL_COMMAND_PASTE,F2_BOOKMARK_MENU};
    for(ScU32 i=0;s==0 && i<8;i++)s=f2_menu_add(tools,labels[i],commands[i],0,0,0,0);
    if(s==0)s=ScUiMenuBind(f2.ui,f2.menubar,f2.menus);
    if(s==0)s=ScUiToolbarBind(f2.ui,f2.toolbar,tools);
    ScUiMenuRelease(file);ScUiMenuRelease(edit);ScUiMenuRelease(view);ScUiMenuRelease(help);ScUiMenuRelease(tools);return s;
}
static ScStatus f2_ui_error(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type==SC_UI_EVENT_ERROR && event->data && event->bytes>=sizeof(ScUiErrorInfo)){
        const ScUiErrorInfo *error=event->data;f2.ui_error=(ScU32)(error->code<0?-error->code:error->code);
        fprintf(stderr,"Files2 UI error=%d cause=%d operation=%u node=%u\n",error->code,error->cause_code,error->operation,error->node);}
    return 0;
}
static ScStatus f2_root_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_COMMAND){ScStatus s=f2_command(event->command);if(s<0)f2_error(s,f2.current?f2.current->path:"");return SC_UI_HANDLED;}
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST){ScStatus s=f2_exit_request();if(s<0 && s!=SC_UI_E_BUSY)f2_error(s,"");return SC_UI_VETO;}
    if(event->type==SC_UI_EVENT_DESTROY && event->node==f2.root){f2.quit=1;return 0;}
    if(event->type==SC_UI_EVENT_TIMER && event->data && event->bytes==sizeof(ScUiTimerEvent)){
        const ScUiTimerEvent *timer=event->data;if(timer->token==f2.reconnect_timer)f2.reconnect_pending=1;return 0;}
    if(event->type==SC_UI_EVENT_VISIBILITY){ScWindowInfo window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;
        ScStatus s=ScUiWindowNative(ui,f2.root,&window);if(s<0)return s;f2.visible=(window.state&SC32_WINDOW_VISIBLE)!=0;
        if(!f2.visible)f2_gesture_cancel(1);
        return 0;}
    if(event->type==SC_UI_EVENT_LAYOUT && event->node==f2.root){
        ScUiMetrics metrics;memset(&metrics,0,sizeof(metrics));metrics.size=sizeof(metrics);metrics.version=1;
        ScStatus s=ScUiMetricsQuery(ui,f2.root,&metrics);if(s<0)return s;
        if(f2.metrics.theme_epoch!=metrics.theme_epoch || f2.metrics.display_epoch!=metrics.display_epoch || f2.metrics.font_epoch!=metrics.font_epoch){
            ScHashClear(f2.icons);f2_gesture_cancel(1);f2.metrics=metrics;f2_table_notify(SC_UI_DATA_UPDATE);f2_tree_notify();}
        ScUiNodeInfo root,side;memset(&root,0,sizeof(root));root.size=sizeof(root);root.version=1;
        memset(&side,0,sizeof(side));side.size=sizeof(side);side.version=1;
        if(ScUiNodeQuery(ui,f2.root,&root)==0 && ScUiNodeQuery(ui,f2.sidebar,&side)==0){
            ScU32 show=metrics.scale_percent && (ScU32)root.rectangle.width*100/metrics.scale_percent>=700;
            if(((side.state&SC_UI_VISIBLE)!=0)!=show){ScUiNodeSetState(ui,f2.sidebar,SC_UI_VISIBLE,show?SC_UI_VISIBLE:0);
                ScUiNodeSetState(ui,f2.splitter,SC_UI_VISIBLE,show?SC_UI_VISIBLE:0);}}
    }
    return 0;
}
static ScStatus f2_key_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_KEY_DOWN)return 0;
    ScU32 key=event->key,mods=event->modifiers,command=0;
    if(mods&SC32_MOD_ALT){if(key==SC32_KEY_LEFT)command=F2_BACK;else if(key==SC32_KEY_RIGHT)command=F2_FORWARD;else if(key==SC32_KEY_UP)command=F2_UP;}
    else if(mods&SC32_MOD_CONTROL){if(key=='l')command=F2_ADDRESS;else if(key=='f')command=F2_FILTER;}
    if(key==SC32_KEY_F1+4)command=F2_REFRESH;
    if(key==27 && (f2.scan.active || f2.order.active))command=F2_CANCEL_SEARCH;
    if(command){ScStatus s=f2_command(command);if(s<0)f2_error(s,f2.current?f2.current->path:"");return SC_UI_HANDLED;}return 0;
}
static ScStatus f2_text_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type==SC_UI_EVENT_VALUE_CHANGED && event->node==f2.filter){f2.filter_pending=1;return 0;}
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->key=='\n'){
        ScU32 command=event->node==f2.address?F2_GO:event->node==f2.search?F2_SEARCH:0;
        if(command){ScStatus s=f2_command(command);if(s<0)f2_error(s,"");return SC_UI_HANDLED;}}
    return 0;
}
static ScStatus f2_tree_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED && !f2.publishing){
        ScUiSelectionInfo selection;memset(&selection,0,sizeof(selection));selection.size=sizeof(selection);selection.version=1;
        if(ScUiSelectionQuery(f2.tree_selection,&selection)==0 && selection.current && !(selection.current&0x80000000u)
            && selection.current<=f2.node_count){ScStatus s=f2_navigate(f2.nodes[selection.current-1].path,F2_NAV_NEW,0,0);
            if(s<0)f2_error(s,f2.nodes[selection.current-1].path);}
    }
    return 0;
}
static ScU32 f2_is_selected(ScU32 id)
{
    ScArray *items=0;if(ScUiSelectionCopy(f2.selection,&items)<0)return 0;
    ScContainerInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;ScU32 found=0;
    if(ScArrayQuery(items,&info)==0)for(ScU32 i=0;i<info.length;i++){ScU32 item;
        if(ScArrayGet(items,i,&item)==0 && item==id){found=1;break;}}
    ScArrayRelease(items);return found;
}
static ScStatus f2_table_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_DROP && event->data && event->bytes==sizeof(ScUiDragData)){
        const ScUiDragData *drop=event->data;if(f2_drop_notice(event,drop))return 0;}
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED){if(!f2.publishing)f2_status(0);return 0;}
    if(event->type==SC_UI_EVENT_CAPTURE_LOST || event->type==SC_UI_EVENT_INPUT_SYNC || event->type==SC_UI_EVENT_FOCUS_OUT){f2_gesture_cancel(0);return 0;}
    if(event->type==SC_UI_EVENT_KEY_DOWN){ScU32 key=event->key,mods=event->modifiers,command=0;
        if(mods&SC32_MOD_CONTROL){if(key=='c')command=SC_SHELL_COMMAND_COPY;else if(key=='x')command=SC_SHELL_COMMAND_CUT;
            else if(key=='v')command=SC_SHELL_COMMAND_PASTE;else if(key=='d')command=mods&SC32_MOD_SHIFT?F2_DEBUG:F2_EDIT;}
        else if(!(mods&SC32_MOD_ALT)){if(key=='\n')command=SC_SHELL_COMMAND_OPEN;else if(key==8)command=F2_UP;
            else if(key==SC32_KEY_F1+1)command=SC_SHELL_COMMAND_RENAME;else if(key==SC32_KEY_DELETE)command=SC_SHELL_COMMAND_DELETE;
            else if(key=='d' || key=='D')command=F2_DEBUG;}
        if(command){ScStatus s=f2_command(command);if(s<0)f2_error(s,f2.current?f2.current->path:"");return SC_UI_HANDLED;}}
    if(event->type==SC_UI_EVENT_POINTER_DOWN){ScU32 edge=event->message?event->message->data[3]:event->buttons,item=0,column=0;
        ScStatus hit=ScUiViewHitTest(ui,f2.table,event->x,event->y,&item,&column);
        if(edge&2u){if(hit==0 && !f2_is_selected(item))ScUiSelectionSet(f2.selection,item,SC_UI_REPLACE);
            ScUiNodeInfo node;memset(&node,0,sizeof(node));node.size=sizeof(node);node.version=1;
            ScUiNodeQuery(ui,f2.table,&node);ScStatus s=f2_menu_prepare(hit==0?SC_SHELL_MENU_SELECTION:SC_SHELL_MENU_BACKGROUND,
                node.rectangle.x+event->x,node.rectangle.y+event->y,1);if(s<0)f2_error(s,"");return SC_UI_HANDLED;}
        if((edge&1u) && hit==0){ScUiSelectionInfo selected;memset(&selected,0,sizeof(selected));selected.size=sizeof(selected);selected.version=1;
            ScUiSelectionQuery(f2.selection,&selected);ScU32 preserve=selected.count>1 && !(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL)) && f2_is_selected(item);
            f2.gesture=item;f2.press_x=event->x;f2.press_y=event->y;ScUiCapture(ui,f2.table,1);
            if(preserve){ScUiFocus(ui,f2.table);return SC_UI_HANDLED;}}
    }
    if(event->type==SC_UI_EVENT_POINTER_MOVE && f2.gesture && (event->buttons&1u)){
        ScI32 x=event->x-f2.press_x,y=event->y-f2.press_y;
        if(x>5 || x<-5 || y>5 || y<-5){ScStatus s=f2_drag_begin();if(s<0)f2_error(s,"");return SC_UI_HANDLED;}}
    if(event->type==SC_UI_EVENT_POINTER_UP && f2.gesture){
        ScU32 edge=event->message?event->message->data[3]:1;if(!(edge&1u))return 0;
        ScU32 item=f2.gesture,column=0,hit=0;f2.gesture=0;ScUiCapture(ui,f2.table,0);
        if(ScUiViewHitTest(ui,f2.table,event->x,event->y,&hit,&column)==0 && hit==item){
            if(f2.last_click==item && event->tick-f2.last_tick<=35 && !(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL))){
                f2.last_click=0;ScStatus s=f2_command(SC_SHELL_COMMAND_OPEN);if(s<0)f2_error(s,"");}
            else {f2.last_click=item;f2.last_tick=event->tick;}}
    }
    return 0;
}
static ScStatus f2_providers(void)
{
    ScUiProviderDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.flags=SC_UI_PROVIDER_SORT|SC_UI_PROVIDER_ASYNC;desc.procedure=f2_table_data;
    ScStatus s=ScUiProviderCreate(&desc,&f2.table_provider);
    if(s==0)s=ScUiSelectionCreate(f2.table_provider,SC_UI_SELECTION_MULTI|SC_UI_SELECTION_ALLOW_EMPTY,&f2.selection);
    if(s==0)s=ScUiViewBind(f2.ui,f2.table,f2.table_provider,f2.selection);
    desc.flags=SC_UI_PROVIDER_TREE|SC_UI_PROVIDER_ASYNC;desc.procedure=f2_tree_data;
    if(s==0)s=ScUiProviderCreate(&desc,&f2.tree_provider);
    if(s==0)s=ScUiSelectionCreate(f2.tree_provider,SC_UI_SELECTION_ALLOW_EMPTY,&f2.tree_selection);
    if(s==0)s=ScUiViewBind(f2.ui,f2.tree,f2.tree_provider,f2.tree_selection);
    ScUiColumnDesc columns[7];memset(columns,0,sizeof(columns));
    static const char *titles[]={"名称","类型","字节","UID:GID","权限","修订","完整路径"};
    static const ScI32 widths[]={260,120,104,96,70,90,330};
    for(ScU32 i=0;i<7;i++){columns[i].size=sizeof(columns[i]);columns[i].version=1;columns[i].column=i+1;
        columns[i].field=i;columns[i].flags=SC_UI_COLUMN_SORTABLE|(i==2 || i==5?SC_UI_COLUMN_RIGHT:0);
        columns[i].title=f2_text(titles[i]);columns[i].width=widths[i];columns[i].min_width=44;columns[i].max_width=1800;
        columns[i].format=i==2?SC_UI_FORMAT_BYTES:i==5?SC_UI_FORMAT_U32:SC_UI_FORMAT_TEXT;
        columns[i].sort_kind=i==2 || i==5?SC_UI_SORT_UNSIGNED:SC_UI_SORT_TEXT;}
    if(s==0)s=ScUiTableSetColumns(f2.ui,f2.table,columns,7);
    if(s==0){ScU32 root;s=f2_tree_add("/",0,0,0,&root);if(s==0)s=ScUiTreeExpand(f2.ui,f2.tree,root,1);}
    return s;
}
static ScStatus f2_reconnect_schedule(void)
{
    if(f2.reconnect_timer || f2.quit || f2.exit_pending)return 0;
    ScMonotonicClock clock;memset(&clock,0,sizeof(clock));clock.size=sizeof(clock);clock.version=1;
    ScStatus s=ScSystemMonotonic(&clock);if(s<0)return s;
    ScUiTimerDesc timer;memset(&timer,0,sizeof(timer));timer.size=sizeof(timer);timer.version=1;
    timer.node=f2.root;timer.flags=SC_UI_TIMER_BACKGROUND;timer.period_ticks=clock.hz?clock.hz:100;
    timer.deadline=clock.ticks;ScU32 previous=timer.deadline.lo;timer.deadline.lo+=timer.period_ticks;
    if(timer.deadline.lo<previous)timer.deadline.hi++;
    return ScUiTimerSet(f2.ui,&timer,&f2.reconnect_timer);
}
static ScStatus f2_connect(void)
{
    if(f2.shell || f2.exit_pending || f2.quit)return 0;
    ScShellConnectDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.procedure=f2_shell_event;desc.budget_bytes=131072;ScStatus s=ScShellConnect(&desc,&f2.shell);
    if(s==0){if(f2.reconnect_timer)ScUiTimerCancel(f2.ui,f2.reconnect_timer);f2.reconnect_timer=0;f2.catalog_dirty=1;}
    else f2_reconnect_schedule();
    return s;
}
static ScStatus f2_start_argument(const char *arguments,char *out,ScU32 capacity)
{
    /* MODERN关联总会给document加双引号；旧Files入口和直接Launch
     * 仍可传裸路径。只解析本应用的单一路径语法，不转交命令解释器。 */
    ScU32 i=0,n=0,quoted=0;
    while(arguments[i]==' ' || arguments[i]=='\t')i++;
    while(arguments[i]){char c=arguments[i++];
        if(c=='"'){quoted=!quoted;continue;}
        if(!quoted && (c==' ' || c=='\t')){while(arguments[i]==' ' || arguments[i]=='\t')i++;
            if(arguments[i])return SC_UI_E_INVALID;
            break;}
        if(quoted && c=='\\' && (arguments[i]=='"' || arguments[i]=='\\'))c=arguments[i++];
        if(n+1>=capacity)return SC_UI_E_RANGE;
        out[n++]=c;
    }
    if(quoted)return SC_UI_E_INVALID;
    out[n]=0;return 0;
}
static ScStatus f2_create(void)
{
    memset(&f2,0,sizeof(f2));f2.next_item=f2.table_revision=f2.tree_revision=1;f2.visible=1;
    f2.identity.size=sizeof(f2.identity);f2.identity.version=1;ScStatus s=ScIdentityQuery(&f2.identity);if(s<0)return s;
    s=f2_paths();if(s<0)return s;s=f2_hash(4,0,&f2.tree_index);if(s==0)s=f2_hash(sizeof(ScUiIcon *),f2_icon_drop,&f2.icons);if(s<0)return s;
    ScUiContextDesc context;memset(&context,0,sizeof(context));context.size=sizeof(context);context.version=1;
    context.cache_budget_bytes=2097152;context.undo_budget_bytes=65536;context.scheduling_quantum=64;context.error_procedure=f2_ui_error;
    s=ScUiContextCreate(&context,&f2.ui);if(s<0)return s;
    ScDisplayInfo display;memset(&display,0,sizeof(display));display.size=sizeof(display);display.version=1;s=ScDisplayQuery(&display);if(s<0)return s;
    ScUiWindowDesc window;memset(&window,0,sizeof(window));window.size=sizeof(window);window.version=1;window.title=f2_text("Files 2");
    window.x=32;window.y=28;window.client_width=display.width>1072?1024:display.width>128?display.width-64:display.width;
    window.client_height=display.height>820?720:display.height>160?display.height-120:display.height;window.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(f2.ui,&window,&f2.root);if(s<0)return s;
    s=f2_layout_node(f2.root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0);if(s<0)return s;
    ScUiNode address_row,filter_row,label,go,browse,search_button,cancel;
    s=f2_widget(f2.root,SC_UI_MENUBAR,"",0,SC_UI_AUTO,26,0,&f2.menubar);
    if(s==0)s=f2_widget(f2.root,SC_UI_TOOLBAR,"",0,SC_UI_AUTO,34,0,&f2.toolbar);
    if(s==0)s=f2_widget(f2.root,SC_UI_PANEL,"",0,SC_UI_AUTO,32,0,&address_row);
    if(s==0)s=f2_widget(address_row,SC_UI_LABEL,"地址",0,44,SC_UI_AUTO,0,&label);
    if(s==0)s=f2_widget(address_row,SC_UI_TEXTBOX,"/",F2_GO,SC_UI_AUTO,SC_UI_AUTO,1,&f2.address);
    if(s==0)s=f2_widget(address_row,SC_UI_BUTTON,"转到",F2_GO,58,SC_UI_AUTO,0,&go);
    if(s==0)s=f2_widget(address_row,SC_UI_BUTTON,"选择…",F2_BROWSE,70,SC_UI_AUTO,0,&browse);
    if(s==0)s=f2_widget(f2.root,SC_UI_PANEL,"",0,SC_UI_AUTO,32,0,&filter_row);
    if(s==0)s=f2_widget(filter_row,SC_UI_LABEL,"过滤",0,44,SC_UI_AUTO,0,&label);
    if(s==0)s=f2_widget(filter_row,SC_UI_TEXTBOX,"",F2_FILTER,SC_UI_AUTO,SC_UI_AUTO,1,&f2.filter);
    if(s==0)s=f2_widget(filter_row,SC_UI_TEXTBOX,"",F2_SEARCH,SC_UI_AUTO,SC_UI_AUTO,1,&f2.search);
    if(s==0)s=f2_widget(filter_row,SC_UI_BUTTON,"子树搜索",F2_SEARCH,86,SC_UI_AUTO,0,&search_button);
    if(s==0)s=f2_widget(filter_row,SC_UI_BUTTON,"取消",F2_CANCEL_SEARCH,58,SC_UI_AUTO,0,&cancel);
    if(s==0)s=f2_widget(f2.root,SC_UI_PROGRESS,"",0,SC_UI_AUTO,5,0,&f2.scan_progress);
    if(s==0)s=ScUiNodeSetState(f2.ui,f2.scan_progress,SC_UI_VISIBLE,0);
    if(s==0)s=f2_widget(f2.root,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&f2.body);
    if(s==0)s=f2_widget(f2.body,SC_UI_PANEL,"",0,218,SC_UI_AUTO,0,&f2.sidebar);
    if(s==0)s=f2_layout_node(f2.sidebar,SC_UI_VBOX,218,SC_UI_AUTO,0);
    if(s==0)s=f2_widget(f2.sidebar,SC_UI_LABEL,"目录",0,SC_UI_AUTO,25,0,&f2.location);
    if(s==0)s=f2_widget(f2.sidebar,SC_UI_TREE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&f2.tree);
    if(s==0)s=f2_widget(f2.body,SC_UI_SPLITTER,"",0,6,SC_UI_AUTO,0,&f2.splitter);
    if(s==0)s=f2_widget(f2.body,SC_UI_TABLE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&f2.table);
    if(s==0)s=f2_widget(f2.root,SC_UI_STATUS,"",0,SC_UI_AUTO,26,0,&f2.status);
    if(s<0)return s;
    ScUiNodeSetFont(f2.ui,f2.table,SC_UI_FONT_DENSE);ScUiNodeSetFont(f2.ui,f2.tree,SC_UI_FONT_DENSE);
    ScUiText name=f2_text("目录地址");ScUiAccessibleSetName(f2.ui,f2.address,&name);
    name=f2_text("当前目录名称过滤");ScUiAccessibleSetName(f2.ui,f2.filter,&name);
    name=f2_text("子树搜索名称");ScUiAccessibleSetName(f2.ui,f2.search,&name);
    name=f2_text("目录树");ScUiAccessibleSetName(f2.ui,f2.tree,&name);
    name=f2_text("文件详情");ScUiAccessibleSetName(f2.ui,f2.table,&name);
    ScUiText tip=f2_text("当前目录名称过滤；不重新读取磁盘。清空恢复全部行。");ScUiTooltipSet(f2.ui,f2.filter,&tip);
    tip=f2_text("按名称递归搜索当前目录；Esc 取消。不能读取的目录单独计数。");ScUiTooltipSet(f2.ui,f2.search,&tip);
    tip=f2_text("拖动表头分隔线调整列宽；Ctrl/Shift 多选；拖到桌面或其它 Files 窗口。");ScUiTooltipSet(f2.ui,f2.table,&tip);
    s=f2_providers();if(s==0)s=f2_menus_create();if(s<0)return s;
    s=f2_listen(f2.root,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE,f2_root_event);
    if(s==0)s=f2_listen(f2.root,SC_UI_CAPTURE_PHASE,f2_key_event);
    if(s==0)s=f2_listen(f2.table,SC_UI_TARGET_PHASE,f2_table_event);
    if(s==0)s=f2_listen(f2.tree,SC_UI_TARGET_PHASE,f2_tree_event);
    if(s==0)s=f2_listen(f2.address,SC_UI_TARGET_PHASE,f2_text_event);
    if(s==0)s=f2_listen(f2.filter,SC_UI_TARGET_PHASE,f2_text_event);
    if(s==0)s=f2_listen(f2.search,SC_UI_TARGET_PHASE,f2_text_event);
    if(s<0)return s;
    ScU32 format=SC32_CLIPBOARD_FILES;ScUiDropTargetDesc drop;memset(&drop,0,sizeof(drop));drop.size=sizeof(drop);drop.version=1;
    drop.actions=SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE;drop.formats=&format;drop.format_count=1;drop.procedure=f2_drop;
    s=ScUiDropTargetRegister(f2.ui,f2.table,&drop);if(s==0)s=ScUiDropTargetRegister(f2.ui,f2.tree,&drop);if(s<0)return s;
    s=ScShellCatalogCreate(&f2.catalog);if(s<0)return s;
    ScStatus connected=f2_connect();if(connected<0)fprintf(stderr,"Files2 shell unavailable=%d\n",connected);
    f2.catalog_dirty=1;
    static const char *places[]={"/","/HOME","/SYS","/APPS","/BIN"};
    for(ScU32 i=0;i<5;i++){s=f2_bookmark_add_path(places[i]);if(s<0)return s;}
    ScStatus bookmarks=f2_bookmarks_load();if(bookmarks<0)fprintf(stderr,"Files2 bookmarks retained error=%d\n",bookmarks);
    f2_places_refresh();f2_history_refresh();
    char arguments[1024],start_path[1024];ScU32 length=0;arguments[0]=0;s=ScProcessArguments(arguments,sizeof(arguments),&length);
    if(s<0)return s;
    s=f2_start_argument(arguments,start_path,sizeof(start_path));
    if(s==0)s=f2_navigate(start_path[0]?start_path:"/",F2_NAV_NEW,0,0);
    if(s<0 && arguments[0]){f2_error(s,arguments);s=f2_navigate("/",F2_NAV_NEW,0,0);}
    if(s==0)s=ScUiFocus(f2.ui,f2.table);
    return s;
}
static ScStatus f2_catalog_step(ScU32 *ready)
{
    if(f2.reconnect_pending){f2.reconnect_pending=0;if(!f2.shell)f2_connect();}
    if(!f2.shell)return 0;
    ScStatus s=ScShellStep(f2.shell,0);if(s==SC_SHELL_READY)*ready=1;
    if(s<0){ScShellRequestClose(f2.catalog_request);f2.catalog_request=0;ScShellClose(f2.shell);f2.shell=0;
        f2_reconnect_schedule();f2_status("桌面服务不可用；保留目录和现有关联快照");return 0;}
    if(f2.catalog_dirty && !f2.catalog_request){ScShellInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        if(ScShellQuery(f2.shell,&info)==0 && info.state==SC_SHELL_CONNECTED){s=ScShellQueryCatalog(f2.shell,0,&f2.catalog_request);
            if(s==0)f2.catalog_dirty=0;else if(s!=SC_SHELL_E_BUSY)return s;}}
    if(f2.catalog_request){ScShellRequestInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        s=ScShellRequestQuery(f2.catalog_request,&info);if(s<0){ScShellRequestClose(f2.catalog_request);f2.catalog_request=0;return s;}
        if(info.state>=SC_SHELL_REQUEST_COMPLETE){
            if(info.state==SC_SHELL_REQUEST_COMPLETE && info.status>=0){ScShellCatalog *candidate=0;s=ScShellRequestCatalog(f2.catalog_request,&candidate);
                if(s==0){ScShellCatalogRelease(f2.catalog);f2.catalog=candidate;ScHashClear(f2.icons);f2_table_notify(SC_UI_DATA_UPDATE);f2_tree_notify();}}
            else s=info.status<0?info.status:SC_SHELL_E_UNKNOWN;
            ScShellRequestClose(f2.catalog_request);f2.catalog_request=0;if(s<0)return s;}
    }
    return 0;
}
static ScStatus f2_background(ScU32 *ready)
{
    ScStatus first=f2_catalog_step(ready),s;
    s=f2_drop_step(ready);if(s<0&&!first)first=s;
    ScU32 menu_busy=0;
    if(f2.shell_menu){s=ScShellMenuStep(f2.shell_menu,0);if(s==SC_SHELL_READY)*ready=1;
        if(s==SC_SHELL_PENDING || s==SC_SHELL_READY)menu_busy=1;
        if(s<0 && s!=f2.menu_error && !first)first=s;
        f2.menu_error=s<0?s:0;}
    F2MenuWork **link=&f2.menu_work;
    while(*link){F2MenuWork *work=*link;s=ScShellMenuStep(work->menu,0);if(s==SC_SHELL_READY)*ready=1;
        if(s==SC_SHELL_END){*link=work->next;ScShellMenuRelease(work->menu);free(work);continue;}
        if(s<0 && s!=work->last_error && !first)first=s;
        work->last_error=s<0?s:0;
        menu_busy=1;link=&work->next;}
    s=f2_jobs_step(ready);if(s<0 && !first)first=s;
    if(f2.writer.active){s=f2_writer_step();if(s<0 && !first)first=s;if(f2.writer.active)*ready=1;}
    if(f2.filter_pending){char candidate[F2_QUERY];s=f2_read_node(f2.filter,candidate,sizeof(candidate));
        if(s<0){f2.filter_pending=0;if(!first)first=s;}
        else {strcpy(f2.filter_text,candidate);
            if(!f2.scan.active && !(f2.order.active && f2.order.publish)){f2.filter_pending=0;
                if(f2.current){s=f2_order_begin(f2.current,0,f2.sort_field,f2.sort_descending,0,0);if(s<0 && !first)first=s;}}}}
    if(f2.scan.active){s=f2_scan_step();if(s<0){ScUiNodeSetState(f2.ui,f2.scan_progress,SC_UI_VISIBLE,0);if(!first)first=s;}if(f2.scan.active)*ready=1;}
    if(f2.order.active){s=f2_order_step();if(s<0){f2_order_cancel();if(!first)first=s;}if(f2.order.active)*ready=1;}
    if(f2.visible && (f2.tree_scan.node || f2.tree_cursor<f2.tree_count)){s=f2_tree_step();if(s<0 && !first)first=s;
        if(f2.tree_scan.node || f2.tree_cursor<f2.tree_count)*ready=1;}
    if(f2.properties){s=ScShellDialogStep(f2.properties);if(s==SC_SHELL_READY)*ready=1;
        if(s==SC_SHELL_END){ScShellDialogClose(f2.properties);f2.properties=0;f2.refresh_pending=1;}}
    if(f2.bookmark_save_pending && !f2.writer.active && !f2.exit_pending){s=f2_bookmarks_save();if(s<0 && !first)first=s;*ready=1;}
    ScU32 jobs_active=0;for(F2Job *job=f2.jobs;job;job=job->next)if(!job->final || job->deferred)jobs_active=1;
    if(f2.refresh_pending && f2.visible && !menu_busy && !jobs_active && !f2.scan.active && !f2.order.active
        && !f2.gesture && !f2.dialog && !f2.exit_pending){
        f2.refresh_pending=0;f2_tree_invalidate();s=f2_navigate(f2.current?f2.current->path:"/",F2_NAV_REFRESH,0,
            f2.current && f2.current->search?f2.current->query:0);if(s<0 && !first)first=s;*ready=1;}
    if(f2.exit_pending && !f2.writer.active && !jobs_active){while(f2.jobs){F2Job *job=f2.jobs;f2.jobs=job->next;f2_job_close(job);}f2.quit=1;}
    return first;
}
static void f2_close(void)
{
    f2.quit=1;f2_gesture_cancel(1);
    if(f2.reconnect_timer && f2.ui)ScUiTimerCancel(f2.ui,f2.reconnect_timer);
    if(f2.ui){if(f2.table)ScUiDropTargetRemove(f2.ui,f2.table);if(f2.tree)ScUiDropTargetRemove(f2.ui,f2.tree);}
    if(f2.popup && f2.ui)ScUiMenuClose(f2.ui,f2.popup);
    ScShellMenuRelease(f2.shell_menu);f2.shell_menu=0;
    while(f2.menu_work){F2MenuWork *work=f2.menu_work;f2.menu_work=work->next;ScShellMenuRelease(work->menu);free(work);}
    while(f2.jobs){F2Job *job=f2.jobs;f2.jobs=job->next;ScShellFileOperationCancel(job->handle);f2_job_close(job);}
    ScShellDialogClose(f2.properties);if(f2.dialog)ScUiDialogRelease(f2.dialog);
    if(f2.writer.candidate.lease){ScLeaseControl control=f2_lease_control(&f2.writer.candidate);ScFileLeaseCancel(&control,&f2.writer.candidate);f2_release_step(&f2.writer.candidate);}
    f2_release_step(&f2.writer.target);if(f2.writer.file)ScFileClose(f2.writer.file);if(f2.writer.target_file)ScFileClose(f2.writer.target_file);
    ScStringRelease(f2.writer.body);f2.writer.body=0;
    ScShellRequestClose(f2.catalog_request);ScShellClose(f2.shell);ScShellCatalogRelease(f2.catalog);
    f2_scan_cancel();f2_order_cancel();f2_tree_scan_clear();
    if(f2.ui && f2.root)ScUiNodeDestroy(f2.ui,f2.root);
    ScUiSelectionRelease(f2.selection);ScUiSelectionRelease(f2.tree_selection);
    ScUiProviderRelease(f2.table_provider);ScUiProviderRelease(f2.tree_provider);
    ScUiMenuRelease(f2.menus);ScUiMenuRelease(f2.places);ScUiMenuRelease(f2.history_menu);
    if(f2.drop_receipt.id)ScUiDropRelease(f2.ui,&f2.drop_receipt);
    ScHashRelease(f2.icons);ScHashRelease(f2.tree_index);ScUiContextRelease(f2.ui);
    f2_snapshot_free(f2.current);for(ScU32 i=0;i<f2.node_count;i++)free(f2.nodes[i].children);
    free(f2.nodes);free(f2.tree_queue);free(f2.history);free(f2.bookmarks);free(f2.drop_bytes);
}
