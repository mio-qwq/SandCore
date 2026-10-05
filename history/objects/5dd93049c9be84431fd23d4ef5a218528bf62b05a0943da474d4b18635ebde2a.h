#ifndef SANDCORE_GFX_H
#define SANDCORE_GFX_H

#include "io.h"

/* SandCore 图形子系统 v2 —— 独立绘图模块 (M3 从 main/console 中抽离)
 *
 * 【双缓冲架构】
 *   所有绘图函数画在"后台缓冲"(内核 .bss 里的 64000B)上,
 *   一幅画完调用 gfx_swap(): 等垂直回扫, 整块拷进 0xA0000 显存。
 *   好处: 画面永不撕裂, 绘制顺序随便折腾。
 *   控制台(console.c)是特例: 终端逐字输出走显存直写, 不经过这里,
 *   否则每敲一个键都要整屏拷贝 (docs/GFX.md 的分工说明)。
 *
 * 坐标系: (0,0) 左上角, x 向右 0..319, y 向下 0..199。
 * 越界坐标一律静默裁剪, 不炸机。 */

#define GFX_LEGACY_W 320
#define GFX_LEGACY_H 200
extern i32 gfx_width,gfx_height;
#define GFX_W gfx_width
#define GFX_H gfx_height
int gfx_resize(int width,int height);
/* M8：原生后台真正保存RGB，不再保存一字节索引。旧API仍接收槽位；
 * RGB图元与透明图标直接用以下接口，alpha为直通透明度、非预乘。
 * VGA回退在最终写入时选最近槽位，不能因此削减高分辨率后端色深。 */
void gfx_rgb_pset(i32 x,i32 y,u32 rgb);
u32 gfx_rgb_get(i32 x,i32 y);
void gfx_argb_pset(i32 x,i32 y,u32 argb);
void gfx_rgb_fill(i32 x,i32 y,i32 w,i32 h,u32 rgb);

void gfx_init(void);                                  /* 后台缓冲清零 */
void gfx_pset(i32 x, i32 y, u8 c);                    /* 单像素 */
u8   gfx_get(i32 x, i32 y);                           /* 读后台缓冲像素 */
void gfx_fill(i32 x, i32 y, i32 w, i32 h, u8 c);      /* 实心矩形 */
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u8 c);  /* 直线 (Bresenham) */
void gfx_circle(i32 cx, i32 cy, i32 r, u8 c, i32 fill); /* 圆 (可实心) */
void gfx_swap(void);                                  /* 后台缓冲 → 显存 */

/* ---- 文字渲染 (字体数据见 font.h / font16.h) ---- */
/* SCF-8 (8x8 ASCII): scale=整数放大, sp=字间距(像素), 透明底 */
void gfx_char8(i32 x, i32 y, char ch, u8 color, i32 scale);
void gfx_text8(i32 x, i32 y, const char *s, u8 color, i32 scale, i32 sp);
/* 原生凤凰 16px：UTF-8 输入；ASCII 前进 8px，汉字前进 16px；
 * 缺字画空心方框占位; bg 非 0 时铺底色, 0 = 透明底 */
void gfx_char16(i32 x, i32 y, const char *utf8, u8 fg, u8 bg);
void gfx_text16(i32 x, i32 y, const char *s, u8 fg, u8 bg);
int  gfx_font_load(const void *buf, u32 size);   /* SCF1MIO 字体文件 → 激活 */

/* M7：UTF-8 按完整 Unicode 标量推进；错误只推进一字节。
 * 三环整串 API 绘制前会拒绝非法编码，内核文案则用缺字框兜底。
 * glyph16 输出 16 个 u16，bit15 在左；返回前进宽 8/16。
 * 未收字返回 0，同时输出 16px 空框，绝不从另一套字库补字。 */
int gfx_utf8_next(const char **text,u32 *scalar);
int gfx_utf8_valid(const char *text);
int gfx_glyph16(u32 scalar,u16 *rows);
void gfx_font_info(u32 *info); /* ABI 版本/汉字数/原生高度/磁盘激活 */

#endif /* SANDCORE_GFX_H */
