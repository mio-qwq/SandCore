/* 会话桌面host只是两张本人Ring3画布的可信角色，不是零环控件树。
 * 80B win_t和旧FRAME长度不变；角色/首帧/焦点只加在动态私有尾部。
 * 本片段尚未接入工作源、未构建或运行，integration-wm.patch负责接线。 */
enum { WM_SHELL_DESKTOP=1,WM_SHELL_TASKBAR=2 };

typedef struct {
    window_record_t *record;
    u32 pixels,pages;
    int x,y,width,height,cw,ch,scale;
} wm_shell_candidate_t;

static void wm_shell_zero(void *value,u32 bytes)
{
    u8 *out=value;
    for(u32 i=0;i<bytes;i++)out[i]=0;
}

static desktop_view_t *wm_shell_scope(int pid)
{
    if(pid<=0 || !task_owned(pid) || TASK(pid).state!=1)return 0;
    u32 sid=session_task(pid);
    session_t *scope=session_get(sid);
    if(!scope || scope->closing || scope->kind!=SESSION_NORMAL
        || !sc32_host_is_host(pid,sid))return 0;
    return view_get(sid);
}

static int wm_shell_pair(desktop_view_t *view,window_record_t **desktop,window_record_t **bar)
{
    if(!view || !view->shell_prepared || !view->shell_pid)return 0;
    int pid=(int)view->shell_pid;
    if(!task_owned(pid) || TASK(pid).state!=1
        || task_generation(pid)!=view->shell_generation
        || !sc32_host_is_host(pid,view->shell_session))return 0;
    window_record_t *d=window_record((int)view->shell_desktop);
    window_record_t *b=window_record((int)view->shell_taskbar);
    if(!d || !b || d==b || d->shell_role!=WM_SHELL_DESKTOP
        || b->shell_role!=WM_SHELL_TASKBAR || d->session!=view->shell_session
        || b->session!=view->shell_session || d->window.owner!=pid || b->window.owner!=pid
        || d->owner_generation!=view->shell_generation
        || b->owner_generation!=view->shell_generation)return 0;
    if(desktop)*desktop=d;
    if(bar)*bar=b;
    return 1;
}

int wm_shell_active(u32 sid)
{
    desktop_view_t *view=view_get(sid);
    return view && view->shell_active && wm_shell_pair(view,0,0);
}

static int wm_shell_preparing(window_record_t *p)
{
    if(!p || !p->shell_role)return 0;
    desktop_view_t *view=view_get(p->session);
    return view && !view->shell_active && wm_shell_pair(view,0,0)
        && p->shell_ready_geometry!=p->gui.geometry;
}

static int wm_shell_ancestor_visible(window_record_t *p)
{
    if(!p->shell_ancestor)return 1;
    window_record_t *root=window_record((int)p->shell_ancestor);
    /* Role根固定无parent；只查一个稳定根，不在每次合成/输入递归
     * 走完整owner树。角色撤销即失去显示/输入资格，页由原owner链收。 */
    return root && root->shell_role && root->session==p->session && wm_shell_active(p->session)
        && !root->extra.hidden && !root->gui.creating && !root->gui.owner_hidden;
}

static int wm_shell_drawable(window_record_t *p)
{
    return p && (window_visible(&p->window)
        || (p->session==session_active() && wm_shell_preparing(p)));
}

static int wm_shell_work_bottom_for(u32 sid)
{
    desktop_view_t *view=view_get(sid);
    if(view && wm_shell_active(sid))return GFX_H-(int)view->shell_height;
    return GFX_H-(GFX_W==320?16:32*display_scale()/100);
}

static void wm_shell_changed(u32 sid)
{
    desktop_view_t *view=view_get(sid);
    if(!view)return;
    if(view->shell_windows_epoch<0xFFFFFFFFu)view->shell_windows_epoch++;
    sc32_host_windows_changed(sid);
}

static int wm_shell_layer(window_record_t *p)
{
    if(p->shell_role==WM_SHELL_DESKTOP)return 0;
    if(p->gui.popup)return 3;
    if(p->shell_role==WM_SHELL_TASKBAR)return 2;
    return 1;
}

