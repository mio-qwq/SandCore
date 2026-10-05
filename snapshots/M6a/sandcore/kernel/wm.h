#ifndef SANDCORE_WM_H
#define SANDCORE_WM_H

#include "io.h"

/* 窗口管理器 / 合成器 (M4) —— 规范见 docs/WM.md */

void wm_init(void);                  /* 建桌面: 终端窗口 + 关于窗口 */
void wm_compose(void);               /* 全帧合成: 壁纸→窗口→任务栏→光标 */
void wm_mouse_poll(void);            /* 读鼠标状态, 处理拖拽/点按 */
void wm_request_compose(void);       /* 标记"画面脏了", 主循环里统一合成 */
int  wm_dirty(void);
void wm_open_about(void);            /* 开一个"关于"窗口 (shell about 命令) */

int  wm_open_user_window(int pid, const char *title, int w, int h);
void wm_close_user_window(int pid, int handle);
void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color);
void wm_user_fill(int pid, int handle, int x, int y, int w, int h, u8 color);
int  wm_user_getkey(int pid);
void wm_key_input(char c);           /* 键盘 ISR 喂键进聚焦窗口队列 */
void panic_screen(const char *reason, u32 vec);   /* 异常红屏 */

#endif /* SANDCORE_WM_H */
