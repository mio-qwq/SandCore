/* =====================================================================
 *  SandCore 窗口管理器 v2 (kernel/wm.c)
 *  ---------------------------------------------------------------------
 *  【M6 质变】窗口成为系统调用提供的资源:
 *    用户程序 (ring3) 经 SYS_WINOPEN 开窗口 → 内核分配"画布"
 *    (客户区像素缓冲) → 程序用 SYS_TXT/FILL 在画布上作画 →
 *    合成器把画布 blit 到屏幕; SYS_GETKEY/SYS_MOUSE 送输入。
 *    程序退出时其窗口自动回收 (owner 检查)。
 *
 *  【桌面】开机即桌面: 图标来自 SandFS 的 desk/ 快捷方式 (文本:
 *    第1行=显示名, 第2行=目标 SCX 路径, 第3行=图标 SCF 路径),
 *    单击图标 → 运行目标程序。图标本体 = SCF1MIO 16x16 文件。
 * ===================================================================== */
#include "io.h"
#include "palette.h"
#include "gfx.h"
#include "mouse.h"
#include "timer.h"
#include "task.h"
#include "fs.h"
#include "wm.h"
#include "font.h"
#include "memory.h"
#include "display.h"
#include "desktop.h"
#include "theme.h"
#include "userspace.h"
#include "auth.h"
#include "task_store.h"
#include "objpool.h"
#include "interrupts.h"
#include "session.h"
#include "keyboard.h"
#include "streams.h"
#include "sc32_gui.h"

extern void scene_paint(void);        /* main.c: 沙漠壁纸 (画进后台缓冲) */

#define TITLE_H (GFX_W==320?13:24*display_scale()/100)
#define BORDER  1
#define CANVAS_W SC_CANVAS_W
#define CANVAS_H SC_CANVAS_H

typedef struct {
    int used;
    int owner;                        /* 创建者 pid (退出自动回收) */
    int handle;                       /* 句柄独立于 z 序，置顶不会串窗口 */
    i32 x, y, w, h;                   /* 外框 */
    char title[12];
    u8 *canvas;                       /* 客户区像素缓冲 */
    i32 cw, ch;                       /* 画布尺寸 */
    u8 keys[16];                      /* 按键队列 */
    u8 kq, kt;
    i32 tx, ty;
} win_t;

static u32 next_handle;
/* 窗口主体仍保留80B诊断布局；扩展状态按稳定handle关联，置顶移动
 * win_t 不会把最小化/原生页所有权串给另一个窗口。 */
typedef struct {int handle,native,scale,hidden,maximized;u32 pages,pressed,released;
    int restore_x,restore_y,restore_w,restore_h,format,opaque;} window_extra_t;
typedef struct {u32 token,owner_generation,pixels,bytes,pages,previous,next;int owner;} capture_t;
static objpool_t capture_pool;
static u32 capture_counter;
static u32 capture_pages;
/* 终端历史按需分配，不能给每个窗口都塞16KB静态数组：BSS的2MB
 * 上界同时保护内核堆。这里只保留稳定句柄和物理页地址，像素画布与
 * 文本历史分别释放；z序移动不迁移此元数据。 */
typedef struct {int handle;char *text;u32 used,theme;int width,height,scale;
    int first,rows,visible,follow,grab,grab_offset;} terminal_t;
typedef struct {
    u32 mode,creating,style,geometry,input,theme,display;
    u32 parent,first_child,previous_child,next_child,modal_child;
    u32 owner_hidden,owner_disabled,minimized;
    u32 attention_previous,attention_next,attention_linked;
    u32 close_pending,close_ready,paint_pending,paint_ready,paint_epoch,painting,painting_geometry;
    int paint_x0,paint_y0,paint_x1,paint_y1;
    int last_x,last_y,last_w,last_h,last_cw,last_ch,last_visible;
    u32 blocked_keys[8],blocked_buttons;
    char title[128];
    ScMessage close_message,paint_message;
} gui_window_t;
typedef struct {
    win_t window;
    window_extra_t extra;
    terminal_t terminal;
    u32 owner_generation,owner_previous,owner_next,session,visibility_epoch;
    int repaint;
    int z_index,creation_index;
    gui_window_t gui;
} window_record_t;
typedef char window_prefix_keeps_abi[(sizeof(win_t)==80)?1:-1];
static objpool_t window_pool;
typedef struct {
    int *order,*created,count,bar_first,wallpaper;
    u32 order_pages,order_capacity;
    int fault_first,fault_modal;
    int dragging,resize,resize_width,resize_height,resize_offset_x,resize_offset_y;
    i32 dragging_x,dragging_y;
    int menu,menu_start,menu_selection,context,context_left,context_top,context_win,context_item,icon_start;
    i32 mouse_previous_x,mouse_previous_y;
    u8 mouse_previous_buttons;
    int display_width,display_height,display_scaling;
    int top_cache,top_known;
    u64 title_hover;
} desktop_view_t;
static objpool_t view_pool;
static desktop_view_t *active_view;
static desktop_view_t *view_get(u32 id){return objpool_get(&view_pool,(int)id);}
#define nwins (active_view->count)
#define window_order (active_view->order)
#define creation_order (active_view->created)
#define window_order_pages (active_view->order_pages)
#define window_order_capacity (active_view->order_capacity)
#define taskbar_first (active_view->bar_first)
#define wallpaper_mode (active_view->wallpaper)
#define fault_head (active_view->fault_first)
#define fault_focus (active_view->fault_modal)
#define drag (active_view->dragging)
#define resizing (active_view->resize)
#define resize_w (active_view->resize_width)
#define resize_h (active_view->resize_height)
#define resize_ox (active_view->resize_offset_x)
#define resize_oy (active_view->resize_offset_y)
#define drag_ox (active_view->dragging_x)
#define drag_oy (active_view->dragging_y)
#define menu_open (active_view->menu)
#define menu_first (active_view->menu_start)
#define menu_selected (active_view->menu_selection)
#define context_open (active_view->context)
#define context_x (active_view->context_left)
#define context_y (active_view->context_top)
#define context_window (active_view->context_win)
#define context_icon (active_view->context_item)
#define icon_first (active_view->icon_start)
static int taskbar_capacity(void);
static void taskbar_reveal(int handle);
static void visibility_changed(win_t *w);
static int gui_can_show(window_record_t *record);
static int gui_eligible(window_record_t *record);
static void gui_commit(win_t *w);
static void gui_focus_update(void);
static void gui_closed(window_record_t *record);
static void gui_attention(window_record_t *record);
static void gui_descendant_visibility(window_record_t *root);
static void gui_close_request(win_t *w);
static int gui_modal_target(int handle);
static u32 gui_route_epoch=1,gui_session_id;
static int gui_focus_any,gui_capture_handle,gui_mutation,gui_focus_defer;
static window_record_t *window_record(int handle)
{return handle>0?objpool_get(&window_pool,handle):0;}
static win_t *window_at(int index)
{return &window_record(window_order[index])->window;}
#define WINDOW(index) (*window_at(index))
static int order_reserve(desktop_view_t *view)
{
    if((u32)view->count<view->order_capacity)return 0;
    u32 capacity=view->order_capacity?view->order_capacity*2:16;
    if(capacity<view->order_capacity || capacity>0x0FFFFFFFu)return -1;
    u32 pages=(capacity*8+4095)/4096,address=pframe_alloc_run(pages);if(!address)return -1;
    int *order=(int *)address,*created=order+capacity;
    for(int i=0;i<view->count;i++){order[i]=view->order[i];created[i]=view->created[i];}
    if(view->order)pframe_free_run((u32)view->order,view->order_pages);
    view->order=order;view->created=created;view->order_pages=pages;view->order_capacity=capacity;return 0;
}
static terminal_t *terminal_of(int handle)
{window_record_t *p=window_record(handle);return p && p->terminal.handle==handle?&p->terminal:0;}
static void terminal_redraw(win_t *w);
static window_extra_t *extra_of(int handle)
{window_record_t *p=window_record(handle);return p?&p->extra:0;}
static win_t *owned_window(int pid,int handle);
static int capture_allowed(int pid,win_t *w)
{
    if(window_record(w->handle)->session!=session_task(pid) && !auth_can_mod(pid))return 0;
    if(auth_uid(w->owner)==AUTH_SYSTEM && !auth_can_mod(pid))return 0;
    return auth_uid(pid)==auth_uid(w->owner) || auth_can_manage(pid);
}
int wm_capture_open(int pid,int handle,u32 out[8])
{
    window_record_t *record=window_record(handle);win_t *w=record?&record->window:0;
    if(!task_owned(pid) || !w || !w->canvas || !capture_allowed(pid,w))return -5;
    if(capture_counter==0x7FFFFFFFu || w->cw<=0 || w->ch<=0
        || (u32)w->cw>0x1000000u/(u32)w->ch)return -4;
    u32 bytes=(u32)w->cw*(u32)w->ch*4,pages=(bytes+4095)/4096;
    int token=(int)(capture_counter+1);if(objpool_claim(&capture_pool,token))return -4;
    capture_t *c=objpool_get(&capture_pool,token);
    u32 address=pframe_alloc_run(pages);if(!address){objpool_release(&capture_pool,token);return -4;}
    int format=extra_of(handle)->format;
    task_render_hold(1);sti();
    u32 *pixels=(u32 *)address;
    if(format==2)for(u32 i=0;i<bytes/4;i++)pixels[i]=((u32 *)w->canvas)[i];
    else for(u32 i=0;i<bytes/4;i++)pixels[i]=0xFF000000u|display_rgb(w->canvas[i]);
    cli();task_render_hold(0);
    c->owner=pid;c->owner_generation=task_generation(pid);c->pixels=address;c->bytes=bytes;c->pages=pages;c->token=++capture_counter;
    c->next=task_record(pid)->captures;
    if(c->next)((capture_t *)objpool_get(&capture_pool,(int)c->next))->previous=c->token;
    task_record(pid)->captures=c->token;
    capture_pages+=pages;for(u32 i=0;i<8;i++)out[i]=0;
    out[0]=1;out[1]=(u32)handle;out[2]=(u32)w->cw;out[3]=(u32)w->ch;out[4]=(u32)w->cw*4;
    out[5]=2;out[6]=bytes;out[7]=task_generation(w->owner);return (int)c->token;
}
static capture_t *capture_find(int pid,u32 token)
{
    if(!token || token>0x7FFFFFFFu || !task_owned(pid))return 0;
    capture_t *c=objpool_get(&capture_pool,(int)token);
    return c && c->token==token && c->owner==pid && c->owner_generation==task_generation(pid)?c:0;
}
int wm_capture_read(int pid,u32 token,u32 offset,void *out,u32 length)
{
    capture_t *c=capture_find(pid,token);if(!c || offset>c->bytes || length>65536)return -1;
    if(length>c->bytes-offset)length=c->bytes-offset;
    for(u32 i=0;i<length;i++)((u8 *)out)[i]=((const u8 *)c->pixels)[offset+i];return (int)length;
}
int wm_capture_close(int pid,u32 token)
{
    capture_t *c=capture_find(pid,token);if(!c)return -1;
    pframe_free_run(c->pixels,c->pages);capture_pages-=c->pages;
    if(c->previous)((capture_t *)objpool_get(&capture_pool,(int)c->previous))->next=c->next;
    else task_record(pid)->captures=c->next;
    if(c->next)((capture_t *)objpool_get(&capture_pool,(int)c->next))->previous=c->previous;
    objpool_release(&capture_pool,(int)token);return 0;
}
void wm_capture_stop(int pid)
{if(task_owned(pid))while(task_record(pid)->captures)if(wm_capture_close(pid,task_record(pid)->captures))break;}
int wm_capture_stop_step(int pid)
{
    if(!task_owned(pid) || !task_record(pid)->captures)return 1;
    if(wm_capture_close(pid,task_record(pid)->captures))return 1;
    return !task_record(pid)->captures;
}
int wm_capture_list(int pid,char *out,u32 capacity)
{
    u32 used=0;if(!capacity)return -1;
    for(int handle=objpool_next(&window_pool,-1);handle>=0;handle=objpool_next(&window_pool,handle)){
        win_t *window=&window_record(handle)->window;if(!capture_allowed(pid,window))continue;
        char line[96],digits[12];int n=0;
        u32 numbers[4]={(u32)window->handle,(u32)window->owner,(u32)window->cw,(u32)window->ch};
        for(u32 j=0;j<4;j++){
            u32 v=numbers[j];int d=0;do{digits[d++]=(char)('0'+v%10);v/=10;}while(v);
            while(d)line[n++]=digits[--d];line[n++]=' ';
        }
        for(u32 j=0;j<12 && window->title[j];j++)line[n++]=window->title[j];line[n++]='\n';
        if((u32)n>=capacity-used)return -4;
        for(int j=0;j<n;j++)out[used++]=line[j];
    }
    out[used]=0;return (int)used;
}
int wm_window_page(int pid,u32 *out,u32 words,u32 after)
{
    if(!task_owned(pid) || words<32 || words>1040 || (after>0x7FFFFFFFu && after!=0xFFFFFFFFu))return -1;
    for(u32 i=0;i<words;i++)out[i]=0;
    u32 capacity=(words-16)/16,count=0,examined=0,last=after;
    int handle=objpool_next(&window_pool,after==0xFFFFFFFFu?-1:(int)after);
    /* 身份过滤不能把分页变成“本页不足就无限扫”。一次最多检查256窗，
     * 空页也给下一游标；零环外部管理员才可跨登录会话枚举与截图。 */
    while(handle>=0 && count<capacity && examined<256){
        window_record_t *p=window_record(handle);win_t *w=&p->window;
        last=(u32)handle;examined++;
        if(capture_allowed(pid,w)){
            u32 *row=out+16+count*16;row[0]=(u32)handle;row[1]=(u32)w->owner;row[2]=p->owner_generation;
            row[3]=p->session;row[4]=(u32)w->cw;row[5]=(u32)w->ch;row[6]=(u32)p->extra.format;
            row[7]=p->session==session_active() && !p->extra.hidden;row[8]=(u32)p->extra.hidden;
            row[9]=(u32)w->x;row[10]=(u32)w->y;row[11]=p->visibility_epoch;row[12]=(u32)p->repaint;
            for(u32 j=0;j<12;j++)((char *)(row+13))[j]=w->title[j];count++;
        }
        handle=objpool_next(&window_pool,handle);
    }
    out[0]=1;out[1]=16;out[2]=16;out[3]=count;out[4]=handle>=0?last:0xFFFFFFFFu;
    out[5]=session_task(pid);out[6]=examined;return 0;
}
/* 保留win_t的80B诊断布局。格式是稳定句柄的扩展状态，画布指针
 * 仍拥有唯一物理页；不能让置顶时只移动指针却丢掉像素格式。 */