static int wm_shell_raise_destination(desktop_view_t *view,window_record_t *p)
{
    int layer=wm_shell_layer(p);
    if(!layer)return 0;
    int at=view->count-1;
    while(at>=0){
        if(at==p->z_index || wm_shell_layer(window_record(view->order[at]))>layer){at--;continue;}
        break;
    }
    return at<p->z_index?at+1:at;
}

static void wm_shell_move_order(desktop_view_t *view,window_record_t *p,int destination)
{
    int from=p->z_index,handle=p->window.handle;
    int moving=view->dragging>=0?view->order[view->dragging]:0;
    int resizing_handle=view->resize>=0?view->order[view->resize]:0;
    if(destination<from){
        for(int i=from;i>destination;i--){
            view->order[i]=view->order[i-1];
            window_record(view->order[i])->z_index=i;
        }
    }else{
        for(int i=from;i<destination;i++){
            view->order[i]=view->order[i+1];
            window_record(view->order[i])->z_index=i;
        }
    }
    view->order[destination]=handle;p->z_index=destination;view->top_known=0;
    window_record_t *drag_record=window_record(moving),*resize_record=window_record(resizing_handle);
    view->dragging=drag_record?drag_record->z_index:-1;
    view->resize=resize_record?resize_record->z_index:-1;
}

static void wm_shell_created(desktop_view_t *view,window_record_t *p)
{
    /* 新普通窗放在taskbar层之前，popup仍在瞬态层。向后只跳过真实
     * 上层对象，不在每次鼠标移动扫描所有普通窗口。 */
    wm_shell_move_order(view,p,wm_shell_raise_destination(view,p));
    if(p->session==session_active() && !p->extra.hidden && !p->gui.popup)wm_shell_focus_set(p);
    wm_shell_changed(p->session);
}

static void wm_shell_focus_set(window_record_t *p)
{
    desktop_view_t *view=view_get(p->session);
    if(!view || !wm_shell_active(p->session))return;
    if(p->gui.popup && (p->gui.popup_flags&SC32_POPUP_NO_ACTIVATE))return;
    view->shell_focus=(u32)p->window.handle;view->top_known=0;
}

static int wm_shell_top(desktop_view_t *view)
{
    window_record_t *focused=window_record((int)view->shell_focus);
    if(focused && focused->session==view->shell_session && gui_eligible(focused)
        && !(focused->gui.popup_flags&SC32_POPUP_NO_ACTIVATE))return focused->z_index;
    for(int i=view->count-1;i>=0;i--){
        window_record_t *p=window_record(view->order[i]);
        if(p->shell_role || !gui_eligible(p) || (p->gui.popup_flags&SC32_POPUP_NO_ACTIVATE))continue;
        view->shell_focus=(u32)p->window.handle;return i;
    }
    window_record_t *desktop=window_record((int)view->shell_desktop);
    if(desktop && gui_eligible(desktop)){
        view->shell_focus=(u32)desktop->window.handle;return desktop->z_index;
    }
    view->shell_focus=0;return -1;
}

static int wm_shell_mouse(int x,int y,u8 buttons,u8 previous)
{
    if(!wm_shell_active(session_active()))return 0;
    int index=hit_window(x,y);
    window_record_t *p=index>=0?window_record(WINDOW(index).handle):0;
    if(!p || !p->shell_role)return 0;
    if(buttons&~previous){
        window_record_t *target=window_record(gui_modal_target(p->window.handle));
        if(target && gui_eligible(target))(void)win_raise(target->z_index);
    }
    /* 专层整个矩形都是客户区，右键也必须给Ring3，不能落入旧Start/
     * 桌面菜单或右下角resize。此处只维护焦点，真实raw沿仍由route投递。 */
    return 1;
}

static int wm_shell_draw_surface(window_record_t *p)
{
    if(!p->shell_role)return 0;
    win_t *w=&p->window;
    if(p->extra.opaque)gfx_opaque_blit(w->x,w->y,w->cw,w->ch,(u32 *)w->canvas,w->cw);
    else gfx_argb_blit(w->x,w->y,w->cw,w->ch,(u32 *)w->canvas,w->cw);
    return 1;
}

static void wm_shell_frame_invalidated(window_record_t *p)
{
    if(!p->shell_role)return;
    p->shell_ready_geometry=p->shell_submitted_geometry=p->shell_submitted_paint=0;
}

