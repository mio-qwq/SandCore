#ifndef SANDCORE_SCGFX_H
#define SANDCORE_SCGFX_H
#include "SC32API.H"
#include "SC32IMAGE.H"
#include "SC32BROKER.H"
#include "SCLIB.H"

/* 真正Ring3图像/编辑库；像素是未预乘0xAARRGGBB。对象不透明，
 * View只借到下一次成功修改或OperationStep，不允许缓存悬空像素。 */
typedef struct ScGfxSurface ScGfxSurface;
typedef struct ScGfxEdit ScGfxEdit;
typedef struct ScGfxOperation ScGfxOperation;
typedef struct {ScI32 x,y,width,height;} ScGfxRect;
enum {
    SC_GFX_ABI_MAJOR=1,SC_GFX_ABI_MINOR=0,SC_GFX_VERSION=1,
    SC_GFX_OK=0,SC_GFX_PENDING=1,SC_GFX_END=2,
    SC_GFX_E_INVALID=-601,SC_GFX_E_VERSION=-602,SC_GFX_E_RANGE=-603,
    SC_GFX_E_MEMORY=-604,SC_GFX_E_STALE=-605,SC_GFX_E_BUSY=-606,
    SC_GFX_E_FORMAT=-607,SC_GFX_E_IO=-608,SC_GFX_E_CANCELLED=-609,
    SC_GFX_E_LIMIT=-610,SC_GFX_E_NOT_FOUND=-611,SC_GFX_E_NOT_SUPPORTED=-612,
    SC_GFX_MAX_WIDTH=1920,SC_GFX_MAX_HEIGHT=1920,SC_GFX_MAX_PIXELS=2073600,
    SC_GFX_DIRTY=1,SC_GFX_EDITING=2,SC_GFX_BUSY=4,
    SC_GFX_FILE_SCB1=1,SC_GFX_FILE_SCB2=2,SC_GFX_FILE_BMP=3,SC_GFX_FILE_COMPRESSED=4,
    SC_GFX_BLEND_COPY=0,SC_GFX_BLEND_OVER=1,SC_GFX_BLEND_ERASE=2,
    SC_GFX_OP_RUNNING=1,SC_GFX_OP_READY=2,SC_GFX_OP_CANCELLED=3,SC_GFX_OP_FAILED=4,
    SC_GFX_HISTORY_UNDO=1,SC_GFX_HISTORY_REDO=2,
    SC_GFX_ROTATE_CW=1,SC_GFX_ROTATE_CCW=2,SC_GFX_FLIP_X=3,SC_GFX_FLIP_Y=4,
    SC_GFX_CLIPBOARD_ARGB=0x10024,
    SC_GFX_FILTER_NEAREST=0,SC_GFX_FILTER_BILINEAR=1
};
enum {SC_GFX_OPERATION_LOAD=1,SC_GFX_OPERATION_SAVE=2,SC_GFX_OPERATION_HISTORY=3,
    SC_GFX_OPERATION_FILL=4,SC_GFX_OPERATION_CLEAR=5,SC_GFX_OPERATION_TRANSFORM=6,
    SC_GFX_OPERATION_COPY=7,SC_GFX_OPERATION_PASTE=8,SC_GFX_OPERATION_PATH=9,
    SC_GFX_OPERATION_COMMITTED=1,SC_GFX_OPERATION_ROLLBACK=2,SC_GFX_OPERATION_WAITING=4};
typedef struct {
    ScU32 size,version,width,height,fill_argb,undo_budget_bytes,flags,reserved;
} ScGfxSurfaceDesc;
typedef struct {
    ScU32 size,version,width,height,stride,bytes,revision,saved_revision;
    ScU32 change_epoch,flags,undo_records,redo_records,history_bytes,history_budget,reserved[2];
} ScGfxSurfaceInfo;
typedef struct {
    ScU32 size,version;const ScU32 *pixels;ScU32 width,height,stride,bytes,change_epoch;
} ScGfxPixels;
typedef struct {ScU32 size,version,argb,radius,opacity,blend,flags,reserved;} ScGfxBrush;
typedef struct {
    ScU32 size,version,state,kind,progress,total,revision,format;
    ScStatus status,cause;ScU32 flags,file_revision,wait_events,reserved;
    char path[SC32_PATH_CAPACITY];
} ScGfxOperationInfo;
typedef struct {
    ScU32 size,version,width,height,bytes,revision,change_epoch,flags;
    ScGfxRect dirty;
} ScGfxEditInfo;

