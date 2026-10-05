#ifndef SANDCORE_WM_H
#define SANDCORE_WM_H

#include "io.h"

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
/* info 写五个 i32 到已校验的内核可达缓冲；只接受本人窗口。x/y 为外框
 * 屏幕坐标，客户区起点还需加 (1,13)，鼠标应用必须同时检查 focused。 */
int  wm_user_info(int pid,int handle,i32 *out);
int  wm_set_wallpaper(int mode);     /* 0 沙丘 / 1 夜色 / 2 暖沙，持久化模式号 */
int  wm_wallpaper(void);
void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color);
void wm_user_fill(int pid, int handle, int x, int y, int w, int h, u8 color);
int  wm_user_getkey(int pid);
void wm_key_input(char c);           /* 键盘 ISR 喂键进聚焦窗口队列 */
void panic_screen(const char *reason, u32 vec);   /* 异常红屏 */

#endif /* SANDCORE_WM_H */
