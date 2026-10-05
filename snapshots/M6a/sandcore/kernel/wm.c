/* =====================================================================
 *  SandCore 窗口管理器 v2 (kernel/wm.c)
 *  ---------------------------------------------------------------------
 *  【M6 质变】窗口成为系统调用提供的资源:
 *    用户程序 (ring3) 经 SYS_WINOPEN 开窗口 → 内核分配"画布"
 *    (客户区像素缓冲) → 程序用 SYS_TXT/FILL 在画布上作画 →
 *    合成器把画布 blit 到屏幕; SYS_GETKEY/SYS_MOUSE 送输入。
 *    程序退出时其窗口自动回收 (owner 检查)。
 *
 *  【桌面】开机即桌面: 图标来自 SandFS 的 desk/*.lnk (文本:
 *    第1行=显示名, 第2行=目标 SCX 路径, 第3行=图标 SCF 路径),
 *    单击图标 → 运行目标程序。图标本体 = SCF1MIO 16x16 文件。
 * ===================================================================== */
#include "io.h"
#include "palette.h"
#include "gfx.h"
#include "term.h"
#include "mouse.h"
#include "timer.h"
#include "task.h"
#include "fs.h"
#include "wm.h"

extern void scene_paint(void);        /* main.c: 沙漠壁纸 (画进后台缓冲) */

#define TITLE_H 13
#define BORDER  1
#define WMAX 6
#define CANVAS_W 304
#define CANVAS_H 150

typedef struct {
    int used;
    int owner;                        /* 创建者 pid (退出自动回收) */
    i32 x, y, w, h;                   /* 外框 */
    char title[12];
    u8 *canvas;                       /* 客户区像素缓冲 */
    i32 cw, ch;                       /* 画布尺寸 */
    u8 keys[16];                      /* 按键队列 */
    u8 kq, kt;
} win_t;

static win_t wins[WMAX];
static int nwins;
static int dirty = 1;

static u8 canvas_pool[WMAX][CANVAS_W * CANVAS_H];   /* ~292KB BSS */

static int  drag = -1;
static i32  drag_ox, drag_oy;

void wm_request_compose(void) { dirty = 1; }
int  wm_dirty(void)           { return dirty; }

/* ---------------- 桌面图标 (desk/*.lnk) ----------------
 * lnk 文本: 第1行 显示名 / 第2行 目标SCX路径 / 第3行 图标SCF路径 */
typedef struct {
    int used;
    char label[12];
    char target[32];
    u8 icon[3000];                    /* 图标 SCF 文件本体 */
} dicon_t;

static dicon_t dicons[4];

static void icn_draw(i32 x, i32 y, const u8 *icn, u8 fg)
{
    /* SCF1MIO: 取第 0 个字形的 16 行点阵直接画 */
    int n = icn[8] | (icn[9] << 8);
    if (n < 1)
        return;
    const u8 *rows = icn + 12 + 4;    /* 跳过第 0 字的 utf8+NUL 槽 */
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            if (rows[r * 17 + c] == '#')
                gfx_pset(x + c, y + r, fg);
}

static void load_desktop(void)
{
    int di = 0;
    for (int i = 0; i < fs_count() && di < 4; i++) {
        const char *n = fs_name(i);
        if (n[0] != 'd' || n[1] != 'e' || n[2] != 's' || n[3] != 'k' || n[4] != '/')
            continue;                 /* 只要 desk/ 下的 .lnk */

        static u8 lnkbuf[4][512];     /* 每个图标一份 lnk 本体 */
        u8 *lnk = lnkbuf[di];
        if (fs_read(n, lnk, 512) < 0)
            continue;

        dicon_t *d = &dicons[di];
        int c = 0, line = 0, k = 0;
        d->label[0] = 0;
        d->target[0] = 0;
        for (k = 0; lnk[k] && k < 500; k++) {
            if (lnk[k] == '\n') {     /* 行结束 */
                if (line == 0) d->label[c] = 0;
                if (line == 1) d->target[c] = 0;
                line++;
                c = 0;
                continue;
            }
            if (line == 0 && c < 11) d->label[c++] = (char)lnk[k];
            if (line == 1 && c < 30) d->target[c++] = (char)lnk[k];
        }
        if (line == 0) d->label[c] = 0;
        if (line == 1) d->target[c] = 0;
        if (line == 2) { /* 第 3 行 = 图标路径, 读 SCF 本体 */
            char icp[32];
            int m = 0;
            k++;                      /* 跳过该行首字符? 不: 行已含全部字符 */
            for (int j = k; lnk[j] && m < 30; j++)
                icp[m++] = (char)lnk[j];
            icp[m] = 0;
            if (fs_read(icp, d->icon, sizeof(d->icon)) > 0)
                d->used = 1;
        }
        di++;
    }
}

