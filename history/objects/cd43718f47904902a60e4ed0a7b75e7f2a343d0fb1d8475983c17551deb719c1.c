/* =====================================================================
 *  SandCore 窗口管理器 v2 (kernel/wm.c)
 *  ---------------------------------------------------------------------
 *  【M6 质变】窗口成为系统调用提供的资源:
 *    用户程序 (ring3) 经 SYS_WINOPEN 开窗口 → 内核分配"画布"
 *    (客户区像素缓冲) → 程序用 SYS_TXT/FILL 在画布上作画 →
 *    合成器把画布 blit 到屏幕; SYS_GETKEY/SYS_MOUSE 送输入。
 *    程序退出时其窗口自动回收 (owner 检查)。
 *
 *  【桌面】开机即桌面: 图标来自 SandFS 的 desk/ 快捷方式 (文本:
 *    第1行=显示名, 第2行=目标 SCX 路径, 第3行=图标 SCF 路径),
 *    单击图标 → 运行目标程序。图标本体 = SCF1MIO 16x16 文件。
 * ===================================================================== */
#include "io.h"
#include "palette.h"
#include "gfx.h"
#include "mouse.h"
#include "timer.h"
#include "task.h"
#include "fs.h"
#include "wm.h"
#include "font.h"
#include "memory.h"
#include "display.h"
#include "desktop.h"

extern void scene_paint(void);        /* main.c: 沙漠壁纸 (画进后台缓冲) */

#define TITLE_H (GFX_W==320?13:24*display_scale()/100)
#define BORDER  1
#define WMAX 6
#define CANVAS_W SC_CANVAS_W
#define CANVAS_H SC_CANVAS_H

typedef struct {
    int used;
    int owner;                        /* 创建者 pid (退出自动回收) */
    int handle;                       /* 句柄独立于 z 序，置顶不会串窗口 */
    i32 x, y, w, h;                   /* 外框 */
    char title[12];
    u8 *canvas;                       /* 客户区像素缓冲 */
    i32 cw, ch;                       /* 画布尺寸 */
    u8 keys[16];                      /* 按键队列 */
    u8 kq, kt;
    i32 tx, ty;
} win_t;

static win_t wins[WMAX];
static int nwins;
static int next_handle;
/* 窗口主体仍保留80B诊断布局；扩展状态按稳定handle关联，置顶移动
 * win_t 不会把最小化/原生页所有权串给另一个窗口。 */
typedef struct {int handle,native,scale,hidden,maximized;u32 pages,pressed,released;
    int restore_x,restore_y,restore_w,restore_h;} window_extra_t;
static window_extra_t extras[WMAX];
static window_extra_t *extra_of(int handle)
{for(int i=0;i<WMAX;i++)if(extras[i].handle==handle)return &extras[i];return 0;}
static int top_visible(void)
{for(int i=nwins-1;i>=0;i--){window_extra_t *e=extra_of(wins[i].handle);if(e && !e->hidden)return i;}return -1;}
static int work_bottom(void){return GFX_H-(GFX_W==320?16:32*display_scale()/100);}
static int wallpaper_mode;
static int dirty = 1;
typedef struct { int active; u32 vector,error,eip,address,order; } fault_card_t;
static fault_card_t fault_cards[NTASK];
static u32 fault_order;
static int fault_focus;

static int fault_top(void)
{
    int top=0;
    for(int i=1;i<NTASK;i++)
        if(fault_cards[i].active && (!top || fault_cards[i].order>fault_cards[top].order)) top=i;
    return top;
}
void wm_exception(int pid,u32 vector,u32 error,u32 eip,u32 address)
{
    if(pid<=0 || pid>=NTASK) return;
    fault_cards[pid]=(fault_card_t){1,vector,error,eip,address,++fault_order};
    fault_focus=1; dirty=1;
}

static u8 canvas_pool[WMAX][CANVAS_W * CANVAS_H];   /* M7 大画布约 324KB BSS，位于 1MB 保留区 */

static int  drag = -1;
static int resizing=-1,resize_w,resize_h;
static i32  drag_ox, drag_oy;

void wm_request_compose(void) { dirty = 1; }
int  wm_dirty(void)           { return dirty; }

