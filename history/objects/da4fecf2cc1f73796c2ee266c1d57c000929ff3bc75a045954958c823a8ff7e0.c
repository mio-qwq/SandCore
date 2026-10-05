/* =====================================================================
 *  SandCore 任务子系统 v2 (kernel/task.c)
 *  ---------------------------------------------------------------------
 *  【v2 质变: 分页 + 多用户程序并发】
 *   - paging_init() 后内核恒等映射全部物理内存;
 *   - 每个用户任务一份页目录: 共享内核映射 + 私有用户区 (0x400000);
 *   - 所有程序都链接在 org 0x400000, 装载时映射到各自物理帧 ——
 *     同一个 hello.scx 可以同时跑 N 份互不覆盖;
 *   - 换任务 = sched_pick 换 esp + CR3;
 *   - 系统调用沿用调用者 CR3，指针经逐页校验后按用户虚址访问。
 *     物理帧可以碎片化；不能用“首帧 + 偏移”猜测后面的页。
 *   - 内核代码、任务表、内核栈位于各目录相同的 supervisor 映射中；
 *     因此切 CR3 之后仍能执行调度器的最后几条指令。
 * ===================================================================== */
#include "io.h"
#include "task.h"
#include "memory.h"
#include "paging.h"
#include "timer.h"
#include "wm.h"
#include "fs.h"
#include "mouse.h"
#include "keyboard.h"
#include "font.h"
#include "display.h"
#include "desktop.h"
#include "image.h"
#include "gfx.h"
#include "process.h"
#include "theme.h"
#include "userspace.h"

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
static int render_hold;
/* mio：CPU计数独立于公共task_t，旧调试器的168B步长不变。PIT
 * 采样使用CPU压栈CS，而不是猜current对应代码特权级。只有任务0
 * 真正待机算idle；三环忙轮询即便什么也不画，仍真实算用户CPU。 */
static u32 cpu_total,cpu_idle,cpu_kernel,cpu_user,cpu_graphics;
static u32 cpu_generation[NTASK],cpu_running[NTASK],cpu_dispatch[NTASK];
static int cpu_ready,kernel_idle;
int task_pid(void) { return current; }
void task_idle(int idle){if(current==0)kernel_idle=idle!=0;}
void task_cpu_tick(u32 interrupted_cs)
{
    if(!cpu_ready)return;
    cpu_total++;
    if(current==0 && kernel_idle){cpu_idle++;return;}
    cpu_running[current]++;
    if(current>0 && (interrupted_cs&3)==3)cpu_user++;
    else {cpu_kernel++;if(current==0 && render_hold)cpu_graphics++;}
}
void task_cpu_info(u32 *out)
{
    /* 调用入口IF=0：全部计数一次复制，分类之和等于总样本；不能
     * 先复制total后被IRQ打断，输出无法相加的两代统计。 */
    for(int i=0;i<64;i++)out[i]=0;
    out[0]=1;out[1]=100;out[2]=sc_ticks;out[3]=cpu_total;
    out[4]=cpu_idle;out[5]=cpu_kernel;out[6]=cpu_user;out[7]=cpu_graphics;
    out[8]=NTASK;out[9]=1;
    for(int i=0;i<NTASK;i++) {
        u32 *row=out+16+i*6;row[0]=(u32)i;row[1]=(u32)tasks[i].state;
        row[2]=cpu_generation[i];row[3]=cpu_running[i];row[4]=cpu_dispatch[i];
    }
}
void task_render_hold(int hold)
{
    /* 只能由任务0在IF=0的短临界区切换。它保护合成期间的窗口表/
     * 画布生命周期，却不屏蔽PIT和8042：IRQ可以记录输入，暂不切到
     * 会调用WINCLOSE/FRAME的三环任务。没有另分配六份大屏快照。
     * 用户系统调用没有这个入口，不能借它禁止自己的抢占。 */
    if(current==0) render_hold=hold!=0;
}

