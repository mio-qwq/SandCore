#ifndef SANDCORE_KEYBOARD_H
#define SANDCORE_KEYBOARD_H

/* PS/2 键盘 (8042 控制器, IRQ1) 驱动接口 */
#include "io.h"
int keyboard_down(u32 key); /* 物理按住状态，用户查询另由 WM 检查焦点 */

void keyboard_isr(void);             /* IRQ1 处理, 由 interrupts.c 分发 */
int  getkey(void);                   /* 阻塞取一键: 可打印字符/'\n'/'\b'/Esc(27) */
int  kbhit(void);                    /* 非阻塞探测: 缓冲区有无按键 */

#endif /* SANDCORE_KEYBOARD_H */