static void wm_shell_submitted(window_record_t *p,const ScSubmitDesc *input,const ScSubmitRect *rectangles)
{
    if(!wm_shell_preparing(p) || !p->gui.painting)return;
    for(u32 i=0;i<input->rectangle_count;i++){
        const ScSubmitRect *r=rectangles+i;
        if(!r->destination_x && !r->destination_y && r->width==(u32)p->window.cw
            && r->height==(u32)p->window.ch){
            p->shell_submitted_geometry=p->gui.geometry;
            p->shell_submitted_paint=p->gui.painting;
            return;
        }
    }
}

static void wm_shell_paint_complete(window_record_t *p,const Sc32PaintEnd *input)
{
    if(!wm_shell_preparing(p) || p->shell_submitted_geometry!=input->geometry_epoch
        || p->shell_submitted_paint!=input->paint_epoch)return;
    /* 只有真实完整Submit与同一claim的成功End才能完成有限首帧。
     * 此后的隐藏损伤留到Activate；不能因caret或新模型再不断排PAINT。 */
    p->shell_ready_geometry=p->gui.geometry;p->gui.paint_ready=0;
    p->shell_submitted_geometry=p->shell_submitted_paint=0;
    wm_shell_changed(p->session);
}

static void wm_shell_paint_cancelled(window_record_t *p)
{
    p->shell_submitted_geometry=p->shell_submitted_paint=0;
}

static void wm_shell_remove_task_button(desktop_view_t *view,window_record_t *p)
{
    int at=p->creation_index;
    if(at<0)return;
    for(int i=at;i<view->task_count-1;i++){
        view->created[i]=view->created[i+1];window_record(view->created[i])->creation_index=i;
    }
    view->task_count--;p->creation_index=-1;
    if(view->bar_first>=view->task_count)view->bar_first=0;
}

static int wm_shell_height_valid(u32 height)
{
    int remaining=GFX_H-TITLE_H-40;
    return remaining>0 && height && height<=(u32)remaining;
}

static int wm_shell_candidate(wm_shell_candidate_t *c,window_record_t *p,int x,int y,
    int width,int height,int role,int bottom)
{
    c->record=p;c->x=x;c->y=y;c->scale=p->extra.scale;
    c->cw=p->window.cw;c->ch=p->window.ch;
    if(role){c->cw=width;c->ch=height;c->scale=1;}
    else if(p->extra.native){
        if(width>GFX_W)width=GFX_W;
        if(height>bottom)height=bottom;
        if(width<96)width=96;
        if(height<TITLE_H+40)height=TITLE_H+40;
        c->cw=width-2;c->ch=height-TITLE_H-1;
    }else{
        int sx=(width-2)/c->cw,sy=(height-TITLE_H-1)/c->ch;
        c->scale=sx<sy?sx:sy;
        if(c->scale<1)c->scale=1;
        if(c->scale>4)c->scale=4;
        width=c->cw*c->scale+2;height=c->ch*c->scale+TITLE_H+1;
    }
    c->width=width;c->height=height;
    if(!role){
        if(c->x>GFX_W-width)c->x=GFX_W-width;
        if(c->y>bottom-height)c->y=bottom-height;
        if(c->x<0)c->x=0;
        if(c->y<0)c->y=0;
    }
    if(c->cw!=p->window.cw || c->ch!=p->window.ch){
        if(c->cw<1 || c->ch<1 || c->cw>1920 || c->ch>1080)return SC32_E_RANGE;
        u32 bytes=(u32)c->cw*(u32)c->ch*(p->extra.format==2?4u:1u);
        c->pages=(bytes+4095u)/4096u;c->pixels=pframe_alloc_run(c->pages);
        if(!c->pixels)return SC32_E_MEMORY;
    }
    return SC32_OK;
}

static void wm_shell_candidates_free(wm_shell_candidate_t *c,u32 count)
{
    for(u32 i=0;i<count;i++)if(c[i].pixels)pframe_free_run(c[i].pixels,c[i].pages);
}

