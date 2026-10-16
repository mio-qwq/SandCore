#ifndef SANDCORE_SCUICANVAS_H
#define SANDCORE_SCUICANVAS_H
#include "SCUI.H"
#include "SCUIARGB.H"
#include "SCGFX.H"

/* 本控件由SCGFX.SCL实现，单向导入SCUI；SCUI本身不依赖SCGFX。
 * 所有输入/绘制经SCUI控件树、捕获、布局和物理clip，不暴露私有帧。 */
typedef struct ScGfxCanvas ScGfxCanvas;
enum {
    SC_GFX_CANVAS_READONLY=1,SC_GFX_CANVAS_CHECKER=2,
    SC_GFX_TOOL_BRUSH=1,SC_GFX_TOOL_ERASER=2,SC_GFX_TOOL_LINE=3,
    SC_GFX_TOOL_RECTANGLE=4,SC_GFX_TOOL_FILL=5,SC_GFX_TOOL_SELECT=6,
    SC_GFX_TOOL_PICKER=7,SC_GFX_TOOL_PAN=8,
    SC_GFX_VIEW_FIT=1,SC_GFX_VIEW_PERCENT=2,
    SC_GFX_CANVAS_CHANGED=1,SC_GFX_CANVAS_VIEW=2,SC_GFX_CANVAS_COLOR=3,
    SC_GFX_CANVAS_SELECTION=4,SC_GFX_CANVAS_CONTEXT=5,SC_GFX_CANVAS_ERROR=6,
    SC_GFX_CANVAS_BUSY=7
};
typedef struct {
    ScU32 size,version,type,node,revision,change_epoch;ScStatus status;
    ScI32 x,y;ScGfxRect dirty;ScU32 argb,reserved[2];
} ScGfxCanvasEvent;
typedef ScStatus (*ScGfxCanvasProc)(ScGfxCanvas *,const ScGfxCanvasEvent *,void *);
typedef struct {
    ScU32 size,version;ScUiContext *ui;ScUiNode parent;
    ScGfxSurface *surface;ScU32 flags;ScGfxCanvasProc procedure;void *context;
    ScDropProc context_drop;ScU32 cache_budget_bytes,pixel_quantum,reserved[2];
} ScGfxCanvasDesc;
typedef struct {
    ScU32 size,version,node,flags,mode,percent,tool,busy;
    ScGfxBrush brush;ScGfxRect selection;ScI32 pan_x,pan_y;
    ScU32 viewport_width,viewport_height,cache_bytes,rendered_pixels,revision,reserved;
} ScGfxCanvasInfo;
ScStatus ScGfxCanvasCreate(const ScGfxCanvasDesc *,ScGfxCanvas **out);
ScStatus ScGfxCanvasRetain(ScGfxCanvas *);
void ScGfxCanvasRelease(ScGfxCanvas *);
ScStatus ScGfxCanvasQuery(const ScGfxCanvas *,ScGfxCanvasInfo *out);
ScStatus ScGfxCanvasSetSurface(ScGfxCanvas *,ScGfxSurface *);
ScStatus ScGfxCanvasSetTool(ScGfxCanvas *,ScU32 tool);
ScStatus ScGfxCanvasSetBrush(ScGfxCanvas *,const ScGfxBrush *);
ScStatus ScGfxCanvasSetMode(ScGfxCanvas *,ScU32 mode,ScU32 percent);
ScStatus ScGfxCanvasSetSelection(ScGfxCanvas *,const ScGfxRect *);
ScStatus ScGfxCanvasCenter(ScGfxCanvas *);
ScStatus ScGfxCanvasPan(ScGfxCanvas *,ScI32 x,ScI32 y);
ScStatus ScGfxCanvasInvalidate(ScGfxCanvas *,const ScGfxRect *dirty);
ScStatus ScGfxCanvasCancel(ScGfxCanvas *);
#endif
