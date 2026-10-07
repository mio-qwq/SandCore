/* =====================================================================
 *  SandCore 中断子系统 (kernel/interrupts.c)
 *  ---------------------------------------------------------------------
 *  职责: 建自己的 IDT (48 个门), 把两颗 8259A PIC 重映射到向量 32..47,
 *  按需放行 IRQ，然后开中断。M7 按 CPU 保存的 CS.RPL 区分故障：
 *  零环 bug 保留 panic；三环交给注册回调或暂停卡片，不能拖垮桌面。
 *
 *  【向量号地图】
 *    0-31   CPU 异常 (除0/缺页/GP...)   -> isr_c_common -> CPL0 panic / CPL3 process_exception
 *    32-47  硬件 IRQ (PIC 重映射后)      -> irq_c_common -> 各驱动
 *             32: PIT 时钟  33: PS/2 键盘  44: 鼠标(M4)
 *    0x7C   SandCall 系统调用门 —— 见 docs/SYSCALL.md
 * ===================================================================== */
#include "io.h"
#include "interrupts.h"
#include "timer.h"
#include "module.h"
#include "keyboard.h"
#include "mouse.h"
#include "wm.h"
#include "task.h"
#include "process.h"
#include "serial.h"
#include "ringdebug.h"
#include "audio.h"
#ifdef M10_DISK_CORE
#include "e1000.h"
#endif

/* ---- IDT 门描述符: 8 字节, 位域见 docs/INTR.md ----
 * M5: 门数扩到 125 (0x7C 系统调用门需要 DPL=3, 其余 0x8E) */
typedef struct __attribute__((packed)) {
    u16 off_lo;      /* 处理函数地址低 16 位 */
    u16 selector;    /* 代码段选择子, 恒 0x08 */
    u8  rsv;         /* 恒 0 */
    u8  attr;        /* P|DPL|类型: 0x8E = 在场|ring0|32位中断门 */
    u16 off_hi;      /* 处理函数地址高 16 位 */
} idt_gate_t;

/* ---- idt.asm 生成的东西 ---- */
extern void *isr_table[125];         /* 125 个桩 (0..31 异常, 32..47 IRQ, 48..124 保留/系统调用) */

static idt_gate_t idt[125];

/* LGDT/LIDT 要的 6 字节指针: 2 字节上限 + 4 字节基址 */
static struct __attribute__((packed)) {
    u16 limit;
    u32 base;
} idt_ptr;

static void idt_set(u32 n, void *handler, u8 attr)
{
    idt[n].off_lo   = (u32)handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].rsv      = 0;
    idt[n].attr     = attr;
    idt[n].off_hi   = (u32)handler >> 16;
}

/* ---------------- 8259A PIC ----------------
 * 两颗级联: 主片管 IRQ0-7, 从片经主片 IRQ2 管 IRQ8-15。
 * 出厂默认向量在 0x08-0x0F —— 和 CPU 异常打架! 必须重映射到 32 起。 */
#define PIC1_CMD 0x20
#define PIC1_DAT 0x21
#define PIC2_CMD 0xA0
#define PIC2_DAT 0xA1

static void pic_remap(void)
{
    outb(PIC1_CMD, 0x11); io_wait();  /* ICW1: 开始初始化 + 要 ICW4 */
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DAT, 32);   io_wait();  /* ICW2: 主片向量基址 -> 32 */
    outb(PIC2_DAT, 40);   io_wait();  /*        从片向量基址 -> 40 */
    outb(PIC1_DAT, 0x04); io_wait();  /* ICW3: 主片 IRQ2 接从片 */
    outb(PIC2_DAT, 0x02); io_wait();  /*        从片接在主片 IRQ2 */
    outb(PIC1_DAT, 0x01); io_wait();  /* ICW4: 8086 模式 */
    outb(PIC2_DAT, 0x01); io_wait();

    /* 屏蔽寄存器: 1=禁 0=放行。只放行 时钟(0)/键盘(1)/级联(2) */
    outb(PIC1_DAT, 0xF8);
    outb(PIC2_DAT, 0xFF);             /* 从片全屏蔽 (鼠标 M4 再开) */
}

/* 中断结束命令 EOI: 不发的话 PIC 认为中断还在处理, 后续同级中断全堵死 */
static void pic_eoi(u32 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

/* ---------------- C 侧分发 ---------------- */

/* CPU 异常按特权级分流，0x7C 进入系统调用；公共出口统一决定是否换栈。 */
void isr_c_common(u32 vec, u32 *frame)
{
    if (vec == 0x7C) {
        syscall_c_common(vec, frame);
        return;
    }
    if (vec < 32) {
        if(ringdebug_exception(vec,frame))return;
        /* CPU 压入的 CS.RPL 是可信来源；current 非零也可能在执行内核
         * 系统调用。CPL0 bug 仍 panic，CPL3 则注册处理/暂停后立即切走。 */
        if((frame[15]&3)==3 && task_pid()>0) process_exception(vec,frame);
        else panic("KERNEL EXCEPTION",vec);
    }
    /* 48..123: 保留向量, 谁触发就无视 */
}

/* 硬件 IRQ 分发后 EOI; 从片 IRQ 还要主从各发一次 */

/* 硬件 IRQ 分发表: 新驱动注册到这里, 不改公共路径 */
static void (*irq_handlers[16])(void) = {
    [0]  = timer_tick,                /* IRQ0: PIT */
    [1]  = keyboard_isr,              /* IRQ1: PS/2 键盘 */
    [3]  = serial_management_isr,     /* M9：COM2，仅从硬件RX取字节。 */
    [4]  = serial_debug_isr,          /* M9：COM1，与管理队列独立。 */
    [12] = mouse_isr,                 /* IRQ12: PS/2 鼠标 */
};

void irq_c_common(u32 vec,u32 *frame)
{
    u32 irq = vec - 32;
    /* mio：采样在轮转之前进行，frame[15]是CPU保存的被打断CS，
     * 不能使用中断桩已经切成0x08的当前CS混淆全部用户任务。 */
    if(irq==0)task_cpu_tick(frame[15]);
    if (irq < 16 && irq_handlers[irq])
        irq_handlers[irq]();
    audio_irq(irq); /* PCI可能共享IRQ；检查本设备status，仅清自己的W1C位。 */
#ifdef M10_DISK_CORE
    e1000_irq(irq); /* 只读清本设备ICR/标记待服务，协议栈永远不进IRQ。 */
#endif
    modules_irq(irq); /* 扩展只投递/确认硬件，EOI和调度仍由公共出口负责。 */
    if(irq!=0)task_kernel_wake(); /* 驱动只唤醒设备服务上下文，调度统一走IRQ出口。 */
    pic_eoi(irq);
    if(irq==4)ringdebug_irq(frame); /* EOI后允许物理调试机停在任意被打断任务。 */
}

/* ---------------- 异常红屏 ---------------- */
void panic(const char *reason, u32 vec)
{
    extern void panic_screen(const char *reason, u32 vec); /* console.c 提供 */
    cli();
    panic_screen(reason, vec);
    for (;;)
        halt();
}

/* ---------------- 总装 ---------------- */
void interrupts_init(void)
{
    for (u32 i = 0; i < 125; i++)
        idt_set(i, isr_table[i], (i == 0x7C || i==3) ? 0xEE : 0x8E);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (u32)idt;
    __asm__ __volatile__("lidt %0" :: "m"(idt_ptr));

    pic_remap();
    timer_init(100);                  /* PIT: 100Hz 心跳 */
    sti();                            /* 万事俱备, 放行中断 */
}
