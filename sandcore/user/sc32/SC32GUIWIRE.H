#ifndef SANDCORE_SC32GUIWIRE_H
#define SANDCORE_SC32GUIWIRE_H
#include "SC32WIRE.H"
#include "../SC32GUI.H"
#include "../SC32POPUP.H"

/* 与公共cdecl分离的私有gate selector。所有输入固定size/version1，
 * 有输出的调用先验完整声明RW再effects；CREATE不含用户回调指针。 */
#define SC32_GUI_CREATE 0x100u
#define SC32_GUI_ACTIVATE 0x101u
#define SC32_GUI_DESTROY 0x102u
#define SC32_GUI_QUERY 0x103u
#define SC32_GUI_PLACE 0x104u
#define SC32_GUI_CONTROL 0x105u
#define SC32_GUI_OWNER 0x106u
#define SC32_GUI_TITLE 0x107u
#define SC32_GUI_PEEK 0x108u
#define SC32_GUI_GET 0x109u
#define SC32_GUI_WAIT 0x10au
#define SC32_GUI_POST 0x10bu
#define SC32_GUI_INPUT 0x10cu
#define SC32_GUI_CAPTURE 0x10du
#define SC32_GUI_INVALIDATE 0x10eu
#define SC32_GUI_PAINT_BEGIN 0x10fu
#define SC32_GUI_PAINT_END 0x110u
#define SC32_GUI_SUBMIT 0x111u
#define SC32_GUI_CLOSE_CANCEL 0x112u
#define SC32_GUI_QUEUE 0x113u
#define SC32_GUI_PAINT_CANCEL 0x114u
#define SC32_GUI_POPUP_CREATE 0x115u
#define SC32_GUI_CURSOR 0x116u
#define SC32_GUI_RECT_LIMIT 64u
typedef struct {ScU32 size,version,window,generation;} Sc32WindowRef;
typedef struct {
    ScU32 size,version,owner_window,owner_generation;ScI32 anchor_x,anchor_y;
    ScU32 anchor_width,anchor_height,preferred_width,preferred_height,kind,flags,reserved[4];
} Sc32PopupCreate;
typedef struct {
    ScU32 size,version,title;ScI32 x,y;
    ScU32 client_width,client_height,style,owner_window,owner_generation,reserved[6];
} Sc32WindowCreate;
typedef struct {
    ScU32 size,version,window,generation;ScI32 x,y;
    ScU32 width,height,reserved[2];
} Sc32WindowPlace;
typedef struct {ScU32 size,version,window,generation,action,reserved;} Sc32WindowAction;
typedef struct {ScU32 size,version,window,generation,owner_window,owner_generation,style,reserved;} Sc32WindowOwner;
typedef struct {ScU32 size,version,window,generation,title,reserved;} Sc32WindowTitle;
typedef struct {ScU32 size,version,timeout,reserved;} Sc32MessageWait;
typedef struct {
    ScU32 size,version,window,generation;ScI32 x,y;ScU32 width,height,reserved;
} Sc32Invalidate;
typedef struct {ScU32 size,version,window,generation,geometry_epoch,paint_epoch;} Sc32PaintEnd;
/* SUBMIT直接使用64B ScSubmitDesc；rectangles是24B项，1..64个为单次
 * 服务预算，不限制窗口/消息/总提交数。整个源bytes及所有矩形先验，
 * 再一次发布；错误不部分写画布，PAINT只由成功End确认。 */
#endif