/* ---------------- 桌面图标 (desk/ 下的 .lnk) ----------------
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
        int len = 0;
        while (n[len]) len++;
        if (len < 9 || n[0] != 'd' || n[1] != 'e' || n[2] != 's' || n[3] != 'k' || n[4] != '/'
         || n[len-4] != '.' || n[len-3] != 'l' || n[len-2] != 'n' || n[len-1] != 'k')
            continue;                 /* 只要 desk/ 下的 .lnk */

        static u8 lnkbuf[4][512];     /* 每个图标一份 lnk 本体 */
        u8 *lnk = lnkbuf[di];
        int bytes = fs_read(n, lnk, 511);
        if (bytes <= 0)
            continue;
        lnk[bytes] = 0;

        dicon_t *d = &dicons[di];
        char icp[32] = {0};
        int c = 0, line = 0, k = 0;
        d->label[0] = 0;
        d->target[0] = 0;
        for (k = 0; k < bytes && lnk[k] && line < 3; k++) {
            if (lnk[k] == '\r') continue;
            if (lnk[k] == '\n') {     /* 行结束 */
                if (line == 0) d->label[c] = 0;
                if (line == 1) d->target[c] = 0;
                if (line == 2) icp[c] = 0;
                line++;
                c = 0;
                continue;
            }
            if (line == 0 && c < 11) d->label[c++] = (char)lnk[k];
            if (line == 1 && c < 30) d->target[c++] = (char)lnk[k];
            if (line == 2 && c < 30) icp[c++] = (char)lnk[k];
        }
        if (line == 0) d->label[c] = 0;
        if (line == 1) d->target[c] = 0;
        if (line == 2) icp[c] = 0;
        int count = fs_read(icp, d->icon, sizeof(d->icon));
        if (count >= 288 && d->icon[0] == 'S' && d->icon[1] == 'C'
         && d->icon[2] == 'F' && d->icon[3] == '1' && d->icon[4] == 'M'
         && d->icon[5] == 'I' && d->icon[6] == 'O' && d->icon[8] == 1) {
            d->used = 1;
            di++;
        }
    }
}

/* ---------------- 窗口句柄 API (系统调用落地) ---------------- */
static int win_create(int owner, const char *title, i32 x, i32 y, i32 w, i32 h,int native)
{
    if (nwins >= WMAX || w < 48 || h < 32 || w > (native?1920:320) || h > (native?1080:184))
        return -1;
    win_t *np = &wins[nwins];          /* 注意: 不能叫 w, 与宽度参数重名 */
    np->used = 1;
    np->owner = owner;
    np->handle = next_handle++;
    window_extra_t *ex=0;
    for(int i=0;i<WMAX;i++)if(!extras[i].handle){ex=&extras[i];break;}
    *ex=(window_extra_t){0};ex->handle=np->handle;ex->native=native;ex->scale=1;
    np->x = x; np->y = y; np->w = w; np->h = h;
    for (int k = 0; k < 11; k++) {
        np->title[k] = title[k];
        if (!title[k]) break;
    }
    np->title[11] = 0;
    /* 找空闲画布；窗口结构移动时保留指针，内容始终跟随窗口。 */
    for (int c = 0; !native && c < WMAX; c++) {
        int busy = 0;
        for (int i = 0; i < nwins; i++)
            if (wins[i].canvas == canvas_pool[c]) busy = 1;
        if (!busy) { np->canvas = canvas_pool[c]; break; }
    }
    np->cw = w - 2 * BORDER;
    np->ch = h - (native?TITLE_H:13) - BORDER;
    if(native) {
        if(np->ch<16){ex->handle=0;return -1;}
        ex->pages=((u32)np->cw*np->ch+4095)/4096;
        np->canvas=(u8 *)pframe_alloc_run(ex->pages);
        if(!np->canvas){ex->handle=0;return -1;}
    } else if(GFX_W!=320) {
        ex->scale=(display_scale()+99)/100;
        np->w=np->cw*ex->scale+2;np->h=np->ch*ex->scale+TITLE_H+1;
        np->x=(GFX_W-np->w)/2;np->y=(work_bottom()-np->h)/2;
    }
    for (u32 i = 0; i < (u32)(np->cw * np->ch); i++)
        np->canvas[i] = PAL_CON_BG;
    np->kq = np->kt = 0;
    np->tx = np->ty = 4;
    nwins++;
    return np->handle;
}

