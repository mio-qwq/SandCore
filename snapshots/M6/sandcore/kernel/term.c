/* =====================================================================
 *  SandCore 终端缓冲 (kernel/term.c)
 *  ---------------------------------------------------------------------
 *  【内容与呈现分离】(M4 架构核心一步)
 *    M1-M3 的 console 直接写显存; 进窗口时代后, shell 的输出要能
 *    被画进任意位置任意大小的窗口 —— 所以把"字符网格"抽成独立缓冲,
 *    呈现交给窗口合成器 (wm.c 读网格画屏)。
 *    好处: 窗口拖动时终端内容零重绘成本 (合成器直接读网格)。
 *
 *  网格: 38 列 × 20 行, 每格 = 字符 + 独立前景色。
 * ===================================================================== */
#include "io.h"
#include "palette.h"
#include "term.h"

static char tb[T_ROWS][T_COLS];       /* 字符网格 */
static u8   tc[T_ROWS][T_COLS];       /* 每格前景色 */
static i32  tx, ty;                   /* 光标 */
static u8   tfg = PAL_TITLE;

static void tscroll(void)
{
    for (int r = 1; r < T_ROWS; r++)
        for (int c = 0; c < T_COLS; c++) {
            tb[r - 1][c] = tb[r][c];
            tc[r - 1][c] = tc[r][c];
        }
    for (int c = 0; c < T_COLS; c++) {
        tb[T_ROWS - 1][c] = ' ';
        tc[T_ROWS - 1][c] = PAL_TITLE;
    }
}

void term_init(void)
{
    for (int r = 0; r < T_ROWS; r++)
        for (int c = 0; c < T_COLS; c++) {
            tb[r][c] = ' ';
            tc[r][c] = PAL_TITLE;
        }
    tx = ty = 0;
    tfg = PAL_TITLE;
}

void term_putc(char c)
{
    switch (c) {
    case '\n':
        tx = 0;
        if (++ty >= T_ROWS) {
            tscroll();
            ty = T_ROWS - 1;
        }
        break;
    case '\r':
        tx = 0;
        break;
    case '\b':                        /* 网格制: 退格就是清掉前一格 */
        if (tx > 0) {
            tx--;
            tb[ty][tx] = ' ';
        }
        break;
    case '\t':                        /* 对齐到 4 的倍数 */
        do {
            tb[ty][tx] = ' ';
            if (++tx >= T_COLS) {
                tx = 0;
                if (++ty >= T_ROWS) {
                    tscroll();
                    ty = T_ROWS - 1;
                }
            }
        } while (tx % 4 != 0);
        break;
    default:
        if ((unsigned char)c >= 32 && (unsigned char)c != 127) {
            tb[ty][tx] = c;
            if (++tx >= T_COLS) {
                tx = 0;
                if (++ty >= T_ROWS) {
                    tscroll();
                    ty = T_ROWS - 1;
                }
            }
        }
        break;
    }
}

void term_puts(const char *s)  { while (*s) term_putc(*s++); }

void term_put_dec(u32 v)
{
    char buf[11];
    i32 n = 0;
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n--)
        term_putc(buf[n]);
}

void term_set_fg(u8 color)     { tfg = color; }
unsigned term_col(void)        { return (unsigned)tx; }

char term_cell(int row, int col)     { return tb[row][col]; }
u8   term_color(int row, int col)    { return tc[row][col]; }
int  term_cursor_row(void)           { return ty; }
int  term_cursor_col(void)           { return tx; }
