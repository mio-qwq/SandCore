#ifndef SANDCORE_FONT_H
#define SANDCORE_FONT_H
/* mio：全系统 ASCII 和汉字服从 font16.txt 指定的凤凰字体策略。
 * 原手绘 SCF-8 已退役；此处包装 import_font.py --ascii 同源提取、
 * mkfont8.py 编译的表。旧接口仍为 8 行、bit7 在左，因此旧应用
 * 保持 10px 行距。8px 是原生 16px 的相邻两行合并，不换字库。
 * 不缩放的原生 ASCII 字形由 gfx_glyph16 提供，宽 8、高 16。
 */
#include "font8_data.h"
#define SCF_ROWS 8
#define SCF_COUNT 95
static const unsigned char *glyph_of(char ch)
{
    unsigned c=(unsigned char)ch;
    if(c<32 || c>126) c=32;
    return sc_font[c-32];
}
#endif