static void win_close(int idx)
{
    if (idx < 0 || idx >= nwins)
        return;
    window_extra_t *ex=extra_of(wins[idx].handle);
    if(ex){if(ex->pages)pframe_free_run((u32)wins[idx].canvas,ex->pages);ex->handle=0;}
    for (int i = idx; i < nwins - 1; i++) {
        wins[i] = wins[i + 1];
    }
    nwins--;
    resizing=-1; /* 关闭会重排z序；拖框不能继续指向补位的别人的窗口 */
    if (drag == idx) drag = -1;
    else if (drag > idx) drag--;
}

static void win_raise(int idx)
{
    win_t top = wins[idx];
    for (int i = idx; i < nwins - 1; i++) {
        wins[i] = wins[i + 1];
    }
    wins[nwins - 1] = top;
}

/* ---------------- 用户窗口 API (系统调用落地) ---------------- */
int wm_open_user_window(int pid, const char *title, int w, int h)
{
    if (h > CANVAS_H + 14) h = CANVAS_H + 14;
    if (w > CANVAS_W + 2) w = CANVAS_W + 2;
    i32 x = (320 - w) / 2, y = (184 - h) / 2;
    int hnd = win_create(pid, title, x, y, w, h,0);
    if (hnd >= 0) dirty = 1;
    return hnd;
}

void wm_close_user_window(int pid, int handle)
{
    for (int i = 0; i < nwins; i++)
        if (wins[i].handle == handle && wins[i].owner == pid) { win_close(i); break; }
    dirty = 1;
}

void wm_close_owner(int pid)
{
    if(pid>0 && pid<NTASK) fault_cards[pid].active=0;
    if(!fault_top()) fault_focus=0;
    for (int i = nwins - 1; i >= 0; i--)
        if (wins[i].owner == pid) win_close(i);
    dirty = 1;
}

static win_t *owned_window(int pid, int handle)
{
    /* 句柄不是 wins 下标：数组只承担 z 序，点击置顶会移动结构。
     * 每次调用同时匹配稳定 ID 和 owner，既防止画进别人的窗口，
     * 也防止关闭中间窗口后老句柄误指向补位的新窗口。 */
    for (int i = 0; i < nwins; i++)
        if (wins[i].owner == pid && wins[i].handle == handle) return &wins[i];
    return 0;
}

#include "wm_native.inc"

int wm_focused(int pid)
{int top=top_visible();return top>=0 && wins[top].owner==pid && tasks[pid].state==1 && !fault_focus && !menu_open && !context_open; }
int wm_user_frame(int pid,int handle,const u8 *pixels,u32 length)
{
    win_t *w=owned_window(pid,handle);
    if(!w || length!=(u32)(w->cw*w->ch)) return -1;
    for(u32 i=0;i<length;i++) w->canvas[i]=pixels[i];
    dirty=1; return 0;
}

int wm_user_info(int pid,int handle,i32 *out)
{
    /* 鼠标 ABI 给出屏幕绝对坐标；应用需要外框位置才能转换为客户区。
     * 拖动由内核负责，应用每轮查询当前位置，不能缓存最初的居中位置。
     * focused 使后台游戏不会响应全局鼠标点击，避免点击穿透前景窗口。 */
    win_t *w=owned_window(pid,handle);
    if(!w) return -1;
    window_extra_t *e=extra_of(handle);
    out[0]=(GFX_W!=320 && !e->native)?0:w->x;
    out[1]=(GFX_W!=320 && !e->native)?0:w->y;
    out[2]=w->cw; out[3]=w->ch;
    out[4]=(w==&wins[top_visible()<0?0:top_visible()] && !e->hidden);
    return 0;
}

int wm_wallpaper(void) { return wallpaper_mode; }
int wm_set_wallpaper(int mode)
{
    if(mode<0 || mode>2) return -1;
    u8 value=(u8)('0'+mode);
    /* 设置先落盘，失败时保留当前壁纸；应用可据返回值显示保存失败。
     * 仅保存模式号，重启时仍用同一套 palette 槽位生成像素，无 64KB
     * 图片缓冲占用低端 BSS，也没有新增自定义魔数或裸 RGB 色号。 */
    if(fs_write("sys/wall.cfg",&value,1)!=1) return -1;
    wallpaper_mode=mode; dirty=1;
    return 0;
}