static u32 canvas_pixel(win_t *w,u32 index)
{ return extra_of(w->handle)->format==2?((u32 *)w->canvas)[index]:w->canvas[index]; }
static void canvas_pixel_write(win_t *w,u32 index,u32 pixel)
{
    window_extra_t *e=extra_of(w->handle);
    if(e->format==2){((u32 *)w->canvas)[index]=pixel;if((pixel>>24)!=255)e->opaque=0;}
    else w->canvas[index]=(u8)pixel;
}
static void canvas_slot(win_t *w,u32 index,u8 color)
{ canvas_pixel_write(w,index,extra_of(w->handle)->format==2?0xFF000000u|display_rgb(color):color); }
static int top_visible(void)
{
    if(active_view->top_known)return active_view->top_cache;
    int result=-1;for(int i=nwins-1;i>=0;i--){window_record_t *p=window_record(WINDOW(i).handle);if(p && gui_can_show(p)){result=i;break;}}
    active_view->top_cache=result;active_view->top_known=1;return result;
}
static int work_bottom(void){return GFX_H-(GFX_W==320?16:32*display_scale()/100);}
static int dirty = 1;
/* 未知范围请求只升级不降级；局部矩形取并集，原始窗口结构不加
 * 字段。合成持有期间其它任务不能改请求，所以一帧结束可整体清空。 */
static int damage_full=1,damage_x0,damage_y0,damage_x1,damage_y1;
static volatile u32 wm_partial_frames,wm_partial_pixels;
static int cursor_dirty=1,cursor_saved;
static int cursor_x,cursor_y,cursor_w,cursor_h;
static u32 cursor_under[16*26]; /* 200%最大16×26；不另分配整屏快照 */
/* 只读诊断计数以PIT的10ms刻度记录真实合成成本；不冒称GPU帧率。
 * 性能脚本同时测宿主耗时/响应，避免QEMU虚拟时间单独掩盖延迟。 */
static volatile u32 wm_frames,wm_ticks_total,wm_ticks_max;
static volatile int wm_cursor_x,wm_cursor_y;
typedef struct { int active; u32 vector,error,eip,address,order;int previous,next;u32 session; } fault_card_t;
static fault_card_t empty_fault_cards;
static fault_card_t *fault_cards_at(int pid)
{void *p=task_data(pid,TASK_DATA_FAULT);return p?p:&empty_fault_cards;}
#define FAULT_CARD(pid) (*fault_cards_at(pid))
u32 wm_task_bytes(void){return sizeof(fault_card_t);}
static u32 fault_order;

static void fault_remove(int pid)
{
    /* 只链接真实暂停卡；无故障时关一个窗口不必遍历所有后台任务。
     * 顺序以链头表示，避免32位累计故障编号回绕影响最新卡片。 */
    fault_card_t *card=&FAULT_CARD(pid);if(!card->active)return;
    desktop_view_t *view=view_get(card->session);
    if(card->previous)FAULT_CARD(card->previous).next=card->next;else if(view)view->fault_first=card->next;
    if(card->next)FAULT_CARD(card->next).previous=card->previous;
    /* 最后一张卡退出时清的是卡片所属桌面的模态状态。隐藏桌面
     * 清理不能借active_view清另一个会话；否则切回虽无卡片，焦点
     * 与GETKEY仍被陈旧fault_modal阻挡。还有其它卡则继续保留模态。 */
    if(view && !view->fault_first)view->fault_modal=0;
    card->active=0;card->previous=card->next=0;
}

static int fault_top(void)
{
    return fault_head;
}
void wm_exception(int pid,u32 vector,u32 error,u32 eip,u32 address)
{
    if(pid<=0 || !task_owned(pid))return;
    u32 id=session_task(pid);if(wm_session_prepare(id))return;desktop_view_t *view=view_get(id);
    fault_remove(pid);FAULT_CARD(pid)=(fault_card_t){1,vector,error,eip,address,++fault_order,0,view->fault_first,id};
    if(view->fault_first)FAULT_CARD(view->fault_first).previous=pid;view->fault_first=pid;
    view->fault_modal=1;if(id==session_active())wm_request_compose();
    gui_focus_update();
}

/* mio：捕获所有权单独记录，不扩168B task_t或80B win_t。
 * 每次消费验证句柄、PID代数及当前可见聚焦；故障和窗口切换会
 * 自动释放。原绝对POINTER合同/沿不改，新相对接口独立消费。 */
static int relative_blocked(void);
static int relative_owner,relative_handle;
static u32 relative_generation,relative_last_x,relative_last_y;
static u32 relative_pressed,relative_released;
static void relative_release(void)
{
    if(!relative_owner)return;
    relative_owner=relative_handle=0;relative_pressed=relative_released=0;
    mouse_relative_mode(0);wm_request_compose();
}
static int relative_active(void)
{
    if(!relative_owner)return 0;
    int top=top_visible();
    if(top<0 || WINDOW(top).owner!=relative_owner || WINDOW(top).handle!=relative_handle
        || TASK(relative_owner).state!=1 || task_generation(relative_owner)!=relative_generation
        || fault_top() || relative_blocked()){
        relative_release();return 0;
    }
    return 1;
}
int wm_user_mouse_capture(int pid,int handle,int enabled)
{
    if(!owned_window(pid,handle) || (enabled!=0 && enabled!=1))return -1;
    if(!enabled){if(relative_owner==pid && relative_handle==handle)relative_release();return 0;}
    if(gui_capture_handle)return -1;
    int top=top_visible();
    if(top<0 || WINDOW(top).owner!=pid || WINDOW(top).handle!=handle || !wm_focused(pid) || fault_top() || relative_blocked())return -1;
    if(relative_active() && relative_owner==pid && relative_handle==handle)return 0;
    relative_release();relative_owner=pid;relative_handle=handle;relative_generation=task_generation(pid);
    mouse_relative_totals(&relative_last_x,&relative_last_y);relative_pressed=relative_released=0;
    mouse_relative_mode(1);wm_request_compose();return 0;
}
int wm_user_mouse_relative(int pid,int handle,i32 *out)
{
    if(!owned_window(pid,handle))return -1;
    for(int i=0;i<8;i++)out[i]=0;out[0]=1;out[7]=wm_focused(pid);
    if(!relative_active() || relative_owner!=pid || relative_handle!=handle)return 0;
    u32 x,y;mouse_relative_totals(&x,&y);
    /* unsigned相减保留回绕差；极端挂起只钳制一次读取的位移，
     * 不改变计数，也不按屏幕坐标重算，边缘可无限转动。 */
    i32 dx=(i32)(x-relative_last_x),dy=(i32)(y-relative_last_y);
    out[1]=dx<-32767?-32767:dx>32767?32767:dx;
    out[2]=dy<-32767?-32767:dy>32767?32767:dy;
    out[3]=mouse_buttons();out[4]=relative_pressed;out[5]=relative_released;out[6]=1;
    relative_last_x=x;relative_last_y=y;relative_pressed=relative_released=0;return 0;
}


/* mio：右下角抓取区有16个逻辑像素宽，按下点通常并不恰好位于
 * 外框最后一个像素。保存它到真实边界的距离，拖动预览及释放均
 * 加回这个距离；原地按下/松开因此不会凭空缩小窗口。它只属于
 * 一次鼠标拖框，不扩充80B窗口结构或任何用户态缓冲/系统调用。 */

void wm_request_compose(void) { dirty=1;damage_full=1; }
void wm_damage_rect(int x,int y,int width,int height)
{
    if(GFX_W==320){wm_request_compose();return;}
    signed long long right=(signed long long)x+width,bottom=(signed long long)y+height;
    if(width<=0 || height<=0 || right<=0 || bottom<=0 || x>=GFX_W || y>=GFX_H)return;
    int x0=x<0?0:x,y0=y<0?0:y;
    int x1=right>GFX_W?GFX_W:(int)right,y1=bottom>GFX_H?GFX_H:(int)bottom;
    if(!dirty){damage_x0=x0;damage_y0=y0;damage_x1=x1;damage_y1=y1;}
    else if(!damage_full){
        if(x0<damage_x0)damage_x0=x0;
        if(y0<damage_y0)damage_y0=y0;
        if(x1>damage_x1)damage_x1=x1;
        if(y1>damage_y1)damage_y1=y1;
    }
    dirty=1;
}
void wm_request_clock(void)
{wm_damage_rect(0,work_bottom(),GFX_W,GFX_H-work_bottom());}
int  wm_dirty(void)           { return dirty || cursor_dirty; }

/* ---------------- 窗口句柄 API (系统调用落地) ---------------- */
static int win_create(int owner, const char *title, i32 x, i32 y, i32 w, i32 h,int native)
{
    if(!task_owned(owner) || TASK(owner).state!=1 || next_handle>0x7FFFFFFFu
        || w<48 || h<32 || w>(native?1920:320) || h>(native?1080:184))
        return -1;
    u32 id=session_task(owner);if(wm_session_prepare(id))return -1;
    desktop_view_t *view=view_get(id);
    int handle=(int)next_handle;if(objpool_claim(&window_pool,handle))return -1;
    window_record_t *record=window_record(handle);win_t *np=&record->window;
    np->used = 1;
    np->owner = owner;
    np->handle=handle;
    window_extra_t *ex=&record->extra;ex->handle=handle;ex->native=native!=0;ex->scale=1;
    ex->format=(native==2 || native==-2)?2:1;
    ex->hidden=native==-2;
    ex->opaque=ex->format==2;
    np->x = x; np->y = y; np->w = w; np->h = h;
    int title_bytes=0;
    while(*title){
        const char *begin=title;u32 scalar;gfx_utf8_next(&title,&scalar);
        if(title_bytes+(int)(title-begin)>11)break;
        while(begin<title)np->title[title_bytes++]=*begin++;
    }
    np->title[title_bytes]=0; /* 80B旧布局保留，但UTF-8标量不被字节截开 */
    np->cw = w - 2 * BORDER;
    np->ch = h - (native?TITLE_H:13) - BORDER;
    if(np->ch<16){objpool_release(&window_pool,handle);return -1;}
    /* 旧VGA索引画布也按真实尺寸借页，内容和ABI不变；不能给无限
     * 个任务只准备六块静态画布。候选失败完整释放，不消费窗口句柄。 */
    ex->pages=((u32)np->cw*(u32)np->ch*(ex->format==2?4u:1u)+4095u)/4096u;
    np->canvas=(u8 *)pframe_alloc_run(ex->pages);
    if(!np->canvas){objpool_release(&window_pool,handle);return -1;}
    if(order_reserve(view)){pframe_free_run((u32)np->canvas,ex->pages);objpool_release(&window_pool,handle);return -1;}
    if(!native && GFX_W!=320) {
        ex->scale=(display_scale()+99)/100;
        np->w=np->cw*ex->scale+2;np->h=np->ch*ex->scale+TITLE_H+1;
        np->x=(GFX_W-np->w)/2;np->y=(work_bottom()-np->h)/2;
    }
    /* RGB工具的首帧之前仍有堆/字体/资源初始化。用当前主题表面
     * 建立等待画布，避免桌面合成把大块控制台黑色抢先闪到屏幕。
     * 旧索引程序仍保留其原始PAL_CON_BG字节，不迁移旧ABI颜色。 */
    /* 画布候选尚未进入任何order/owner链，IRQ只收原始输入。大窗口
     * 初填期间仅暂停切换并开放IRQ，不能用数百万像素的IF=0循环
     * 消耗8042硬件缓冲；完成后再一次发布稳定对象。 */
    task_render_hold(1);sti();
    for (u32 i = 0; i < (u32)(np->cw * np->ch); i++) {
        if(ex->format==2)((u32 *)np->canvas)[i]=0xFF000000u|theme_color(TH_FACE);
        else canvas_slot(np,i,PAL_CON_BG);
    }
    cli();task_render_hold(0);
    np->kq = np->kt = 0;
    np->tx = np->ty = 4;
    record->owner_generation=task_generation(owner);record->owner_next=task_record(owner)->windows;
    if(record->owner_next)window_record((int)record->owner_next)->owner_previous=(u32)handle;
    task_record(owner)->windows=(u32)handle;record->z_index=record->creation_index=view->count;
    record->session=id;record->visibility_epoch=1;record->repaint=1;
    view->order[view->count]=view->created[view->count]=handle;next_handle++;
    view->count++;
    view->top_known=0;
    if(view==active_view)taskbar_reveal(handle);
    gui_focus_update();
    return np->handle;
}

static void win_close_handle(int handle)
{
    window_record_t *record=window_record(handle);if(!record)return;
    desktop_view_t *view=view_get(record->session);if(!view)return;
    /* 关闭一个终端也必须撤销附着作业，即使父应用还有别的窗口。
     * 在wins重排之前通知，子任务关闭其它窗口后重新寻找当前句柄。
     * 不保留跨task_stop的数组下标，避免递归关闭移动了窗口。 */
    int owner=record->window.owner;
    streams_terminal_closed(owner,handle);
    userspace_terminal_closed(owner,handle);
    record=window_record(handle);if(!record)return;int idx=record->z_index;
    gui_focus_defer++;gui_closed(record);
    int drag_handle=view->dragging>=0?view->order[view->dragging]:0,resize_handle=view->resize>=0?view->order[view->resize]:0;
    if(relative_handle==handle)relative_release();
    terminal_t *term=&record->terminal;
    if(term->text)pframe_free_run((u32)term->text,4);
    pframe_free_run((u32)record->window.canvas,record->extra.pages);
    if(record->owner_previous)window_record((int)record->owner_previous)->owner_next=record->owner_next;
    else task_record(owner)->windows=record->owner_next;
    if(record->owner_next)window_record((int)record->owner_next)->owner_previous=record->owner_previous;
    for (int i = idx; i < view->count - 1; i++) {
        view->order[i]=view->order[i+1];window_record(view->order[i])->z_index=i;
    }
    for(int i=record->creation_index;i<view->count-1;i++){
        view->created[i]=view->created[i+1];window_record(view->created[i])->creation_index=i;
    }
    view->count--;
    view->top_known=0;
    objpool_release(&window_pool,handle);
    window_record_t *drag_record=window_record(drag_handle),*resize_record=window_record(resize_handle);
    view->dragging=drag_record?drag_record->z_index:-1;view->resize=resize_record?resize_record->z_index:-1;
    if(!view->count){pframe_free_run((u32)view->order,view->order_pages);view->order=view->created=0;view->order_pages=view->order_capacity=0;}
    if(view->bar_first>=view->count)view->bar_first=0;
    if(view==active_view)wm_request_compose();
    gui_focus_defer--;
    gui_focus_update();
}

