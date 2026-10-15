/* =====================================================================
 *  SandCore PS/2 鼠标驱动 (kernel/mouse.c)
 *  ---------------------------------------------------------------------
 *  【协议】默认3字节；真正协商ID3/4才改4字节，从8042依次读出:
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
#include "gfx.h"
#include "keyboard.h"
#include "sc32_raw.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64

static i32 mx, my;                /* 光标位置 */
static u8  mbtn;                  /* 按键位图 */
static u8 blocked_buttons;
static u8  pkt[4];                /* 只有协商成功的轮设备才读第4字节 */
static u8 packet_bytes=3,device_id;
static int device_ready;
static u8  cycle;                 /* 包内序号由真实device_id决定 */
static u8  suppress;              /* init 期间丢弃一切数据 */
typedef struct {i32 x,y;u8 buttons;} mouse_event_t;
static mouse_event_t events[64];
static volatile u8 event_head,event_tail;
static u32 event_overflow;
static u32 relative_x,relative_y;
static int relative_mode;
void mouse_relative_totals(u32 *x,u32 *y){*x=relative_x;*y=relative_y;}
void mouse_relative_mode(int enabled){relative_mode=enabled!=0;}
void mouse_session_switch(void)
{blocked_buttons|=mbtn;mbtn=0;event_head=event_tail=0;relative_mode=0;}

static void event_push(void)
{
    /* 单纯保存“最新按钮状态”会吞掉同一帧中的按下+松开：下一次
     * 主循环看到的仍是0。保留按钮边沿及其坐标，连续同按钮移动
     * 可以合并为最新位置，防止1080p拖拽塞满队列。
     * 仅IRQ写head；主循环在IF=0取tail，不需要libc或锁库。 */
    u8 previous=(u8)((event_head+63)&63);
    if(event_head!=event_tail && events[previous].buttons==mbtn) {
        events[previous].x=mx;events[previous].y=my;return;
    }
    u8 next=(u8)((event_head+1)&63);
    if(next==event_tail) {
        /* 极端超过63个未处理边沿时保留最新输入，记诊断计数。
         * 正常连续移动已合并，普通人手/自动验收不能依赖溢出。 */
        event_tail=(u8)((event_tail+1)&63);event_overflow++;
    }
    events[event_head]=(mouse_event_t){mx,my,mbtn};event_head=next;
}
int mouse_next_event(i32 *x,i32 *y,u8 *buttons)
{
    if(event_tail==event_head)return 0;
    mouse_event_t *e=&events[event_tail];*x=e->x;*y=e->y;*buttons=e->buttons;
    event_tail=(u8)((event_tail+1)&63);return 1;
}
int mouse_events_pending(void){return event_tail!=event_head;}

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
static int mouse_reply(u8 *reply)
{
    for(u32 budget=100000;budget;budget--){
        u8 status=inb(PS2_STAT);if(!(status&1u))continue;
        /* 控制器配置已收完；现在非AUX字节是真实键盘输入。交原
         * 解码器保存，不能为等待鼠标ACK把按键当回复或直接丢弃。 */
        if(!(status&0x20u)){keyboard_isr();continue;}
        *reply=inb(PS2_DATA);return 0;
    }
    return -1;
}
static int mouse_command(u8 command)
{
    ps2_cmd(0xD4);ps2_put(command);
    u8 reply;return !mouse_reply(&reply) && reply==0xFA?0:-1;
}
static int mouse_rate(u8 rate)
{return mouse_command(0xF3) || mouse_command(rate);}
static int wheel_negotiate(void)
{
    packet_bytes=3;device_id=0;device_ready=0;
    /* PS/2协议的200/100/80握手与F2真实ID决定包长。不是凭QEMU名字
     * 假定设备有轮，也不把中键/按键转换成轮事实。最后恢复100Hz，
     * 旧x/y/buttons格式及相对totals保持；不启用额外按钮的接口承诺。 */
    if(!mouse_rate(200) && !mouse_rate(100) && !mouse_rate(80) && !mouse_command(0xF2)){
        u8 id;
        if(!mouse_reply(&id) && (id==0 || id==3 || id==4)){
            device_id=id;packet_bytes=id?4:3;device_ready=1;
        }
    }
    if(!device_ready){
        /* 协商半途不能假定仍是3字节（F6不保证撤IntelliMouse模式）。
         * 第一次FF可能只是旧参数，第二次才是真RESET；每次分别
         * 领取ACK/BAT/ID。无法证实模式就不启上报，不错位投鼠标。 */
        for(u32 attempt=0;attempt<2 && !device_ready;attempt++){
            u8 bat,id;
            if(!mouse_command(0xFF) && !mouse_reply(&bat) && bat==0xAA
                && !mouse_reply(&id) && id==0){packet_bytes=3;device_id=0;device_ready=1;}
        }
    }
    if(device_ready && mouse_rate(100))device_ready=0;
    return device_ready?0:-1;
}

