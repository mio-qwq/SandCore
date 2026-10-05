#ifndef SANDCORE_IO_H
#define SANDCORE_IO_H

/* x86 端口 IO —— 全内核共用的最小硬件访问层
 * 内核禁止任何 libc, 这两个内联汇编函数就是我们对硬件世界的全部喉舌。 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef signed int     i32;

/* 写 8 位 I/O 端口。 "Nd" 约束: 允许编译器把端口号编码进指令立即数
 * (outb 只认 0..0xFF 的立即数或 DX 寄存器, 让编译器挑最短编码) */
static inline void outb(u16 p, u8 v)
{
    __asm__ __volatile__("outb %0, %1" :: "a"(v), "Nd"(p));
}

static inline u8 inb(u16 p)
{
    u8 r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}

/* 16 位端口读写 (ATA 数据口一次吞吐 16 位) */
static inline void outw(u16 p, u16 v)
{
    __asm__ __volatile__("outw %0, %1" :: "a"(v), "Nd"(p));
}

static inline u16 inw(u16 p)
{
    u16 r;
    __asm__ __volatile__("inw %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}

/* 短暂等待: 向调试端口 0x80 写一个无用字节, 约占 1us。
 * 给慢速老芯片 (PIC/PIT/键盘控制器) 在连续端口操作之间喘口气,
 * QEMU 上不必要, 但真机上是祖传保险。 */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

/* 停机等中断: CPU 睡眠直到下一个中断到来。
 * 轮询循环里用它代替死转, 把 CPU 让给中断处理。 */
static inline void halt(void)
{
    __asm__ __volatile__("hlt");
}

static inline void sti(void)  { __asm__ __volatile__("sti"); }
static inline void cli(void)  { __asm__ __volatile__("cli"); }

#endif /* SANDCORE_IO_H */