static void win_raise(int idx)
{
    if(idx<0 || idx>=nwins)return;
    int allowed=gui_modal_target(WINDOW(idx).handle);
    window_record_t *target=window_record(allowed);
    if(target && target->session==session_active())idx=target->z_index;
    int previous=top_visible();if(previous>=0)task_notify(WINDOW(previous).owner,TASK_EVENT_INPUT);
    int handle=window_order[idx],drag_handle=drag>=0?WINDOW(drag).handle:0,resize_handle=resizing>=0?WINDOW(resizing).handle:0;
    for (int i = idx; i < nwins - 1; i++) {
        window_order[i]=window_order[i+1];window_record(window_order[i])->z_index=i;
    }
    window_order[nwins-1]=handle;window_record(handle)->z_index=nwins-1;
    active_view->top_known=0;
    window_record_t *drag_record=window_record(drag_handle),*resize_record=window_record(resize_handle);
    drag=drag_record?drag_record->z_index:-1;resizing=resize_record?resize_record->z_index:-1;
    taskbar_reveal(handle);
    task_notify(window_record(handle)->window.owner,TASK_EVENT_INPUT);
    gui_focus_update();
}

/* ---------------- 用户窗口 API (系统调用落地) ---------------- */
int wm_open_user_window(int pid, const char *title, int w, int h)
{
    if (h > CANVAS_H + 14) h = CANVAS_H + 14;
    if (w > CANVAS_W + 2) w = CANVAS_W + 2;
    i32 x = (320 - w) / 2, y = (184 - h) / 2;
    int hnd = win_create(pid, title, x, y, w, h,0);
    if (hnd >= 0 && session_task(pid)==session_active()) wm_request_compose();
    return hnd;
}

void wm_close_user_window(int pid, int handle)
{
    if(owned_window(pid,handle))win_close_handle(handle);
}

void wm_close_owner(int pid)
{
    if(relative_owner==pid)relative_release();
    if(pid>0 && task_owned(pid))fault_remove(pid);
    if(!fault_top()) fault_focus=0;
    if(task_owned(pid))while(task_record(pid)->windows){
        window_record_t *record=window_record((int)task_record(pid)->windows);
        if(!record || record->owner_generation!=task_generation(pid)){task_record(pid)->windows=0;break;}
        win_close_handle(record->window.handle);
    }
    if(session_task(pid)==session_active())wm_request_compose();
}
int wm_close_owner_step(int pid)
{
    if(!task_owned(pid))return 1;
    if(relative_owner==pid)relative_release();fault_remove(pid);if(!fault_top())fault_focus=0;
    u32 handle=task_record(pid)->windows;
    if(handle){win_close_handle((int)handle);return !task_record(pid)->windows;}
    return 1;
}

static win_t *owned_window(int pid, int handle)
{
    /* 句柄不是 wins 下标：数组只承担 z 序，点击置顶会移动结构。
     * 每次调用同时匹配稳定 ID 和 owner，既防止画进别人的窗口，
     * 也防止关闭中间窗口后老句柄误指向补位的新窗口。 */
    window_record_t *record=window_record(handle);
    return record && task_owned(pid) && record->window.owner==pid && record->owner_generation==task_generation(pid)?&record->window:0;
}

static int window_visible(win_t *w)
{window_record_t *p=window_record(w->handle);return p && p->session==session_active() && gui_can_show(p);}
static void window_changed(win_t *w)
{if(window_visible(w))wm_request_compose();else window_record(w->handle)->repaint=1;}
static void visibility_changed(win_t *w)
{
    window_record_t *p=window_record(w->handle);p->visibility_epoch++;if(!p->visibility_epoch)p->visibility_epoch=1;
    desktop_view_t *view=view_get(p->session);if(view)view->top_known=0;
    p->repaint=1;p->window.kq=p->window.kt=0;p->extra.pressed=p->extra.released=0;
    task_notify_generation(w->owner,p->owner_generation,TASK_EVENT_VISIBILITY);
    if(p->session==session_active())wm_request_compose();
    gui_commit(w);gui_focus_update();
}
void wm_session_repaint(u32 id)
{
    desktop_view_t *view=view_get(id);if(!view)return;
    for(int i=0;i<view->count;i++){
        window_record_t *p=window_record(view->order[i]);p->visibility_epoch++;
        if(!p->visibility_epoch)p->visibility_epoch=1;p->repaint=1;p->terminal.theme=0;
        task_notify_generation(p->window.owner,p->owner_generation,TASK_EVENT_VISIBILITY);
        if(p->gui.mode){p->gui.theme++;ScMessage m={0};m.id=SC32_MSG_THEME;m.window=(u32)p->window.handle;
            m.generation=m.window;m.epoch=p->gui.theme;sc32_gui_emit(p->window.owner,&m);
            p->gui.paint_pending=p->gui.paint_ready=p->gui.painting=0;gui_attention(p);
            Sc32Invalidate invalidate={36,1,(u32)p->window.handle,(u32)p->window.handle,0,0,(u32)p->window.cw,(u32)p->window.ch,0};
            (void)wm_sc32_invalidate(p->window.owner,&invalidate);}
    }
    if(id==session_active())wm_request_compose();
}
int wm_user_visibility(int pid,int handle,u32 out[8])
{
    win_t *w=owned_window(pid,handle);if(!w)return -1;window_record_t *p=window_record(handle);
    task_record(pid)->visibility_aware=1;
    for(int i=0;i<8;i++)out[i]=0;
    out[0]=1;out[1]=window_visible(w);out[2]=p->visibility_epoch;out[3]=p->session;
    out[4]=p->repaint;out[5]=wm_focused(pid);out[6]=p->extra.hidden;
    if(out[1])p->repaint=0;return 0;
}
int wm_task_hidden_gui(int pid)
{
    if(!task_owned(pid) || !task_record(pid)->windows)return 0;
    for(u32 h=task_record(pid)->windows;h;h=window_record((int)h)->owner_next)
        if(window_visible(&window_record((int)h)->window))return 0;
    return 1;
}
int wm_defer_draw(int pid,int handle,int format)
{
    if(!task_owned(pid) || task_record(pid)->visibility_aware)return 0;
    if(!handle){handle=(int)task_record(pid)->windows;if(terminal_of(handle))return 0;}
    win_t *w=owned_window(pid,handle);if(!w || window_visible(w))return 0;
    return format<0 || extra_of(handle)->format==format;
}

#include "wm_native.inc"
static int relative_blocked(void){return menu_open || context_open;}
#include "wm_terminal.inc"

int wm_focused(int pid)
{int top=top_visible();return top>=0 && WINDOW(top).owner==pid && TASK(pid).state==1 && !fault_focus && !menu_open && !context_open; }
int wm_user_frame(int pid,int handle,const u8 *pixels,u32 length)
{
    win_t *w=owned_window(pid,handle);
    if(!w || extra_of(handle)->format!=1 || length!=(u32)(w->cw*w->ch)) return -1;
    if(!window_visible(w)){window_record(handle)->repaint=1;return 0;}
    /* 旧FRAME也能提交高分辨率原生索引画布，不能因为是兼容接口
     * 就一直关IRQ复制1..2MB。校验完成后固定当前任务/CR3，主循环
     * 暂不改窗口或合成，硬件驱动只入队。结束先关IRQ再解除持有，
     * 原返回值、逐字节内容、重画请求及错误无写入合同完全保留。 */
    task_render_hold(1);sti();
    for(u32 i=0;i<length;i++) w->canvas[i]=pixels[i];
    cli();task_render_hold(0);
    /* 旧索引FRAME只改变自己的客户画布。原生后端仍产生真实重画
     * 请求，但范围按当前整数显示倍数换算；无需每帧重画整张壁纸、
     * 桌面图标和任务栏。下层/透明叠窗沿原合成顺序重画该矩形。
     * VGA由wm_damage_rect原样回退整屏，旧长度/寄存器/内容不改。 */
    window_extra_t *extra=extra_of(handle);
    int scale=extra->native?1:extra->scale;
    wm_damage_rect(w->x+1,w->y+TITLE_H,w->cw*scale,w->ch*scale);
    return 0;
}

int wm_user_info(int pid,int handle,i32 *out)
{
    /* 鼠标 ABI 给出屏幕绝对坐标；应用需要外框位置才能转换为客户区。
     * 拖动由内核负责，应用每轮查询当前位置，不能缓存最初的居中位置。
     * focused 使后台游戏不会响应全局鼠标点击，避免点击穿透前景窗口。 */
    win_t *w=owned_window(pid,handle);
    if(!w) return -1;
    window_extra_t *e=extra_of(handle);
    out[0]=(GFX_W!=320 && !e->native)?0:w->x;
    out[1]=(GFX_W!=320 && !e->native)?0:w->y;
    out[2]=w->cw; out[3]=w->ch;
    int top=top_visible();out[4]=top>=0 && w==&WINDOW(top) && !e->hidden;
    return 0;
}

int wm_wallpaper(void) {desktop_view_t *v=view_get(session_current());return v?v->wallpaper:0;}
int wm_set_wallpaper(int mode)
{
    if(mode<0 || mode>2) return -1;
    u8 value=(u8)('0'+mode);
    /* 设置先落盘，失败时保留当前壁纸；应用可据返回值显示保存失败。
     * 仅保存模式号，重启时仍用同一套 palette 槽位生成像素，无 64KB
     * 图片缓冲占用低端 BSS，也没有新增自定义魔数或裸 RGB 色号。 */
    desktop_view_t *view=view_get(session_current());char path[64];
    if(!view || session_config_path("WALL.CFG",path)<0 || fs_write(path,&value,1)!=1)return -1;
    view->wallpaper=mode;if(view==active_view)wm_request_compose();
    return 0;
}

static void canvas_char(win_t *w, int x, int y, char ch, u8 color)
{
    /* 字形数据取统一凤凰字体的兼容 8px 表；这里只更换绘制目的地。
     * SYS_TXT 是透明底绘制，字形的零位保持画布原像素，适合覆盖图形。
     * 退格因此不能靠画一个空格完成，流式文字接口必须另行清整格。
     * 客户区坐标和屏幕坐标分开，拖动只变外框位置，不会重画字形。 */
    const u8 *g = glyph_of(ch);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (x+c >= 0 && x+c < w->cw && y+r >= 0 && y+r < w->ch
             && ((g[r] >> (7-c)) & 1)) canvas_slot(w,(y+r)*w->cw+x+c,color);
}

void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color)
{
    win_t *w = owned_window(pid, handle);
    if (!w) return;
    if(!window_visible(w)){window_record(handle)->repaint=1;return;}
    int start = x;
    while (*s) {
        if (*s == '\n') { x = start; y += 10; }
        else { canvas_char(w, x, y, *s, color); x += 8; }
        s++;
    }
    wm_request_compose();
}

int wm_user_utf8(int pid,int handle,int x,int y,const char *text,u8 color)
{
    /* 使用与桌面相同的解码器/字形入口，程序无需私自解析 SCF。
     * 只写 owner 的画布，透明底，不经过后台合成缓冲。坐标先限幅，
     * 使 4095B 字串的累积字宽仍不会发生有符号整数溢出；负坐标
     * 合法，逐像素裁剪后可用于滚动/部分字形显露的自定义界面。 */
    win_t *w=owned_window(pid,handle);
    if(!w || x < -32768 || x > 32767 || y < -32768 || y > 32767) return -1;
    if(!window_visible(w)){int count=0;u32 scalar;while(*text){gfx_utf8_next(&text,&scalar);if(scalar!='\n' && scalar!='\r')count++;}window_record(handle)->repaint=1;return count;}
    int left=x,count=0; u32 scalar; u16 rows[16];
    while(*text) {
        gfx_utf8_next(&text,&scalar);
        if(scalar=='\n') { x=left; y+=18; continue; }
        if(scalar=='\r') { x=left; continue; }
        int width=gfx_glyph16(scalar,rows);
        if(!width) width=16;
        for(int r=0;r<16;r++) for(int c=0;c<width;c++)
            if(x+c>=0 && x+c<w->cw && y+r>=0 && y+r<w->ch
               && (rows[r]&(0x8000u>>c))) canvas_slot(w,(y+r)*w->cw+x+c,color);
        x+=width; count++;
    }
    wm_request_compose(); return count;
}

void wm_user_fill(int pid, int handle, int x, int y, int w2, int h2, u8 color)
{
    win_t *w = owned_window(pid, handle);
    if (!w || w2 <= 0 || h2 <= 0) return;
    if(!window_visible(w)){window_record(handle)->repaint=1;return;}
    for (int j = 0; j < w->ch; j++)
        for (int i = 0; i < w->cw; i++)
            if ((i32)(i-x) >= 0 && (u32)(i-x) < (u32)w2
             && (i32)(j-y) >= 0 && (u32)(j-y) < (u32)h2)
                canvas_slot(w,j*w->cw+i,color);
    if (x == 0 && y == 0 && w2 >= w->cw && h2 >= w->ch) w->tx = w->ty = 4;
    wm_request_compose();
}

