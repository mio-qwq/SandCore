/* =====================================================================
 *  SandCore 任务子系统 v2 (kernel/task.c)
 *  ---------------------------------------------------------------------
 *  【v2 质变: 分页 + 多用户程序并发】
 *   - paging_init() 后内核恒等映射全部物理内存;
 *   - 每个用户任务一份页目录: 共享内核映射 + 私有用户区 (0x400000);
 *   - 所有程序都链接在 org 0x400000, 装载时映射到各自物理帧 ——
 *     同一个 hello.scx 可以同时跑 N 份互不覆盖;
 *   - 换任务 = sched_pick 换 esp + CR3;
 *   - 用户指针翻译: 物理地址 = uphys + (虚地址 - 0x400000),
 *     内核经恒等映射直读 (v1 信任模型, M7 收紧)。
 * ===================================================================== */
#include "io.h"
#include "task.h"
#include "memory.h"
#include "paging.h"
#include "term.h"
#include "timer.h"
#include "wm.h"
#include "fs.h"
#include "mouse.h"

/* ---- GDT: 内核2 + 用户2 + TSS ---- */
static u64 gdt[6];
static struct __attribute__((packed)) { u16 len; u32 base; } gdt_ptr;
static u8  tss[104];                       /* 32 位 TSS 最小 104 字节 */

extern void gdt_flush(u32 gdtr_addr);      /* idt.asm: lgdt + 重载段寄存器 */
extern void tss_flush(void);               /* idt.asm: ltr 0x28 */

static void tss_set_esp0(u32 esp)
{
    *(u32 *)(tss + 4) = esp;               /* TSS.esp0 */
    *(u16 *)(tss + 8) = 0x10;              /* TSS.ss0  = 内核数据段 */
}

/* ---- 任务表 ---- */
task_t tasks[NTASK];
static int current;                        /* IRQ 发生时正在跑的任务 */

#define SYS_EXIT    0x00
#define SYS_PUTS    0x01
#define SYS_GETTICK 0x02
#define SYS_WINOPEN 0x10
#define SYS_WINCLOSE 0x11
#define SYS_TXT     0x12
#define SYS_FILL    0x13
#define SYS_GETKEY  0x14
#define SYS_MOUSE   0x15
#define SYS_FSREAD  0x30
#define SYS_FSWRITE 0x31
#define SYS_FSLIST  0x32
#define SYS_EXEC    0x40

void task_init(void)
{
    paging_init();                        /* 先开分页 (恒等映射, 环境不变) */

    gdt[0] = 0;
    gdt[1] = 0x00CF9A000000FFFFULL;       /* 0x08 内核代码 */
    gdt[2] = 0x00CF92000000FFFFULL;       /* 0x10 内核数据 */
    gdt[3] = 0x00CFFA000000FFFFULL;       /* 0x18 用户代码 DPL3 */
    gdt[4] = 0x00CFF2000000FFFFULL;       /* 0x20 用户数据 DPL3 */
    {
        u32 base = (u32)tss;
        /* TSS 描述符字节: [4]=基址16-23 [5]=属性0x89 [6]=0 [7]=基址24-31
         * 【M5 调试实录】基址高位和属性位置曾摆反, 切换即 GP */
        u32 lo = 0x0067u | ((base & 0xFFFFu) << 16);
        u32 hi = ((base >> 16) & 0xFFu)
               | (0x89u << 8)
               | ((base >> 24) & 0xFFu) << 24;
        gdt[5] = lo | ((u64)hi << 32);    /* 0x28 TSS 描述符 */
    }
    gdt_ptr.len = (u16)(sizeof(gdt) - 1);
    gdt_ptr.base = (u32)gdt;
    gdt_flush((u32)&gdt_ptr);
    tss_flush();

    for (int i = 0; i < NTASK; i++)
        tasks[i].state = 0;
    tasks[0].state = 1;                   /* 任务 0 = 内核主循环 */
    tasks[0].pd = paging_kernel_pd();
    tasks[0].kstack_top = 0x90000;        /* 引导栈 (恒等映射) */
    tasks[0].saved_esp = 0;
    current = 0;
    tss_set_esp0(0x90000);
}

