#ifndef SANDCORE_DISPLAY_H
#define SANDCORE_DISPLAY_H
#include "io.h"
/* mio：原生显示扩展与 VGA 回退共存。width/height 是真实像素，scale
 * 是 UI 百分比；它不把应用坐标乘入显卡模式，也不更换字体来源。 */
void display_init(void);
int display_apply(int width,int height,int scale,int action);
/* action=0 预览（15秒无确认回退），1 确认并保存，2 取消；确认时参数忽略。
 * 失败返回 -1 参数/设备，-2 内存，-3 持久化；成功0。 */
void display_poll(void);
void display_info(u32 *out); /* 8双字：宽、高、缩放、后端、代数、支持位图、预览秒、最大宽 */
int display_scale(void);
void display_palette(u32 *out);
void display_color(int slot,u8 r,u8 g,u8 b);
void display_present(const u8 *pixels,int width,int height);
#endif