int wm_user_puts(int pid, const char *s)
{
    /* 【流式输出属于哪个窗口】
     * 一个应用可以开多窗口：选 handle 最大的本人窗口，即最近创建的窗口，
     * 而不是 z 序最顶的窗口。点别人的窗口不会把本应用的输出重定向过去。
     * tx/ty 存在窗口结构里，程序之间、窗口之间都不共享文字游标。
     * ASCII 字宽 8px、行步长 10px，四边留 4px；滚动源偏移必须是
     * 10*cw 字节（完整像素行），不是 cw 字节（只移动 1px）。 */
    win_t *w=task_owned(pid)?owned_window(pid,(int)task_record(pid)->windows):0;
    if (!w) return -1;
    if(terminal_of(w->handle))return wm_terminal_puts(pid,w->handle,s);
    while (*s) {
        char c = *s++;
        if (c == '\r') { w->tx = 4; continue; }
        if (c == '\b') {
            if (w->tx >= 12) w->tx -= 8;
            else if (w->ty >= 14) {
                /* 上一行末格由客户区可用列数算出，不硬写 38 或 40。
                 * Shell 自己的长度守卫决定是否允许退格，窗口只负责像素
                 * 和游标回退；长命令折行后也能逐字删除而不留上下半截。 */
                w->ty -= 10;
                w->tx = 4 + ((w->cw-8)/8-1)*8;
            }
            for (int r = 0; r < 8; r++)
                for (int x = 0; x < 8; x++) canvas_slot(w,(w->ty+r)*w->cw+w->tx+x,PAL_CON_BG);
            continue;
        }
        if (c == '\n' || w->tx + 8 > w->cw - 4) { w->tx = 4; w->ty += 10; }
        if (w->ty + 8 > w->ch - 4) {
            for (int i = 4*w->cw; i < (w->ch-14)*w->cw; i++) canvas_pixel_write(w,i,canvas_pixel(w,i+10*w->cw));
            for (int i = (w->ch-14)*w->cw; i < (w->ch-4)*w->cw; i++) canvas_slot(w,i,PAL_CON_BG);
            w->ty -= 10;
        }
        if (c != '\n') { canvas_char(w, w->tx, w->ty, c, PAL_TITLE); w->tx += 8; }
    }
    wm_request_compose();
    return 0;
}

/* ---------------- 绘制 ---------------- */
static void draw_window(int i)
{
    win_t *w = &WINDOW(i);
    window_extra_t *ex=extra_of(w->handle);if(!gui_can_show(window_record(w->handle)))return;
    if(GFX_W!=320){native_draw_window(i);return;}
    int focused = window_record(w->handle)->gui.mode?gui_focus_any==w->handle:i==top_visible();

    gfx_fill(w->x, w->y, w->w, w->h, focused ? PAL_CON_TINT : PAL_WIN_TITLE);
    gfx_fill(w->x + BORDER, w->y + TITLE_H, w->w - 2 * BORDER,
             w->h - TITLE_H - BORDER, PAL_CON_BG);
    gfx_fill(w->x + BORDER, w->y + BORDER, w->w - 2 * BORDER, TITLE_H - 2,
             focused ? PAL_CON_TINT : PAL_WIN_TITLE);
    gfx_text8(w->x + 4, w->y + 3, w->title, PAL_CON_BG, 1, 1);
    gfx_fill(w->x + w->w - 12, w->y + 2, 10, 10, PAL_PANIC);
    gfx_line(w->x + w->w - 10, w->y + 4, w->x + w->w - 4, w->y + 10, PAL_TITLE);
    gfx_line(w->x + w->w - 10, w->y + 10, w->x + w->w - 4, w->y + 4, PAL_TITLE);

    /* 画布 blit: 客户区像素逐点贴到后台缓冲 */
    for (int r = 0; r < w->ch; r++)
        for (int c = 0; c < w->cw; c++)
            if(ex->format==2) gfx_argb_pset(w->x+BORDER+c,w->y+TITLE_H+r,((u32 *)w->canvas)[r*w->cw+c]);
            else gfx_pset(w->x + BORDER + c, w->y + TITLE_H + r, w->canvas[r * w->cw + c]);
}

static void draw_taskbar(void)
{
    if(GFX_W!=320){native_draw_taskbar();return;}
    gfx_fill(0, 184, 320, 16, PAL_TASKBAR);
    gfx_text16(4, 184, "沙核", PAL_TITLE, 0);
    /* 创建序索引不随置顶改变；只画物理屏幕容得下的当前页。
     * 上一/下一页能访问所有窗口，不把固定六按钮变成任务数量门槛。 */
    int overflow=nwins>taskbar_capacity(),origin=overflow?60:44;
    if(overflow){gfx_text8(46,188,"<",taskbar_first?PAL_UI_CYAN+7:PAL_UI_MUTED,1,0);
        gfx_text8(214,188,">",taskbar_first+taskbar_capacity()<nwins?PAL_UI_CYAN+7:PAL_UI_MUTED,1,0);}
    for(int slot=0;slot<taskbar_capacity() && taskbar_first+slot<nwins;slot++) {
        int chosen=rank_window(slot);
        if(chosen<0) continue;
        int x=origin+slot*30; char label[4];
        /* 三字符按钮必须先传达用途。Studio/Debug 共用 SC 品牌前缀，
         * 如果直接截前三字，两者都会显示 SC 而无法分辨。这里只跳过
         * 通用的「SC 空格」前缀，不按应用名硬编码；完整标题仍在标题栏。
         * 遇到 NUL 后统一补空格，不能继续读取短标题后面的历史字节。 */
        const char *short_title=WINDOW(chosen).title;
        if(short_title[0]=='S' && short_title[1]=='C' && short_title[2]==' ')
            short_title+=3;
        int ended=0;
        for(int j=0;j<3;j++) {
            if(!short_title[j]) ended=1;
            label[j]=ended?' ':short_title[j];
        }
        label[3]=0;
        gfx_fill(x,186,28,12,chosen==nwins-1?PAL_UI_NIGHT+6:PAL_TASKBAR);
        gfx_text8(x+2,188,label,chosen==nwins-1?PAL_UI_CYAN+7:PAL_UI_MUTED,1,0);
    }
    gfx_text8(238, 188, "UP ", PAL_CON_TINT, 1, 1);
    u32 s = sc_ticks / 100;
    char b[11];
    i32 n = 0;
    do { b[n++] = (char)('0' + s % 10); s /= 10; } while (s);
    i32 x = 263;
    while (n--) {
        gfx_char8(x, 188, b[n], PAL_CON_TINT, 1);
        x += 8;
    }
    gfx_text8(x + 8, 188, "S", PAL_CON_TINT, 1, 1);
}

static const char *cursor_art[13] = {
    "#.......",
    "##......",
    "#o#.....",
    "#oo#....",
    "#ooo#...",
    "#oooo#..",
    "#ooooo#.",
    "#oooooo#",
    "#oooooo#",
    "#ooo####",
    "#o#.#...",
    "#..#.#..",
    "....#...",
};

static void draw_cursor_at(int x,int y)
{
    for (int r = 0; r < 13; r++)
        for (int c = 0; c < 8; c++) {
            char a = cursor_art[r][c];
            if (a == '#')
                gfx_fill(x+unit(c),y+unit(r),unit(c+1)-unit(c),unit(r+1)-unit(r),PAL_TITLE);
            else if (a == 'o')
                gfx_fill(x+unit(c),y+unit(r),unit(c+1)-unit(c),unit(r+1)-unit(r),PAL_SHADOW);
        }
}
static void cursor_capture(int x,int y)
{
    cursor_x=x;cursor_y=y;cursor_w=unit(8);cursor_h=unit(13);
    for(int r=0;r<cursor_h;r++)for(int c=0;c<cursor_w;c++)
        cursor_under[r*cursor_w+c]=gfx_rgb_get(x+c,y+r);
    cursor_saved=1;
}
static void cursor_restore(void)
{
    for(int r=0;r<cursor_h;r++)for(int c=0;c<cursor_w;c++)
        gfx_rgb_pset(cursor_x+c,cursor_y+r,cursor_under[r*cursor_w+c]);
}

static void draw_icons(void)
{
    if(GFX_W!=320){native_draw_icons();return;}
    for (int i = 0; i < 4; i++) {
        const desk_item_t *item=desktop_item(0,i);if(!item)continue;
        i32 x = 8, y = 8 + i * 44;
        desktop_icon(x,y,16,item);
        char label[64];native_elide(label,item->label,112);native_text_rgb(x,y+20,label,theme_color(TH_DESKTOP_TEXT));
    }
}

static void fault_hex(int x,int y,u32 value)
{
    char b[9];
    for(int i=0;i<8;i++) b[i]="0123456789ABCDEF"[(value>>(28-i*4))&15];
    b[8]=0; gfx_text8(x,y,b,PAL_CON_TINT,1,1);
}
static void fault_bounds(int *out)
{
    /* 命中与绘图只算同一份几何。640/200%卡片窄时改为单列，不能
     * 先画大卡再在旧VGA固定位置接收叉号；底部避开真实任务栏。 */
    if(GFX_W==320){out[0]=22;out[1]=38;out[2]=272;out[3]=118;return;}
    int width=unit(480);if(width>GFX_W-unit(32))width=GFX_W-unit(32);
    int height=unit(width<unit(424)?184:172);
    if(height>work_bottom()-unit(24))height=work_bottom()-unit(24);
    out[0]=(GFX_W-width)/2;out[1]=(work_bottom()-height)/2;out[2]=width;out[3]=height;
}
static void fault_native_hex(int x,int y,const char *label,u32 value)
{
    char text[16];int at=0;while(*label)text[at++]=*label++;
    text[at++]=' ';
    for(int i=0;i<8;i++)text[at++]="0123456789ABCDEF"[(value>>(28-i*4))&15];
    text[at]=0;native_text_rgb(x,y,text,theme_color(TH_TEXT));
}
static int fault_click(int x,int y)
{
    int pid=fault_top(),box[4];if(!pid)return 0;fault_bounds(box);
    if(x<box[0] || x>=box[0]+box[2] || y<box[1] || y>=box[1]+box[3])return 0;
    fault_focus=1;
    int bx=GFX_W==320?278:box[0]+box[2]-unit(28);
    int by=GFX_W==320?41:box[1]+unit(4),size=GFX_W==320?12:unit(22);
    if(x>=bx && x<bx+size && y>=by && y<by+size)task_stop(pid);
    return 1;
}
static void draw_fault(void)
{
    int pid=fault_top(); if(!pid) return;
    fault_card_t *f=&FAULT_CARD(pid); const char *why="UNHANDLED EXCEPTION";
    if(f->vector==0) why="DIVIDE BY ZERO";
    if(f->vector==6) why="INVALID INSTRUCTION";
    if(f->vector==13) why="PROTECTION VIOLATION";
    if(f->vector==14) why="INVALID MEMORY ACCESS";
    if(GFX_W!=320){
        int b[4];fault_bounds(b);int x=b[0],y=b[1],w=b[2],h=b[3],narrow=w<unit(424);
        if(theme_classic())native_bevel(x,y,w,h,0);
        else{
            native_round(x+unit(3),y+unit(4),w,h,unit(theme_radius()),theme_color(TH_SHADOW));
            native_round(x,y,w,h,unit(theme_radius()),theme_color(TH_LINE));
        }
        native_round(x+1,y+1,w-2,h-2,theme_classic()?0:unit(theme_radius()),theme_color(TH_PAPER));
        gfx_rgb_fill(x+unit(12),y+unit(10),unit(3),unit(14),theme_color(TH_ALERT));
        native_text_rgb(x+unit(24),y+unit(8),"APPLICATION PAUSED",theme_color(TH_TEXT));
        int bx=x+w-unit(28);native_round(bx,y+unit(4),unit(22),unit(22),theme_classic()?0:unit(4),theme_color(TH_ALERT));
        native_text_rgb(bx+unit(7),y+unit(6),"x",theme_color(TH_SELECT_TEXT));
        native_text_rgb(x+unit(20),y+unit(38),why,theme_color(TH_ALERT));
        if(narrow){
            fault_native_hex(x+unit(20),y+unit(62),"EIP",f->eip);
            fault_native_hex(x+unit(20),y+unit(82),"VEC",f->vector);
            fault_native_hex(x+unit(20),y+unit(102),"ERR",f->error);
            fault_native_hex(x+unit(20),y+unit(122),"MEM",f->address);
            native_text_rgb(x+unit(20),y+unit(144),TASK(pid).name,theme_color(TH_MUTED));
        }else{
            fault_native_hex(x+unit(20),y+unit(76),"EIP",f->eip);
            fault_native_hex(x+w/2,y+unit(76),"VEC",f->vector);
            fault_native_hex(x+unit(20),y+unit(102),"ERR",f->error);
            fault_native_hex(x+w/2,y+unit(102),"MEM",f->address);
            native_text_rgb(x+unit(20),y+unit(128),TASK(pid).name,theme_color(TH_MUTED));
        }
        native_text_rgb(x+unit(20),y+h-unit(20),"Close ends this program.",theme_color(TH_MUTED));return;
    }
    /* 深色卡片、青色数据、红色异常边：给诊断建立视觉层次。
     * 固定位置不依赖故障程序的窗口元数据；绘在普通窗口之后，不占
     * 画布池，所以窗口耗尽也不至于让暂停任务失去可操作的退出入口。 */
    gfx_fill(26,42,272,118,PAL_SHADOW); gfx_fill(22,38,272,118,PAL_CON_BG);
    gfx_fill(22,38,3,118,PAL_PANIC); gfx_fill(25,38,269,23,PAL_WIN_TITLE);
    gfx_text8(34,46,"APPLICATION PAUSED",PAL_TITLE,1,1);
    gfx_fill(278,41,12,12,PAL_PANIC);
    gfx_line(281,44,287,50,PAL_TITLE); gfx_line(281,50,287,44,PAL_TITLE);
    gfx_text8(34,68,why,PAL_CON_TINT,1,1);
    gfx_text8(34,84,"EIP",PAL_TITLE,1,1); fault_hex(70,84,f->eip);
    gfx_text8(154,84,"VEC",PAL_TITLE,1,1); fault_hex(190,84,f->vector);
    gfx_text8(34,98,"ERR",PAL_TITLE,1,1); fault_hex(70,98,f->error);
    gfx_text8(154,98,"MEM",PAL_TITLE,1,1); fault_hex(190,98,f->address);
    gfx_text8(34,119,TASK(pid).name,PAL_TITLE,1,1);
    gfx_text8(34,137,"CLOSE CARD TO END PROGRAM",PAL_CON_TINT,1,1);
}