/* ---------------- 窗口句柄 API (系统调用落地) ---------------- */
static int win_create(int owner, const char *title, i32 x, i32 y, i32 w, i32 h)
{
    if (nwins >= WMAX || w > CANVAS_W || h > CANVAS_H)
        return -1;
    win_t *np = &wins[nwins];          /* 注意: 不能叫 w, 与宽度参数重名 */
    np->used = 1;
    np->owner = owner;
    np->x = x; np->y = y; np->w = w; np->h = h;
    for (int k = 0; k < 11; k++) {
        np->title[k] = title[k];
        if (!title[k]) break;
    }
    np->title[11] = 0;
    np->canvas = canvas_pool[nwins];
    np->cw = (w - 2 * BORDER > CANVAS_W) ? CANVAS_W : (w - 2 * BORDER);
    np->ch = (h - TITLE_H - 2 * BORDER > CANVAS_H) ? CANVAS_H
           : (h - TITLE_H - 2 * BORDER);
    for (u32 i = 0; i < (u32)(np->cw * np->ch); i++)
        np->canvas[i] = PAL_CON_BG;
    np->kq = np->kt = 0;
    return nwins++;
}

static void win_close(int idx)
{
    if (idx < 0 || idx >= nwins)
        return;
    for (int i = idx; i < nwins - 1; i++) {
        wins[i] = wins[i + 1];
        wins[i].canvas = canvas_pool[i];   /* 画布跟随槽位 */
    }
    nwins--;
    if (drag == idx) drag = -1;
    else if (drag > idx) drag--;
}

static void win_raise(int idx)
{
    win_t top = wins[idx];
    top.canvas = canvas_pool[nwins - 1];   /* 画布跟槽位走 */
    for (int i = idx; i < nwins - 1; i++) {
        wins[i] = wins[i + 1];
        wins[i].canvas = canvas_pool[i];
    }
    wins[nwins - 1] = top;
}

/* ---------------- 用户窗口 API (系统调用落地) ---------------- */
int wm_open_user_window(int pid, const char *title, int w, int h)
{
    if (h > CANVAS_H) h = CANVAS_H;
    if (w > CANVAS_W) w = CANVAS_W;
    i32 x = (320 - w) / 2, y = (184 - h) / 2;
    int hnd = win_create(pid, title, x, y, w, h);
    if (hnd >= 0) dirty = 1;
    return hnd;
}

void wm_close_user_window(int pid, int handle)
{
    if (handle >= 0 && handle < nwins && wins[handle].owner == pid)
        win_close(handle);
    dirty = 1;
}

void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color)
{
    (void)pid;
    if (handle < 0 || handle >= nwins)
        return;
    win_t *w = &wins[handle];
    while (*s) {
        gfx_char8(w->x + BORDER + x, w->y + TITLE_H + y, *s, color, 1);
        s++;
        x += 8;
    }
    dirty = 1;
}

void wm_user_fill(int pid, int handle, int x, int y, int w2, int h2, u8 color)
{
    (void)pid;
    if (handle < 0 || handle >= nwins)
        return;
    win_t *w = &wins[handle];
    for (int j = 0; j < h2; j++)
        for (int i = 0; i < w2; i++) {
            i32 px = w->x + BORDER + x + i, py = w->y + TITLE_H + y + j;
            if (px >= w->x + 1 && px < w->x + w->w - 1
             && py >= w->y + TITLE_H && py < w->y + w->h - 1)
                gfx_pset(px, py, color);
        }
    dirty = 1;
}

