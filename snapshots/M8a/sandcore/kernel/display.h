#ifndef SANDCORE_DISPLAY_H
#define SANDCORE_DISPLAY_H
#include "io.h"
/* mio：原生显示扩展与 VGA 回退共存。width/height 是真实像素，scale
 * 是 UI 百分比；它不把应用坐标乘入显卡模式，也不更换字体来源。 */
void display_init(void);
int display_apply(int width,int height,int scale,int action);
/* 仅严格验证完整候选，不切模式/预览/写盘。支持模式与设备在场
 * 分开：apply负责设备与内存。字段/重复/未知键规则见CONFIG.md。 */
int display_config_check(const char *path);
/* action=0 预览（15秒无确认回退），1 确认并保存，2 取消；确认时参数忽略。
 * 失败返回 -1 参数/设备，-2 内存，-3 持久化；成功0。 */
void display_poll(void);
void display_info(u32 *out); /* 8双字：宽、高、缩放、后端、代数、支持位图、预览秒、最大宽 */
int display_scale(void);
void display_palette(u32 *out);
void display_color(int slot,u8 r,u8 g,u8 b);
void display_present(const u8 *pixels,int width,int height);
u32 display_rgb(u8 slot);             /* 旧槽位到0x00RRGGBB，保留原程序配色 */
u8 display_index(u32 rgb);            /* 仅VGA兼容边界量化，原生RGB不经过它 */
void display_present_rgb(const u32 *pixels,int width,int height);
/* 部分提交使用整屏后台的原stride；仅复制裁剪后的矩形，不改变
 * 显卡模式或缩放。VGA保留原整帧有界回扫，局部优化仅原生后端。 */
void display_present_rgb_rect(const u32 *pixels,int x,int y,int w,int h,int stride);
#endif