/* ---------------- 合成 ---------------- */
void wm_compose(void)
{
    u32 started=sc_ticks;
    relative_active();
    terminal_refresh_all(); /* 同一合成保护内重排历史，不与三环输出交叉 */
    if(!dirty && cursor_saved && GFX_W!=320) {
        /* 纯指针移动不重画照片/窗口。先恢复旧位置，再捕获新位置，
         * 两个区域可相交；最后同时提交，不能把旧光标存进新底图。 */
        int old_x=cursor_x,old_y=cursor_y,old_w=cursor_w,old_h=cursor_h;
        cursor_restore();
        if(!relative_active()){cursor_capture(mouse_x(),mouse_y());draw_cursor_at(cursor_x,cursor_y);}
        else cursor_saved=0;
        gfx_swap_rect(old_x,old_y,old_w,old_h);
        gfx_swap_rect(cursor_x,cursor_y,cursor_w,cursor_h);
    }else {
        int partial=dirty && !damage_full && GFX_W!=320;
        int old_x=cursor_x,old_y=cursor_y,old_w=cursor_w,old_h=cursor_h;
        int had_cursor=cursor_saved;
        if(partial){
            /* 后台的旧箭头不属于场景：先恢复其原底图，避免重画
             * 半透明窗口时把箭头颜色混入窗口。写视口只作用场景，
             * 光标恢复/捕获与最后两个设备矩形提交必须是完整的。 */
            if(had_cursor)cursor_restore();
            gfx_clip(damage_x0,damage_y0,damage_x1-damage_x0,damage_y1-damage_y0);
        }
        if(GFX_W==320 || !desktop_wallpaper())scene_paint();
        draw_icons();
        for(int i=0;i<nwins;i++)draw_window(i);
        draw_taskbar();native_draw_overlays();draw_fault();
        gfx_clip_reset();
        if(!relative_active()){cursor_capture(mouse_x(),mouse_y());draw_cursor_at(cursor_x,cursor_y);}
        else cursor_saved=0;
        if(partial){
            gfx_swap_rect(damage_x0,damage_y0,damage_x1-damage_x0,damage_y1-damage_y0);
            if(had_cursor)gfx_swap_rect(old_x,old_y,old_w,old_h);
            gfx_swap_rect(cursor_x,cursor_y,cursor_w,cursor_h);
            wm_partial_frames++;
            wm_partial_pixels+=(u32)(damage_x1-damage_x0)*(u32)(damage_y1-damage_y0);
        }else gfx_swap();
    }
    wm_cursor_x=cursor_x;wm_cursor_y=cursor_y;
    u32 elapsed=sc_ticks-started;
    wm_frames++;wm_ticks_total+=elapsed;if(elapsed>wm_ticks_max)wm_ticks_max=elapsed;
    dirty = 0;
    damage_full=0;
    cursor_dirty=0;
}

/* ---------------- 鼠标交互 ---------------- */
static int hit_window(i32 x, i32 y)
{
    for (int i = nwins - 1; i >= 0; i--)
        if (gui_can_show(window_record(WINDOW(i).handle)) && x >= WINDOW(i).x && x < WINDOW(i).x + WINDOW(i).w
         && y >= WINDOW(i).y && y < WINDOW(i).y + WINDOW(i).h)
            return i;
    return -1;
}

static void wm_mouse_event(i32 x,i32 y,u8 b)
{
    /* 上一个包属于当前桌面，切换时重新同步且丢弃旧按钮沿。 */
#define pmx (active_view->mouse_previous_x)
#define pmy (active_view->mouse_previous_y)
#define pbtn (active_view->mouse_previous_buttons)

    if (x == pmx && y == pmy && b == pbtn)
        return;
    int focused=top_visible();if(focused>=0)task_notify(WINDOW(focused).owner,TASK_EVENT_INPUT);

    /* 暂停卡片必须优先于新桌面命中。即使程序占满大屏幕，它也不能
     * 抢走关闭故障任务的入口；仅VGA兼容卡片保留固定坐标。 */
    if ((b&1) && !(pbtn&1)) {
        if(fault_click(x,y)) {
            pmx=x;pmy=y;pbtn=b;wm_request_compose();return;
        }
        fault_focus=0;
    }
    if(relative_active()){
        relative_pressed|=b&~pbtn;relative_released|=pbtn&~b;
        window_extra_t *e=extra_of(relative_handle);
        if(e){e->pressed|=b&~pbtn;e->released|=pbtn&~b;}
        pmx=x;pmy=y;pbtn=b;return;
    }
    if(terminal_mouse(x,y,b,pbtn)){cursor_dirty=1;pmx=x;pmy=y;pbtn=b;return;}
    int old_drag=drag,old_resize=resizing;
    int old_drag_x=old_drag>=0?WINDOW(old_drag).x:0,old_drag_y=old_drag>=0?WINDOW(old_drag).y:0;
    int old_focus_handle=focused>=0?WINDOW(focused).handle:0;
    /* 只有标题按钮的hover属于合成器，其余客户区hover由应用重绘。
     * 因而光标在空桌面/正文移动不制造整屏dirty；跨标题按钮仍更新
     * 已设计的悬停颜色，点击/拖框/菜单/拖动仍走完整场景更新。 */
    u64 old_hover=active_view->title_hover,new_hover=0;
    int hover_index=hit_window(x,y);
    int gui_client=0;
    if(hover_index>=0) {
        win_t *w=&WINDOW(hover_index);
        window_record_t *p=window_record(w->handle);int controls=p->gui.mode && w->w<unit(180)?1:3;
        /* native_mouse可能置顶/关闭并重排order；客户区资格必须在
         * 处理前按稳定对象取值，不能用旧数组下标判断后来的窗口。 */
        gui_client=p->gui.mode && x>w->x && x<w->x+w->w-1
            && y>=w->y+TITLE_H && y<w->y+w->h-1;
        int box=unit(24),left=w->x+w->w-controls*box;
        if((!p->gui.mode || gui_eligible(p)) && y>=w->y && y<w->y+TITLE_H && x>=left && x<w->x+w->w)
            new_hover=((u64)(u32)w->handle<<2)|(u32)((x-left)/box+1);
    }
    active_view->title_hover=new_hover;
    if(native_mouse(x,y,b,pbtn)) {
        int new_focus=top_visible();
        int no_chrome_change=gui_client && new_focus>=0 && WINDOW(new_focus).handle==old_focus_handle
            && old_drag<0 && drag<0 && old_resize<0 && resizing<0 && !menu_open && !context_open && old_hover==new_hover;
        if(GFX_W!=320 && b==pbtn && drag>=0 && drag==old_drag && old_resize<0
           && resizing<0 && !menu_open && !context_open && old_hover==new_hover){
            win_t *w=&WINDOW(drag);int pad=unit(8);
            /* 拖动不是只更新新位置：旧暴露区、圆角与两层投影必须
             * 一起恢复。按并集重画原层级，后方窗口/透明前景仍正确。 */
            wm_damage_rect(old_drag_x-pad,old_drag_y-pad,w->w+2*pad,w->h+2*pad);
            wm_damage_rect(w->x-pad,w->y-pad,w->w+2*pad,w->h+2*pad);
        }else if(!no_chrome_change && (GFX_W==320 || b!=pbtn || drag>=0 || old_drag>=0 || resizing>=0 || old_resize>=0
           || menu_open || context_open || old_hover!=new_hover))wm_request_compose();
        if(drag>=0)gui_commit(&WINDOW(drag));gui_focus_update();
        cursor_dirty=1;pmx=x;pmy=y;pbtn=b;return;
    }

    if ((b & 1) && !(pbtn & 1)) {     /* 左键按下沿 */
        if(fault_click(x,y)) {
            pmx=x; pmy=y; pbtn=b; wm_request_compose(); return;
        }
        fault_focus=0;
        if(y>=184 && taskbar_click(x)) {
            drag=-1; pmx=x; pmy=y; pbtn=b; wm_request_compose(); return;
        }
        int w = hit_window(x, y);
        /* 图标命中? (桌面层, 优先级最低但在最上面画 —— 这里先查图标) */
        for (int i = 0; i < 4; i++) {
            const desk_item_t *item=desktop_item(0,i);if(w>=0 || !item)continue;
            i32 ix = 8, iy = 8 + i * 44;
            if (x >= ix && x < ix + 24 && y >= iy && y < iy + 18) {
                task_exec_scx(item->command);
                pmx = x; pmy = y; pbtn = b;
                wm_request_compose();
                return;
            }
        }
        if (w >= 0) {
            win_raise(w);
            w = nwins - 1;
            if (x >= WINDOW(w).x + WINDOW(w).w - 12 && x < WINDOW(w).x + WINDOW(w).w - 2
             && y >= WINDOW(w).y + 2 && y < WINDOW(w).y + 12) {
                gui_close_request(&WINDOW(w));
                drag = -1;
            } else if (y < WINDOW(w).y + TITLE_H) {
                drag = nwins - 1;
                drag_ox = x - WINDOW(drag).x;
                drag_oy = y - WINDOW(drag).y;
            }
        }
    }
    if (!(b & 1) && (pbtn & 1))
        drag = -1;

    if (drag >= 0 && (b & 1)) {
        i32 nx = x - drag_ox, ny = y - drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > GFX_W - WINDOW(drag).w) nx = GFX_W - WINDOW(drag).w;
        if (ny > work_bottom() - WINDOW(drag).h) ny = work_bottom() - WINDOW(drag).h;
        WINDOW(drag).x = nx;
        WINDOW(drag).y = ny;
    }

    pmx = x; pmy = y; pbtn = b;
    if(drag>=0)gui_commit(&WINDOW(drag));gui_focus_update();
    wm_request_compose();
}

void wm_mouse_poll(void)
{
    /* 调用方IF=0，依次交给现有命中/拖拽路径。两个边沿即便同在
     * 一次绘图期间到达也不能只剩最后状态；输入处理后才绘下一帧。
     * 同按钮连续移动由驱动合并，始终保留最后实际坐标。 */
    i32 x,y;u8 buttons;
    while(mouse_next_event(&x,&y,&buttons))wm_mouse_event(x,y,buttons);
}

/* ---------------- 键盘路由 ----------------
 * keyboard.c 的队列由这里消费: 按键进"聚焦窗口"(最顶) 的队列 */
int wm_key_input(char c)
{
    if((u8)c==0x8C){session_t *s=session_get(session_active());
        if(s && s->kind==SESSION_LOCKED)(void)session_show_login();
        else (void)task_exec_scx("APPS/SESSION.SCX");return 1;}
    if((u8)c==27)relative_release();
    if(native_menu_key((u8)c))return 1;
    if(fault_focus && fault_top()) { if(c==27) task_stop(fault_top()); return 1; }
    if (!nwins) return 1;
    int top=top_visible();if(top<0)return 1;
    win_t *w = &WINDOW(top);
    if (!nwins || !w->used || TASK(w->owner).state!=1)
        return 1;
    /* 新窗口只收真实raw消息；启用raw前留下的兼容字符也不堆进它
     * 永不读取的15槽旧队列，更不能用字符去猜一个不存在的KEYUP。 */
    if(window_record(w->handle)->gui.mode)return 1;
    /* 用户继续键入时回到末尾；CLI后台输出本身不抢走手动滚动位置。
     * Up/Down仍交给Shell召回历史，不把原方向键暗改成终端滚动。 */
    terminal_t *term=terminal_of(w->handle);
    if(term && !term->follow){term->follow=1;term->width=0;wm_request_compose();}
    u8 n = (u8)((w->kq + 1) % 16);
    /* 一帧后驱动可能积累十几字。不能把128槽一次倾进15有效槽
     * 然后丢尾部，否则Shell/SCCC命令再次出现截断。满时让主循环
     * 保留驱动队首，先恢复抢占让应用消费，下一轮按原顺序重试。
     * 不扩大win_t，继续保持80B ABI与诊断布局。 */
    if(n==w->kt)return 0;
    w->keys[w->kq]=(u8)c;w->kq=n;
    task_notify(w->owner,TASK_EVENT_INPUT);
    streams_terminal_input(w->owner,w->handle);
    return 1;
}

static int win_popkey(win_t *w)
{
    if (w->kq == w->kt)
        return -1;
    u8 c = w->keys[w->kt];
    w->kt = (u8)((w->kt + 1) % 16);
    return c;
}

int wm_user_getkey(int pid)
{
    if (!wm_focused(pid))
        return -1;
    return win_popkey(&WINDOW(top_visible()));
}
int wm_user_peekkey(int pid)
{
    if(!wm_focused(pid))return -1;
    win_t *w=&WINDOW(top_visible());return w->kq==w->kt?-1:(int)w->keys[w->kt];
}

/* ---------------- 初始化 ---------------- */
void wm_init(void)
{
    if(objpool_init(&window_pool,sizeof(window_record_t),0) || objpool_init(&capture_pool,sizeof(capture_t),0)
        || objpool_init(&view_pool,sizeof(desktop_view_t),0))panic("WINDOW MEMORY",0);
    if(wm_session_prepare(session_active()))panic("DESKTOP MEMORY",0);
    active_view=view_get(session_active());
    gui_session_id=session_active();
    if(sc32_gui_init())panic("GUI MEMORY",0);
    nwins = 0;
    next_handle = 1;
    taskbar_first=0;
    desktop_reload();                 /* 配置菜单和多彩快捷方式，失败保留可用桌面 */
    /* 初始模式已在会话prepare按该身份读取，不再从公共SYS覆盖个人快照。 */
    wm_request_compose();
    if(session_get(session_active())->uid<-1)(void)session_show_login();
}

