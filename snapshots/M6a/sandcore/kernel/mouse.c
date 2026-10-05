/* =====================================================================
 *  SandCore PS/2 鼠标驱动 (kernel/mouse.c)
 *  ---------------------------------------------------------------------
 *  【协议】鼠标包固定 3 字节, 从 8042 控制器 (0x60) 依次读出:
 *    B0: bit0 左键 bit1 右键 bit2 中键 bit3 恒 1(同步位)
 *        bit4 X 负号  bit5 Y 负号  bit6/7 溢出(忽略)
 *    B1: X 位移 (8 位, 符号看 B0.4, 负值 = B1-256)
 *    B2: Y 位移 (正 = 鼠标上推, 屏幕 y 要取反)
 *  同步策略: B0 的 bit3 不是 1 就说明字节错位, 整包丢弃重对齐。
 *
 *  【初始化】8042 标准四步:
 *    0xA8 开辅助口 → 改控制器配置(放行鼠标中断+恢复时钟) →
 *    经 0xD4 对鼠标发 F6(默认值) → 发 F4(开始上报)。
 *    ACK(0xFA) 会混进数据流, init 期间用 suppress 压掉。
 * ===================================================================== */
#include "io.h"
#include "mouse.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64

static i32 mx, my;                /* 光标位置 */
static u8  mbtn;                  /* 按键位图 */
static u8  pkt[3];                /* 当前包积累 */
static u8  cycle;                 /* 包内字节序号 0..2 */
static u8  suppress;              /* init 期间丢弃一切数据 */

static void ps2_wait_in(void)     /* 等控制器输入缓冲空 (可写) */
{
    u32 t = 100000;
    while (t-- && (inb(PS2_STAT) & 0x02))
        ;
}

static void ps2_wait_out(void)    /* 等输出缓冲有数据 (可读) */
{
    u32 t = 100000;
    while (t-- && !(inb(PS2_STAT) & 0x01))
        ;
}

static void ps2_cmd(u8 c)  { ps2_wait_in(); outb(PS2_STAT, c); }
static void ps2_put(u8 d)  { ps2_wait_in(); outb(PS2_DATA, d); }
static u8  ps2_get(void)   { ps2_wait_out(); return inb(PS2_DATA); }

static void clamp_pos(void)
{
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > 319) mx = 319;
    if (my > 199) my = 199;
}

void mouse_init(void)
{
    suppress = 1;

    ps2_cmd(0xA8);                /* 开辅助口 (鼠标) */
    ps2_cmd(0x20);                /* 读控制器配置字节 */
    u8 cfg = ps2_get();
    cfg |= 0x02;                  /* bit1: 放行鼠标中断 */
    cfg &= (u8)~0x20;             /* bit5: 恢复鼠标时钟线 */
    ps2_cmd(0x60);                /* 写回配置 */
    ps2_put(cfg);

    ps2_cmd(0xD4); ps2_put(0xF6); /* 对鼠标: 恢复默认值 */
    ps2_cmd(0xD4); ps2_put(0xF4); /* 对鼠标: 开始上报数据 */

    /* 放行 IRQ12 (从片 bit4), 级联线 IRQ2 保持畅通 */
    outb(0x21, (u8)(inb(0x21) & ~0x04));
    outb(0xA1, (u8)(inb(0xA1) & ~0x10));

    /* 清掉 init 期间塞在缓冲里的 ACK 等杂音 */
    for (u32 t = 0; t < 200000 && (inb(PS2_STAT) & 0x01); t++)
        (void)inb(PS2_DATA);

    suppress = 0;
    mx = 160; my = 100;           /* 光标出生在屏幕中央 */
    mbtn = 0;
    cycle = 0;
}

void mouse_isr(void)
{
    if (!(inb(PS2_STAT) & 0x01))
        return;                   /* 没数据 (防御性) */
    u8 d = inb(PS2_DATA);
    if (suppress)
        return;

    if (cycle == 0 && !(d & 0x08))
        return;                   /* 同步位不对: 字节流错位, 丢弃重对齐 */

    pkt[cycle++] = d;
    if (cycle < 3)
        return;
    cycle = 0;

    if (pkt[0] & 0xC0)            /* 溢出包: 这一帧作废 */
        return;

    i32 dx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    i32 dy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);

    mx += dx;
    my -= dy;                     /* 鼠标向上推 = 屏幕 y 减小 */
    clamp_pos();
    mbtn = pkt[0] & 0x07;
}

i32 mouse_x(void)       { return mx; }
i32 mouse_y(void)       { return my; }
u8  mouse_buttons(void) { return mbtn; }
