/* ============================================================
 *  SandCore 内核 —— main.c
 *  M0 引导画面 → M1 中断/控制台 → M2 内存/Shell → M3 中文/双缓冲
 *  → M4 窗口/鼠标 → M6 桌面化:
 *     * 分页开启, 每任务独立地址空间 (多用户程序并发的前提)
 *     * 内核不再内置 Shell —— shell 是 SandFS 上的普通 ring3 程序
 *     * 桌面图标来自 SandFS 的 desk/ 快捷方式 (含 SCF 图标)
 *     * ATA + SandFS + 字体文件上盘 (渲染器零改动)
 * ============================================================ */
#include "io.h"
#include "palette.h"
#include "gfx.h"
#include "interrupts.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "memory.h"
#include "heap.h"
#include "paging.h"
#include "ata.h"
#include "fs.h"
#include "task.h"
#include "wm.h"
#include "module.h"
#include "display.h"

/* ---------------- 调色板 ----------------
 * DAC 端口协议: 0x3C8 写起始槽位, 0x3C9 连写 R/G/B;
 * 每通道 6 位精度, 8 位色值必须 >>2。槽位表见 docs/GFX.md。 */
static void palette_init(void)
{
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        u8 r = 0, g = 0, b = 0;
        if (i >= PAL_SKY && i < PAL_SKY + 15) {         /* 天空渐变 */
            u32 t = (u32)(i - PAL_SKY) * 255 / 14;
            r = (u8)(28 + t * 122 / 255);
            g = (u8)(22 + t * 68 / 255);
            b = (u8)(70 - t * 34 / 255);
        } else if (i >= PAL_SAND_L && i < PAL_SAND_L + 8) {
            u32 t = (u32)(i - PAL_SAND_L) * 255 / 7;    /* 后排亮沙 */
            r = (u8)(125 - t * 37 / 255);
            g = (u8)(96 - t * 28 / 255);
            b = (u8)(60 - t * 16 / 255);
        } else if (i >= PAL_SAND_D && i < PAL_SAND_D + 8) {
            u32 t = (u32)(i - PAL_SAND_D) * 255 / 7;    /* 前排暗沙 */
            r = (u8)(100 - t * 30 / 255);
            g = (u8)(76 - t * 23 / 255);
            b = (u8)(48 - t * 14 / 255);
        } else if(i>=PAL_UI_NIGHT && i<PAL_UI_INK) {
            /* 七组材质渐变，RGB 端点只在权威调色板初始化出现。
             * 软件 3D 把亮度离散为 0..7，图形应用无需重编内核换色。 */
            static const u8 ends[7][6]={
                {8,12,24,34,48,67},{20,58,69,129,224,222},
                {87,41,39,255,202,116},{31,38,55,156,177,183},
                {12,45,43,86,164,105},{50,30,38,171,105,66},
                {16,45,69,64,158,179}
            };
            int group=(i-PAL_UI_NIGHT)/8,t=(i-PAL_UI_NIGHT)%8;
            r=(u8)(ends[group][0]+(ends[group][3]-ends[group][0])*t/7);
            g=(u8)(ends[group][1]+(ends[group][4]-ends[group][1])*t/7);
            b=(u8)(ends[group][2]+(ends[group][5]-ends[group][2])*t/7);
        } else {
            switch (i) {
            case PAL_SUN_CORE: r = 255; g = 224; b = 130; break;
            case PAL_SUN_EDGE: r = 255; g = 166; b = 66;  break;
            case PAL_STAR:     r = 170; g = 170; b = 205; break;
            case PAL_TITLE:    r = 235; g = 232; b = 220; break;
            case PAL_SHADOW:   r = 25;  g = 16;  b = 20;  break;
            case PAL_CON_BG:   r = 16;  g = 18;  b = 30;  break;
            case PAL_CON_TINT: r = 110; g = 200; b = 190; break;
            case PAL_CON_WARN: r = 235; g = 180; b = 80;  break;
            case PAL_PANIC:    r = 200; g = 45;  b = 45;  break;
            case PAL_WIN_TITLE:r = 96;  g = 100; b = 116; break;
            case PAL_TASKBAR:  r = 26;  g = 28;  b = 40;  break;
            case PAL_UI_INK: r=10; g=13; b=23; break;
            case PAL_UI_TEXT: r=220; g=234; b=232; break;
            case PAL_UI_MUTED: r=110; g=137; b=153; break;
            case PAL_UI_ALERT: r=235; g=94; b=103; break;
            case PAL_UI_PANEL: r=24; g=32; b=48; break;
            case PAL_UI_LINE: r=53; g=73; b=91; break;
            }
        }
        display_color(i,r,g,b);      /* 两种设备使用同一调色板定义 */
        outb(0x3C9, r >> 2);
        outb(0x3C9, g >> 2);
        outb(0x3C9, b >> 2);
    }
}

/* ---------------- 地形 ----------------
 * 抛物线"鼓包"叠出连绵沙丘 (256 定点, 内核禁浮点) */
static i32 bump(i32 x, i32 cx, i32 half_w, i32 hmax)
{
    i32 t = (x - cx) * 256 / half_w;
    if (t < -256 || t > 256)
        return 0;
    return hmax * (256 - t * t / 256) / 256;
}

