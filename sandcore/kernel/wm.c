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
    win_t window;
    window_extra_t extra;
    terminal_t terminal;
    u32 owner_generation,owner_previous,owner_next,session,visibility_epoch;
    int repaint;
    int z_index,creation_index;
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
    int result=-1;for(int i=nwins-1;i>=0;i--){window_extra_t *e=extra_of(WINDOW(i).handle);if(e && !e->hidden){result=i;break;}}
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
    ex->format=native==2?2:1;
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
    for (u32 i = 0; i < (u32)(np->cw * np->ch); i++) {
        if(ex->format==2)((u32 *)np->canvas)[i]=0xFF000000u|theme_color(TH_FACE);
        else canvas_slot(np,i,PAL_CON_BG);
    }
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
}

static void win_raise(int idx)
{
    if(idx<0 || idx>=nwins)return;
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
{window_record_t *p=window_record(w->handle);return p && p->session==session_active() && !p->extra.hidden;}
static void window_changed(win_t *w)
{if(window_visible(w))wm_request_compose();else window_record(w->handle)->repaint=1;}
static void visibility_changed(win_t *w)
{
    window_record_t *p=window_record(w->handle);p->visibility_epoch++;if(!p->visibility_epoch)p->visibility_epoch=1;
    desktop_view_t *view=view_get(p->session);if(view)view->top_known=0;
    p->repaint=1;p->window.kq=p->window.kt=0;p->extra.pressed=p->extra.released=0;
    task_notify_generation(w->owner,p->owner_generation,TASK_EVENT_VISIBILITY);
    if(p->session==session_active())wm_request_compose();
}
void wm_session_repaint(u32 id)
{
    desktop_view_t *view=view_get(id);if(!view)return;
    for(int i=0;i<view->count;i++){
        window_record_t *p=window_record(view->order[i]);p->visibility_epoch++;
        if(!p->visibility_epoch)p->visibility_epoch=1;p->repaint=1;p->terminal.theme=0;
        task_notify_generation(p->window.owner,p->owner_generation,TASK_EVENT_VISIBILITY);
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
    window_extra_t *ex=extra_of(w->handle);if(ex->hidden)return;
    if(GFX_W!=320){native_draw_window(i);return;}
    int focused = (i == top_visible());

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
        if (!extra_of(WINDOW(i).handle)->hidden && x >= WINDOW(i).x && x < WINDOW(i).x + WINDOW(i).w
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
    /* 只有标题按钮的hover属于合成器，其余客户区hover由应用重绘。
     * 因而光标在空桌面/正文移动不制造整屏dirty；跨标题按钮仍更新
     * 已设计的悬停颜色，点击/拖框/菜单/拖动仍走完整场景更新。 */
    u64 old_hover=active_view->title_hover,new_hover=0;
    int hover_index=hit_window(x,y);
    if(hover_index>=0) {
        win_t *w=&WINDOW(hover_index);
        int box=unit(24),left=w->x+w->w-3*box;
        if(y>=w->y && y<w->y+TITLE_H && x>=left && x<w->x+w->w)new_hover=((u64)(u32)w->handle<<2)|(u32)((x-left)/box+1);
    }
    active_view->title_hover=new_hover;
    if(native_mouse(x,y,b,pbtn)) {
        if(GFX_W!=320 && b==pbtn && drag>=0 && drag==old_drag && old_resize<0
           && resizing<0 && !menu_open && !context_open && old_hover==new_hover){
            win_t *w=&WINDOW(drag);int pad=unit(8);
            /* 拖动不是只更新新位置：旧暴露区、圆角与两层投影必须
             * 一起恢复。按并集重画原层级，后方窗口/透明前景仍正确。 */
            wm_damage_rect(old_drag_x-pad,old_drag_y-pad,w->w+2*pad,w->h+2*pad);
            wm_damage_rect(w->x-pad,w->y-pad,w->w+2*pad,w->h+2*pad);
        }else if(GFX_W==320 || b!=pbtn || drag>=0 || old_drag>=0 || resizing>=0 || old_resize>=0
           || menu_open || context_open || old_hover!=new_hover)wm_request_compose();
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
                int owner = WINDOW(w).owner;
                task_stop(owner);   /* 最后一个窗口关闭意味着 GUI 任务结束 */
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