static int wm_shell_resize_group(desktop_view_t *view,window_record_t *desktop,
    window_record_t *bar,u32 height,int ordinary)
{
    u32 capacity=2u+(ordinary?(u32)view->task_count:0u),count=0;
    if(capacity>0xFFFFF000u/sizeof(wm_shell_candidate_t))return SC32_E_RANGE;
    u32 metadata_pages=(capacity*sizeof(wm_shell_candidate_t)+4095u)/4096u;
    wm_shell_candidate_t *list=(wm_shell_candidate_t *)pframe_alloc_run(metadata_pages);
    if(!list)return SC32_E_MEMORY;
    wm_shell_zero(list,capacity*sizeof(*list));
    int bottom=GFX_H-(int)height,error;
    error=wm_shell_candidate(list,desktop,0,0,GFX_W,bottom,1,bottom);count=1;
    if(!error){error=wm_shell_candidate(list+1,bar,0,bottom,GFX_W,(int)height,1,bottom);count=2;}
    if(!error && ordinary){
        for(int i=0;i<view->task_count;i++){
            window_record_t *p=window_record(view->created[i]);
            if(!p || p->shell_role || TASK(p->window.owner).state==2)continue;
            int x=p->window.x,y=p->window.y,w=p->window.w,h=p->window.h;
            if(p->extra.maximized){x=y=0;w=GFX_W;h=bottom;}
            else if(h>bottom && (!p->gui.mode || (p->gui.style&SC32_STYLE_RESIZE)))h=bottom;
            error=wm_shell_candidate(list+count,p,x,y,w,h,0,bottom);count++;
            if(error)break;
        }
    }
    if(error){wm_shell_candidates_free(list,count);pframe_free_run((u32)list,metadata_pages);return error;}
    /* 两surface与所有受工作区影响的画布先预留完整候选，任何OOM都
     * 不改旧工作区或像素。候选整行复制只发生在低频布局事务；IRQ
     * 可继续排真实输入，用户任务/主循环不会读到半提交或陈旧指针。 */
    task_render_hold(1);sti();
    for(u32 i=0;i<count;i++)if(list[i].pixels){
        wm_shell_candidate_t *c=list+i;window_record_t *p=c->record;
        for(int y=0;y<c->ch;y++)for(int x=0;x<c->cw;x++){
            u32 pixel=x<p->window.cw && y<p->window.ch?canvas_pixel(&p->window,(u32)y*(u32)p->window.cw+(u32)x)
                :(p->extra.format==2?0xFF000000u|theme_color(TH_FACE):PAL_CON_BG);
            if(p->extra.format==2)((u32 *)c->pixels)[(u32)y*(u32)c->cw+(u32)x]=pixel;
            else ((u8 *)c->pixels)[(u32)y*(u32)c->cw+(u32)x]=(u8)pixel;
        }
    }
    cli();task_render_hold(0);
    gui_mutation++;
    view->shell_height=height;
    for(u32 i=0;i<count;i++){
        wm_shell_candidate_t *c=list+i;window_record_t *p=c->record;
        if(c->pixels){
            pframe_free_run((u32)p->window.canvas,p->extra.pages);
            p->window.canvas=(u8 *)c->pixels;p->extra.pages=c->pages;c->pixels=0;
        }
        p->window.x=c->x;p->window.y=c->y;p->window.w=c->width;p->window.h=c->height;
        p->window.cw=c->cw;p->window.ch=c->ch;p->extra.scale=c->scale;
        if(p==desktop || p==bar)p->extra.native=1;
    }
    gui_mutation--;
    for(u32 i=0;i<count;i++)gui_commit(&list[i].record->window);
    pframe_free_run((u32)list,metadata_pages);
    wm_shell_changed(view->shell_session);
    if(view==active_view)wm_request_compose();
    return SC32_OK;
}

