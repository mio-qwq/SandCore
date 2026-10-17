#ifndef SANDCORE_WM_H
#define SANDCORE_WM_H

#include "io.h"
#include "../user/sc32/SC32GUIWIRE.H"
#include "sc32_raw.h"
#include "../user/SC32DROP2.H"
/* M10a1：会话桌面资源不依赖固定会话数组，核心只在切换时通知WM。 */
int wm_session_prepare(u32 id);
void wm_session_switched(u32 previous,u32 current);
void wm_session_destroy(u32 id);
void wm_session_repaint(u32 id);
int wm_user_visibility(int pid,int handle,u32 out[8]);
int wm_task_hidden_gui(int pid);
int wm_defer_draw(int pid,int handle,int format); /* 旧程序绘制调用的可见性等待。 */
#define SC_CANVAS_W 318
#define SC_CANVAS_H 170

/* 窗口管理器 / 合成器 v2（M6）—— 规范见 docs/WM.md。
 * 公共接口中的 pid 由内核分发器提供，用户不能伪造另一个 owner。
 * handle 是稳定编号，与 z 序索引无关；所有用户绘图先写独占 canvas，
 * 合成才访问 gfx 后台缓冲。关闭/置顶重排不能改变 handle/画布的关联。 */

void wm_init(void);                  /* 只建文件图标桌面，不自动开任何窗口 */
void wm_compose(void);               /* 全帧合成: 壁纸→窗口→任务栏→光标 */
void wm_mouse_poll(void);            /* 读鼠标状态, 处理拖拽/点按 */
int wm_user_mouse_capture(int pid,int handle,int enabled);
int wm_user_mouse_relative(int pid,int handle,i32 *out); /* 新8xi32快照 */
void wm_request_compose(void);       /* 标记"画面脏了", 主循环里统一合成 */
/* mio：可信零环追加物理屏幕半开损伤矩形，不修改用户画布内容。
 * 多次请求合并；任何未知范围的旧request_compose均升级全屏，
 * 不能被后来的局部请求降回局部。VGA保持旧完整合成。clock只
 * 标记任务栏；窗口更新须包含圆角投影与旧/新位置暴露区域。 */
void wm_damage_rect(int x,int y,int w,int h);
void wm_request_clock(void);
int  wm_dirty(void);
void wm_open_about(void);            /* 历史内核关于页接口，当前 SandShell 不含 about 命令 */

int  wm_open_user_window(int pid, const char *title, int w, int h);
void wm_close_user_window(int pid, int handle);
void wm_close_owner(int pid);        /* 任务退出时回收全部窗口 */
int wm_close_owner_step(int pid);
int  wm_user_puts(int pid, const char *s); /* 最近创建的窗口维护独立文字游标 */
/* M8终端只为本人ARGB窗口启用。历史按需分配4页，窗口关闭时释放；
 * 重绘按当前宽度/缩放/主题重新排版，不能把旧像素放大当清晰文字。
 * command=0启用/1清空/2重绘；拥有关系与终端状态必须同时成立。
 * stream允许可信CLI路由指定已授权的owner/handle，三环不能直接调用。 */
int wm_terminal_control(int pid,int handle,int command);
int wm_terminal_owned(int pid,int handle);
int wm_terminal_size(int pid,int handle,int *columns,int *rows);
int wm_terminal_puts(int pid,int handle,const char *text);
/* info 写五个 i32 到已校验的内核可达缓冲；只接受本人窗口。x/y 为外框
 * 屏幕坐标，客户区起点还需加 (1,13)，鼠标应用必须同时检查 focused。 */
int  wm_user_info(int pid,int handle,i32 *out);
int  wm_set_wallpaper(int mode);     /* 0 沙丘 / 1 夜色 / 2 暖沙，持久化模式号 */
int  wm_wallpaper(void);
void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color);
int wm_user_utf8(int pid,int handle,int x,int y,const char *s,u8 color);
void wm_user_fill(int pid, int handle, int x, int y, int w, int h, u8 color);
int  wm_user_getkey(int pid);
int wm_key_input(char c);            /* 1已处理，0窗口队满：驱动队首保留 */
/* 两个整帧入口均由IF=0、已验证完整用户映射的分发器调用。先
 * 查owner/格式/精确长度，再固定当前任务并STI复制；返回前CLI
 * 解除持有。IRQ只能记录输入/计时，不允许换CR3、关闭/调整窗口
 * 或合成半帧；客户端仍只看到一次原子整帧提交，不借内核指针。 */
