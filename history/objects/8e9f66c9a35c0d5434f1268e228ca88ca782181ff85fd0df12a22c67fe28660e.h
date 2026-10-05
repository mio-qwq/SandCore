#ifndef SANDCORE_WM_H
#define SANDCORE_WM_H

#include "io.h"
#define SC_CANVAS_W 318
#define SC_CANVAS_H 170

/* 窗口管理器 / 合成器 v2（M6）—— 规范见 docs/WM.md。
 * 公共接口中的 pid 由内核分发器提供，用户不能伪造另一个 owner。
 * handle 是稳定编号，与 z 序索引无关；所有用户绘图先写独占 canvas，
 * 合成才访问 gfx 后台缓冲。关闭/置顶重排不能改变 handle/画布的关联。 */

void wm_init(void);                  /* 只建文件图标桌面，不自动开任何窗口 */
void wm_compose(void);               /* 全帧合成: 壁纸→窗口→任务栏→光标 */
void wm_mouse_poll(void);            /* 读鼠标状态, 处理拖拽/点按 */
void wm_request_compose(void);       /* 标记"画面脏了", 主循环里统一合成 */
int  wm_dirty(void);
void wm_open_about(void);            /* 历史内核关于页接口，当前 SandShell 不含 about 命令 */

int  wm_open_user_window(int pid, const char *title, int w, int h);
void wm_close_user_window(int pid, int handle);
void wm_close_owner(int pid);        /* 任务退出时回收全部窗口 */
int  wm_user_puts(int pid, const char *s); /* 最近创建的窗口维护独立文字游标 */
/* M8终端只为本人ARGB窗口启用。历史按需分配4页，窗口关闭时释放；
 * 重绘按当前宽度/缩放/主题重新排版，不能把旧像素放大当清晰文字。
 * command=0启用/1清空/2重绘；拥有关系与终端状态必须同时成立。
 * stream允许可信CLI路由指定已授权的owner/handle，三环不能直接调用。 */
int wm_terminal_control(int pid,int handle,int command);
int wm_terminal_owned(int pid,int handle);
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
int wm_user_pointer(int pid,int handle,i32 *out); /* 客户坐标/按住/按下沿/松开沿/焦点 */
int wm_user_pointer_peek(int pid,int handle,i32 *out);
int wm_user_peekkey(int pid); /* 与GETKEY同样聚焦检查，但不消费队首 */
u32 wm_legacy_mouse(int pid); /* 高分辨率下为旧画布虚拟化9位鼠标坐标 */
/* 卡片不占 WMAX 槽，窗口满也能诊断；暂停保留画布，关闭卡片才结束任务。 */
void panic_screen(const char *reason, u32 vec);   /* 异常红屏 */

#endif /* SANDCORE_WM_H */
