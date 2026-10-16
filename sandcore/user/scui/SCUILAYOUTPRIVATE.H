#ifndef SANDCORE_SCUI_LAYOUT_FIX_PRIVATE_H
#define SANDCORE_SCUI_LAYOUT_FIX_PRIVATE_H
/* 扩展只进入一次布局的候选数组，不改变活节点或任何公开ABI。 */
typedef struct {
    ScU32 node,revision;
    ScUiRect rectangle,clip,child_clip,overflow;
    ScUiSize preferred;
    ScWideCounter scroll_x,scroll_y,content_width,content_height;
    ScU32 scroll_changed,range_needed;
} UiLayoutPlacement;
#endif