/* ---------------- 绘制 ---------------- */
static void draw_window(int i)
{
    win_t *w = &wins[i];
    int focused = (i == nwins - 1);

    gfx_fill(w->x, w->y, w->w, w->h, focused ? PAL_CON_TINT : PAL_WIN_TITLE);
    gfx_fill(w->x + BORDER, w->y + TITLE_H, w->w - 2 * BORDER,
             w->h - TITLE_H - BORDER, PAL_CON_BG);
    gfx_fill(w->x + BORDER, w->y + BORDER, w->w - 2 * BORDER, TITLE_H - 2,
             focused ? PAL_CON_TINT : PAL_WIN_TITLE);
    gfx_text8(w->x + 4, w->y + 3, w->title, PAL_CON_BG, 1, 1);
    gfx_fill(w->x + w->w - 12, w->y + 2, 10, 10, PAL_PANIC);
    gfx_line(w->x + w->w - 10, w->y + 4, w->x + w->w - 4, w->y + 10, PAL_TITLE);
    gfx_line(w->x + w->w - 10, w->y + 10, w->x + w->w - 4, w->y + 4, PAL_TITLE);

    /* 画布 blit: 客户区像素逐点贴到后台缓冲 */
    for (int r = 0; r < w->ch; r++)
        for (int c = 0; c < w->cw; c++)
            gfx_pset(w->x + BORDER + c, w->y + TITLE_H + r, w->canvas[r * w->cw + c]);
}

static void draw_taskbar(void)
{
    gfx_fill(0, 184, 320, 16, PAL_TASKBAR);
    gfx_text16(4, 184, "沙核", PAL_TITLE, 0);
    gfx_text8(238, 188, "UP ", PAL_CON_TINT, 1, 1);
    u32 s = sc_ticks / 100;
    char b[11];
    i32 n = 0;
    do { b[n++] = (char)('0' + s % 10); s /= 10; } while (s);
    i32 x = 254 + (n - 1) * 8;
    while (n--) {
        gfx_char8(x, 188, b[n], PAL_CON_TINT, 1);
        x += 8;
    }
    gfx_text8(x + 8, 188, "S", PAL_CON_TINT, 1, 1);
}

static const char *cursor_art[13] = {
    "#.......",
    "##......",
    "#o#.....",
    "#oo#....",
    "#ooo#...",
    "#oooo#..",
    "#ooooo#.",
    "#oooooo#",
    "#oooooo#",
    "#ooo####",
    "#o#.#...",
    "#..#.#..",
    "....#...",
};

static void draw_cursor(void)
{
    i32 x = mouse_x(), y = mouse_y();
    for (int r = 0; r < 13; r++)
        for (int c = 0; c < 8; c++) {
            char a = cursor_art[r][c];
            if (a == '#')
                gfx_pset(x + c, y + r, PAL_TITLE);
            else if (a == 'o')
                gfx_pset(x + c, y + r, PAL_SHADOW);
        }
}

static void draw_icons(void)
{
    for (int i = 0; i < 4; i++) {
        if (!dicons[i].used)
            continue;
        i32 x = 8, y = 8 + i * 44;
        icn_draw(x, y, dicons[i].icon, PAL_TITLE);
        gfx_text8(x + 20, y + 4, dicons[i].label, PAL_TITLE, 1, 1);
    }
}

/* ---------------- 合成 ---------------- */
void wm_compose(void)
{
    scene_paint();
    draw_icons();
    for (int i = 0; i < nwins; i++)
        draw_window(i);
    draw_taskbar();
    draw_cursor();
    gfx_swap();
    dirty = 0;
}

/* ---------------- 鼠标交互 ---------------- */
static int hit_window(i32 x, i32 y)
{
    for (int i = nwins - 1; i >= 0; i--)
        if (x >= wins[i].x && x < wins[i].x + wins[i].w
         && y >= wins[i].y && y < wins[i].y + wins[i].h)
            return i;
    return -1;
}