int wm_shell_prepare(int pid,u32 desktop,u32 dgen,u32 bar,u32 bgen,u32 height)
{
    desktop_view_t *view=wm_shell_scope(pid);
    if(!view)return SC32_E_ACCESS;
    if(!wm_shell_height_valid(height))return SC32_E_RANGE;
    int error;window_record_t *d=gui_record(pid,desktop,dgen,&error);
    if(!d)return error;
    window_record_t *b=gui_record(pid,bar,bgen,&error);if(!b)return error;
    if(d==b || d->session!=view->shell_session || b->session!=view->shell_session
        || d->extra.format!=2 || b->extra.format!=2 || d->gui.creating || b->gui.creating
        || d->gui.popup || b->gui.popup || d->gui.parent || b->gui.parent
        || d->gui.first_child || b->gui.first_child || d->terminal.text || b->terminal.text
        || d->gui.painting || b->gui.painting)return SC32_E_INVALID;
    if(view->shell_active)return SC32_E_BUSY;
    if(view->shell_prepared && (view->shell_pid!=(u32)pid || view->shell_generation!=task_generation(pid)
        || view->shell_desktop!=desktop || view->shell_taskbar!=bar))return SC32_E_BUSY;
    if(view->shell_prepared && view->shell_height==height)return SC32_OK;
    /* Resize候选在授专层之前准备；失败连标题/任务按钮/隐藏状态也不改。 */
    error=wm_shell_resize_group(view,d,b,height,0);if(error)return error;
    gui_focus_defer++;
    view->shell_pid=(u32)pid;view->shell_generation=task_generation(pid);
    view->shell_desktop=desktop;view->shell_taskbar=bar;view->shell_prepared=1;
    d->shell_role=WM_SHELL_DESKTOP;b->shell_role=WM_SHELL_TASKBAR;
    wm_shell_frame_invalidated(d);wm_shell_frame_invalidated(b);
    d->extra.hidden=b->extra.hidden=1;
    d->gui.minimized=b->gui.minimized=0;
    wm_shell_remove_task_button(view,d);wm_shell_remove_task_button(view,b);
    wm_shell_move_order(view,d,0);
    wm_shell_move_order(view,b,wm_shell_raise_destination(view,b));
    visibility_changed(&d->window);visibility_changed(&b->window);
    Sc32Invalidate a={36,1,desktop,dgen,0,0,(u32)d->window.cw,(u32)d->window.ch,0};
    Sc32Invalidate z={36,1,bar,bgen,0,0,(u32)b->window.cw,(u32)b->window.ch,0};
    (void)wm_sc32_invalidate(pid,&a);(void)wm_sc32_invalidate(pid,&z);
    gui_focus_defer--;gui_focus_update();
    wm_shell_changed(view->shell_session);
    return SC32_OK;
}

int wm_shell_activate(int pid)
{
    desktop_view_t *view=wm_shell_scope(pid);window_record_t *d,*b;
    if(!view)return SC32_E_ACCESS;
    if(!wm_shell_pair(view,&d,&b))return SC32_E_STALE;
    if(view->shell_active)return SC32_E_BUSY;
    if(d->shell_ready_geometry!=d->gui.geometry || b->shell_ready_geometry!=b->gui.geometry)return SC32_E_AGAIN;
    int error=wm_shell_resize_group(view,d,b,view->shell_height,1);if(error)return error;
    /* 默认桌面继续存在直到候选已完整真实提交。这里一次发布层级、
     * 工作区与VISIBILITY，后续模型损伤走正常可见PAINT，不抢应用焦点。 */
    gui_focus_defer++;
    view->shell_active=1;view->shell_focus=0;view->menu=view->context=0;
    d->extra.hidden=b->extra.hidden=0;view->top_known=0;
    visibility_changed(&d->window);visibility_changed(&b->window);
    gui_focus_defer--;gui_focus_update();
    wm_shell_changed(view->shell_session);
    if(view==active_view)wm_request_compose();
    return SC32_OK;
}

int wm_shell_workspace(int pid,u32 height)
{
    desktop_view_t *view=wm_shell_scope(pid);window_record_t *d,*b;
    if(!view || !view->shell_active)return SC32_E_ACCESS;
    if(!wm_shell_pair(view,&d,&b))return SC32_E_STALE;
    if(!wm_shell_height_valid(height))return SC32_E_RANGE;
    if(view->shell_height==height)return SC32_OK;
    return wm_shell_resize_group(view,d,b,height,1);
}

