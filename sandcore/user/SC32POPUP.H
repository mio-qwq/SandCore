#ifndef SANDCORE_SC32POPUP_H
#define SANDCORE_SC32POPUP_H
#include "SC32GUI.H"

/* 新入口只有正文与WM/有序设备服务接通后才登记cap及正式库版本。
 * popup是同一owner域的真正窗口，既有窗口字段和保留字意义不变。 */
enum {
    SC32_CAP_POPUPS=524288,SC32_CAP_POINTER_CURSOR=1048576,SC32_CAP_POINTER_WHEEL=2097152,
    SC32_POPUP_MENU=1,SC32_POPUP_COMBO=2,SC32_POPUP_TOOLTIP=3,SC32_POPUP_CUSTOM=4,
    SC32_POPUP_NO_ACTIVATE=1,SC32_POPUP_HIT_TRANSPARENT=2,
    SC32_CURSOR_ARROW=0,SC32_CURSOR_TEXT=1,SC32_CURSOR_RESIZE_H=2,SC32_CURSOR_RESIZE_V=3,
    SC32_CURSOR_RESIZE_NWSE=4,SC32_CURSOR_RESIZE_NESW=5,SC32_CURSOR_HAND=6,
    SC32_CURSOR_FORBIDDEN=7,SC32_CURSOR_DRAG_COPY=8,SC32_CURSOR_DRAG_MOVE=9,
    SC32_MSG_WHEEL=21,
    SC32_WINDOW_POPUP=1024,SC32_WINDOW_NO_ACTIVATE=2048,SC32_WINDOW_HIT_TRANSPARENT=4096
};
typedef struct {
    ScU32 size,version,owner_window,owner_generation;
    ScI32 anchor_x,anchor_y;
    ScU32 anchor_width,anchor_height,preferred_width,preferred_height,kind,flags;
    ScWindowProc procedure;void *context;ScU32 reserved[2];
} ScPopupDesc;

ScStatus ScPopupCreate(const ScPopupDesc *description,ScWindow *out);
ScStatus ScPaintCancel(const ScPaintInfo *paint);
ScStatus ScPointerSetCursor(ScWindow window,ScU32 shape);

/* WHEEL：data[0/1]本人客户x/y有符号，data[2/3]竖/横detent有符号，
 * data[4/5]buttons/modifiers。数据只来自已协商的真实设备包。
 * IPC/clipboard与GUI组合唤醒走ScBrokerWait的INPUT|STREAM事实，
 * 不声明没有生产者的GUI消息或伪称键鼠/TEXT。 */
#endif