void wm_mouse_poll(void)
{
    static i32 pmx = -1, pmy = -1;
    static u8  pbtn = 0;

    i32 x = mouse_x(), y = mouse_y();
    u8  b = mouse_buttons();
    if (x == pmx && y == pmy && b == pbtn)
        return;

    if ((b & 1) && !(pbtn & 1)) {     /* 左键按下沿 */
        /* 图标命中? (桌面层, 优先级最低但在最上面画 —— 这里先查图标) */
        for (int i = 0; i < 4; i++) {
            if (!dicons[i].used) continue;
            i32 ix = 8, iy = 8 + i * 44;
            if (x >= ix && x < ix + 24 && y >= iy && y < iy + 18) {
                task_exec_scx(dicons[i].target);
                pmx = x; pmy = y; pbtn = b;
                dirty = 1;
                return;
            }
        }
        int w = hit_window(x, y);
        if (w >= 0) {
            win_raise(w);
            if (x >= wins[w].x + wins[w].w - 12 && x < wins[w].x + wins[w].w - 2
             && y >= wins[w].y + 2 && y < wins[w].y + 12) {
                win_close(w);
                drag = -1;
            } else if (y < wins[w].y + TITLE_H) {
                drag = nwins - 1;
                drag_ox = x - wins[drag].x;
                drag_oy = y - wins[drag].y;
            }
        }
    }
    if (!(b & 1) && (pbtn & 1))
        drag = -1;

    if (drag >= 0 && (b & 1)) {
        i32 nx = x - drag_ox, ny = y - drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > 320 - wins[drag].w) nx = 320 - wins[drag].w;
        if (ny > 184 - wins[drag].h) ny = 184 - wins[drag].h;
        wins[drag].x = nx;
        wins[drag].y = ny;
    }

    pmx = x; pmy = y; pbtn = b;
    dirty = 1;
}

/* ---------------- 键盘路由 ----------------
 * keyboard.c 的队列由这里消费: 按键进"聚焦窗口"(最顶) 的队列 */
void wm_key_input(char c)
{
    win_t *w = &wins[nwins - 1];
    if (!nwins || !w->used)
        return;
    u8 n = (u8)((w->kq + 1) % 16);
    if (n != w->kt) {
        w->keys[w->kq] = (u8)c;
        w->kq = n;
    }
}

static int win_popkey(win_t *w)
{
    if (w->kq == w->kt)
        return -1;
    char c = w->keys[w->kt];
    w->kt = (u8)((w->kt + 1) % 16);
    return c;
}

int wm_user_getkey(int pid)
{
    (void)pid;
    if (nwins == 0)
        return -1;
    return win_popkey(&wins[nwins - 1]);
}

/* ---------------- 初始化 ---------------- */
void wm_init(void)
{
    nwins = 0;
    for (int i = 0; i < 4; i++)
        dicons[i].used = 0;
    load_desktop();                   /* 读 desk/*.lnk + 图标 */
    dirty = 1;
}

void wm_open_about(void)              /* 兼容保留: about 命令开中文页窗口 */
{
    /* v2: 关于页内容由应用自己画, 这里不再内置 */
}

/* ---------------- 异常红屏 (interrupts.c 调) ---------------- */
void panic_screen(const char *reason, u32 vec)
{
    gfx_fill(0, 0, 320, 200, PAL_PANIC);
    gfx_text8(16, 16, "== SANDCORE PANIC ==", 0, 1, 1);
    gfx_text8(16, 32, reason, 0, 1, 1);
    gfx_text8(16, 48, "VEC 0x", 0, 1, 1);
    gfx_char8(72, 48, "0123456789ABCDEF"[(vec >> 4) & 0xF], 0, 1);
    gfx_char8(80, 48, "0123456789ABCDEF"[vec & 0xF], 0, 1);
    gfx_text8(16, 64, "----", 0, 1, 1);
    gfx_swap();
}