int task_spawn(const char *name, u32 pd, u32 entry, u32 user_esp)
{
    for (int i = 1; i < NTASK; i++) {
        if (tasks[i].state != 0)
            continue;
        u32 ks = pframe_alloc();          /* 内核栈一页 (恒等映射可达) */
        if (ks == 0)
            return -1;

        /* 预置"初始现场", iretd 直接落进 ring3:
         * 【M5 调试实录】CS/SS 必须带 RPL3 (0x1B/0x23), 否则 GP */
        u32 *f = (u32 *)(ks + 4096 - 76);
        for (int k = 0; k < 19; k++)
            f[k] = 0;
        f[14] = entry;                    /* EIP */
        f[15] = 0x1B;                     /* CS = 0x18|RPL3 */
        f[16] = 0x202;                    /* EFLAGS: IF=1 */
        f[17] = user_esp;                 /* ESP = 用户栈 */
        f[18] = 0x23;                     /* SS = 0x20|RPL3 */

        tasks[i].pd = pd;
        tasks[i].kstack_top = ks + 4096;
        tasks[i].saved_esp = (u32)f;
        tasks[i].entry = entry;
        tasks[i].user_esp = user_esp;
        tasks[i].state = 1;
        for (int k = 0; k < 11; k++) {
            tasks[i].name[k] = name[k];
            if (!name[k]) break;
        }
        tasks[i].name[11] = 0;
        return i;
    }
    return -1;
}

/* ---------------- 调度 ---------------- */
u32 sched_pick(u32 cur_esp)
{
    tasks[current].saved_esp = cur_esp;

    int next = current;
    for (int i = 1; i <= NTASK; i++) {
        int cand = (current + i) % NTASK;
        if (tasks[cand].state == 1) {
            next = cand;
            break;
        }
    }
    if (next != current) {
        current = next;
        tss_set_esp0(tasks[current].kstack_top);
        paging_switch(tasks[current].pd); /* 换地址空间 */
    }
    return tasks[current].saved_esp;
}

/* ---------------- 系统调用 (int 0x7C, DPL3) ----------------
 * frame: [8]=EBX [9]=EDX [10]=ECX?? 布局见 idt.asm (pusha 逆序):
 *   [7]=EAX前是ECX... 精确: [4]=EDI [5]=ESI [6]=EBP [7]=旧ESP
 *   [8]=EBX [9]=EDX [10]=ECX [11]=EAX
 * 用户指针翻译: 物理地址 = uphys + (虚 - 0x400000) */
static char *uptr(u32 p)
{
    return (char *)(tasks[current].uphys + (p - 0x400000u));
}

