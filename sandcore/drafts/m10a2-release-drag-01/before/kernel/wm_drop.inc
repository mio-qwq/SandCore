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
