/* 仅返回可信值，不给三环foreign画布、标题、过程或内核地址。最高命中
 * 使用实际z序：未登记的菜单/普通窗仍遮挡下面目标，透明Tooltip除外。 */
static void drop_window_fact(window_record_t *p,WmDropFact *out)
{
    *out=(WmDropFact){0};
    out->window=(u32)p->window.handle;out->generation=out->window;
    out->pid=(u32)p->window.owner;out->pid_generation=p->owner_generation;
    out->session=p->session;out->geometry=p->gui.geometry;out->visibility=p->visibility_epoch;
    out->x=p->window.x+window_client_dx(&p->window);out->y=p->window.y+window_client_dy(&p->window);
    out->width=(u32)p->window.cw;out->height=(u32)p->window.ch;
    out->captured=gui_capture_handle==p->window.handle;out->input=p->gui.input;
    out->buttons=active_view?active_view->mouse_previous_buttons:0;
}
/* 注册是本人资源声明，允许隐藏/尚未激活桌面；At/Validate另查实际
 * 可见输入资格，不能让后台构造GUI时就因session未显示而初始化失败。 */
int wm_sc32_drop_register(int pid,u32 window,u32 generation,WmDropFact *out)
{
    int error;window_record_t *p=gui_record(pid,window,generation,&error);if(!p)return error;
    if(!out || TASK(pid).state!=1)return SC32_E_STALE;
    drop_window_fact(p,out);return 0;
}
int wm_sc32_drop_window(int pid,u32 window,u32 generation,WmDropFact *out)
{
    int error;window_record_t *p=gui_record(pid,window,generation,&error);if(!p)return error;
    if(!out || !gui_eligible(p) || menu_open || context_open || fault_focus || relative_active())return SC32_E_ACCESS;
    drop_window_fact(p,out);return 0;
}
int wm_sc32_drop_source(int pid,u32 window,u32 generation,int at,WmDropFact *out)
{
    int error=wm_sc32_drop_window(pid,window,generation,out);if(error)return error;
    if(at && (!out->captured || gui_focus_any!=(int)window))return SC32_E_ACCESS;
    return 0;
}
int wm_sc32_drop_point(i32 x,i32 y,WmDropFact *out)
{
    if(!active_view || !out || x<0 || y<0 || x>=GFX_W || y>=work_bottom()
        || menu_open || context_open || fault_focus || relative_active() || drag>=0 || resizing>=0)return SC32_E_NOT_FOUND;
    int index=hit_window(x,y);if(index<0)return SC32_E_NOT_FOUND;
    window_record_t *p=window_record(WINDOW(index).handle);
    if(!p || !p->gui.mode || !gui_eligible(p))return SC32_E_NOT_FOUND;
    drop_window_fact(p,out);
    if(x<out->x || y<out->y || (u32)(x-out->x)>=out->width || (u32)(y-out->y)>=out->height)return SC32_E_NOT_FOUND;
    return 0;
}
int wm_sc32_drop_point_current(i32 x,i32 y)
{return active_view && x==active_view->mouse_previous_x && y==active_view->mouse_previous_y;}

static void drop_release_record(window_record_t *source,const ScMessage *message,i32 x,i32 y)
{
    gui_window_t *g=&source->gui;g->drop_release_epoch=0;
    WmDropFact point;if(wm_sc32_drop_point(x,y,&point))return;
    u32 registration=sc32_drop_registration(point.window);if(!registration)return;
    u32 now[2];timer_monotonic(now);u32 lifetime=timer_frequency_hz()*30u;
    g->drop_release_expires_lo=now[0]+lifetime;
    g->drop_release_expires_hi=now[1]+(g->drop_release_expires_lo<now[0]);
    g->drop_release_route=gui_route_epoch;g->drop_release_lo=message->sequence_lo;g->drop_release_hi=message->sequence_hi;
    g->drop_release_geometry=g->geometry;g->drop_release_visibility=source->visibility_epoch;
    g->drop_release_target=point.window;g->drop_release_target_geometry=point.geometry;
    g->drop_release_target_visibility=point.visibility;g->drop_release_registration=registration;
    g->drop_release_x=x;g->drop_release_y=y;g->drop_release_epoch=message->epoch;
}
int wm_sc32_drop_released(int pid,const ScDropReleasePoint *request,WmDropFact *source,WmDropFact *point,u32 *registration)
{
    int error=wm_sc32_drop_source(pid,request->source_window,request->source_generation,1,source);if(error)return error;
    window_record_t *p=window_record((int)request->source_window);gui_window_t *g=&p->gui;
    u32 now[2];timer_monotonic(now);
    if(now[1]>g->drop_release_expires_hi || (now[1]==g->drop_release_expires_hi && now[0]>=g->drop_release_expires_lo))return SC32_E_STALE;
    if(!g->drop_release_epoch || request->input_epoch!=g->drop_release_epoch || source->input!=g->drop_release_epoch
        || g->drop_release_route!=gui_route_epoch || request->sequence_lo!=g->drop_release_lo
        || request->sequence_hi!=g->drop_release_hi || request->screen_x!=g->drop_release_x
        || request->screen_y!=g->drop_release_y || source->geometry!=g->drop_release_geometry
        || source->visibility!=g->drop_release_visibility || (source->buttons&1u))return SC32_E_STALE;
    error=wm_sc32_drop_point(request->screen_x,request->screen_y,point);if(error)return error;
    if(point->window!=g->drop_release_target || point->geometry!=g->drop_release_target_geometry
        || point->visibility!=g->drop_release_target_visibility)return SC32_E_STALE;
    *registration=g->drop_release_registration;return 0;
}
void wm_sc32_drop_release_consume(u32 window)
{window_record_t *p=window_record((int)window);if(p)p->gui.drop_release_epoch=0;}
