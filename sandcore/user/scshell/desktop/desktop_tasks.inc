/* 枚举每轮只取一个有界page，完整候选才换任务栏；STALE从头重取，
 * 空过滤page并非END。按钮的对象四元组直接来自WM，绝不按PID猜窗口。 */
static ScStatus desk_tasks_step(void)
{
    ScShellWindowInfo rows[8];ScShellWindowPage page;
    memset(&page,0,sizeof(page));page.size=sizeof(page);page.version=1;
    if(!desk.task_scanning){desk.task_cursor=desk.task_epoch=desk.task_used=0;desk.task_scanning=1;}
    ScStatus s=ScShellHostWindows(desk.host.host_epoch,desk.task_cursor,desk.task_epoch,rows,8,&page);
    if(s==SC32_E_STALE){desk.task_scanning=0;return SC_UI_PENDING;}
    if(s<0){desk.task_scanning=0;return s;}
    if(!desk_reserve((void **)&desk.task_candidate,&desk.task_allocated,desk.task_used+page.count,sizeof(DeskTask)))
        return SC_UI_E_MEMORY;
    for(ScU32 i=0;i<page.count;i++){
        memset(desk.task_candidate+desk.task_used,0,sizeof(DeskTask));
        desk.task_candidate[desk.task_used++].value=rows[i];
    }
    desk.task_cursor=page.cursor;desk.task_epoch=page.epoch;
    if(!(page.flags&SC32_SHELL_WINDOWS_FINISHED))return SC_UI_PENDING;
    int changed=desk.task_used!=desk.task_count;
    for(ScU32 i=0;!changed && i<desk.task_used;i++)
        if(memcmp(&desk.tasks[i].value,&desk.task_candidate[i].value,sizeof(ScShellWindowInfo)))changed=1;
    if(changed){DeskTask *old=desk.tasks;ScU32 capacity=desk.task_capacity;
        desk.tasks=desk.task_candidate;desk.task_capacity=desk.task_allocated;desk.task_count=desk.task_used;
        desk.task_candidate=old;desk.task_allocated=capacity;
        if(desk.task_first>=desk.task_count)desk.task_first=0;
        if(desk.bar_focus>desk.task_count)desk.bar_focus=0;
        ScUiInvalidate(desk.ui,desk.task_canvas,0);
    }
    desk.task_scanning=desk.task_dirty=0;return 0;
}
static ScStatus desk_tasks_action(ScU32 index,ScU32 action)
{
    if(index>=desk.task_count)return SC_UI_E_STALE;
    const ScWindowInfo *w=&desk.tasks[index].value.window;
    ScShellWindowAction request;memset(&request,0,sizeof(request));request.size=sizeof(request);request.version=1;
    request.host_epoch=desk.host.host_epoch;request.window=w->window;request.generation=w->generation;
    request.pid=w->owner_pid;request.pid_generation=w->owner_generation;request.action=action;
    ScWindowInfo out;memset(&out,0,sizeof(out));out.size=sizeof(out);out.version=1;
    ScStatus s=ScShellHostWindowAction(&request,&out);desk.task_dirty=1;return s;
}
static ScStatus desk_tasks_arrange(ScU32 tiled)
{
    ScU32 count=desk.task_count;if(!count)return 0;
    ScU32 columns=1;if(tiled)while(columns<count/columns || columns*columns<count)columns++;
    ScU32 rows=(count+columns-1)/columns;ScStatus first=0;
    for(ScU32 i=0;i<count;i++){
        ScWindowInfo *w=&desk.tasks[i].value.window;
        if(w->state&SC32_WINDOW_MODAL)continue;
        ScShellWindowAction a;memset(&a,0,sizeof(a));a.size=sizeof(a);a.version=1;
        a.host_epoch=desk.host.host_epoch;a.window=w->window;a.generation=w->generation;
        a.pid=w->owner_pid;a.pid_generation=w->owner_generation;a.action=SC32_SHELL_ACTION_PLACE;
        if(tiled){a.x=(ScI32)(i%columns*desk.host.work_width/columns);
            a.y=(ScI32)(i/columns*desk.host.work_height/rows);
            a.width=desk.host.work_width/columns;a.height=desk.host.work_height/rows;}
        else {ScU32 offset=i%12u*desk.metrics.row_height;
            a.x=(ScI32)offset;a.y=(ScI32)offset;
            a.width=desk.host.work_width*3/4;a.height=desk.host.work_height*3/4;}
        ScWindowInfo out;memset(&out,0,sizeof(out));out.size=sizeof(out);out.version=1;
        ScStatus s=ScShellHostWindowAction(&a,&out);if(s<0 && !first)first=s;
        ScProcessYield();
    }
    desk.task_dirty=1;return first;
}
static ScStatus desk_bar_paint(ScUiContext *ui,ScUiNode node,ScUiPainter *p,void *context)
{
    (void)context;ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiNodeQuery(ui,node,&info);if(s<0)return s;
    ScUiRect area={0,0,info.rectangle.width,info.rectangle.height};
    ScUiPainterFill(p,&area,SC32_COLOR_FACE);ScUiPainterLine(p,0,0,area.width-1,0,SC32_COLOR_HIGHLIGHT);
    ScI32 gap=desk_scale(5),start=desk_scale(88),clock=desk_scale(145);
    if(start>area.width/3)start=area.width/3;
    if(clock>area.width/3)clock=area.width/3;
    ScUiRect button={gap,gap,start-gap*2,area.height-gap*2};
    ScUiPainterFill(p,&button,SC32_COLOR_FACE_ALT);desk_frame(p,&button,SC32_COLOR_LINE);
    ScUiNodeInfo state;memset(&state,0,sizeof(state));state.size=sizeof(state);state.version=1;
    ScU32 keyboard=ScUiNodeQuery(ui,node,&state)==0 && (state.state&SC_UI_FOCUSED);
    if(keyboard && !desk.bar_focus)desk_frame(p,&button,SC32_COLOR_ACCENT);
    ScUiText text=desk_text("沙核");ScUiPainterText(p,&button,&text,SC_UI_FONT_HEADING,SC_UI_TEXT_CENTER);
    ScUiRect time={area.width-clock,gap,clock-gap,area.height-gap*2};
    text=desk_text(desk.clock);ScUiPainterTextColor(p,&time,&text,SC_UI_FONT_DENSE,SC_UI_TEXT_CENTER|SC_UI_TEXT_ELLIPSIS,
        desk.layout_unsaved?SC32_COLOR_ALERT:SC32_COLOR_TEXT);
    ScI32 width=area.width-start-clock-gap;if(width<1)return 0;
    ScI32 minimum=desk_scale(80),normal=desk_scale(190);
    ScU32 shown=(ScU32)(width/minimum);if(!shown)shown=1;
    if(shown>desk.task_count)shown=desk.task_count;
    ScI32 step=shown?width/(ScI32)shown:width;if(step>normal)step=normal;
    for(ScU32 i=0;i<desk.task_count;i++)desk.tasks[i].button.width=0;
    for(ScU32 i=0;i<shown && desk.task_first+i<desk.task_count;i++){
        DeskTask *task=desk.tasks+desk.task_first+i;
        ScUiRect r={start+(ScI32)i*step,gap,step-gap,area.height-gap*2};task->button=r;
        ScU32 focused=task->value.window.state&SC32_WINDOW_FOCUSED;
        ScUiPainterFill(p,&r,focused?SC32_COLOR_SELECT:SC32_COLOR_FACE_ALT);
        desk_frame(p,&r,focused?SC32_COLOR_ACCENT:SC32_COLOR_LINE);
        if(keyboard && desk.bar_focus==desk.task_first+i+1)desk_frame(p,&r,SC32_COLOR_ACCENT);
        ScUiRect title=r;title.x+=gap;title.width-=gap*2;
        char display[128];desk_display_name(task->value.title,(ScU32)strlen(task->value.title),display,sizeof(display));
        text=desk_text(display);
        ScUiPainterTextColor(p,&title,&text,SC_UI_FONT_BODY,SC_UI_TEXT_ELLIPSIS,
            focused?SC32_COLOR_SELECT_TEXT:SC32_COLOR_TEXT);
        if(task->value.window.state&SC32_WINDOW_MINIMIZED)
            ScUiPainterLine(p,r.x+gap,r.y+r.height-3,r.x+r.width-gap,r.y+r.height-3,SC32_COLOR_MUTED);
    }
    return 0;
}
static ScStatus desk_task_menu(ScU32 index,ScI32 x,ScI32 y)
{
    desk.pressed=index;ScUiMenu *menu=0;ScStatus s=ScUiMenuCreate(&menu);
    const char *labels[5]={"恢复","最小化","最大化","切换到窗口","关闭"};
    const ScU32 commands[5]={DESK_TASK_RESTORE,DESK_TASK_MINIMIZE,DESK_TASK_MAXIMIZE,DESK_TASK_FOCUS,DESK_TASK_CLOSE};
    for(ScU32 i=0;s==0 && i<5;i++){
        ScUiMenuItem item;memset(&item,0,sizeof(item));item.size=sizeof(item);item.version=1;
        item.command=commands[i];item.label=desk_text(labels[i]);ScU32 id;s=ScUiMenuInsert(menu,0,&item,&id);
    }
    if(s==0){if(desk.popup)ScUiMenuClose(desk.ui,desk.popup);
        s=ScUiMenuOpen(desk.ui,desk.bar,menu,x,y,0,&desk.popup);}
    ScUiMenuRelease(menu);return s;
}
static ScStatus desk_bar_input(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_KEY_DOWN){
        if(event->key==SC32_KEY_F1+5){ScUiFocus(ui,desk.canvas);return SC_UI_HANDLED;}
        if(event->key==SC32_KEY_LEFT || event->key==SC32_KEY_RIGHT || event->key==SC32_KEY_HOME || event->key==SC32_KEY_END){
            if(event->key==SC32_KEY_HOME)desk.bar_focus=0;
            else if(event->key==SC32_KEY_END)desk.bar_focus=desk.task_count;
            else if(event->key==SC32_KEY_LEFT){if(desk.bar_focus)desk.bar_focus--;}
            else if(desk.bar_focus<desk.task_count)desk.bar_focus++;
            if(desk.bar_focus){desk.task_first=desk.bar_focus-1;}
            ScUiInvalidate(ui,desk.task_canvas,0);return SC_UI_HANDLED;
        }
        if(event->key=='\n' || event->key==13 || event->key==' ' || (event->key==27 && (event->modifiers&SC32_MOD_CONTROL))){
            ScStatus s;
            if(!desk.bar_focus || event->key==27)s=desk_menu(SC_SHELL_MENU_START,0,0);
            else s=desk_tasks_action(desk.bar_focus-1,SC32_SHELL_ACTION_FOCUS);
            desk_error(s,"");return SC_UI_HANDLED;
        }
        if(event->key==SC32_KEY_F1+9 && (event->modifiers&SC32_MOD_SHIFT) && desk.bar_focus){
            ScUiRect r=desk.tasks[desk.bar_focus-1].button;desk_error(desk_task_menu(desk.bar_focus-1,r.x,r.y),"");return SC_UI_HANDLED;
        }
    }
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->key==27){if(desk.popup)ScUiMenuClose(desk.ui,desk.popup);desk.popup=0;return SC_UI_HANDLED;}
    if(event->type==SC_UI_EVENT_WHEEL && event->message){ScI32 delta=(ScI32)event->message->data[2];
        ScI32 next=(ScI32)desk.task_first-delta;
        if(next<0)next=0;
        if((ScU32)next>=desk.task_count)next=desk.task_count?(ScI32)desk.task_count-1:0;
        if((ScU32)next!=desk.task_first){desk.task_first=(ScU32)next;ScUiInvalidate(desk.ui,desk.task_canvas,0);}
        return SC_UI_HANDLED;
    }
    if(event->type!=SC_UI_EVENT_POINTER_DOWN)return 0;
    if(!event->message || !(event->message->data[3]&3u))return 0;
    ScUiFocus(ui,desk.task_canvas);
    if(event->x<desk_scale(88)){
        desk.bar_focus=0;
        desk_error(desk_menu(SC_SHELL_MENU_START,0,0),"");return SC_UI_HANDLED;
    }
    for(ScU32 i=0;i<desk.task_count;i++){
        ScUiRect *r=&desk.tasks[i].button;
        if(r->width<=0 || event->x<r->x || event->x>=r->x+r->width
            || event->y<r->y || event->y>=r->y+r->height)continue;
        ScStatus s;
        desk.bar_focus=i+1;
        if(event->buttons&2u)s=desk_task_menu(i,event->x,event->y);
        else {ScU32 state=desk.tasks[i].value.window.state;
            s=desk_tasks_action(i,(state&SC32_WINDOW_FOCUSED)?SC32_SHELL_ACTION_MINIMIZE:
                (state&SC32_WINDOW_MINIMIZED)?SC32_SHELL_ACTION_RESTORE:SC32_SHELL_ACTION_FOCUS);}
        desk_error(s,desk.tasks[i].value.executable);return SC_UI_HANDLED;
    }
    return 0;
}
