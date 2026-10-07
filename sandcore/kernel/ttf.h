#ifndef SANDCORE_TTF_H
#define SANDCORE_TTF_H
#include "io.h"

/* 原凤凰12/16 TTF的全字库。脸编号就是原生像素数，不是可写路径。
 * 旧GLYPH16/FONT8布局保持；扩展位图用独立固定版本结构。
 * 轮廓/字体/缓存属于内核，全局版本失效，不向三环泄漏内部地址。 */
#define TTF_BITMAP_ROWS 96u
#define TTF_BITMAP_WORDS (16u+TTF_BITMAP_ROWS*4u)
/* 16字头：版本/代数/字形ID/映射成功/请求像素/前进宽/画布宽高/
 * 基线/画布相对笔尖左偏移/脸编号/左轴承26.6/前进26.6/保留3字。
 * 随后96行，每行4个u32，
 * 每字bit31在左，超过有效宽高部分为0；宽≤128、高≤96。 */
int ttf_init(void);
int ttf_glyph16(u32 scalar,u16 out[16]);
int ttf_font8(u32 scalar,u8 out[8]);
int ttf_info(u32 face,u32 out[16]);
int ttf_bitmap(u32 scalar,u32 face,u32 pixels,u32 out[TTF_BITMAP_WORDS]);
u32 ttf_mapped(void);
u32 ttf_epoch(void);
#endif