void wm_shell_detach(int pid,u32 generation)
{
    if(pid<=0 || !task_owned(pid) || task_generation(pid)!=generation)return;
    desktop_view_t *view=view_get(session_task(pid));
    if(!view || view->shell_pid!=(u32)pid || view->shell_generation!=generation)return;
    u32 sid=view->shell_session,desktop=view->shell_desktop,bar=view->shell_taskbar;
    /* 先撤绑定，重复owner-stopped不会递归停止或重复拆角色。窗口页
     * 仍由旧owner阶段释放；这里不给陈旧PID/gen新权限。 */
    view->shell_pid=view->shell_generation=view->shell_desktop=view->shell_taskbar=0;
    view->shell_prepared=view->shell_active=view->shell_height=view->shell_focus=0;
    view->top_known=0;view->menu=view->context=0;
    gui_focus_defer++;
    popup_close_group(view,0,desktop);popup_close_group(view,0,bar);
    u32 surfaces[2]={desktop,bar};
    for(u32 i=0;i<2;i++){
        window_record_t *p=window_record((int)surfaces[i]);
        if(!p || p->window.owner!=pid || p->owner_generation!=generation)continue;
        p->extra.hidden=1;p->gui.painting=p->gui.paint_ready=0;gui_attention(p);
        visibility_changed(&p->window);
    }
    if(gui_capture_handle && window_record(gui_capture_handle)
        && window_record(gui_capture_handle)->window.owner==pid)gui_capture_release(3);
    if(relative_owner==pid)relative_release();
    gui_focus_defer--;gui_focus_update();wm_shell_changed(sid);
    if(view==active_view)wm_request_compose();
}

static void wm_shell_closing(window_record_t *p)
{
    desktop_view_t *view=view_get(p->session);
    if(!p->shell_role || !view || !view->shell_prepared || view->shell_pid!=(u32)p->window.owner
        || view->shell_generation!=p->owner_generation)return;
    int pid=p->window.owner;u32 generation=p->owner_generation;
    wm_shell_detach(pid,generation);
    if(task_owned(pid) && task_generation(pid)==generation && TASK(pid).state!=2)task_stop(pid);
}

static void wm_shell_display(desktop_view_t *view)
{
    window_record_t *d,*b;
    if(!wm_shell_pair(view,&d,&b))return;
    u32 height=view->shell_height;
    if(!wm_shell_height_valid(height))height=(u32)(GFX_H-TITLE_H-40);
    wm_shell_frame_invalidated(d);wm_shell_frame_invalidated(b);
    int error=height?wm_shell_resize_group(view,d,b,height,1):SC32_E_RANGE;
    if(error){int pid=(int)view->shell_pid;u32 generation=view->shell_generation;
        /* 硬件显示模式已经提交，不能沿旧surface尺寸宣称新桌面可用。
         * 撤角色显示真实旧回退，普通应用仍活；重启沿可信host链处理。 */
        wm_shell_detach(pid,generation);task_stop(pid);
    }
}

void wm_shell_info(u32 sid,ScShellHostInfo *out)
{
    desktop_view_t *view=view_get(sid);window_record_t *d,*b;
    out->work_x=out->work_y=0;out->work_width=(u32)GFX_W;
    out->work_height=(u32)wm_shell_work_bottom_for(sid);
    u32 display[8];display_info(display);out->display_epoch=display[4];
    if(!wm_shell_pair(view,&d,&b))return;
    out->desktop=(u32)d->window.handle;out->desktop_generation=out->desktop;
    out->taskbar=(u32)b->window.handle;out->taskbar_generation=out->taskbar;
    if(!view->shell_active && (d->shell_ready_geometry!=d->gui.geometry
        || b->shell_ready_geometry!=b->gui.geometry))out->flags|=SC32_SHELL_PREPARING;
}

static int wm_shell_manageable(window_record_t *p,u32 sid)
{
    if(!p || p->session!=sid || p->shell_role || p->gui.popup || !task_owned(p->window.owner)
        || task_generation(p->window.owner)!=p->owner_generation || session_task(p->window.owner)!=sid
        || (TASK(p->window.owner).state!=1 && TASK(p->window.owner).state!=3))return 0;
    u32 identity[8];
    return !auth_info(p->window.owner,identity) && (int)identity[1]>=0
        && (int)identity[2]>=0 && identity[3]==AUTH_NORMAL;
}