ScStatus ScGfxSurfaceCreate(const ScGfxSurfaceDesc *,ScGfxSurface **out);
ScStatus ScGfxSurfaceRetain(ScGfxSurface *);
void ScGfxSurfaceRelease(ScGfxSurface *);
ScStatus ScGfxSurfaceQuery(const ScGfxSurface *,ScGfxSurfaceInfo *out);
ScStatus ScGfxSurfacePixels(const ScGfxSurface *,ScGfxPixels *out);
ScStatus ScGfxSurfacePixel(const ScGfxSurface *,ScI32 x,ScI32 y,ScU32 *out);
ScStatus ScGfxSurfaceMarkSaved(ScGfxSurface *,ScU32 expected_revision);
ScStatus ScGfxSurfaceHistoryBudget(ScGfxSurface *,ScU32 bytes);
/* 每个Edit有独立失败回滚，成功Commit才截断Redo；任何失败仍可Abort。 */
ScStatus ScGfxEditBegin(ScGfxSurface *,const char *label,ScGfxEdit **out);
ScStatus ScGfxEditPixel(ScGfxEdit *,ScI32 x,ScI32 y,ScU32 argb,ScU32 blend);
ScStatus ScGfxEditQuery(const ScGfxEdit *,ScGfxEditInfo *out);
ScStatus ScGfxEditCommit(ScGfxEdit *);
void ScGfxEditAbort(ScGfxEdit *);
ScStatus ScGfxHistoryLabel(const ScGfxSurface *,ScU32 direction,ScU32 index,char *out,ScU32 capacity);

/* 长任务按pixel_budget推进，来源surface在任务期间固定/锁定。 */
ScStatus ScGfxLoadBegin(const char *path,ScGfxOperation **out);
/* data借用至operation ready/failed/cancelled，Step分批封存来源。 */
ScStatus ScGfxLoadMemoryBegin(const void *data,ScU32 bytes,ScGfxOperation **out);
ScStatus ScGfxSaveBegin(ScGfxSurface *,const char *path,ScU32 format,ScU32 expected_destination_revision,ScGfxOperation **out);
ScStatus ScGfxHistoryBegin(ScGfxSurface *,ScU32 direction,ScGfxOperation **out);
ScStatus ScGfxFillBegin(ScGfxSurface *,ScI32 x,ScI32 y,ScU32 argb,ScGfxOperation **out);
ScStatus ScGfxClearBegin(ScGfxSurface *,ScU32 argb,ScGfxOperation **out);
ScStatus ScGfxClearRectBegin(ScGfxSurface *,const ScGfxRect *,ScU32 argb,ScGfxOperation **out);
ScStatus ScGfxTransformBegin(ScGfxSurface *,ScU32 transform,ScGfxOperation **out);
ScStatus ScGfxClipboardCopyBegin(ScGfxSurface *,const ScGfxRect *,ScU32 expected_epoch,ScGfxOperation **out);
ScStatus ScGfxClipboardPasteBegin(ScGfxSurface *,ScI32 x,ScI32 y,ScGfxOperation **out);
ScStatus ScGfxOperationStep(ScGfxOperation *,ScU32 pixel_budget);
ScStatus ScGfxOperationQuery(const ScGfxOperation *,ScGfxOperationInfo *out);
ScStatus ScGfxOperationSurface(ScGfxOperation *,ScGfxSurface **out);
ScStatus ScGfxOperationCancel(ScGfxOperation *);
/* Close活动任务会先请求Cancel并返回PENDING；直至0才消费对象。 */
ScStatus ScGfxOperationClose(ScGfxOperation *);

/* 可复用整数ARGB算式；Render保持原图，输出只在所给目标区域。 */
ScU32 ScGfxArgbOver(ScU32 source,ScU32 destination);
ScU32 ScGfxArgbErase(ScU32 destination,ScU32 coverage);
ScStatus ScGfxSample(const ScGfxPixels *,ScI32 x_q8,ScI32 y_q8,ScU32 filter,ScU32 *out);
#endif
