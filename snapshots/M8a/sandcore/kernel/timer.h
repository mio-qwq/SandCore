#ifndef SANDCORE_TIMER_H
#define SANDCORE_TIMER_H

#include "io.h"

/* PIT (8253/8254) 时钟: 系统心跳 + 唯一的时间来源 */

void timer_init(u32 hz);             /* 配置通道0为方波, 每 1/hz 秒触发 IRQ0 */
void timer_tick(void);               /* IRQ0 处理 (由 interrupts.c 分发), 别手动调 */
u32  timer_uptime(void);             /* 开机以来的百分之一秒数 */

extern volatile u32 sc_ticks;        /* 心跳计数 (中断里自增) */

#endif /* SANDCORE_TIMER_H */
