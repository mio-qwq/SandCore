#ifndef SANDCORE_TERM_H
#define SANDCORE_TERM_H

#include "io.h"

/* 终端缓冲 (M4 起接替 console.c)
 * 字符网格本体与光标状态都在这里, 窗口合成器 (wm.c) 负责把它画进
 * 终端窗口的客户区 —— 也就是"内容"与"呈现"分离:
 *   shell → term_* (写网格) → wm_compose (读网格画屏)
 * 38 列 × 20 行, 每格 8×8, 恰好放进 304×160 的窗口客户区。 */

#define T_COLS 38
#define T_ROWS 20

void term_init(void);                /* 清网格, 光标归位 */
void term_putc(char c);              /* 写一字符 (\n \r \b \t) */
void term_puts(const char *s);
void term_put_dec(u32 v);            /* 十进制无符号数打到光标处 */
void term_set_fg(u8 color);
unsigned term_col(void);             /* 光标列 (shell 守卫提示符用) */

/* 合成器读取用 */
char term_cell(int row, int col);
u8   term_color(int row, int col);
int  term_cursor_row(void);
int  term_cursor_col(void);

#endif /* SANDCORE_TERM_H */