static void canvas_char(win_t *w, int x, int y, char ch, u8 color)
{
    /* 字形数据取统一凤凰字体的兼容 8px 表；这里只更换绘制目的地。
     * SYS_TXT 是透明底绘制，字形的零位保持画布原像素，适合覆盖图形。
     * 退格因此不能靠画一个空格完成，流式文字接口必须另行清整格。
     * 客户区坐标和屏幕坐标分开，拖动只变外框位置，不会重画字形。 */
    const u8 *g = glyph_of(ch);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (x+c >= 0 && x+c < w->cw && y+r >= 0 && y+r < w->ch
             && ((g[r] >> (7-c)) & 1)) w->canvas[(y+r)*w->cw+x+c] = color;
}

void wm_user_text(int pid, int handle, int x, int y, const char *s, u8 color)
{
    win_t *w = owned_window(pid, handle);
    if (!w) return;
    int start = x;
    while (*s) {
        if (*s == '\n') { x = start; y += 10; }
        else { canvas_char(w, x, y, *s, color); x += 8; }
        s++;
    }
    dirty = 1;
}

int wm_user_utf8(int pid,int handle,int x,int y,const char *text,u8 color)
{
    /* 使用与桌面相同的解码器/字形入口，程序无需私自解析 SCF。
     * 只写 owner 的画布，透明底，不经过后台合成缓冲。坐标先限幅，
     * 使 4095B 字串的累积字宽仍不会发生有符号整数溢出；负坐标
     * 合法，逐像素裁剪后可用于滚动/部分字形显露的自定义界面。 */
    win_t *w=owned_window(pid,handle);
    if(!w || x < -32768 || x > 32767 || y < -32768 || y > 32767) return -1;
    int left=x,count=0; u32 scalar; u16 rows[16];
    while(*text) {
        gfx_utf8_next(&text,&scalar);
        if(scalar=='\n') { x=left; y+=18; continue; }
        if(scalar=='\r') { x=left; continue; }
        int width=gfx_glyph16(scalar,rows);
        if(!width) width=16;
        for(int r=0;r<16;r++) for(int c=0;c<width;c++)
            if(x+c>=0 && x+c<w->cw && y+r>=0 && y+r<w->ch
               && (rows[r]&(0x8000u>>c))) w->canvas[(y+r)*w->cw+x+c]=color;
        x+=width; count++;
    }
    dirty=1; return count;
}

void wm_user_fill(int pid, int handle, int x, int y, int w2, int h2, u8 color)
{
    win_t *w = owned_window(pid, handle);
    if (!w || w2 <= 0 || h2 <= 0) return;
    for (int j = 0; j < w->ch; j++)
        for (int i = 0; i < w->cw; i++)
            if ((i32)(i-x) >= 0 && (u32)(i-x) < (u32)w2
             && (i32)(j-y) >= 0 && (u32)(j-y) < (u32)h2)
                w->canvas[j*w->cw+i] = color;
    if (x == 0 && y == 0 && w2 >= w->cw && h2 >= w->ch) w->tx = w->ty = 4;
    dirty = 1;
}

