#ifndef SANDCORE_SCGFXDRAW_H
#define SANDCORE_SCGFXDRAW_H
#include "SCGFX.H"
/* 可复用2D路径；坐标为有符号整数像素，内部以Q8自适应细分，
 * clip是原图半开矩形。所有路径/边与操作动态拥有，无固定路径条数。 */
typedef struct ScGfxPath ScGfxPath;
enum {SC_GFX_PATH_STROKE=1,SC_GFX_PATH_FILL_EVEN_ODD=2,SC_GFX_PATH_FILL_NONZERO=3};
typedef struct {ScU32 size,version,commands,revision;ScGfxRect bounds;} ScGfxPathInfo;
typedef struct {ScU32 size,version,mode;ScGfxBrush brush;ScGfxRect clip;ScU32 flags,reserved[3];} ScGfxPathDrawDesc;
ScStatus ScGfxPathCreate(ScGfxPath **out);
ScStatus ScGfxPathRetain(ScGfxPath *);
void ScGfxPathRelease(ScGfxPath *);
ScStatus ScGfxPathQuery(const ScGfxPath *,ScGfxPathInfo *out);
ScStatus ScGfxPathMoveTo(ScGfxPath *,ScI32 x,ScI32 y);
ScStatus ScGfxPathLineTo(ScGfxPath *,ScI32 x,ScI32 y);
ScStatus ScGfxPathQuadraticTo(ScGfxPath *,ScI32 cx,ScI32 cy,ScI32 x,ScI32 y);
ScStatus ScGfxPathCubicTo(ScGfxPath *,ScI32 cx1,ScI32 cy1,ScI32 cx2,ScI32 cy2,ScI32 x,ScI32 y);
ScStatus ScGfxPathClose(ScGfxPath *);
ScStatus ScGfxPathClear(ScGfxPath *);
/* Begin锁住path与surface；OperationStep预算涵盖细分、边和像素。
 * Fill自动闭合每个子路径，支持even-odd/nonzero，不借内核绘图。 */
ScStatus ScGfxDrawPathBegin(ScGfxSurface *,ScGfxPath *,const ScGfxPathDrawDesc *,ScGfxOperation **out);
#endif