static void wm_shell_window_value(window_record_t *p,ScWindowInfo *out)
{
    if(p->gui.mode){(void)wm_sc32_query(p->window.owner,(u32)p->window.handle,(u32)p->window.handle,out);return;}
    wm_shell_zero(out,sizeof(*out));out->size=sizeof(*out);out->version=1;
    out->window=out->generation=(u32)p->window.handle;
    out->owner_pid=(u32)p->window.owner;out->owner_generation=p->owner_generation;out->session=p->session;
    out->x=p->window.x;out->y=p->window.y;out->width=p->window.w;out->height=p->window.h;
    out->client_width=(u32)p->window.cw;out->client_height=(u32)p->window.ch;out->format=(u32)p->extra.format;
    out->state=(window_visible(&p->window)?SC32_WINDOW_VISIBLE:0)
        |(gui_focus_any==p->window.handle?SC32_WINDOW_FOCUSED:0)
        |(p->extra.hidden?SC32_WINDOW_MINIMIZED:0)|(p->extra.maximized?SC32_WINDOW_MAXIMIZED:0)
        |(gui_eligible(p)?SC32_WINDOW_INPUT:0)|(TASK(p->window.owner).state==2?SC32_WINDOW_CLOSING:0);
    out->geometry_epoch=p->gui.geometry;out->visibility_epoch=p->visibility_epoch;
    out->input_epoch=p->gui.input;out->theme_epoch=p->gui.theme;out->display_epoch=p->gui.display;
}

int wm_shell_window_at(int pid,int x,int y,ScWindowInfo *out)
{
    desktop_view_t *view=wm_shell_scope(pid);
    if(!view || !view->shell_active || view->shell_session!=session_active())return SC32_E_ACCESS;
    if(x<0 || y<0 || x>=GFX_W || y>=GFX_H)return SC32_E_RANGE;
    int index=hit_window(x,y);window_record_t *p=index<0?0:window_record(WINDOW(index).handle);
    if(!p || !window_visible(&p->window))return SC32_E_NOT_FOUND;
    if(p->session!=view->shell_session || !task_owned(p->window.owner)
        || task_generation(p->window.owner)!=p->owner_generation || TASK(p->window.owner).state!=1)return SC32_E_ACCESS;
    u32 identity[8];
    if(auth_info(p->window.owner,identity) || (int)identity[1]<0 || (int)identity[2]<0
        || identity[3]!=AUTH_NORMAL)return SC32_E_ACCESS;
    /* 使用WM真实hit/z序，不能用creation分页顺序猜覆盖；无票/无指针，
     * 后续拖放仍独立查可接收target与完整真实sender。 */
    wm_shell_window_value(p,out);return SC32_OK;
}

int wm_shell_window_next(int pid,u32 after,u32 budget,ScShellWindowInfo *row,
    u32 *cursor,u32 *epoch,u32 *examined,int *finished)
{
    desktop_view_t *view=wm_shell_scope(pid);
    if(!view || !view->shell_active)return SC32_E_ACCESS;
    if(after>0x7FFFFFFFu || !budget || budget>64u)return SC32_E_INVALID;
    if(view->shell_windows_epoch==0xFFFFFFFFu)return SC32_E_RANGE;
    wm_shell_zero(row,sizeof(*row));*cursor=after;*epoch=view->shell_windows_epoch;*examined=0;*finished=0;
    /* created按从不复用的handle递增，专层/popup已经不在此索引。
     * 二分后在本人SID最多过滤64条，不扫其它SID或历史句柄空洞；
     * 64是一次检查预算，空页仍给真实推进cursor。 */
    int low=0,high=view->task_count;
    while(low<high){int middle=low+(high-low)/2;
        if((u32)view->created[middle]<=after)low=middle+1;else high=middle;
    }
    for(;low<view->task_count && *examined<budget;low++){
        int handle=view->created[low];window_record_t *p=window_record(handle);
        *cursor=(u32)handle;(*examined)++;
        if(!wm_shell_manageable(p,view->shell_session))continue;
        row->size=sizeof(*row);row->version=1;wm_shell_window_value(p,&row->window);
        const char *title=p->gui.mode?p->gui.title:p->window.title;
        for(u32 n=0;n<127 && title[n];n++)row->title[n]=title[n];
        if(userspace_query(p->window.owner,3,0,row->executable,sizeof(row->executable))<0)return SC32_E_STALE;
        return SC32_OK;
    }
    if(low==view->task_count)*finished=1;
    return SC32_OK;
}