int wm_user_puts(int pid, const char *s)
{
    /* 【流式输出属于哪个窗口】
     * 一个应用可以开多窗口：选 handle 最大的本人窗口，即最近创建的窗口，
     * 而不是 z 序最顶的窗口。点别人的窗口不会把本应用的输出重定向过去。
     * tx/ty 存在窗口结构里，程序之间、窗口之间都不共享文字游标。
     * ASCII 字宽 8px、行步长 10px，四边留 4px；滚动源偏移必须是
     * 10*cw 字节（完整像素行），不是 cw 字节（只移动 1px）。 */
    win_t *w = 0;
    for (int i = 0; i < nwins; i++)
        if (wins[i].owner == pid && (!w || wins[i].handle > w->handle)) w = &wins[i];
    if (!w) return -1;
    while (*s) {
        char c = *s++;
        if (c == '\r') { w->tx = 4; continue; }
        if (c == '\b') {
            if (w->tx >= 12) w->tx -= 8;
            else if (w->ty >= 14) {
                /* 上一行末格由客户区可用列数算出，不硬写 38 或 40。
                 * Shell 自己的长度守卫决定是否允许退格，窗口只负责像素
                 * 和游标回退；长命令折行后也能逐字删除而不留上下半截。 */
                w->ty -= 10;
                w->tx = 4 + ((w->cw-8)/8-1)*8;
            }
            for (int r = 0; r < 8; r++)
                for (int x = 0; x < 8; x++) w->canvas[(w->ty+r)*w->cw+w->tx+x] = PAL_CON_BG;
            continue;
        }
        if (c == '\n' || w->tx + 8 > w->cw - 4) { w->tx = 4; w->ty += 10; }
        if (w->ty + 8 > w->ch - 4) {
            for (int i = 4*w->cw; i < (w->ch-14)*w->cw; i++) w->canvas[i] = w->canvas[i+10*w->cw];
            for (int i = (w->ch-14)*w->cw; i < (w->ch-4)*w->cw; i++) w->canvas[i] = PAL_CON_BG;
            w->ty -= 10;
        }
        if (c != '\n') { canvas_char(w, w->tx, w->ty, c, PAL_TITLE); w->tx += 8; }
    }
    dirty = 1;
    return 0;
}

/* ---------------- 绘制 ---------------- */
static void draw_window(int i)
{
    win_t *w = &wins[i];
    window_extra_t *ex=extra_of(w->handle);if(ex->hidden)return;
    if(GFX_W!=320){native_draw_window(i);return;}
    int focused = (i == top_visible());

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
    if(GFX_W!=320){native_draw_taskbar();return;}
    gfx_fill(0, 184, 320, 16, PAL_TASKBAR);
    gfx_text16(4, 184, "沙核", PAL_TITLE, 0);
    /* 全客户区应用可能完全遮住父 IDE/调试器，任务条提供稳定句柄的
     * 窗口切换入口。按 handle 排序，置顶只改变 z 序，不会交换按钮。
     * 六个 30px 槽在品牌与时钟之间，不覆盖游戏客户区。 */
    for(int slot=0;slot<nwins;slot++) {
        int chosen=-1,rank=0;
        for(int i=0;i<nwins;i++) {
            rank=0; for(int j=0;j<nwins;j++) if(wins[j].handle<wins[i].handle) rank++;
            if(rank==slot) chosen=i;
        }
        if(chosen<0) continue;
        int x=44+slot*30; char label[4];
        /* 三字符按钮必须先传达用途。Studio/Debug 共用 SC 品牌前缀，
         * 如果直接截前三字，两者都会显示 SC 而无法分辨。这里只跳过
         * 通用的「SC 空格」前缀，不按应用名硬编码；完整标题仍在标题栏。
         * 遇到 NUL 后统一补空格，不能继续读取短标题后面的历史字节。 */
        const char *short_title=wins[chosen].title;
        if(short_title[0]=='S' && short_title[1]=='C' && short_title[2]==' ')
            short_title+=3;
        int ended=0;
        for(int j=0;j<3;j++) {
            if(!short_title[j]) ended=1;
            label[j]=ended?' ':short_title[j];
        }
        label[3]=0;
        gfx_fill(x,186,28,12,chosen==nwins-1?PAL_UI_NIGHT+6:PAL_TASKBAR);
        gfx_text8(x+2,188,label,chosen==nwins-1?PAL_UI_CYAN+7:PAL_UI_MUTED,1,0);
    }
    gfx_text8(238, 188, "UP ", PAL_CON_TINT, 1, 1);
    u32 s = sc_ticks / 100;
    char b[11];
    i32 n = 0;
    do { b[n++] = (char)('0' + s % 10); s /= 10; } while (s);
    i32 x = 263;
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
                gfx_fill(x+unit(c),y+unit(r),unit(c+1)-unit(c),unit(r+1)-unit(r),PAL_TITLE);
            else if (a == 'o')
                gfx_fill(x+unit(c),y+unit(r),unit(c+1)-unit(c),unit(r+1)-unit(r),PAL_SHADOW);
        }
}

