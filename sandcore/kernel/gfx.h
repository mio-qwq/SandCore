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
/* mio：可信合成器的物理像素写入视口，半开矩形[x,x+w)×[y,y+h)。
 * 默认/resize后无视口；设置先裁屏，空交集禁止所有后台写入。
 * 所有点/填充/ARGB/不透明行/背景拷贝都须服从，读像素不裁。
 * 不支持嵌套：一次场景绘制后必须reset，再捕获/绘制完整光标。
 * 这不是三环裁剪API，不改旧画布/FRAME长度/alpha或SCAPI布局。
 * bounds输出实际可写的x/y/宽/高，供大图行循环跳过无关区域。 */
void gfx_clip(int x,int y,int w,int h);
void gfx_clip_reset(void);
void gfx_clip_bounds(i32 *out);
/* M8：原生后台真正保存RGB，不再保存一字节索引。旧API仍接收槽位；
 * RGB图元与透明图标直接用以下接口，alpha为直通透明度、非预乘。
 * VGA回退在最终写入时选最近槽位，不能因此削减高分辨率后端色深。 */
void gfx_rgb_pset(i32 x,i32 y,u32 rgb);
u32 gfx_rgb_get(i32 x,i32 y);
void gfx_argb_pset(i32 x,i32 y,u32 argb);
void gfx_rgb_fill(i32 x,i32 y,i32 w,i32 h,u32 rgb);
/* mio：高分辨率热路径按行裁剪一次，不能每个像素重复调用边界/
 * 调色板接口。ARGB仍是直通alpha，透明像素保留背景；row_map的
 * 输入已由壁纸所有者合成为RGB，采样索引逐项指向有效源行。 */
void gfx_argb_blit(i32 x,i32 y,int w,int h,const u32 *pixels,int stride);
/* 仅供已验证整画布alpha=255的窗口使用；仍掩掉高字节写XRGB，
 * 原用户画布不改字节。透明画布必须继续调用上面的逐通道合成。 */
void gfx_opaque_blit(i32 x,i32 y,int w,int h,const u32 *pixels,int stride);
void gfx_rgb_row_map(int y,const u32 *source,const u32 *indices,int count);
/* mio：旧索引画布的一行直接查色并整数放大，scale为1..4。
 * count是源像素数；输出scale行，仍遵守物理屏幕和损伤视口。
 * 与逐像素gfx_pset/gfx_fill字节等价，只把重复裁剪移至行边界。
 * 调色板每次取当前值，不缓存过期主题/设备颜色；不改变旧画布。 */
void gfx_indexed_row(int x,int y,const u8 *pixels,int count,int scale);
/* 背景所有者预采样整屏：原生每字为XRGB，VGA每字为实际DAC索引。
 * 尺寸必须完全等于当前模式，不能把ARGB或别的分辨率送来直接拷贝。 */
void gfx_background_copy(const u32 *pixels,int width,int height);
void gfx_swap_rect(int x,int y,int w,int h);

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