int wm_session_prepare(u32 id)
{
    if(!session_get(id) || !view_pool.stride)return -1;
    if(view_get(id))return 0;
    if(objpool_claim(&view_pool,(int)id))return -4;
    if(theme_session_prepare(id) || desktop_session_prepare(id)){objpool_release(&view_pool,(int)id);return -4;}
    desktop_view_t *view=view_get(id);view->dragging=view->resize=view->context_win=view->context_item=-1;
    view->display_width=GFX_W;view->display_height=GFX_H;view->display_scaling=display_scale();
    view->mouse_previous_x=view->mouse_previous_y=-1;
    char path[64];u8 mode;
    session_t *s=session_get(id);
    if(session_config_path_for(id,"WALL.CFG",path)>=0 && fs_access_identity(path,s->uid,s->gid,FS_ACCESS_READ)
        && fs_read(path,&mode,1)==1 && mode>='0' && mode<='2')view->wallpaper=mode-'0';
    return 0;
}
void wm_session_switched(u32 previous,u32 current)
{
    relative_release();keyboard_session_switch();mouse_session_switch();
    wm_sc32_loss_epoch();gui_session_id=current;
    desktop_view_t *old=view_get(previous);if(old){old->dragging=old->resize=-1;old->menu=old->context=0;}
    active_view=view_get(current);drag=resizing=-1;menu_open=context_open=0;
    if(active_view->display_width!=GFX_W || active_view->display_height!=GFX_H || active_view->display_scaling!=display_scale())wm_display_changed();
    desktop_session_activate(current);
    active_view->mouse_previous_x=mouse_x();active_view->mouse_previous_y=mouse_y();active_view->mouse_previous_buttons=0;
    /* 沿两个会话的窗口链更新事件；切换不是高频输入热路径。旧键和
     * 按钮沿不能在恢复后重放，客户主题/尺寸缓存由新代数请求重画。 */
    for(int pass=0;pass<2;pass++){desktop_view_t *view=view_get(pass?current:previous);if(!view)continue;
        for(int i=0;i<view->count;i++){win_t *w=&window_record(view->order[i])->window;visibility_changed(w);
            terminal_t *t=terminal_of(w->handle);if(t)t->width=0;}}
    cursor_saved=0;cursor_dirty=1;wm_request_compose();
    gui_focus_update();
}
void wm_session_destroy(u32 id)
{
    desktop_view_t *view=view_get(id);if(!view)return;
    while(view->count)win_close_handle(view->order[view->count-1]);
    if(view->order)pframe_free_run((u32)view->order,view->order_pages);
    objpool_release(&view_pool,(int)id);
}

void wm_open_about(void)              /* 兼容保留: about 命令开中文页窗口 */
{
    /* v2: 关于页内容由应用自己画, 这里不再内置 */
}

/* ---------------- 异常红屏 (interrupts.c 调) ---------------- */
void panic_screen(const char *reason, u32 vec)
{
    gfx_fill(0, 0, GFX_W, GFX_H, PAL_PANIC);
    gfx_text8(16, 16, "== SANDCORE PANIC ==", PAL_SHADOW, 1, 1);
    gfx_text8(16, 32, reason, PAL_SHADOW, 1, 1);
    gfx_text8(16, 48, "VEC 0x", PAL_SHADOW, 1, 1);
    gfx_char8(70, 48, "0123456789ABCDEF"[(vec >> 4) & 0xF], PAL_SHADOW, 1);
    gfx_char8(78, 48, "0123456789ABCDEF"[vec & 0xF], PAL_SHADOW, 1);
    gfx_text8(16, 64, "----", PAL_SHADOW, 1, 1);
    gfx_swap();
}

/* ---------------- SC32消息窗口协调 ----------------
 * 控件/过程/应用context不进零环；这里只保存窗口事实、输入资格、
 * 代数与尚未确认的损伤。旧80B前缀和旧窗口关闭行为保留。 */
