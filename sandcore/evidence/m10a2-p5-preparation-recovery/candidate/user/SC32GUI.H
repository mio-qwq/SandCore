#ifndef SANDCORE_SC32GUI_H
#define SANDCORE_SC32GUI_H
#include "SCTYPES.H"

/* 正式cdecl GUI合同；窗口过程与应用context只在三环保存和调用。
 * 稳定窗口编号/代数是值，不是内核窗口、画布或过程指针。 */
typedef ScU32 ScWindow;
typedef struct ScMessage ScMessage;
typedef ScStatus (*ScWindowProc)(ScWindow,const ScMessage *,void *);
struct ScMessage {
    ScU32 size,version,id,window,generation,sequence_lo,sequence_hi,tick,flags,epoch;
    ScU32 data[6];
};
#define SC32_MSG_CREATE 1u
#define SC32_MSG_PAINT 2u
#define SC32_MSG_SIZE 3u
#define SC32_MSG_MOVE 4u
#define SC32_MSG_FOCUS 5u
#define SC32_MSG_KEYDOWN 6u
#define SC32_MSG_KEYUP 7u
#define SC32_MSG_TEXT 8u
#define SC32_MSG_MOUSEMOVE 9u
#define SC32_MSG_BUTTONDOWN 10u
#define SC32_MSG_BUTTONUP 11u
#define SC32_MSG_COMMAND 12u
#define SC32_MSG_CLOSE 13u
#define SC32_MSG_DESTROY 14u
#define SC32_MSG_CAPTURELOST 15u
#define SC32_MSG_VISIBILITY 16u
#define SC32_MSG_THEME 17u
#define SC32_MSG_DISPLAY 18u
#define SC32_MSG_TIMER 19u
#define SC32_MSG_QUEUELOSS 20u
#define SC32_MSG_USER 0x8000u
#define SC32_MSG_KERNEL 1u
#define SC32_MSG_COALESCED 2u
#define SC32_MSG_REPEAT 4u
#define SC32_MSG_DEFERRED 8u
#define SC32_MSG_APPLICATION 16u
#define SC32_MSG_RAWLOSS 32u
#define SC32_MSG_PULSE 64u

#define SC32_STYLE_CLOSE 1u
#define SC32_STYLE_MINIMIZE 2u
#define SC32_STYLE_MAXIMIZE 4u
#define SC32_STYLE_RESIZE 8u
#define SC32_STYLE_MODAL 16u
#define SC32_STYLE_HIDDEN 32u
#define SC32_STYLE_DISABLED 64u
#define SC32_STYLE_STANDARD 15u
#define SC32_WINDOW_VISIBLE 1u
#define SC32_WINDOW_FOCUSED 2u
#define SC32_WINDOW_MINIMIZED 4u
#define SC32_WINDOW_MAXIMIZED 8u
#define SC32_WINDOW_MODAL 16u
#define SC32_WINDOW_INPUT 32u
#define SC32_WINDOW_CLOSING 64u
#define SC32_WINDOW_PAINT 128u
#define SC32_WINDOW_CREATING 256u
#define SC32_WINDOW_CAPTURED 512u
#define SC32_WINDOW_PREPARING 8192u
#define SC32_WINDOW_SHOW 0u
#define SC32_WINDOW_HIDE 1u
#define SC32_WINDOW_MINIMIZE 2u
#define SC32_WINDOW_MAXIMIZE 3u
#define SC32_WINDOW_RESTORE 4u
#define SC32_WINDOW_FOCUS 5u

#define SC32_MOD_SHIFT 1u
#define SC32_MOD_CONTROL 2u
#define SC32_MOD_ALT 4u
#define SC32_MOD_CAPS 8u
#define SC32_MOD_NUM 16u
#define SC32_KEY_UP 0x1001u
#define SC32_KEY_DOWN 0x1002u
#define SC32_KEY_LEFT 0x1003u
#define SC32_KEY_RIGHT 0x1004u
#define SC32_KEY_HOME 0x1005u
#define SC32_KEY_END 0x1006u
#define SC32_KEY_PAGEUP 0x1007u
#define SC32_KEY_PAGEDOWN 0x1008u
#define SC32_KEY_INSERT 0x1009u
#define SC32_KEY_DELETE 0x100au
#define SC32_KEY_F1 0x1101u
#define SC32_KEY_PAUSE 0x1301u
#define SC32_KEY_PRINT 0x1302u

typedef struct {
    ScU32 size,version;const char *title;
    ScI32 x,y;ScU32 client_width,client_height,style,owner_window,owner_generation;
    ScWindowProc procedure;void *context;ScU32 reserved[4];
} ScWindowDesc;
typedef struct {
    ScU32 size,version,window,generation,owner_pid,owner_generation,session;
    ScI32 x,y,width,height;ScU32 client_width,client_height,format,state;
    ScU32 geometry_epoch,visibility_epoch,input_epoch,theme_epoch,display_epoch,reserved[4];
} ScWindowInfo;
typedef struct {
    ScU32 size,version,window,generation,input_epoch,modifiers;
    ScI32 x,y;ScU32 buttons,captured,focused,visible,physical_down[8],reserved[4];
} ScInputSnapshot;
typedef struct {
    ScU32 size,version,window,generation,geometry_epoch,paint_epoch;
    ScI32 x,y;ScU32 width,height,client_width,client_height,format,flags,reserved[2];
} ScPaintInfo;
typedef struct {ScU32 source_x,source_y,destination_x,destination_y,width,height;} ScSubmitRect;
typedef struct {
    ScU32 size,version,window,generation,geometry_epoch;
    const void *pixels;ScU32 bytes,width,height,stride;
    const ScSubmitRect *rectangles;ScU32 rectangle_count,reserved[4];
} ScSubmitDesc;
typedef struct {
    ScU32 size,version,messages,pages,lost_lo,lost_hi,input_epoch,flags;
} ScQueueInfo;

ScStatus ScWindowCreate(const ScWindowDesc *desc,ScWindow *out);
ScStatus ScWindowDestroy(ScWindow window);
ScStatus ScWindowQuery(ScWindow window,ScWindowInfo *out);
ScStatus ScWindowPlace(ScWindow window,ScI32 x,ScI32 y,ScU32 width,ScU32 height,ScWindowInfo *out);
ScStatus ScWindowControl(ScWindow window,ScU32 action,ScWindowInfo *out);
ScStatus ScWindowSetTitle(ScWindow window,const char *title);
ScStatus ScWindowSetOwner(ScWindow window,ScWindow owner,ScU32 style,ScWindowInfo *out);
ScStatus ScWindowRejectClose(ScWindow window);
ScStatus ScWindowInvalidate(ScWindow window,ScI32 x,ScI32 y,ScU32 width,ScU32 height);
ScStatus ScPaintBegin(ScWindow window,ScPaintInfo *out);
ScStatus ScPaintEnd(const ScPaintInfo *paint);
ScStatus ScWindowSubmit(const ScSubmitDesc *submit);
ScStatus ScInputQuery(ScWindow window,ScInputSnapshot *out);
ScStatus ScPointerCapture(ScWindow window,ScU32 enabled);
ScStatus ScMessagePeek(ScMessage *out);
ScStatus ScMessageGet(ScMessage *out,ScU32 timeout_ticks);
ScStatus ScMessagePost(const ScMessage *message);
ScStatus ScMessageDispatch(const ScMessage *message);
ScStatus ScDefaultWindowProc(ScWindow window,const ScMessage *message,void *context);
ScStatus ScMessageQueueQuery(ScQueueInfo *out);
#endif