int wm_user_frame(int pid,int handle,const u8 *pixels,u32 length);
int wm_focused(int pid);
void wm_exception(int pid,u32 vector,u32 error,u32 eip,u32 address);
void wm_display_changed(void); /* 显示模式/缩放变化：重排工作区且保留所有任务 */
int wm_open_native_window(int pid,const char *title,int w,int h);
int wm_open_rgb_window(int pid,const char *title,int w,int h);
int wm_user_frame_rgb(int pid,int handle,const u32 *pixels,u32 bytes);
int wm_user_fill_rgb(int pid,int handle,int x,int y,int w,int h,u32 rgb);
int wm_user_text_rgb(int pid,int handle,int x,int y,const char *text,u32 rgb);
int wm_window_control(int pid,int handle,int command); /* 0最小化/1最大化/2恢复 */
/* 新增本人原生窗口定位：屏幕外框x/y，客户区w/h，全部裁入工作区。 */
int wm_user_place(int pid,int handle,int x,int y,int width,int height);
int wm_user_pointer(int pid,int handle,i32 *out); /* 客户坐标/按住/按下沿/松开沿/焦点 */
int wm_user_pointer_peek(int pid,int handle,i32 *out);
int wm_user_peekkey(int pid); /* 与GETKEY同样聚焦检查，但不消费队首 */
u32 wm_legacy_mouse(int pid); /* 高分辨率下为旧画布虚拟化9位鼠标坐标 */
/* 卡片按任务分配，画布不足也能诊断；暂停保留画布，关闭卡片才结束任务。 */
void panic_screen(const char *reason, u32 vec);   /* 异常红屏 */
/* M9截图复制真实客户画布，不依赖宿主screendump。snapshot在打开时
 * 冻结像素/尺寸，窗口之后关闭、重排或调整均不影响已有快照。 */
int wm_capture_open(int pid,int handle,u32 out[8]);
int wm_capture_read(int pid,u32 token,u32 offset,void *out,u32 length);
int wm_capture_close(int pid,u32 token);
int wm_capture_list(int pid,char *out,u32 capacity);
int wm_window_page(int pid,u32 *out,u32 words,u32 after);
void wm_capture_stop(int pid);
int wm_capture_stop_step(int pid);

/* M10a1：仅内核任务存储器使用，资源按真实任务申请。 */
u32 wm_task_bytes(void);

/* SC32消息窗口与旧窗口共享合成/画布，接口只接受可信局部请求。
 * 用户缓冲完整前检由sc32_gui执行；任何函数都不执行三环过程。 */
/* Drop事实只供内核WM/Broker协作使用，字段均是值。 */
typedef struct {
    u32 window,generation,pid,pid_generation,session,geometry,visibility;
    i32 x,y;u32 width,height,captured,buttons,input;
} WmDropFact;
int wm_sc32_drop_register(int pid,u32 window,u32 generation,WmDropFact *out);
int wm_sc32_drop_window(int pid,u32 window,u32 generation,WmDropFact *out);
int wm_sc32_drop_source(int pid,u32 window,u32 generation,int at,WmDropFact *out);
int wm_sc32_drop_point(i32 x,i32 y,WmDropFact *out);
int wm_sc32_drop_point_current(i32 x,i32 y);
int wm_sc32_drop_released(int pid,const ScDropReleasePoint *request,WmDropFact *source,WmDropFact *point,u32 *registration);
void wm_sc32_drop_release_consume(u32 window);
int wm_sc32_create(int pid,const Sc32WindowCreate *input,const char *title,Sc32WindowRef *out);
int wm_sc32_popup_create(int pid,const Sc32PopupCreate *input,Sc32WindowRef *out);
int wm_sc32_cursor(int pid,u32 handle,u32 generation,u32 shape);
int wm_sc32_activate(int pid,u32 handle,u32 generation);
int wm_sc32_destroy(int pid,u32 handle,u32 generation);
int wm_sc32_query(int pid,u32 handle,u32 generation,ScWindowInfo *out);
int wm_sc32_place(int pid,const Sc32WindowPlace *input,ScWindowInfo *out);
int wm_sc32_control(int pid,const Sc32WindowAction *input,ScWindowInfo *out);
int wm_sc32_owner(int pid,const Sc32WindowOwner *input,ScWindowInfo *out);
int wm_sc32_title(int pid,u32 handle,u32 generation,const char *title);
int wm_sc32_capture(int pid,u32 handle,u32 generation,u32 enabled);
int wm_sc32_input(int pid,u32 handle,u32 generation,ScInputSnapshot *out);
int wm_sc32_invalidate(int pid,const Sc32Invalidate *input);
int wm_sc32_paint_begin(int pid,u32 handle,u32 generation,ScPaintInfo *out);
int wm_sc32_paint_end(int pid,const Sc32PaintEnd *input);
int wm_sc32_paint_cancel(int pid,const Sc32PaintEnd *input);
int wm_sc32_submit(int pid,const ScSubmitDesc *input,const ScSubmitRect *rectangles);
int wm_sc32_cancel_close(int pid,u32 handle,u32 generation);
int wm_sc32_owner_has_windows(int pid);
u32 wm_sc32_change_count(int pid,u32 handle,u32 generation);
int wm_sc32_focus_pid(void);
int wm_sc32_pending(int pid,ScMessage *message,int consume);
u32 wm_sc32_pending_count(int pid);
int wm_sc32_message_valid(int pid,const ScMessage *message);
void wm_sc32_loss_owner(int pid);
void wm_sc32_input_revoked(int pid,const ScMessage *message);
void wm_sc32_loss_epoch(void);
void wm_sc32_owner_stopped(int pid);
int wm_sc32_attention_stop(int pid); /* 每调用仅解绑一窗，退出预算由GUI清理器控制。 */
u32 wm_sc32_route_epoch(void);
u32 wm_sc32_session_id(void);
int wm_sc32_route(const Sc32RawEvent *event);
int wm_wallpaper_snapshot(int pid,u32 *mode,u32 *session);
#endif /* SANDCORE_WM_H */
