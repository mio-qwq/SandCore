/* =====================================================================
 *  SandCore 图形子系统 v2 (kernel/gfx.c)
 *  ---------------------------------------------------------------------
 *  双缓冲实现: 后台缓冲是内核 .bss 里的一块 64000B 内存,
 *  所有图元往那儿画; gfx_swap() 等垂直回扫后一次性整块拷进显存。
 *  整块拷贝用 u32 搬运 (16000 次), 一帧开销可忽略。
 * ===================================================================== */
#include "io.h"
#include "gfx.h"

#define VGA_FB ((volatile u8 *)0xA0000)

static u8 gfx_bb[GFX_W * GFX_H];      /* 后台缓冲 (64000B, .bss) */

void gfx_init(void)
{
    for (u32 i = 0; i < GFX_W * GFX_H; i++)
        gfx_bb[i] = 0;
}

void gfx_pset(i32 x, i32 y, u8 c)
{
    if ((u32)x < GFX_W && (u32)y < GFX_H)
        gfx_bb[y * GFX_W + x] = c;
}

u8 gfx_get(i32 x, i32 y)
{
    if ((u32)x >= GFX_W || (u32)y >= GFX_H)
        return 0;
    return gfx_bb[y * GFX_W + x];
}

void gfx_fill(i32 x, i32 y, i32 w, i32 h, u8 c)
{
    for (i32 j = y; j < y + h; j++)
        for (i32 i = x; i < x + w; i++)
            gfx_pset(i, j, c);
}

/* Bresenham 直线: 全整数, 八向对称处理 */
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u8 c)
{
    i32 dx = x1 - x0, dy = y1 - y0;
    i32 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    i32 err = dx - dy;
    for (;;) {
        gfx_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        i32 e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* 圆: 中点法逐列填充; fill=0 只描轮廓 */
void gfx_circle(i32 cx, i32 cy, i32 r, u8 c, i32 fill)
{
    if (r < 0)
        return;
    for (i32 y = -r; y <= r; y++) {
        /* 求 x^2 + y^2 = r^2 的 x (整数开方, 逐次逼近) */
        i32 x = r;
        while (x * x + y * y > r * r)
            x--;
        if (fill) {
            for (i32 i = cx - x; i <= cx + x; i++)
                gfx_pset(i, cy + y, c);
        } else {
            gfx_pset(cx + x, cy + y, c);
            gfx_pset(cx - x, cy + y, c);
        }
    }
}

void gfx_swap(void)
{
    /* 等垂直回扫: 先等上一场结束, 再等新回扫开始 */
    while (inb(0x3DA) & 0x08)
        ;
    while (!(inb(0x3DA) & 0x08))
        ;
    u32 *src = (u32 *)gfx_bb;
    volatile u32 *dst = (volatile u32 *)VGA_FB;
    for (u32 i = 0; i < GFX_W * GFX_H / 4; i++)
        dst[i] = src[i];
}

/* ---------------- 文字渲染 ---------------- */
#include "font.h"
#include "font16.h"

/* SCF-8 单字符 (font.h 的直角表, 0x20..0x7E), 透明底 */
void gfx_char8(i32 x, i32 y, char ch, u8 color, i32 scale)
{
    const unsigned char *g = glyph_of(ch);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if ((g[r] >> (7 - c)) & 1)
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        gfx_pset(x + c * scale + dx, y + r * scale + dy, color);
}

void gfx_text8(i32 x, i32 y, const char *s, u8 color, i32 scale, i32 sp)
{
    for (; *s; s++, x += 8 * scale + sp)
        gfx_char8(x, y, *s, color, scale);
}

/* SCF-16 单字: 查 UTF-8 表; 缺字画空心方框 (看得见的"我不认识这个字") */
void gfx_char16(i32 x, i32 y, const char *utf8, u8 fg, u8 bg)
{
    const zh16_t *g = zh16_find(utf8);
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            u8 px;
            if (!g)
                px = (r == 0 || r == 15 || c == 0 || c == 15) ? fg : bg;
            else
                px = (g->r[r] && g->r[r][c] == '#') ? fg : bg;
            if (px || bg)
                gfx_pset(x + c, y + r, px ? px : bg);
        }
}

/* 连续中英文混排: 中文 16px, ASCII 按倍宽 (16px) 对齐, 两种字体同高 */
void gfx_text16(i32 x, i32 y, const char *s, u8 fg, u8 bg)
{
    while (*s) {
        unsigned char b = (unsigned char)*s;
        if (b < 0x80) {                       /* ASCII: 半宽槽位也占 16px */
            char tmp[2] = { (char)b, 0 };
            gfx_char8(x + 4, y + 4, b, fg, 2);
            s++;
        } else {                              /* 中文: 3 字节 UTF-8 */
            gfx_char16(x, y, s, fg, bg);
            s += 3;
        }
        x += 18;                              /* 16px 字宽 + 2px 间距 */
    }
}

/* ---------------- 字体文件加载 (M5) ----------------
 * SCF1MIO 格式: magic(7) + count(4) + 每字[utf8(3)+NUL + 16行×(16+NUL)]
 * 由 mkfs.py 从 font16.txt 导出并放上 SandFS; 内核开机读取激活。
 * 解析只做"指针接线"—— 文件里的行字符串本身就是 NUL 结尾的 C 串。 */
static zh16_t zh16_loaded[64];                /* 加载后的字形表 */

int gfx_font_load(const void *buf, u32 size)
{
    const u8 *p = (const u8 *)buf;
    if (size < 12)
        return -1;
    if (p[0] != 'S' || p[1] != 'C' || p[2] != 'F' || p[3] != '1'
     || p[4] != 'M' || p[5] != 'I' || p[6] != 'O')
        return -1;
    int n = p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24);
    if (n < 1 || n > 64)
        return -1;

    const u8 *q = p + 12;
    for (int i = 0; i < n; i++) {
        zh16_loaded[i].zh = (const char *)q;
        q += 4;
        for (int r = 0; r < 16; r++) {
            zh16_loaded[i].r[r] = (const char *)q;
            q += 17;
        }
    }
    font16_use(zh16_loaded, n);
    return n;
}