/* 功能号统一使用 task.h，避免头文件、分发器与应用文档各养一份常量。 */

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
    cpu_total=cpu_idle=cpu_kernel=cpu_user=cpu_graphics=0;
    for(int i=0;i<NTASK;i++)cpu_generation[i]=cpu_running[i]=cpu_dispatch[i]=0;
    cpu_generation[0]=1;kernel_idle=0;cpu_ready=1;
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
        for (int k = 0; k < 4; k++) f[k] = 0x23; /* 用户数据段不能恢复为空选择子 */
        f[14] = entry;                    /* EIP */
        f[15] = 0x1B;                     /* CS = 0x18|RPL3 */
        f[16] = 0x202;                    /* EFLAGS: IF=1 */
        f[17] = user_esp;                 /* ESP = 用户栈 */
        f[18] = 0x23;                     /* SS = 0x20|RPL3 */

        process_reset(i);
        userspace_spawn(i,current);
        cpu_generation[i]++;if(!cpu_generation[i])cpu_generation[i]=1;
        cpu_running[i]=cpu_dispatch[i]=0;
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
    /* IRQ在完整保存现场、驱动处理及EOI后到这里。合成器已主动开
     * 中断，因此仍能收完整PS/2包和计时；返回本现场只暂缓任务切换。
     * 回收也留到hold解除以后，防止合成读着的物理画布被释放复用。 */
    if(render_hold && current==0) return cur_esp;
    /* 【回收为什么延后一轮】
     * EXIT 的 C 分发器只是把任务置为 2；这时 ESP 仍在它自己的内核栈上。
     * 立刻 free 该栈，下一次分配就可能覆盖本函数的返回地址。
     * 这里排除 current，只回收更早已经切走的僵尸：先释放私有地址空间，
     * 再释放内核栈，最后 state=0 才允许 task_spawn 复用任务槽。
     * 调度入口的 IF=0，所以回收与分配不会在两条指令之间互相穿插。 */
    for (int i = 1; i < NTASK; i++)
        if (i != current && tasks[i].state == 2) {
            paging_free_task_dir(tasks[i].pd);
            pframe_free(tasks[i].kstack_top - PAGE_SIZE);
            tasks[i].state = 0;
        }
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
        cpu_dispatch[current]++;
        tss_set_esp0(tasks[current].kstack_top);
        paging_switch(tasks[current].pd); /* 换地址空间 */
    }
    return tasks[current].saved_esp;
}

u32 syscall_finish(u32 cur_esp)
{
    /* 【为什么不在每次绘字之后切任务】
     * GUI 一次重绘可能调用几十次 TXT/FILL；若每次都切回任务0的 hlt，
     * 每个字符要再等一次 PIT，一行文字就从毫秒变成秒，按键队列很快满。
     * 普通调用直接恢复当前现场，100Hz PIT 仍保证任何程序不能霸占 CPU。
     * EXIT 则必须立刻离开僵尸代码；它仍走完整 sched_pick 和安全回收链。
     * 这个函数只决定出口，不在 C 里直接改 ESP；真正换栈由汇编桩完成。 */
    u32 *frame=(u32 *)cur_esp;
    return tasks[current].state==1 && frame[12]!=0x107C ? cur_esp : sched_pick(cur_esp);
}

/* ---------------- 系统调用 (int 0x7C, DPL3) ----------------
 * frame 是 idt.asm 保存现场的起点，不是 CPU 原始压栈的起点。
 * 四个段寄存器占 [0..3]，pusha 逆序占 [4..11]：
 *   [4]=EDI [5]=ESI [6]=EBP [7]=旧ESP [8]=EBX [9]=EDX [10]=ECX [11]=EAX。
 * 返回值必须写 frame[11]，否则 popa 会把 C 侧 eax 覆盖回旧的功能号。
 * 软中断只换 CPL 与栈，不换 CR3；所以用户虚址在这里仍有效，
 * 但必须先确认它属于本任务且映射在场，不能信任 ring3 传来的整数。 */
static char *uptr(u32 p)
{
    /* 系统调用仍在调用者 CR3 中，直接访问用户虚址，不假定物理页连续。 */
    return paging_user_range(tasks[current].pd, p, 1) ? (char *)p : 0;
}

static char *ustr(u32 p)
{
    /* 字符串没有显式长度，逐字节检查终止符，同时确认所跨过的每一页。
     * 4096 是扫描上限而非映射大小；超过上限仍无 NUL 就判失败，
     * 防止应用把输出或 EXEC 变成无限扫描。检查先于解引用。 */
    for (u32 i = 0; i < 4096; i++) {
        if (!paging_user_range(tasks[current].pd, p+i, 1)) return 0;
        if (!((char *)p)[i]) return (char *)p;
    }
    return 0;
}

