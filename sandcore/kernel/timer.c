/* =====================================================================
 *  SandCore 时钟驱动 (kernel/timer.c)
 *  ---------------------------------------------------------------------
 *  8253/8254 PIT 通道 0 配成方波发生器, 输出接 IRQ0。
 *  输入时钟 1193182 Hz, 除以 divisor 得到触发频率。
 *  它是 SandCore 的唯一时间源: 抢占调度(M5)、超时、时钟显示全靠它。
 * ===================================================================== */
#include "io.h"
#include "timer.h"

volatile u32 sc_ticks = 0;           /* 旧u32低字照常回绕；既有GETTICK/等待合同不变。 */
static volatile u32 tick_high;
static u32 tick_frequency=100;

#define PIT_CH0   0x40               /* 通道0数据口 */
#define PIT_CMD   0x43               /* 命令口 */
#define PIT_HZ_IN 1193182            /* 输入基频 */

void timer_init(u32 hz)
{
    if (hz == 0)
        hz = 100;
    tick_frequency=hz;
    u32 div = PIT_HZ_IN / hz;

    outb(PIT_CMD, 0x36);             /* 通道0 | 先低后高字节 | 模式3方波 | BCD关 */
    outb(PIT_CH0, div & 0xFF);       /* 除数低 8 位 */
    outb(PIT_CH0, (div >> 8) & 0xFF);/* 除数高 8 位 */
}

void timer_tick(void)
{
    /* 同一次IF=0硬件IRQ内写低字并记真实进位。即使应用多年没有
     * 查询，也不会漏掉任何lo回绕；没有浮点/64位除法或用户回调。 */
    if(++sc_ticks==0)tick_high++;
}

u32 timer_uptime(void)
{
    return sc_ticks;                 /* 单位: 1/100 秒 (hz=100) */
}

void timer_monotonic(u32 out[2])
{
    u32 flags;__asm__ __volatile__("pushfl;popl %0":"=r"(flags));cli();
    /* 单CPU只有IRQ0能改两字。短临界区同时取得lo/hi，不能在lo
     * 刚回绕而hi尚未进位时交付倒退快照；已有IF=0调用保持关闭。 */
    out[0]=sc_ticks;out[1]=tick_high;
    if(flags&0x200u)sti();
}

u32 timer_frequency_hz(void)
{return tick_frequency;}