static void draw_icons(void)
{
    if(GFX_W!=320){native_draw_icons();return;}
    for (int i = 0; i < 4; i++) {
        if (!dicons[i].used)
            continue;
        i32 x = 8, y = 8 + i * 44;
        icn_draw(x, y, dicons[i].icon, PAL_TITLE);
        gfx_text8(x, y + 20, dicons[i].label, PAL_TITLE, 1, 1);
    }
}

static void fault_hex(int x,int y,u32 value)
{
    char b[9];
    for(int i=0;i<8;i++) b[i]="0123456789ABCDEF"[(value>>(28-i*4))&15];
    b[8]=0; gfx_text8(x,y,b,PAL_CON_TINT,1,1);
}
static void draw_fault(void)
{
    int pid=fault_top(); if(!pid) return;
    fault_card_t *f=&fault_cards[pid]; const char *why="UNHANDLED EXCEPTION";
    if(f->vector==0) why="DIVIDE BY ZERO";
    if(f->vector==6) why="INVALID INSTRUCTION";
    if(f->vector==13) why="PROTECTION VIOLATION";
    if(f->vector==14) why="INVALID MEMORY ACCESS";
    /* 深色卡片、青色数据、红色异常边：给诊断建立视觉层次。
     * 固定位置不依赖故障程序的窗口元数据；绘在普通窗口之后，不占
     * 画布池，所以窗口耗尽也不至于让暂停任务失去可操作的退出入口。 */
    gfx_fill(26,42,272,118,PAL_SHADOW); gfx_fill(22,38,272,118,PAL_CON_BG);
    gfx_fill(22,38,3,118,PAL_PANIC); gfx_fill(25,38,269,23,PAL_WIN_TITLE);
    gfx_text8(34,46,"APPLICATION PAUSED",PAL_TITLE,1,1);
    gfx_fill(278,41,12,12,PAL_PANIC);
    gfx_line(281,44,287,50,PAL_TITLE); gfx_line(281,50,287,44,PAL_TITLE);
    gfx_text8(34,68,why,PAL_CON_TINT,1,1);
    gfx_text8(34,84,"EIP",PAL_TITLE,1,1); fault_hex(70,84,f->eip);
    gfx_text8(154,84,"VEC",PAL_TITLE,1,1); fault_hex(190,84,f->vector);
    gfx_text8(34,98,"ERR",PAL_TITLE,1,1); fault_hex(70,98,f->error);
    gfx_text8(154,98,"MEM",PAL_TITLE,1,1); fault_hex(190,98,f->address);
    gfx_text8(34,119,tasks[pid].name,PAL_TITLE,1,1);
    gfx_text8(34,137,"CLOSE CARD TO END PROGRAM",PAL_CON_TINT,1,1);
}

/* ---------------- 合成 ---------------- */
void wm_compose(void)
{
    scene_paint();
    draw_icons();
    for (int i = 0; i < nwins; i++)
        draw_window(i);
    draw_taskbar();
    native_draw_overlays();
    draw_fault();
    draw_cursor();
    gfx_swap();
    dirty = 0;
}