int wm_shell_window_action(int pid,const ScShellWindowAction *action,ScWindowInfo *out)
{
    desktop_view_t *view=wm_shell_scope(pid);
    if(!view || !view->shell_active)return SC32_E_ACCESS;
    window_record_t *p=action->window && action->window<=0x7FFFFFFFu?window_record((int)action->window):0;
    if(!p || action->generation!=action->window || action->pid!=(u32)p->window.owner
        || action->pid_generation!=p->owner_generation)return SC32_E_STALE;
    if(!wm_shell_manageable(p,view->shell_session))return SC32_E_ACCESS;
    if(action->action>SC32_SHELL_ACTION_CLOSE || (action->action!=SC32_SHELL_ACTION_PLACE
        && (action->x || action->y || action->width || action->height)))return SC32_E_INVALID;
    if(p->gui.mode && (p->gui.creating || (p->gui.style&SC32_STYLE_DISABLED) || p->gui.owner_disabled))return SC32_E_ACCESS;
    if(p->gui.mode && gui_modal_target(p->window.handle)!=p->window.handle
        && action->action!=SC32_SHELL_ACTION_FOCUS && action->action!=SC32_SHELL_ACTION_RESTORE)return SC32_E_ACCESS;
    int error=0;
    if(action->action==SC32_SHELL_ACTION_FOCUS){
        window_record_t *target=window_record(gui_modal_target(p->window.handle));
        if(p->session!=session_active() || !target || !gui_eligible(target))return SC32_E_ACCESS;
        (void)win_raise(target->z_index);wm_request_compose();
    }else if(action->action==SC32_SHELL_ACTION_MINIMIZE || action->action==SC32_SHELL_ACTION_MAXIMIZE
        || action->action==SC32_SHELL_ACTION_RESTORE){
        u32 style=action->action==SC32_SHELL_ACTION_MINIMIZE?SC32_STYLE_MINIMIZE:SC32_STYLE_MAXIMIZE;
        if(p->gui.mode && action->action!=SC32_SHELL_ACTION_RESTORE && !(p->gui.style&style))return SC32_E_NOT_SUPPORTED;
        int command=action->action==SC32_SHELL_ACTION_MINIMIZE?0:action->action==SC32_SHELL_ACTION_MAXIMIZE?1:2;
        if(wm_window_control(p->window.owner,p->window.handle,command))return SC32_E_MEMORY;
        if(command==2 && p->session==session_active()){
            window_record_t *target=window_record(gui_modal_target(p->window.handle));
            if(target && gui_eligible(target))(void)win_raise(target->z_index);
        }
    }else if(action->action==SC32_SHELL_ACTION_PLACE){
        if(action->width<94 || action->height<40 || action->width>1920 || action->height>1080)return SC32_E_RANGE;
        if(p->gui.mode && (action->width!=(u32)p->window.cw || action->height!=(u32)p->window.ch)
            && !(p->gui.style&SC32_STYLE_RESIZE))return SC32_E_NOT_SUPPORTED;
        if(p->gui.mode){Sc32WindowPlace place={32,1,action->window,action->generation,action->x,action->y,action->width,action->height};
            error=wm_sc32_place(p->window.owner,&place,out);if(error)return error;
        }else{
            if(native_resize(&p->window,(int)action->width+2,(int)action->height+TITLE_H+1))return SC32_E_MEMORY;
            p->window.x=action->x;p->window.y=action->y;
            if(p->window.x>GFX_W-p->window.w)p->window.x=GFX_W-p->window.w;
            int bottom=wm_shell_work_bottom_for(p->session);
            if(p->window.y>bottom-p->window.h)p->window.y=bottom-p->window.h;
            if(p->window.x<0)p->window.x=0;
            if(p->window.y<0)p->window.y=0;
            p->extra.maximized=0;visibility_changed(&p->window);window_changed(&p->window);
        }
    }else{
        if(p->gui.mode && (!(p->gui.style&SC32_STYLE_CLOSE) || p->gui.close_pending))return p->gui.close_pending?SC32_E_BUSY:SC32_E_NOT_SUPPORTED;
        /* 现代CLOSE只排本人真实请求，应用仍能veto；旧窗口原合同是
         * 停止owner，不能偷偷把它改成单窗Destroy或给普通应用新权限。 */
        gui_close_request(&p->window);
    }
    wm_shell_window_value(p,out);wm_shell_changed(p->session);
    return SC32_OK;
}
