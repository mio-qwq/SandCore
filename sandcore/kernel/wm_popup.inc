/* 瞬态popup只占本人owner域的真实画布/z序，不进入任务栏。
 * owner、会话、焦点与几何变化撤链；这里没有控件或用户过程。 */
static u32 popup_focus_group(int handle)
{
    window_record_t *p=window_record(handle);
    return p?(p->gui.popup?p->gui.popup_root:(u32)handle):0;
}
static window_record_t *popup_input_target(window_record_t *p)
{
    if(p && p->gui.popup && (p->gui.popup_flags&SC32_POPUP_NO_ACTIVATE))
        return window_record((int)p->gui.popup_root);
    return p;
}
static void popup_unlink(window_record_t *p)
{
    if(!p->gui.popup_linked)return;
    desktop_view_t *view=view_get(p->session);
    window_record_t *before=window_record((int)p->gui.popup_previous),*after=window_record((int)p->gui.popup_next);
    if(before)before->gui.popup_next=p->gui.popup_next;else if(view)view->popup_first=p->gui.popup_next;
    if(after)after->gui.popup_previous=p->gui.popup_previous;else if(view)view->popup_last=p->gui.popup_previous;
    p->gui.popup_previous=p->gui.popup_next=p->gui.popup_linked=0;
}
static void popup_link(window_record_t *p)
{
    desktop_view_t *view=view_get(p->session);
    p->gui.popup_previous=view->popup_last;
    window_record_t *before=window_record((int)view->popup_last);
    if(before)before->gui.popup_next=(u32)p->window.handle;else view->popup_first=(u32)p->window.handle;
    view->popup_last=(u32)p->window.handle;p->gui.popup_linked=1;
}
static void popup_close_group(desktop_view_t *view,u32 keep_group,u32 close_group)
{
    if(!view)return;
    gui_focus_defer++;
    /* popup固定owner且禁止普通窗挂进瞬态链，孩子总晚于父亲创建。
     * 由尾叶先关闭，保存的previous仍活，单次O(k)且不递归占内核栈。 */
    for(u32 handle=view->popup_last;handle;){
        window_record_t *p=window_record((int)handle);if(!p)break;
        u32 previous=p->gui.popup_previous;
        if((close_group && p->gui.popup_root==close_group)
            || (!close_group && (!keep_group || p->gui.popup_root!=keep_group))){
            (void)wm_sc32_destroy(p->window.owner,handle,handle);
        }
        handle=previous;
    }
    gui_focus_defer--;
}
static void popup_raise_group(u32 group)
{
    if(!active_view)return;
    for(u32 handle=active_view->popup_first;handle;){
        window_record_t *p=window_record((int)handle);if(!p)break;
        if(p->gui.popup_root==group && gui_can_show(p))win_raise(p->z_index);
        handle=p->gui.popup_next;
    }
}
static int popup_resize(win_t *w,int width,int height)
{
    if(width<1 || height<1)return -1;
    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    if(width==w->cw && height==w->ch)return 0;
    window_extra_t *extra=extra_of(w->handle);
    u32 pages=((u32)width*(u32)height*4u+4095u)/4096u;
    u32 *pixels=(u32 *)pframe_alloc_run(pages);if(!pixels)return -1;
    task_render_hold(1);sti();
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        pixels[y*width+x]=y<w->ch && x<w->cw?((u32 *)w->canvas)[y*w->cw+x]:0xFF000000u|theme_color(TH_FACE);
    }
    cli();task_render_hold(0);
    pframe_free_run((u32)w->canvas,extra->pages);w->canvas=(u8 *)pixels;extra->pages=pages;
    w->w=w->cw=width;w->h=w->ch=height;
    if(w->x+width>GFX_W)w->x=GFX_W-width;
    if(w->y+height>work_bottom())w->y=work_bottom()-height;
    return 0;
}
int wm_sc32_popup_create(int pid,const Sc32PopupCreate *input,Sc32WindowRef *out)
{
    if(input->kind<SC32_POPUP_MENU || input->kind>SC32_POPUP_CUSTOM || (input->flags&~3u)
        || !input->preferred_width || !input->preferred_height || input->preferred_width>1920u
        || input->preferred_height>1080u || input->anchor_width>1920u || input->anchor_height>1080u
        || input->anchor_x<-32768 || input->anchor_x>32767 || input->anchor_y<-32768 || input->anchor_y>32767)return SC32_E_INVALID;
    int error;window_record_t *parent=gui_record(pid,input->owner_window,input->owner_generation,&error);if(!parent)return error;
    u32 group=popup_focus_group(parent->window.handle);
    if(!gui_eligible(parent) || parent->gui.creating || group!=popup_focus_group(gui_focus_any))return SC32_E_ACCESS;
    u32 flags=input->flags;
    if(input->kind==SC32_POPUP_MENU || input->kind==SC32_POPUP_COMBO){
        if(flags&SC32_POPUP_HIT_TRANSPARENT)return SC32_E_INVALID;
        flags|=SC32_POPUP_NO_ACTIVATE;
    }
    if(input->kind==SC32_POPUP_TOOLTIP)flags|=SC32_POPUP_NO_ACTIVATE|SC32_POPUP_HIT_TRANSPARENT;
    int width=(int)input->preferred_width,height=(int)input->preferred_height;
    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    int ax=parent->window.x+window_client_dx(&parent->window)+input->anchor_x;
    int ay=parent->window.y+window_client_dy(&parent->window)+input->anchor_y;
    int x=ax,y=ay+(int)input->anchor_height;
    if(parent->gui.popup){x=ax+(int)input->anchor_width;y=ay;
        if(x+width>GFX_W)x=ax-width;
    }else if(y+height>work_bottom())y=ay-height;
    if(x+width>GFX_W)x=GFX_W-width;
    if(y+height>work_bottom())y=work_bottom()-height;
    if(x<0)x=0;
    if(y<0)y=0;
    int handle=win_create(pid,"",x,y,width,height,-3);if(handle<0)return SC32_E_MEMORY;
    window_record_t *p=window_record(handle);gui_window_t *g=&p->gui;
    g->mode=g->creating=g->geometry=g->input=g->theme=g->display=1;
    g->popup_kind=input->kind;g->popup_flags=flags;g->popup_root=group;
    g->last_x=x;g->last_y=y;g->last_w=g->last_cw=width;g->last_h=g->last_ch=height;
    gui_link_parent(p,parent);popup_link(p);task_record(pid)->visibility_aware=1;
    *out=(Sc32WindowRef){16,1,(u32)handle,(u32)handle};return SC32_OK;
}
int wm_sc32_cursor(int pid,u32 handle,u32 generation,u32 shape)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    if(shape>SC32_CURSOR_DRAG_MOVE)return SC32_E_RANGE;
    if(!gui_eligible(p) || p->gui.creating || gui_focus_any!=(int)handle)return SC32_E_ACCESS;
    if(p->gui.cursor!=shape){p->gui.cursor=shape;cursor_dirty=1;task_kernel_wake();}
    return SC32_OK;
}
static u32 pointer_cursor_shape(void)
{
    window_record_t *p=window_record(gui_capture_handle);
    if(!p){int index=hit_window(mouse_x(),mouse_y());p=index>=0?window_record(WINDOW(index).handle):0;p=popup_input_target(p);}
    if(!p || !p->gui.mode || !gui_eligible(p) || gui_focus_any!=p->window.handle)return SC32_CURSOR_ARROW;
    int x=mouse_x()-p->window.x-window_client_dx(&p->window),y=mouse_y()-p->window.y-window_client_dy(&p->window);
    if(!gui_capture_handle && (x<0 || y<0 || x>=p->window.cw || y>=p->window.ch))return SC32_CURSOR_ARROW;
    return p->gui.cursor;
}
