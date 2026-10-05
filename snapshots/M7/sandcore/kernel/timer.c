/* =====================================================================
 *  SandCore 时钟驱动 (kernel/timer.c)
 *  ---------------------------------------------------------------------
 *  8253/8254 PIT 通道 0 配成方波发生器, 输出接 IRQ0。
 *  输入时钟 1193182 Hz, 除以 divisor 得到触发频率。
 *  它是 SandCore 的唯一时间源: 抢占调度(M5)、超时、时钟显示全靠它。
 * ===================================================================== */
#include "io.h"
#include "timer.h"

volatile u32 sc_ticks = 0;           /* IRQ0 每跳一次 +1 (u32 溢出约 497 天, 不管) */

#define PIT_CH0   0x40               /* 通道0数据口 */
#define PIT_CMD   0x43               /* 命令口 */
#define PIT_HZ_IN 1193182            /* 输入基频 */

void timer_init(u32 hz)
{
    if (hz == 0)
        hz = 100;
    u32 div = PIT_HZ_IN / hz;

    outb(PIT_CMD, 0x36);             /* 通道0 | 先低后高字节 | 模式3方波 | BCD关 */
    outb(PIT_CH0, div & 0xFF);       /* 除数低 8 位 */
    outb(PIT_CH0, (div >> 8) & 0xFF);/* 除数高 8 位 */
}

void timer_tick(void)
{
    sc_ticks++;
}

u32 timer_uptime(void)
{
    return sc_ticks;                 /* 单位: 1/100 秒 (hz=100) */
}