/* ---------------- 鼠标交互 ---------------- */
static int hit_window(i32 x, i32 y)
{
    for (int i = nwins - 1; i >= 0; i--)
        if (!extra_of(wins[i].handle)->hidden && x >= wins[i].x && x < wins[i].x + wins[i].w
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

    /* 暂停卡片必须优先于新桌面命中。即使程序占满大屏幕，它也不能
     * 抢走关闭故障任务的入口；兼容卡片的固定坐标供旧验收脚本使用。 */
    if ((b&1) && !(pbtn&1)) {
        int fault=fault_top();
        if(fault && x>=22 && x<294 && y>=38 && y<156) {
            fault_focus=1;
            if(x>=278 && x<290 && y>=41 && y<53) task_stop(fault);
            pmx=x;pmy=y;pbtn=b;dirty=1;return;
        }
        fault_focus=0;
    }
    if(native_mouse(x,y,b,pbtn)) {pmx=x;pmy=y;pbtn=b;dirty=1;return;}

    if ((b & 1) && !(pbtn & 1)) {     /* 左键按下沿 */
        int fault=fault_top();
        if(fault && x>=22 && x<294 && y>=38 && y<156) {
            fault_focus=1;
            if(x>=278 && x<290 && y>=41 && y<53) task_stop(fault);
            pmx=x; pmy=y; pbtn=b; dirty=1; return;
        }
        fault_focus=0;
        if(y>=184 && x>=44 && x<44+nwins*30) {
            int slot=(x-44)/30,chosen=-1;
            for(int i=0;i<nwins;i++) {
                int rank=0; for(int j=0;j<nwins;j++) if(wins[j].handle<wins[i].handle) rank++;
                if(rank==slot) chosen=i;
            }
            if(chosen>=0) win_raise(chosen);
            drag=-1; pmx=x; pmy=y; pbtn=b; dirty=1; return;
        }
        int w = hit_window(x, y);
        /* 图标命中? (桌面层, 优先级最低但在最上面画 —— 这里先查图标) */
        for (int i = 0; i < 4; i++) {
            if (w >= 0 || !dicons[i].used) continue;
            i32 ix = 8, iy = 8 + i * 44;
            if (x >= ix && x < ix + 24 && y >= iy && y < iy + 18) {
                task_exec_scx(dicons[i].target);
                pmx = x; pmy = y; pbtn = b;
                dirty = 1;
                return;
            }
        }
        if (w >= 0) {
            win_raise(w);
            w = nwins - 1;
            if (x >= wins[w].x + wins[w].w - 12 && x < wins[w].x + wins[w].w - 2
             && y >= wins[w].y + 2 && y < wins[w].y + 12) {
                int owner = wins[w].owner;
                task_stop(owner);   /* 最后一个窗口关闭意味着 GUI 任务结束 */
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
        if (nx > GFX_W - wins[drag].w) nx = GFX_W - wins[drag].w;
        if (ny > work_bottom() - wins[drag].h) ny = work_bottom() - wins[drag].h;
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
    if(native_menu_key((u8)c))return;
    if(fault_focus && fault_top()) { if(c==27) task_stop(fault_top()); return; }
    if (!nwins) return;
    int top=top_visible();if(top<0)return;
    win_t *w = &wins[top];
    if (!nwins || !w->used || tasks[w->owner].state!=1)
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
    u8 c = w->keys[w->kt];
    w->kt = (u8)((w->kt + 1) % 16);
    return c;
}

int wm_user_getkey(int pid)
{
    if (!wm_focused(pid))
        return -1;
    return win_popkey(&wins[top_visible()]);
}

/* ---------------- 初始化 ---------------- */
void wm_init(void)
{
    nwins = 0;
    next_handle = 1;
    for (int i = 0; i < 4; i++)
        dicons[i].used = 0;
    load_desktop();                   /* 读 desk/ 快捷方式 + 图标 */
    desktop_reload();                 /* 配置菜单和多彩快捷方式，失败保留可用桌面 */
    u8 mode=0;
    wallpaper_mode=0;
    if(fs_read("sys/wall.cfg",&mode,1)==1 && mode>='0' && mode<='2') wallpaper_mode=mode-'0';
    dirty = 1;
}

void wm_open_about(void)              /* 兼容保留: about 命令开中文页窗口 */
{
    /* v2: 关于页内容由应用自己画, 这里不再内置 */
}

/* ---------------- 异常红屏 (interrupts.c 调) ---------------- */
void panic_screen(const char *reason, u32 vec)
{
    gfx_fill(0, 0, GFX_W, GFX_H, PAL_PANIC);
    gfx_text8(16, 16, "== SANDCORE PANIC ==", PAL_SHADOW, 1, 1);
    gfx_text8(16, 32, reason, PAL_SHADOW, 1, 1);
    gfx_text8(16, 48, "VEC 0x", PAL_SHADOW, 1, 1);
    gfx_char8(70, 48, "0123456789ABCDEF"[(vec >> 4) & 0xF], PAL_SHADOW, 1);
    gfx_char8(78, 48, "0123456789ABCDEF"[vec & 0xF], PAL_SHADOW, 1);
    gfx_text8(16, 64, "----", PAL_SHADOW, 1, 1);
    gfx_swap();
}