static void clamp_pos(void)
{
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= GFX_W) mx = GFX_W-1;
    if (my >= GFX_H) my = GFX_H-1;
}

void mouse_init(void)
{
    /* mio：8042配置回复走共享数据口，status不会把它标成鼠标包。
     * 若IRQ1在ps2_cmd(0x20)与ps2_get之间抢读，就会把控制器配置
     * （常见0x43）当F9扫描码，同时初始化拿不到原配置。M8把任意
     * 键门槛移到原生欢迎页后才使伪键暴露为自动跳过欢迎页。
     * 初始化以有界端口轮询完成，期间保存并关闭IF；事务全部结束
     * 才恢复原IF，既不清空真实已排队文字，也不修改GETKEY ABI。 */
    u32 initial_flags;
    __asm__ __volatile__("pushfl; popl %0":"=r"(initial_flags)::"memory");
    cli();
    suppress = 1;

    ps2_cmd(0xA8);                /* 开辅助口 (鼠标) */
    ps2_cmd(0x20);                /* 读控制器配置字节 */
    u8 cfg = ps2_get();
    cfg |= 0x02;                  /* bit1: 放行鼠标中断 */
    cfg &= (u8)~0x20;             /* bit5: 恢复鼠标时钟线 */
    ps2_cmd(0x60);                /* 写回配置 */
    ps2_put(cfg);

    ps2_cmd(0xD4); ps2_put(0xF6); /* 对鼠标: 恢复默认值 */
    (void)ps2_get();              /* 逐条收ACK，避免未消费回复挤占下一命令 */
    int negotiated=wheel_negotiate();
    if(!negotiated && mouse_command(0xF4))device_ready=0;

    /* 放行 IRQ12 (从片 bit4), 级联线 IRQ2 保持畅通 */
    outb(0x21, (u8)(inb(0x21) & ~0x04));
    outb(0xA1, (u8)(inb(0xA1) & ~0x10));

    /* 清掉 init 期间塞在缓冲里的 ACK 等杂音 */
    for (u32 t = 0; t < 200000 && (inb(PS2_STAT) & 0x01); t++){
        if(inb(PS2_STAT)&0x20u)(void)inb(PS2_DATA);else keyboard_isr();
    }

    suppress = 0;
    mx = 160; my = 100;           /* 光标出生在屏幕中央 */
    mbtn = 0;
    cycle = 0;
    event_head=event_tail=0;event_overflow=0;
    if(initial_flags&512)sti();
}

void mouse_isr(void)
{
    u8 status=inb(PS2_STAT);
    if (!(status & 0x01) || !(status & 0x20))
        return;                   /* 没数据 (防御性) */
    u8 d = inb(PS2_DATA);
    if (suppress || !device_ready)
        return;

    if (cycle == 0 && !(d & 0x08))
        return;                   /* 同步位不对: 字节流错位, 丢弃重对齐 */

    pkt[cycle++] = d;
    if (cycle < packet_bytes)
        return;
    cycle = 0;

    if (pkt[0] & 0xC0)            /* 溢出包: 这一帧作废 */
        return;

    i32 dx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    i32 dy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);

    relative_x+=(u32)dx;relative_y-=(u32)dy;
    if(!relative_mode){mx+=dx;my-=dy;}
    clamp_pos();
    u8 previous=mbtn,physical=pkt[0]&7;blocked_buttons&=physical;mbtn=physical&~blocked_buttons;
    i32 wheel=0,horizontal=0;
    if(device_id==3)wheel=-(i32)(signed char)pkt[3];
    else if(device_id==4){
        if(pkt[3]&0x40u){i32 value=pkt[3]&63u;if(value&32)value-=64;horizontal=-value;}
        else{ i32 value=pkt[3]&15u;if(value&8)value-=16;wheel=-value; }
    }
    if(sc32_raw_active()){
        /* 同一包先发布真实移动/按钮，再发布轮量。纯轮包不额外造一
         * 次相同位置MOVE，避免无意义唤醒与潜在整客户重绘。 */
        if((!wheel && !horizontal) || dx || dy || previous!=mbtn)sc32_raw_mouse(mx,my,mbtn,keyboard_modifiers());
        if(wheel || horizontal)sc32_raw_wheel(mx,my,wheel,horizontal,mbtn,keyboard_modifiers());
    }
    else event_push();
}

i32 mouse_x(void)       { clamp_pos();return mx; }
i32 mouse_y(void)       { clamp_pos();return my; }
u8  mouse_buttons(void) { return mbtn; }
int mouse_wheel_available(void){return device_ready && (device_id==3 || device_id==4);}