void syscall_c_common(u32 vec, u32 *frame)
{
    (void)vec;
    /* pusha 布局对应的 u32 下标: EDI=4 ESI=5 EBP=6 旧ESP=7
     * EBX=8 EDX=9 ECX=10 EAX=11 */
    switch (frame[11]) {                  /* EAX = 功能号 */
    case SYS_PUTS:
        term_puts(uptr(frame[8]));        /* EBX = 字符串 */
        frame[11] = 0;
        break;
    case SYS_GETTICK:
        frame[11] = sc_ticks;
        break;
    case SYS_EXIT:
        tasks[current].state = 2;         /* 僵尸: 调度器不再选它 */
        frame[11] = 0;
        break;
    case SYS_WINOPEN: {                   /* EBX=标题 ECX=宽 EDX=高 */
        char t[12];
        const char *src = uptr(frame[8]);
        for (int k = 0; k < 11; k++) { t[k] = src[k]; if (!t[k]) break; }
        t[11] = 0;
        frame[11] = (u32)wm_open_user_window(current, t,
                                             (int)frame[10], (int)frame[9]);
        break;
    }
    case SYS_WINCLOSE:                    /* EBX = 窗口句柄 */
        wm_close_user_window(current, (int)frame[8]);
        frame[11] = 0;
        break;
    case SYS_TXT:                         /* EBX=句柄 ECX=x EDX=y ESI=串 EDI=色 */
        wm_user_text(current, (int)frame[8], (int)frame[10], (int)frame[9],
                     uptr(frame[5]), (u8)frame[4]);
        frame[11] = 0;
        break;
    case SYS_FILL:                        /* EBX=句柄 ECX=x EDX=y ESI=宽 EDI=高 EBP=色 */
        wm_user_fill(current, (int)frame[8], (int)frame[10], (int)frame[9],
                     (int)frame[5], (int)frame[4], (u8)frame[6]);
        frame[11] = 0;
        break;
    case SYS_GETKEY:
        frame[11] = (u32)wm_user_getkey(current);
        break;
    case SYS_MOUSE:                       /* 打包: x9b y9b 按键3b... 简化 x|y<<9|btn<<18 */
        frame[11] = (u32)(mouse_x() | (mouse_y() << 9) | (mouse_buttons() << 18));
        break;
    case SYS_FSREAD:                      /* EBX=名 ECX=缓冲 EDX=上限 */
        frame[11] = (u32)fs_read(uptr(frame[8]), uptr(frame[10]), frame[9]);
        break;
    case SYS_FSWRITE:                     /* EBX=名 ECX=数据 EDX=长度 */
        frame[11] = (u32)fs_write(uptr(frame[8]),
                                  (const u8 *)uptr(frame[10]), frame[9]);
        break;
    case SYS_FSLIST: {                    /* EBX=缓冲 EDX=上限: "名 大小
" 文本 */
        char *out = uptr(frame[8]);
        u32 max = frame[9], used = 0;
        for (int i = 0; i < fs_count() && used + 48 < max; i++) {
            const char *n = fs_name(i);
            while (*n) out[used++] = *n++;
            out[used++] = ' ';
            char b[12]; u32 v = fs_size(i); int k = 0;
            do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (k--) out[used++] = b[k];
            out[used++] = '\n';
        }
        out[used] = 0;
        frame[11] = used;
        break;
    }
    case SYS_EXEC:                        /* EBX = SCX 路径 */
        frame[11] = (u32)task_exec_scx(uptr(frame[8]));
        break;
    default:
        frame[11] = 0xFFFFFFFF;
        break;
    }
}

/* ---------------- SCX 加载器 (分页版) ---------------- */
#define USER_SLOT  0x400000u

static u8 scx_stage[65536];           /* 文件中转 (内核区, 恒等可达) */

int task_exec_scx(const char *path)
{
    int n = fs_read(path, scx_stage, sizeof(scx_stage));
    if (n < 36)
        return -1;
    if (scx_stage[0] != 'S' || scx_stage[1] != 'C' || scx_stage[2] != 'X'
     || scx_stage[3] != '1' || scx_stage[4] != 'M' || scx_stage[5] != 'I'
     || scx_stage[6] != 'O')
        return -2;

    u32 entry_rva = *(u32 *)(scx_stage + 8);
    u32 load_size = *(u32 *)(scx_stage + 12);
    u32 bss_size  = *(u32 *)(scx_stage + 16);
    if (load_size == 0 || load_size > 0x100000u)
        return -3;

    u32 pd = paging_new_task_dir();
    u32 npages = (load_size + bss_size + PAGE_SIZE - 1) / PAGE_SIZE + 1; /* +1 栈页 */
    u32 first = 0;
    for (u32 i = 0; i < npages; i++) {
        u32 fr = pframe_alloc();
        if (fr == 0)
            return -4;
        if (i == 0) first = fr;
        paging_map_user(pd, USER_BASE + i * PAGE_SIZE, fr);
        /* 经恒等映射把内容写进物理帧 */
        u8 *dst = (u8 *)fr;
        for (u32 k = 0; k < PAGE_SIZE; k++) {
            u32 file_off = i * PAGE_SIZE + k;
            dst[k] = (file_off < load_size) ? scx_stage[36 + file_off]
                   : (file_off < load_size + bss_size) ? 0 : 0;
        }
    }
    /* 用户栈: 用户区最后一页 */
    paging_map_user(pd, 0x7FF000u, pframe_alloc());

    char nm[12];
    const char *p = path;
    for (int k = 0; k < 11; k++) {
        nm[k] = p[k];
        if (!p[k]) break;
    }
    nm[11] = 0;
    return task_spawn(nm, pd, 0x400000u + entry_rva, 0x7FFFF0u);
}