static window_record_t *gui_record(int pid,u32 handle,u32 generation,int *error)
{
    window_record_t *p=handle && handle<=0x7FFFFFFFu?window_record((int)handle):0;
    if(!p || !p->gui.mode || generation!=handle){*error=SC32_E_STALE;return 0;}
    if(!task_owned(pid) || p->window.owner!=pid || p->owner_generation!=task_generation(pid)){
        *error=SC32_E_ACCESS;return 0;
    }
    *error=SC32_OK;return p;
}
static int gui_can_show(window_record_t *p)
{return p && !p->extra.hidden && (!p->gui.mode || (!p->gui.creating && !p->gui.owner_hidden));}
static int gui_modal_target(int handle)
{
    window_record_t *p=window_record(handle);
    while(p && p->gui.mode && p->gui.modal_child){
        window_record_t *child=window_record((int)p->gui.modal_child);
        if(!child || !gui_can_show(child))break;
        p=child;
    }
    return p?p->window.handle:handle;
}
static int gui_eligible(window_record_t *p)
{
    return p && gui_can_show(p) && p->session==session_active() && TASK(p->window.owner).state==1
        && (!p->gui.mode || (!(p->gui.style&SC32_STYLE_DISABLED) && !p->gui.owner_disabled
            && gui_modal_target(p->window.handle)==p->window.handle));
}
static void gui_attention(window_record_t *p)
{
    gui_window_t *g=&p->gui;
    if(TASK(p->window.owner).state==2)g->paint_ready=g->close_ready=0;
    int ready=g->paint_ready || g->close_ready;
    if(ready && !g->attention_linked){
        u32 head=sc32_gui_attention_head(p->window.owner);g->attention_next=head;g->attention_previous=0;
        if(head && window_record((int)head))window_record((int)head)->gui.attention_previous=(u32)p->window.handle;
        sc32_gui_attention_set(p->window.owner,(u32)p->window.handle);g->attention_linked=1;
    }else if(!ready && g->attention_linked){
        window_record_t *before=window_record((int)g->attention_previous),*after=window_record((int)g->attention_next);
        if(before)before->gui.attention_next=g->attention_next;
        else sc32_gui_attention_set(p->window.owner,g->attention_next);
        if(after)after->gui.attention_previous=g->attention_previous;
        g->attention_previous=g->attention_next=g->attention_linked=0;
    }
}
static void gui_message(window_record_t *p,u32 id,u32 epoch,u32 a,u32 b,u32 c,u32 d)
{
    if(!p || !p->gui.mode)return;
    ScMessage message={0};
    message.id=id;message.window=(u32)p->window.handle;message.generation=message.window;
    message.epoch=epoch;message.data[0]=a;message.data[1]=b;message.data[2]=c;message.data[3]=d;
    sc32_gui_emit(p->window.owner,&message);
}
static void gui_input_message(window_record_t *p,ScMessage *message,u32 tick)
{
    /* sequence属于投递顺序，输入tick仍是ISR保存的真实产生时间。
     * 即使低字恰好为0，也不能被稍后的pump时间覆盖。 */
    sc32_gui_stamp(p->window.owner,message);message->tick=tick;
    sc32_gui_emit(p->window.owner,message);
}
static void gui_capture_release(u32 reason)
{
    window_record_t *p=window_record(gui_capture_handle);if(!p){gui_capture_handle=0;return;}
    gui_capture_handle=0;p->gui.input++;
    gui_message(p,SC32_MSG_CAPTURELOST,p->gui.input,reason,0,0,0);
}
static void gui_focus_update(void)
{
    if(!active_view || gui_mutation || gui_focus_defer)return;
    int handle=0,top=top_visible();
    if(top>=0 && !fault_focus && !menu_open && !context_open && gui_eligible(window_record(WINDOW(top).handle)))handle=WINDOW(top).handle;
    if(handle==gui_focus_any)return;
    window_record_t *old=window_record(gui_focus_any),*next=window_record(handle);
    if(gui_capture_handle && gui_capture_handle!=handle)gui_capture_release(1);
    if(relative_handle && relative_handle!=handle)relative_release();
    gui_focus_any=handle;
    if(!sc32_gui_routing())wm_sc32_loss_epoch();
    if(old && old->gui.mode){old->gui.input++;gui_message(old,SC32_MSG_FOCUS,old->gui.input,0,(u32)handle,0,0);}
    if(next && next->gui.mode){next->gui.input++;gui_message(next,SC32_MSG_FOCUS,next->gui.input,1,old?(u32)old->window.handle:0,0,0);}
}
u32 wm_sc32_route_epoch(void){return gui_route_epoch;}
u32 wm_sc32_session_id(void){return gui_session_id;}
void wm_sc32_loss_epoch(void){if(++gui_route_epoch==0)gui_route_epoch=1;}
int wm_sc32_focus_pid(void)
{window_record_t *p=window_record(gui_focus_any);return p && p->gui.mode?p->window.owner:0;}
static void gui_link_parent(window_record_t *p,window_record_t *parent)
{
    gui_window_t *g=&p->gui;g->parent=parent?(u32)parent->window.handle:0;
    if(parent){g->next_child=parent->gui.first_child;
        if(g->next_child)window_record((int)g->next_child)->gui.previous_child=(u32)p->window.handle;
        parent->gui.first_child=(u32)p->window.handle;
        if(g->style&SC32_STYLE_MODAL)parent->gui.modal_child=(u32)p->window.handle;
        g->owner_hidden=parent->gui.owner_hidden || parent->extra.hidden || parent->gui.creating;
        g->owner_disabled=parent->gui.owner_disabled || (parent->gui.style&SC32_STYLE_DISABLED);
    }
}
static void gui_unlink_parent(window_record_t *p)
{
    gui_window_t *g=&p->gui;window_record_t *parent=window_record((int)g->parent);
    window_record_t *before=window_record((int)g->previous_child),*after=window_record((int)g->next_child);
    if(before)before->gui.next_child=g->next_child;else if(parent)parent->gui.first_child=g->next_child;
    if(after)after->gui.previous_child=g->previous_child;
    if(parent && parent->gui.modal_child==(u32)p->window.handle)parent->gui.modal_child=0;
    g->parent=g->previous_child=g->next_child=0;
}
static u32 gui_next_descendant(u32 root,u32 current)
{
    window_record_t *p=window_record((int)current);if(!p)return 0;
    if(p->gui.first_child)return p->gui.first_child;
    while(current!=root){
        if(p->gui.next_child)return p->gui.next_child;
        current=p->gui.parent;p=window_record((int)current);if(!p)return 0;
    }
    return 0;
}
static void gui_descendant_visibility(window_record_t *root)
{
    gui_focus_defer++;
    u32 handle=gui_next_descendant((u32)root->window.handle,(u32)root->window.handle);
    while(handle){window_record_t *p=window_record((int)handle),*parent=window_record((int)p->gui.parent);
        u32 hidden=p->gui.owner_hidden,disabled=p->gui.owner_disabled;
        p->gui.owner_hidden=parent->gui.owner_hidden || parent->extra.hidden || parent->gui.creating;
        p->gui.owner_disabled=parent->gui.owner_disabled || (parent->gui.style&SC32_STYLE_DISABLED);
        /* 祖先变化是真实可见性/输入资格变化，同样撤top缓存。否则
         * 已隐藏孩子仍可能成为缓存中的唯一top，剩余可见窗无法聚焦。 */
        if(hidden!=p->gui.owner_hidden || disabled!=p->gui.owner_disabled)visibility_changed(&p->window);
        else gui_commit(&p->window);
        handle=gui_next_descendant((u32)root->window.handle,handle);}
    gui_focus_defer--;gui_focus_update();
}
int wm_sc32_owner_has_windows(int pid)
{
    if(!task_owned(pid))return 0;
    for(u32 h=task_record(pid)->windows;h;h=window_record((int)h)->owner_next)
        if(window_record((int)h)->gui.mode)return 1;
    return 0;
}
u32 wm_sc32_change_count(int pid,u32 handle,u32 generation)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return 0;
    u32 count=1,child=gui_next_descendant(handle,handle);
    while(child){count++;child=gui_next_descendant(handle,child);}return count;
}
static void gui_commit(win_t *w)
{
    window_record_t *p=window_record(w->handle);
    if(!p || !p->gui.mode || gui_mutation || TASK(w->owner).state==2)return;
    gui_window_t *g=&p->gui;int visible=window_visible(w);
    if(g->last_cw!=w->cw || g->last_ch!=w->ch || g->last_w!=w->w || g->last_h!=w->h){
        g->geometry++;if(!g->geometry)g->geometry=1;g->painting=0;g->input++;
        if(!sc32_gui_routing())wm_sc32_loss_epoch();
        /* 新尺寸是全新的客户坐标域。旧大矩形不能并进缩小后的损伤，
         * 旧PAINT顺序也不能排在已发布的新SIZE之前。 */
        g->paint_pending=g->paint_ready=0;gui_attention(p);
        gui_message(p,SC32_MSG_SIZE,g->geometry,(u32)w->cw,(u32)w->ch,(u32)w->w,(u32)w->h);
        g->last_w=w->w;g->last_h=w->h;g->last_cw=w->cw;g->last_ch=w->ch;
        Sc32Invalidate input={36,1,(u32)w->handle,(u32)w->handle,0,0,(u32)w->cw,(u32)w->ch,0};
        (void)wm_sc32_invalidate(w->owner,&input);
    }
    if(g->last_x!=w->x || g->last_y!=w->y){
        g->input++;gui_message(p,SC32_MSG_MOVE,g->geometry,(u32)w->x,(u32)w->y,0,0);
        g->last_x=w->x;g->last_y=w->y;if(!sc32_gui_routing())wm_sc32_loss_epoch();
    }
    if(g->last_visible!=visible){
        g->paint_ready=g->painting=0;gui_attention(p);
        if(!sc32_gui_routing())wm_sc32_loss_epoch();
        g->input++;gui_message(p,SC32_MSG_VISIBILITY,p->visibility_epoch,(u32)visible,(u32)p->extra.hidden,0,0);
        g->last_visible=visible;
        if(visible)g->minimized=0;
        if(!visible && gui_capture_handle==w->handle)gui_capture_release(2);
        if(visible){g->paint_pending=0;Sc32Invalidate input={36,1,(u32)w->handle,(u32)w->handle,0,0,(u32)w->cw,(u32)w->ch,0};
            (void)wm_sc32_invalidate(w->owner,&input);}
        gui_attention(p);
    }
}
static void gui_closed(window_record_t *p)
{
    if(!p->gui.mode)return;
    if(!sc32_gui_routing() && window_visible(&p->window))wm_sc32_loss_epoch();
    if(gui_focus_any==p->window.handle){p->gui.input++;gui_message(p,SC32_MSG_FOCUS,p->gui.input,0,0,0,0);gui_focus_any=0;}
    if(gui_capture_handle==p->window.handle)gui_capture_release(3);
    p->gui.close_ready=p->gui.paint_ready=0;gui_attention(p);
    gui_unlink_parent(p);
    /* 旧WINCLOSE也可能关闭新窗。剩余owned子窗解除失效owner引用，
     * 新Destroy主动按子树关闭，EXIT沿既有owner链收齐，不递归占栈。 */
    while(p->gui.first_child){window_record_t *child=window_record((int)p->gui.first_child);
        gui_unlink_parent(child);child->gui.owner_hidden=child->gui.owner_disabled=0;
        visibility_changed(&child->window);gui_descendant_visibility(child);}
    gui_message(p,SC32_MSG_DESTROY,p->gui.input,0,0,0,0);
}
static void gui_close_request(win_t *w)
{
    window_record_t *p=window_record(w->handle);
    if(!p || !p->gui.mode){task_stop(w->owner);return;}
    if(!(p->gui.style&SC32_STYLE_CLOSE) || p->gui.creating || p->gui.close_pending)return;
    p->gui.close_pending=p->gui.close_ready=1;ScMessage *m=&p->gui.close_message;*m=(ScMessage){0};
    m->id=SC32_MSG_CLOSE;m->window=(u32)w->handle;m->generation=m->window;m->epoch=p->gui.input;
    sc32_gui_stamp(w->owner,m);gui_attention(p);sc32_gui_notify(w->owner);
}
int wm_sc32_create(int pid,const Sc32WindowCreate *input,const char *title,Sc32WindowRef *out)
{
    if(input->style&~127u || input->client_width<94 || input->client_height<40 || input->client_width>1920
        || input->client_height>1080 || (!input->owner_window && input->owner_generation)
        || ((input->style&SC32_STYLE_MODAL) && !input->owner_window))return SC32_E_INVALID;
    window_record_t *parent=0;int error;
    if(input->owner_window){parent=gui_record(pid,input->owner_window,input->owner_generation,&error);if(!parent)return error;
        if((input->style&SC32_STYLE_MODAL) && parent->gui.modal_child)return SC32_E_BUSY;}
    int width=(int)input->client_width+2,height=(int)input->client_height+TITLE_H+1;
    if(width>GFX_W)width=GFX_W;
    if(height>work_bottom())height=work_bottom();
    int x=input->x,y=input->y;
    if(x<0)x=0;
    if(y<0)y=0;
    if(x>GFX_W-width)x=GFX_W-width;
    if(y>work_bottom()-height)y=work_bottom()-height;
    int handle=win_create(pid,title,x,y,width,height,-2);if(handle<0)return SC32_E_MEMORY;
    window_record_t *p=window_record(handle);gui_window_t *g=&p->gui;
    g->mode=g->creating=g->geometry=g->input=g->theme=g->display=1;g->style=input->style;
    g->last_x=p->window.x;g->last_y=p->window.y;g->last_w=p->window.w;g->last_h=p->window.h;
    g->last_cw=p->window.cw;g->last_ch=p->window.ch;
    for(u32 i=0;title[i] && i<127;i++)g->title[i]=title[i];
    gui_link_parent(p,parent);task_record(pid)->visibility_aware=1;
    *out=(Sc32WindowRef){16,1,(u32)handle,(u32)handle};return SC32_OK;
}
int wm_sc32_activate(int pid,u32 handle,u32 generation)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    if(!p->gui.creating)return SC32_E_BUSY;
    gui_focus_defer++;
    p->gui.creating=0;p->extra.hidden=(p->gui.style&SC32_STYLE_HIDDEN)!=0;
    visibility_changed(&p->window);gui_descendant_visibility(p);
    if(window_visible(&p->window) && !(p->gui.style&SC32_STYLE_DISABLED))win_raise(p->z_index);
    gui_focus_defer--;gui_focus_update();return SC32_OK;
}
int wm_sc32_destroy(int pid,u32 handle,u32 generation)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    gui_mutation++;
    for(;;){
        while(p->gui.first_child)p=window_record((int)p->gui.first_child);
        u32 parent=p->gui.parent;int last=p->window.handle==(int)handle;
        /* 关闭叶后回到刚保存的父边，每条owner边至多走上下两次。
         * 深对话框链不每叶从根重扫，也不递归耗尽内核栈。 */
        win_close_handle(p->window.handle);if(last)break;
        p=window_record((int)parent);
        /* 兼容旧终端附着作业的关闭回调也可能关闭父窗。正常路径
         * 沿父边，父已不存在才从仍存活的根继续收齐剩余候选。 */
        if(!p){p=window_record((int)handle);if(!p)break;}
    }
    gui_mutation--;gui_focus_update();return SC32_OK;
}
int wm_sc32_query(int pid,u32 handle,u32 generation,ScWindowInfo *out)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    *out=(ScWindowInfo){0};out->size=96;out->version=1;out->window=handle;out->generation=handle;
    out->owner_pid=(u32)pid;out->owner_generation=p->owner_generation;out->session=p->session;
    out->x=p->window.x;out->y=p->window.y;out->width=p->window.w;out->height=p->window.h;
    out->client_width=(u32)p->window.cw;out->client_height=(u32)p->window.ch;out->format=(u32)p->extra.format;
    out->state=(window_visible(&p->window)?SC32_WINDOW_VISIBLE:0)|(gui_focus_any==(int)handle?SC32_WINDOW_FOCUSED:0)
        |(p->gui.minimized?SC32_WINDOW_MINIMIZED:0)|(p->extra.maximized?SC32_WINDOW_MAXIMIZED:0)
        |((p->gui.style&SC32_STYLE_MODAL)?SC32_WINDOW_MODAL:0)|(gui_eligible(p)?SC32_WINDOW_INPUT:0)
        |(p->gui.close_pending?SC32_WINDOW_CLOSING:0)|(p->gui.paint_pending?SC32_WINDOW_PAINT:0)
        |(p->gui.creating?SC32_WINDOW_CREATING:0)|(gui_capture_handle==(int)handle?SC32_WINDOW_CAPTURED:0);
    out->geometry_epoch=p->gui.geometry;out->visibility_epoch=p->visibility_epoch;out->input_epoch=p->gui.input;
    out->theme_epoch=p->gui.theme;out->display_epoch=p->gui.display;return SC32_OK;
}
int wm_sc32_place(int pid,const Sc32WindowPlace *input,ScWindowInfo *out)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(p->gui.creating)return SC32_E_BUSY;
    if(input->width<94 || input->height<40 || input->width>1920 || input->height>1080)return SC32_E_INVALID;
    gui_mutation++;int result=wm_user_place(pid,(int)input->window,input->x,input->y,(int)input->width,(int)input->height);gui_mutation--;
    if(result)return SC32_E_MEMORY;
    gui_commit(&p->window);gui_descendant_visibility(p);gui_focus_update();
    return wm_sc32_query(pid,input->window,input->generation,out);
}
int wm_sc32_control(int pid,const Sc32WindowAction *input,ScWindowInfo *out)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(p->gui.creating)return SC32_E_BUSY;
    if(input->action>SC32_WINDOW_FOCUS)return SC32_E_INVALID;
    if(input->action==SC32_WINDOW_FOCUS && (!gui_eligible(p) || p->session!=session_active()))return SC32_E_ACCESS;
    gui_mutation++;int result=0;
    if(input->action==SC32_WINDOW_HIDE)p->extra.hidden=1;
    else if(input->action==SC32_WINDOW_SHOW)p->extra.hidden=0;
    else if(input->action==SC32_WINDOW_MINIMIZE){p->gui.minimized=1;result=wm_window_control(pid,(int)input->window,0);}
    else if(input->action==SC32_WINDOW_MAXIMIZE)result=wm_window_control(pid,(int)input->window,1);
    else if(input->action==SC32_WINDOW_RESTORE)result=wm_window_control(pid,(int)input->window,2);
    else if(gui_eligible(p) && p->session==session_active())win_raise(p->z_index);else result=-1;
    gui_mutation--;if(result)return SC32_E_MEMORY;
    if(input->action==SC32_WINDOW_SHOW || input->action==SC32_WINDOW_HIDE || input->action==SC32_WINDOW_MAXIMIZE)p->gui.minimized=0;
    gui_focus_defer++;visibility_changed(&p->window);gui_descendant_visibility(p);
    gui_focus_defer--;gui_focus_update();
    return wm_sc32_query(pid,input->window,input->generation,out);
}
int wm_sc32_owner(int pid,const Sc32WindowOwner *input,ScWindowInfo *out)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(input->style&~127u || (!input->owner_window && input->owner_generation)
        || ((input->style&SC32_STYLE_MODAL) && !input->owner_window))return SC32_E_INVALID;
    window_record_t *parent=0;
    if(input->owner_window){parent=gui_record(pid,input->owner_window,input->owner_generation,&error);if(!parent)return error;
        for(window_record_t *q=parent;q;q=window_record((int)q->gui.parent))if(q==p)return SC32_E_INVALID;
        if((input->style&SC32_STYLE_MODAL) && parent->gui.modal_child && parent->gui.modal_child!=input->window)return SC32_E_BUSY;}
    gui_focus_defer++;
    gui_unlink_parent(p);p->gui.owner_hidden=p->gui.owner_disabled=0;p->gui.style=input->style;gui_link_parent(p,parent);
    visibility_changed(&p->window);gui_descendant_visibility(p);wm_request_compose();
    if((p->gui.style&SC32_STYLE_MODAL) && gui_eligible(p))win_raise(p->z_index);
    gui_focus_defer--;gui_focus_update();
    return wm_sc32_query(pid,input->window,input->generation,out);
}
int wm_sc32_title(int pid,u32 handle,u32 generation,const char *title)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    u32 i=0;while(title[i]){p->gui.title[i]=title[i];i++;}p->gui.title[i]=0;
    int n=0;const char *text=title;
    while(*text){const char *first=text;u32 scalar;gfx_utf8_next(&text,&scalar);
        if(n+(int)(text-first)>11)break;
        while(first<text)p->window.title[n++]=*first++;
    }
    p->window.title[n]=0;window_changed(&p->window);return SC32_OK;
}
int wm_sc32_capture(int pid,u32 handle,u32 generation,u32 enabled)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    if(enabled>1)return SC32_E_INVALID;
    if(!enabled){if(gui_capture_handle==(int)handle)gui_capture_release(6);return SC32_OK;}
    if(!gui_eligible(p) || gui_focus_any!=(int)handle || relative_active())return SC32_E_ACCESS;
    if(gui_capture_handle!=(int)handle){gui_capture_release(6);gui_capture_handle=(int)handle;p->gui.input++;}
    return SC32_OK;
}
int wm_sc32_input(int pid,u32 handle,u32 generation,ScInputSnapshot *out)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    *out=(ScInputSnapshot){0};out->size=96;out->version=1;out->window=handle;out->generation=handle;
    out->input_epoch=p->gui.input;out->visible=window_visible(&p->window);
    out->focused=gui_focus_any==(int)handle && gui_eligible(p);out->captured=gui_capture_handle==(int)handle;
    if(out->focused){out->modifiers=keyboard_modifiers();out->x=mouse_x()-p->window.x-1;out->y=mouse_y()-p->window.y-TITLE_H;
        out->buttons=mouse_buttons();keyboard_physical_snapshot(out->physical_down);}
    return SC32_OK;
}
int wm_sc32_invalidate(int pid,const Sc32Invalidate *input)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(TASK(pid).state==2)return SC32_E_STALE;
    if(input->x<0 || input->y<0 || !input->width || !input->height || (u32)input->x>(u32)p->window.cw
        || (u32)input->y>(u32)p->window.ch || input->width>(u32)p->window.cw-(u32)input->x
        || input->height>(u32)p->window.ch-(u32)input->y)return SC32_E_RANGE;
    gui_window_t *g=&p->gui;int x1=input->x+(int)input->width,y1=input->y+(int)input->height;
    if(!g->paint_pending){g->paint_x0=input->x;g->paint_y0=input->y;g->paint_x1=x1;g->paint_y1=y1;}
    else{
        if(input->x<g->paint_x0)g->paint_x0=input->x;
        if(input->y<g->paint_y0)g->paint_y0=input->y;
        if(x1>g->paint_x1)g->paint_x1=x1;
        if(y1>g->paint_y1)g->paint_y1=y1;
    }
    g->paint_pending=1;if(++g->paint_epoch==0)g->paint_epoch=1;
    /* 隐藏/创建中的窗口只累计模型损伤，既不排PAINT又不唤醒绘图。
     * 恢复时以新VISIBILITY之后的完整重画重新建立投递顺序。 */
    if(!window_visible(&p->window) || g->creating){g->paint_ready=0;gui_attention(p);return SC32_OK;}
    ScMessage *message=&g->paint_message;
    if(!g->paint_ready){*message=(ScMessage){0};message->id=SC32_MSG_PAINT;message->window=input->window;
        message->generation=input->generation;sc32_gui_stamp(pid,message);g->paint_ready=1;}
    else message->flags|=SC32_MSG_COALESCED;
    message->epoch=g->geometry;message->data[0]=(u32)g->paint_x0;message->data[1]=(u32)g->paint_y0;
    message->data[2]=(u32)(g->paint_x1-g->paint_x0);message->data[3]=(u32)(g->paint_y1-g->paint_y0);
    message->data[4]=g->paint_epoch;gui_attention(p);if(window_visible(&p->window))sc32_gui_notify(pid);return SC32_OK;
}
int wm_sc32_paint_begin(int pid,u32 handle,u32 generation,ScPaintInfo *out)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    if(!window_visible(&p->window))return SC32_E_AGAIN;
    if(p->gui.painting)return SC32_E_BUSY;
    if(!p->gui.paint_pending)return SC32_E_AGAIN;
    p->gui.painting=p->gui.paint_epoch;p->gui.painting_geometry=p->gui.geometry;
    p->gui.paint_ready=0;gui_attention(p);
    *out=(ScPaintInfo){0};out->size=64;out->version=1;out->window=handle;out->generation=handle;
    out->geometry_epoch=p->gui.geometry;out->paint_epoch=p->gui.painting;
    out->x=p->gui.paint_x0;out->y=p->gui.paint_y0;out->width=(u32)(p->gui.paint_x1-p->gui.paint_x0);
    out->height=(u32)(p->gui.paint_y1-p->gui.paint_y0);out->client_width=(u32)p->window.cw;
    out->client_height=(u32)p->window.ch;out->format=2;return SC32_OK;
}
int wm_sc32_paint_end(int pid,const Sc32PaintEnd *input)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    gui_window_t *g=&p->gui;
    if(!g->painting || input->paint_epoch!=g->painting || input->geometry_epoch!=g->geometry
        || input->geometry_epoch!=g->painting_geometry)return SC32_E_STALE;
    g->painting=0;
    if(input->paint_epoch==g->paint_epoch)g->paint_pending=g->paint_ready=0;
    else if(!g->paint_ready && window_visible(&p->window)){
        /* 回调可重入GET并先领走新PAINT，Begin仍因旧claim返回BUSY。
         * End不能因那次消费忘掉新损伤；新顺序重投仍未确认的矩形。 */
        sc32_gui_stamp(pid,&g->paint_message);g->paint_ready=1;
    }
    gui_attention(p);if(g->paint_ready)sc32_gui_notify(pid);return SC32_OK;
}
int wm_sc32_submit(int pid,const ScSubmitDesc *input,const ScSubmitRect *rectangles)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(input->geometry_epoch!=p->gui.geometry)return SC32_E_STALE;
    if(!window_visible(&p->window))return SC32_E_AGAIN;
    if(((u32)input->pixels&3u) || !input->width || !input->height || input->width>0xFFFFFFFFu/4 || input->stride<input->width*4
        || (input->stride&3u) || input->height-1>(0xFFFFFFFFu-input->width*4)/input->stride
        || (input->height-1)*input->stride+input->width*4>input->bytes)return SC32_E_RANGE;
    for(u32 i=0;i<input->rectangle_count;i++){
        const ScSubmitRect *r=rectangles+i;
        if(!r->width || !r->height || r->source_x>input->width || r->source_y>input->height
            || r->width>input->width-r->source_x || r->height>input->height-r->source_y
            || r->destination_x>(u32)p->window.cw || r->destination_y>(u32)p->window.ch
            || r->width>(u32)p->window.cw-r->destination_x || r->height>(u32)p->window.ch-r->destination_y)return SC32_E_RANGE;
    }
    /* 整源和矩形表由gate预检。重定位/尺寸/任一rect错均在第一笔
     * canvas写入前拒绝；复制期间固定任务/CR3收IRQ，末尾一次损伤
     * 发布。部分透明不会把未知旧区域误标为opaque。 */
    int changed=0,x0=p->window.cw,y0=p->window.ch,x1=0,y1=0;u32 opaque=0xFF000000u;
    task_render_hold(1);sti();
    for(u32 i=0;i<input->rectangle_count;i++){
        const ScSubmitRect *r=rectangles+i;
        for(u32 y=0;y<r->height;y++){
            const u32 *source=(const u32 *)((const u8 *)input->pixels+(r->source_y+y)*input->stride)+r->source_x;
            u32 *target=(u32 *)p->window.canvas+(r->destination_y+y)*(u32)p->window.cw+r->destination_x;
            for(u32 x=0;x<r->width;x++){u32 value=source[x];opaque&=value;
                if(target[x]!=value){target[x]=value;changed=1;
                    int dx=(int)(r->destination_x+x),dy=(int)(r->destination_y+y);
                    if(dx<x0)x0=dx;
                    if(dy<y0)y0=dy;
                    if(dx+1>x1)x1=dx+1;
                    if(dy+1>y1)y1=dy+1;
                }
            }
        }
    }
    cli();task_render_hold(0);
    if((opaque&0xFF000000u)!=0xFF000000u)p->extra.opaque=0;
    else if(input->rectangle_count==1 && !rectangles[0].destination_x && !rectangles[0].destination_y
        && rectangles[0].width==(u32)p->window.cw && rectangles[0].height==(u32)p->window.ch)p->extra.opaque=1;
    if(changed)wm_damage_rect(p->window.x+1+x0,p->window.y+TITLE_H+y0,x1-x0,y1-y0);
    return SC32_OK;
}
int wm_sc32_cancel_close(int pid,u32 handle,u32 generation)
{
    int error;window_record_t *p=gui_record(pid,handle,generation,&error);if(!p)return error;
    p->gui.close_pending=p->gui.close_ready=0;gui_attention(p);return SC32_OK;
}
int wm_sc32_pending(int pid,ScMessage *message,int consume)
{
    window_record_t *chosen=0;ScMessage *best=0;
    for(u32 h=sc32_gui_attention_head(pid);h;){window_record_t *p=window_record((int)h);if(!p)break;
        ScMessage *a=p->gui.close_ready?&p->gui.close_message:0;
        ScMessage *b=p->gui.paint_ready && window_visible(&p->window) && !p->gui.creating?&p->gui.paint_message:0;
        if(!a || (b && (b->sequence_hi<a->sequence_hi || (b->sequence_hi==a->sequence_hi && b->sequence_lo<a->sequence_lo))))a=b;
        if(a && (!best || a->sequence_hi<best->sequence_hi || (a->sequence_hi==best->sequence_hi && a->sequence_lo<best->sequence_lo))){best=a;chosen=p;}
        h=p->gui.attention_next;
    }
    if(!best)return 0;
    *message=*best;
    if(consume){if(best==&chosen->gui.close_message)chosen->gui.close_ready=0;else chosen->gui.paint_ready=0;gui_attention(chosen);}
    return 1;
}
int wm_sc32_message_valid(int pid,const ScMessage *message)
{
    if(message->id==SC32_MSG_DESTROY)return 1;
    if(!message->window)return message->id>=SC32_MSG_USER;
    int error;return gui_record(pid,message->window,message->generation,&error)!=0;
}
u32 wm_sc32_pending_count(int pid)
{
    u32 count=0;
    for(u32 h=sc32_gui_attention_head(pid);h;){window_record_t *p=window_record((int)h);if(!p)break;
        count+=(p->gui.close_ready!=0)+(p->gui.paint_ready && window_visible(&p->window) && !p->gui.creating);
        h=p->gui.attention_next;}
    return count;
}
void wm_sc32_loss_owner(int pid)
{
    window_record_t *captured=window_record(gui_capture_handle);
    if(captured && captured->window.owner==pid)gui_capture_release(5);
    if(relative_owner==pid)relative_release();
    if(!task_owned(pid))return;
    for(u32 h=task_record(pid)->windows;h;h=window_record((int)h)->owner_next){window_record_t *p=window_record((int)h);
        if(p->gui.mode){p->gui.input++;keyboard_physical_snapshot(p->gui.blocked_keys);p->gui.blocked_buttons=mouse_buttons();}}
    if(active_view){if(drag>=0 && WINDOW(drag).owner==pid)drag=-1;if(resizing>=0 && WINDOW(resizing).owner==pid)resizing=-1;
        active_view->mouse_previous_x=mouse_x();active_view->mouse_previous_y=mouse_y();active_view->mouse_previous_buttons=mouse_buttons();}
}
void wm_sc32_input_revoked(int pid,const ScMessage *message)
{
    int error;window_record_t *p=gui_record(pid,message->window,message->generation,&error);if(!p)return;
    /* 缺口尚未被应用领取时，新按下也不能在之后以repeat复活。
     * 只阻断真实按住位，真实UP在route中清位；不伪造断码或松开。 */
    if(message->id==SC32_MSG_KEYDOWN && message->data[0]<255){u32 key=message->data[0];
        p->gui.blocked_keys[key>>5]|=1u<<(key&31);}
    else if(message->id==SC32_MSG_BUTTONDOWN || message->id==SC32_MSG_MOUSEMOVE)p->gui.blocked_buttons|=message->data[2];
}
void wm_sc32_owner_stopped(int pid)
{
    /* 退出先撤唯一capture与焦点，不为永不恢复的进程逐窗复制键状态。
     * attention与消息页随后按预算清，窗口由既有owner链分步释放。 */
    window_record_t *captured=window_record(gui_capture_handle);
    if(captured && captured->window.owner==pid)gui_capture_release(3);
    if(relative_owner==pid)relative_release();
    if(active_view){if(drag>=0 && WINDOW(drag).owner==pid)drag=-1;
        if(resizing>=0 && WINDOW(resizing).owner==pid)resizing=-1;}
    gui_focus_update();
}
int wm_sc32_attention_stop(int pid)
{
    u32 handle=sc32_gui_attention_head(pid);if(!handle)return 1;
    window_record_t *p=window_record((int)handle);
    if(!p){sc32_gui_attention_set(pid,0);return 1;}
    p->gui.close_pending=p->gui.close_ready=p->gui.paint_pending=p->gui.paint_ready=p->gui.painting=0;
    gui_attention(p);return !sc32_gui_attention_head(pid);
}
int wm_sc32_route(const Sc32RawEvent *event)
{
    if(event->kind==SC32_RAW_KEY){
        gui_focus_update();window_record_t *p=window_record(gui_focus_any);
        int top=top_visible();window_record_t *top_record=top>=0?window_record(WINDOW(top).handle):0;
        if(!p && top_record && top_record->gui.mode && !menu_open && !context_open && !fault_focus && event->legacy!=0x8c)return 1;
        if(!p || !p->gui.mode || menu_open || context_open || fault_focus || event->legacy==0x8c){
            if(event->down && event->legacy){int accepted=wm_key_input((char)event->legacy);gui_focus_update();return accepted;}return 1;
        }
        u32 physical=event->physical;if(physical<256){u32 bit=1u<<(physical&31);
            if(event->down && (p->gui.blocked_keys[physical>>5]&bit))return 1;
            if(!event->down)p->gui.blocked_keys[physical>>5]&=~bit;}
        if(event->down && event->symbol==27){relative_release();gui_capture_release(4);}
        ScMessage message={0};message.id=event->down?SC32_MSG_KEYDOWN:SC32_MSG_KEYUP;
        message.window=(u32)p->window.handle;message.generation=message.window;message.epoch=p->gui.input;
        message.data[0]=physical;message.data[1]=event->symbol;message.data[2]=event->text;
        message.data[3]=event->modifiers;message.data[4]=event->repeat;
        if(event->repeat)message.flags|=SC32_MSG_REPEAT;
        if(event->flags&SC32_RAW_PULSE)message.flags|=SC32_MSG_PULSE;
        gui_input_message(p,&message,event->tick);
        if(event->down && event->text>=32){message=(ScMessage){0};message.id=SC32_MSG_TEXT;
            message.window=(u32)p->window.handle;message.generation=message.window;message.epoch=p->gui.input;
            message.data[0]=event->text;message.data[1]=event->modifiers;gui_input_message(p,&message,event->tick);}
        return 1;
    }
    if(event->kind!=SC32_RAW_MOUSE)return 1;
    u32 previous=active_view?active_view->mouse_previous_buttons:0;
    int old_x=active_view?active_view->mouse_previous_x:event->x,old_y=active_view?active_view->mouse_previous_y:event->y;
    int capture=gui_capture_handle;
    int original_index=hit_window(event->x,event->y);
    int chrome=drag>=0 || resizing>=0 || menu_open || context_open || fault_focus
        || event->y>=work_bottom() || relative_active();
    window_record_t *original=original_index>=0?window_record(WINDOW(original_index).handle):0;
    /* 系统菜单关闭或modal重定向可能让点击后命中的窗变化。按点击
     * 前的实际消费资格保留屏障，不能把系统/被阻断父窗的按钮事实
     * 再投给刚置顶或刚显现的客户区。 */
    if(original && original->gui.mode && !gui_eligible(original))chrome=1;
    if(original && (event->x<=original->window.x || event->y<original->window.y+TITLE_H
        || event->x>=original->window.x+original->window.w-1 || event->y>=original->window.y+original->window.h-1))chrome=1;
    if(!capture)wm_mouse_event(event->x,event->y,(u8)event->buttons);
    else{cursor_dirty=1;active_view->mouse_previous_x=event->x;active_view->mouse_previous_y=event->y;active_view->mouse_previous_buttons=(u8)event->buttons;}
    gui_focus_update();int index=hit_window(event->x,event->y);
    if(!capture && (chrome || drag>=0 || resizing>=0))return 1;
    int under=gui_capture_handle?gui_capture_handle:index>=0?WINDOW(index).handle:0;
    capture=gui_capture_handle;
    window_record_t *p=window_record(under);if(!p || !p->gui.mode || !gui_eligible(p) || menu_open || context_open || fault_focus)return 1;
    int x=event->x-p->window.x-1,y=event->y-p->window.y-TITLE_H;
    if(!capture && (x<0 || y<0 || x>=p->window.cw || y>=p->window.ch))return 1;
    if(old_x!=event->x || old_y!=event->y){ScMessage m={0};m.id=SC32_MSG_MOUSEMOVE;m.window=(u32)under;m.generation=m.window;m.epoch=p->gui.input;
        m.data[0]=(u32)x;m.data[1]=(u32)y;m.data[2]=event->buttons;m.data[4]=event->modifiers;
        if(event->flags&SC32_RAW_COALESCED)m.flags|=SC32_MSG_COALESCED;
        gui_input_message(p,&m,event->tick);
    }
    u32 changed=previous^event->buttons;p->gui.blocked_buttons&=event->buttons;
    for(u32 button=0;button<3;button++)if((changed&(1u<<button)) && !(p->gui.blocked_buttons&(1u<<button))){
        ScMessage m={0};m.id=event->buttons&(1u<<button)?SC32_MSG_BUTTONDOWN:SC32_MSG_BUTTONUP;
        m.window=(u32)under;m.generation=m.window;m.epoch=p->gui.input;m.data[0]=(u32)x;m.data[1]=(u32)y;
        m.data[2]=event->buttons;m.data[3]=1u<<button;m.data[4]=event->modifiers;gui_input_message(p,&m,event->tick);}
    return 1;
}