void task_stop(int pid)
{
    /* pid=0 是桌面主循环，不能经 GUI 红叉关闭；可运行/暂停任务都可变僵尸。
     * 窗口先回收，下一帧立即显露下层内容；页与栈交给 sched_pick 延后释放。
     * 同一接口供 SYS_EXIT 和窗口红叉使用，避免两条退出路径语义漂移。 */
    if (pid <= 0 || pid >= NTASK || tasks[pid].state == 0 || tasks[pid].state == 2) return;
    tasks[pid].state = 2;           /* 先改变状态，避免关闭调试子进程时重复进入 */
    userspace_stop(pid,process_status(pid));
    process_detach_owner(pid);
    wm_close_owner(pid);
    tasks[pid].state = 2;
}

void syscall_c_common(u32 vec, u32 *frame)
{
    (void)vec;
    /* pusha 布局对应的 u32 下标: EDI=4 ESI=5 EBP=6 旧ESP=7
     * EBX=8 EDX=9 ECX=10 EAX=11 */
    switch (frame[11]) {                  /* EAX = 功能号 */
    case SYS_PUTS: {
        const char *text=ustr(frame[8]);
        int result=text?wm_user_puts(current,text):-1;
        /* 有本人窗口仍保留旧路由；无窗口CLI才使用内核记下的授权。
         * 非法字符串不能降级另一路由，错误UTF-8也不产生部分输出。 */
        if(text && result==-1)result=userspace_puts(current,text);
        frame[11]=(u32)result;break;
    }
    case SYS_GETTICK:
        frame[11] = sc_ticks;
        break;
    case SYS_USERALLOC:
        frame[11]=paging_user_alloc(tasks[current].pd,frame[8]);break;
    case SYS_USERFREE:
        frame[11]=(u32)paging_user_free(tasks[current].pd,frame[8]);break;
    case SYS_STATUS:
        frame[11]=(u32)process_status((int)frame[8]); break;
    case SYS_YIELD:
        frame[11]=0; frame[12]=0x107C; break; /* 只标记出口，让汇编统一换 ESP */
    case SYS_EXCHANDLER:
        frame[11]=(u32)process_handler((int)frame[8],frame[10]); break;
    case SYS_EXCRETURN:
        if(process_exception_return(frame,frame[8])) frame[11]=(u32)-1;
        break; /* 成功时保留恢复现场的 EAX，不能覆盖成普通 syscall 返回值 */
    case SYS_GETARGS: {
        /* 参数已在 EXEC 时拷贝到任务槽，父任务退出也不会留下悬空指针。
         * 调用者显式提供容量；复制最多 max-1 字节，始终保留末尾 NUL。
         * 返回实际复制长度，便于 CLI 在空参数时显示用法。 */
        u32 max = frame[9], used = 0;
        if (!max || !paging_user_range(tasks[current].pd, frame[8], max)) {
            frame[11] = (u32)-1; break;
        }
        char *out = uptr(frame[8]);
        while (used+1 < max && tasks[current].args[used]) {
            out[used] = tasks[current].args[used]; used++;
        }
        out[used] = 0;
        frame[11] = used;
        break;
    }
    case SYS_EXIT:
        process_exit(current,(int)frame[8]);
        task_stop(current);
        frame[11] = 0;
        break;
    case SYS_WINOPEN: {                   /* EBX=标题 ECX=宽 EDX=高 */
        const char *src = ustr(frame[8]);
        if (!src) { frame[11] = (u32)-1; break; }
        frame[11] = (u32)wm_open_user_window(current, src,
                                             (int)frame[10], (int)frame[9]);
        break;
    }
    case SYS_WINCLOSE:                    /* EBX = 窗口句柄 */
        wm_close_user_window(current, (int)frame[8]);
        frame[11] = 0;
        break;
    case SYS_TXT:                         /* EBX=句柄 ECX=x EDX=y ESI=串 EDI=色 */
        if (!ustr(frame[5])) { frame[11] = (u32)-1; break; }
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
    case SYS_KEYPEEK:
        frame[11] = (u32)wm_user_peekkey(current);
        break;
    case SYS_MOUSE:                       /* 打包: x9b y9b 按键3b... 简化 x|y<<9|btn<<18 */
        frame[11] = wm_legacy_mouse(current);
        break;
    case SYS_FRAME:
        if(frame[9]>1920u*1080u || !paging_user_range(tasks[current].pd,frame[10],frame[9])) frame[11]=(u32)-1;
        else frame[11]=(u32)wm_user_frame(current,(int)frame[8],(u8 *)frame[10],frame[9]);
        break;
    case SYS_KEYDOWN:
        frame[11]=wm_focused(current)?(u32)keyboard_down(frame[8]):0; break;
    case SYS_FONT8:
        if(!paging_user_range(tasks[current].pd,frame[8],1024)) { frame[11]=(u32)-1; break; }
        for(int c=0;c<128;c++) for(int r=0;r<8;r++) ((u8 *)frame[8])[c*8+r]=glyph_of((char)c)[r];
        frame[11]=1024; break;
    case SYS_GLYPH16:
        /* 返回行是位图副本，不泄露 SCF 字符串或 supervisor 指针。
         * 标量和完整输出区都先检查，错误调用不写任何半个字形。 */
        if(frame[8]>0x10FFFF || (frame[8]>=0xD800 && frame[8]<=0xDFFF)
           || !paging_user_range(tasks[current].pd,frame[10],32)) frame[11]=(u32)-1;
        else frame[11]=(u32)gfx_glyph16(frame[8],(u16 *)frame[10]);
        break;
    case SYS_UTF8TEXT: {
        const char *text=ustr(frame[5]);
        if(!text) { frame[11]=(u32)-1; break; }
        /* 整串先验证再画；否则发现最后一个坏字节时，用户会得到
         * “失败但已经画出前半串”的难以恢复状态。缺字是合法编码，
         * 仍绘制占位框；编码错误与字库未收录必须分开处理。 */
        if(!gfx_utf8_valid(text)) { frame[11]=(u32)-2; break; }
        frame[11]=(u32)wm_user_utf8(current,(int)frame[8],(int)frame[10],
                                    (int)frame[9],text,(u8)frame[4]);
        break;
    }
    case SYS_FONTINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],16)) frame[11]=(u32)-1;
        else { gfx_font_info((u32 *)frame[8]); frame[11]=0; }
        break;
    case SYS_WALLPAPER:
        frame[11]=(u32)wm_set_wallpaper((int)frame[8]);
        break;
    case SYS_WININFO:
        if(!paging_user_range(tasks[current].pd,frame[10],20)) { frame[11]=(u32)-1; break; }
        frame[11]=(u32)wm_user_info(current,(int)frame[8],(i32 *)frame[10]);
        break;
    case SYS_FSREAD:                      /* EBX=名 ECX=缓冲 EDX=上限 */
        if (!ustr(frame[8]) || !paging_user_range(tasks[current].pd, frame[10], frame[9])) {
            frame[11] = (u32)-1; break;
        }
        frame[11] = (u32)fs_read(uptr(frame[8]), uptr(frame[10]), frame[9]);
        break;
    /* 以下M8调用先检查完整输出映射，才交给各子系统；新增原生接口
     * 不改变旧WINOPEN/MOUSE布局，旧应用无需重新编译才能启动。 */
    case SYS_FSREADAT:
        if(!ustr(frame[8]) || !paging_user_range(tasks[current].pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)fs_read_at(ustr(frame[8]),(void *)frame[10],frame[9],frame[5]);
        break;
    case SYS_DISPLAYINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],32))frame[11]=(u32)-1;
        else {display_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_DISPLAYAPPLY:
        frame[11]=(u32)display_apply((int)frame[8],(int)frame[10],(int)frame[9],(int)frame[5]);break;
    case SYS_PALETTEINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],1024))frame[11]=(u32)-1;
        else {display_palette((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_THEMEINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],128))frame[11]=(u32)-1;
        else {theme_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_THEMELOAD: {
        /* action=2重读固定配置不使用用户字符串；其它动作先复制验证
         * 完整路径。解析/写盘成功才换快照，失败不能改掉当前主题。 */
        const char *path=frame[10]==2?0:ustr(frame[8]);
        frame[11]=frame[10]!=2 && !path?(u32)-1:(u32)theme_load(path,(int)frame[10]);break;
    }
    case SYS_THEMEPATH:
        if(!frame[9] || frame[9]>64 || !paging_user_range(tasks[current].pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)theme_path((int)frame[8],(char *)frame[10],frame[9]);
        break;
    case SYS_POINTER:
    case SYS_POINTERPEEK:
        if(!paging_user_range(tasks[current].pd,frame[10],24))frame[11]=(u32)-1;
        else frame[11]=(u32)(frame[11]==SYS_POINTERPEEK?wm_user_pointer_peek(current,(int)frame[8],(i32 *)frame[10])
                            :wm_user_pointer(current,(int)frame[8],(i32 *)frame[10]));
        break;
    case SYS_WINOPEN2: {
        const char *title=ustr(frame[8]);
        frame[11]=title?(u32)wm_open_native_window(current,title,(int)frame[10],(int)frame[9]):(u32)-1;break;
    }
    case SYS_WINRGB: {
        const char *title=ustr(frame[8]);
        frame[11]=title?(u32)wm_open_rgb_window(current,title,(int)frame[10],(int)frame[9]):(u32)-1;break;
    }
    case SYS_FRAME32:
        if(frame[9]>1920u*1080u*4 || !paging_user_range(tasks[current].pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)wm_user_frame_rgb(current,(int)frame[8],(const u32 *)frame[10],frame[9]);
        break;
    case SYS_FILLRGB:
        frame[11]=(u32)wm_user_fill_rgb(current,(int)frame[8],(int)frame[10],(int)frame[9],(int)frame[5],(int)frame[4],frame[6]);break;
    case SYS_TEXTRGB: {
        const char *text=ustr(frame[5]);
        if(!text)frame[11]=(u32)-1;
        else if(!gfx_utf8_valid(text))frame[11]=(u32)-2;
        else frame[11]=(u32)wm_user_text_rgb(current,(int)frame[8],(int)frame[10],(int)frame[9],text,frame[4]);
        break;
    }
    case SYS_WINCONTROL:
        frame[11]=(u32)wm_window_control(current,(int)frame[8],(int)frame[10]);break;
    case SYS_DESKTOPRELOAD:
        frame[11]=(u32)desktop_reload();wm_request_compose();break;
    case SYS_MONITOR: {
        /* 统计是只读快照，绝不包含内核指针；已用页不是CPU利用率。
         * 8个头字段+8行20B任务，空槽同样记录，便于观测退出回收。 */
        if(!paging_user_range(tasks[current].pd,frame[8],192)){frame[11]=(u32)-1;break;}
        u32 *out=(u32 *)frame[8];for(int i=0;i<48;i++)out[i]=0;
        out[0]=1;out[1]=sc_ticks;out[2]=memory_managed_bytes();
        out[3]=memory_used_bytes();out[4]=memory_free_bytes();out[5]=NTASK;out[6]=fs_count();
        for(int i=0;i<NTASK;i++) {
            u32 *row=out+8+i*5;row[0]=i;row[1]=tasks[i].state;
            for(int j=0;j<12;j++)((char *)(row+2))[j]=tasks[i].name[j];
            if(tasks[i].state==1)out[7]++;
        }
        frame[11]=0;break;
    }
    case SYS_CPUINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],256))frame[11]=(u32)-1;
        else {task_cpu_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_STORAGEINFO:
        if(!paging_user_range(tasks[current].pd,frame[8],128))frame[11]=(u32)-1;
        else {fs_storage_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_FSWRITE:                     /* EBX=名 ECX=数据 EDX=长度 */
        if (!ustr(frame[8]) || !paging_user_range(tasks[current].pd, frame[10], frame[9])) {
            frame[11] = (u32)-1; break;
        }
        if(!fs_user_mutable(ustr(frame[8]),0)) { frame[11]=(u32)-5; break; }
        frame[11] = (u32)fs_write(uptr(frame[8]),
                                  (const u8 *)uptr(frame[10]), frame[9]);
        break;
    case SYS_FSLIST: {                    /* EBX=缓冲 EDX=上限；每行“路径 空格 大小 LF” */
        char *out = uptr(frame[8]);
        u32 max = frame[9], used = 0;
        if (!max || !paging_user_range(tasks[current].pd, frame[8], max)) {
            frame[11] = (u32)-1; break;
        }
        for (int i = 0; i < fs_count() && used + 80 < max; i++) {
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
        frame[11] = ustr(frame[8]) ? (u32)task_exec_scx(ustr(frame[8])) : (u32)-1;
        break;
    case SYS_USERQUERY: {
        const char *name=frame[8]==4?ustr(frame[10]):0;
        if(!frame[5] || frame[5]>256 || !paging_user_range(tasks[current].pd,frame[9],frame[5])
           || (frame[8]==4 && !name))frame[11]=(u32)-1;
        else frame[11]=(u32)userspace_query(current,(int)frame[8],name,(char *)frame[9],frame[5]);
        break;
    }
    case SYS_USERCONFIG: {
        const char *path=frame[9]==2?0:ustr(frame[10]);
        frame[11]=frame[9]!=2 && !path?(u32)-1:(u32)userspace_config((int)frame[8],path,(int)frame[9]);break;
    }
    case SYS_CONFIGCHECK: {
        const char *path=ustr(frame[10]);int result=-1;
        if(path){
            int kind=(int)frame[8];
            if(kind==0 || kind==1)result=desktop_config_check(kind,path);
            else if(kind==2)result=theme_check(path);
            else if(kind==3)result=display_config_check(path);
            else if(kind==4){
                u32 info[2];u8 text[16];
                if(fs_stat(path,info) || info[0]!=1 || !info[1] || info[1]>sizeof(text))result=-2;
                else if(fs_read(path,text,info[1])!=(int)info[1])result=-2;
                else {result=text[0]>='0' && text[0]<='2'?0:-1;for(u32 i=1;i<info[1];i++)if(text[i]!='\r' && text[i]!='\n')result=-1;}
            }
        }
        frame[11]=(u32)result;break;
    }
    case SYS_CHDIR: {
        const char *path=ustr(frame[8]);
        frame[11]=path?(u32)userspace_chdir(current,path):(u32)-1;break;
    }
    case SYS_RESOLVE: {
        const char *path=ustr(frame[8]);
        if(!path || !frame[9] || frame[9]>64 || !paging_user_range(tasks[current].pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)userspace_resolve(current,path,(char *)frame[10],frame[9]);
        break;
    }
    case SYS_CLIRUN: {
        const char *command=ustr(frame[8]);
        frame[11]=command?(u32)userspace_cli_run(current,command,(int)frame[10]):(u32)-1;break;
    }
    case SYS_JOBSTATUS:
        frame[11]=(u32)userspace_job_status(current,frame[8]);break;
    case SYS_TERMINAL:
        frame[11]=(u32)wm_terminal_control(current,(int)frame[8],(int)frame[10]);break;
    case SYS_FSDIR:
        if(!ustr(frame[8]) || !frame[9] || !paging_user_range(tasks[current].pd,frame[10],frame[9])) frame[11]=(u32)-1;
        else frame[11]=(u32)fs_list_dir(ustr(frame[8]),(char *)frame[10],frame[9]);
        break;
    case SYS_MKDIR:
    case SYS_REMOVE:
        if(!ustr(frame[8])) { frame[11]=(u32)-1; break; }
        if(!fs_user_mutable(ustr(frame[8]),1)) { frame[11]=(u32)-5; break; }
        frame[11]=(u32)(frame[11]==SYS_MKDIR ? fs_mkdir(ustr(frame[8])) : fs_remove(ustr(frame[8])));
        break;
    case SYS_RENAME:
        if(!ustr(frame[8]) || !ustr(frame[10])) { frame[11]=(u32)-1; break; }
        if(!fs_user_mutable(ustr(frame[8]),1) || !fs_user_mutable(ustr(frame[10]),1)) { frame[11]=(u32)-5; break; }
        frame[11]=(u32)fs_rename(ustr(frame[8]),ustr(frame[10])); break;
    case SYS_STAT:
        if(!ustr(frame[8]) || !paging_user_range(tasks[current].pd,frame[10],8)) frame[11]=(u32)-1;
        else frame[11]=(u32)fs_stat(ustr(frame[8]),(u32 *)frame[10]);
        break;
    case SYS_DBGEXEC: {
        int pid=ustr(frame[8])?task_exec_scx(ustr(frame[8])):-1;
        frame[11]=(u32)process_debug_bind(pid,current); break;
    }
    case SYS_DEBUG:
        /* 先检查调用者缓冲，再检查调试拥有关系，不能借目标 pid
         * 把 supervisor 恒等映射当成任意地址读写入口。 */
        if((frame[10]==DBG_INFO || frame[10]==DBG_READ)
           && !paging_user_range(tasks[current].pd,frame[5],frame[4])) frame[11]=(u32)-1;
        else frame[11]=(u32)process_debug(current,(int)frame[8],(int)frame[10],frame[9],(void *)frame[5],frame[4]);
        break;
    default:
        frame[11] = 0xFFFFFFFF;
        break;
    }
}

/* ---------------- SCX 加载器 (分页版) ---------------- */
static u8 scx_stage[262144];          /* M7 编译器代码中转；BSS 已移至 1MB 保留区 */

int task_exec_scx(const char *path)
{
    /* EXEC 接受“路径 空格 参数串”。内核只切出首个路径，参数原样传给
     * 用户程序解释；这样汇编器/记事本可以选择输入输出文件，内核无需
     * 知道任何具体应用的语法。拷贝完再建任务，消除父子缓冲共享。 */
    char filename[64];
    int len = 0;
    while (*path == ' ') path++;
    while (path[len] && path[len] != ' ' && len < 63) { filename[len] = path[len]; len++; }
    if (path[len] && path[len] != ' ') return -1;
    filename[len] = 0;
    const char *arguments = path + len;
    while (*arguments == ' ') arguments++;
    int arglen = 0;
    while (arguments[arglen] && arglen < 127) arglen++;
    if (arguments[arglen]) return -1;
    u32 icon_offset,icon_length;
    int checked=image_scx_info(filename,scx_stage,&icon_offset,&icon_length);
    if(checked)return checked;

    u32 entry_rva = *(u32 *)(scx_stage + 8);
    u32 load_size = *(u32 *)(scx_stage + 12);
    u32 bss_size  = *(u32 *)(scx_stage + 16);
    u32 stack_size = *(u32 *)(scx_stage + 20);
    /* 图标只是容器资源，不搬进程序地址域。先校验整个容器，再仅读
     * 载荷，既保留旧装载上限，也支持“最大载荷+独立图标”大文件。 */
    if(fs_read_at(filename,scx_stage+36,load_size,36)!=(int)load_size)return -1;

    u32 pd = paging_new_task_dir();
    if (!pd) return -4;
    u32 npages = (load_size + bss_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = 0; i < npages; i++) {
        u32 fr = pframe_alloc();
        if (fr == 0) { paging_free_task_dir(pd); return -4; }
        paging_map_user(pd, USER_BASE + i * PAGE_SIZE, fr);
        /* 经恒等映射把内容写进物理帧 */
        u8 *dst = (u8 *)fr;
        for (u32 k = 0; k < PAGE_SIZE; k++) {
            u32 file_off = i * PAGE_SIZE + k;
            dst[k] = (file_off < load_size) ? scx_stage[36 + file_off]
                   : (file_off < load_size + bss_size) ? 0 : 0;
        }
    }
    /* 栈在用户区顶端向下生长，不紧挨映像尾部。
     * SCX 建议长度按页向上取整；每页先分配成功再登记，失败时统一回滚
     * 已登记的用户页。映像上限 0x3D0000B、普通栈 128KB，并另留一页异常栈，三者不重叠。 */
    for (u32 i = 0; i < (stack_size+PAGE_SIZE-1)/PAGE_SIZE; i++) {
        u32 fr = pframe_alloc();
        if (!fr) { paging_free_task_dir(pd); return -4; }
        paging_map_user(pd, USER_TOP-(i+1)*PAGE_SIZE, fr);
        for (u32 k = 0; k < PAGE_SIZE; k++) ((u8 *)fr)[k] = 0;
    }

    char nm[12];
    /* 异常栈独立映射一页，不依赖程序损坏的普通栈；失败完整回滚。 */
    u32 exception_frame=pframe_alloc();
    if(!exception_frame) { paging_free_task_dir(pd); return -4; }
    paging_map_user(pd,USER_EXCEPTION_TOP-PAGE_SIZE,exception_frame);
    for(u32 k=0;k<PAGE_SIZE;k++) ((u8 *)exception_frame)[k]=0;
    const char *p = filename;
    for(int i=0;filename[i];i++) if(filename[i]=='/') p=filename+i+1;
    for (int k = 0; k < 11; k++) {
        nm[k] = p[k];
        if (!p[k]) break;
    }
    nm[11] = 0;
    int pid = task_spawn(nm, pd, USER_BASE + entry_rva, USER_TOP - 16);
    if (pid < 0) paging_free_task_dir(pd);
    else {
        for (int i = 0; i <= arglen; i++) tasks[pid].args[i] = arguments[i];
        userspace_executable(pid,filename);
    }
    return pid;
}
