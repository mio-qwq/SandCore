#ifndef SANDCORE_SCUIARGB_H
#define SANDCORE_SCUIARGB_H
#include "SCUI.H"
/* 此入口由SCUI.SCL提供。目标/源矩形都是物理像素，严格1:1，
 * 按现有Painter clip裁剪；不二次DPI缩放，不改变调用者原像素。 */
typedef struct {
    ScU32 size,version;const ScU32 *pixels;ScU32 width,height,stride,bytes;
    ScUiRect source,destination;ScU32 flags,reserved[3];
} ScUiArgbBlit;
enum {SC_UI_ARGB_OVER=0,SC_UI_ARGB_COPY=1};
ScStatus ScUiPainterArgb(ScUiPainter *,const ScUiArgbBlit *);
#endif
