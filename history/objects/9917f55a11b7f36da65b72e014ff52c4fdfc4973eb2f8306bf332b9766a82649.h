#ifndef SANDCORE_MOUSE_H
#define SANDCORE_MOUSE_H

#include "io.h"

/* PS/2 鼠标驱动 (IRQ12) —— 规范见 docs/WM.md §1 */

void mouse_init(void);           /* PS/2 控制器初始化 + 放行 IRQ12 */
void mouse_isr(void);            /* IRQ12 处理 (interrupts.c 分发) */

i32 mouse_x(void);               /* 光标位置 (已夹紧到屏幕内) */
i32 mouse_y(void);
u8  mouse_buttons(void);         /* bit0 左键 bit1 右键 bit2 中键 */
int mouse_next_event(i32 *x,i32 *y,u8 *buttons); /* IF=0主循环按包取事件 */

#endif /* SANDCORE_MOUSE_H */