static i32 ridge_back(i32 x)
{
    return 148 - bump(x,  90, 150, 18) - bump(x, 235, 120, 12);
}

static i32 ridge_front(i32 x)
{
    return 178 - bump(x, 160, 220, 10);
}

/* ============================================================
 *  开机画面: 沙漠、落日、中英文标题
 *  scene_paint() 只画进后台缓冲 (wm 壁纸复用);
 *  scene_draw() = paint + 上屏, 用于开机展示。
 * ============================================================ */
void scene_paint(void)
{
    if(modules_paint()) return;       /* M7：实际场景可来自已重定位的 CORE.SKM */
    int wallpaper=wm_wallpaper();
    for (i32 x = 0; x < 320; x++) {
        i32 rb = ridge_back(x);
        i32 rf = ridge_front(x);
        for (i32 y = 0; y < 200; y++) {
            u8 c;
            if (y < rb) {
                u32 t = (u32)y * 14 / 145;
                if (t > 14)
                    t = 14;
                c = PAL_SKY + (u8)t;
            } else if (y < rf) {
                i32 d = y - rb;
                c = PAL_SAND_L + (u8)(d > 42 ? 7 : d / 6);
            } else {
                i32 d = y - rf;
                c = PAL_SAND_D + (u8)(d > 42 ? 7 : d / 6);
            }
            gfx_pset(x, y, c);
        }
    }

    /* 壁纸变体复用已登记的渐变槽位，模式切换是设计好的整体氛围，
     * 不在用户态临时改 DAC。默认保留沙丘，夜色与暖沙提供安静背景。 */
    if(wallpaper)
        for(i32 y=0;y<184;y++)
            gfx_fill(0,y,320,1,wallpaper==1 ? PAL_SKY+(u8)(y*4/184)
                                                       : PAL_SAND_D+(u8)(y*7/184));

    for (u32 i = 0; i < 48; i++) {
        u32 sx = (i * 97 + 13) % 320;
        u32 sy = (i * 53 + 7) % 22;
        gfx_pset((i32)sx, (i32)sy, PAL_STAR);
    }

    gfx_circle(282, 106, 14, PAL_SUN_EDGE, 1);
    gfx_circle(282, 106, 9, PAL_SUN_CORE, 1);

}

void scene_draw(void)
{
    scene_paint();
    /* 标题只属于开机页，桌面留白给图标与窗口。 */
    gfx_text8(46, 31, "SANDCORE", PAL_SHADOW, 3, 6);
    gfx_text8(43, 28, "SANDCORE", PAL_TITLE, 3, 6);
    gfx_text16(143, 62, "沙核", PAL_TITLE, 0);
    gfx_text8(150, 84, "V0.7", PAL_TITLE, 1, 1);
    gfx_fill(0, 184, 320, 16, PAL_SHADOW);
    gfx_text8(102, 188, "PRESS ANY KEY", PAL_TITLE, 1, 1);
    gfx_swap();
}

/* ============================================================
 *  内核主入口
 * ============================================================ */
void kmain(void)
{
    gfx_init();
    palette_init();
    scene_draw();                     /* 开机画面 (分页前, 恒等即线性) */

    interrupts_init();                /* IDT/PIC/PIT/键盘 */
    mouse_init();                     /* PS/2 鼠标 */

    getkey();                         /* 任意键: 中断管线贯通的活证据 */

    /* ---- 桌面化初始化 (M6) ---- */
    memory_init();                    /* E820 + 位图分配器 */
    heap_init();                      /* 内核堆 (2-3MB) */
    pf_reserve(0x300000, 0x500000);   /* 页表区+任务槽保护区 (3-8MB) */
    pf_reserve(MODULE_BASE,MODULE_BYTES); /* 在任何用户帧分配前预留永久零环模块区 */
    task_init();                      /* 分页开启 + GDT/TSS/调度器 */
    ata_init();                       /* ATA PIO 驱动 */
    fs_init();                        /* SandFS 超级块+目录表 */
    wm_init();                        /* 桌面图标 + 窗口表 */
    cli();
    modules_init();                  /* CORE 先于 MOD，安装真实零环回调 */
    sti();

    /* 字体文件上盘策略: SandFS 里的 font.scf 优先为默认字体,
     * 读不到回落编译在内核里的内置表 (渲染器零改动) */
    static u8 fbuf[65536];
    int fn = fs_read("sys/font.scf", fbuf, sizeof(fbuf));
    if (fn > 0)
        gfx_font_load(fbuf, (u32)fn);

    cli();
    display_init();                  /* 设备/配置有效才进入原生模式，否则保留13h */
    sti();

    wm_request_compose();             /* 桌面第一帧 */

    /* 主循环 = 任务 0: 鼠标轮询/秒跳/脏了就合成, 其余时间 hlt */
    u32 last_sec = 0xFFFFFFFF;
    for (;;) {
        u32 sec = sc_ticks / 100;
        if (sec != last_sec) {
            last_sec = sec;
            wm_request_compose();     /* 任务栏时钟 */
        }
        cli();                       /* 合成期间冻结窗口表，避免用户调用插入半帧 */
        display_poll();              /* 设置程序退出也不会取消安全回退计时 */
        wm_mouse_poll();              /* 图标/拖拽/关闭/置顶 */
        if (wm_dirty())
            wm_compose();
        sti();
        halt();
    }
}
