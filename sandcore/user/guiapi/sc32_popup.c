#include "../sc32/SC32PRIVATE.H"
#include "../sc32/SC32GUIWIRE.H"
#include "../SC32POPUP.H"

typedef char gui_popup_desc_64[(sizeof(ScPopupDesc)==64)?1:-1];
typedef char gui_popup_wire_64[(sizeof(Sc32PopupCreate)==64)?1:-1];

ScStatus ScPopupCreate(const ScPopupDesc *description,ScWindow *out)
{
    ScStatus checked=sc32_out(out,4);if(checked<0)return gui_error(checked,SC32_GUI_POPUP_CREATE);
    checked=sc32_record_in(description,sizeof(ScPopupDesc));if(checked<0)return gui_error(checked,SC32_GUI_POPUP_CREATE);
    ScPopupDesc value=*description;
    if(!value.owner_window || !value.owner_generation || value.reserved[0] || value.reserved[1]
        || value.kind<SC32_POPUP_MENU || value.kind>SC32_POPUP_CUSTOM || (value.flags&~3u))return gui_error(SC32_E_INVALID,SC32_GUI_POPUP_CREATE);
    Sc32PopupCreate request;sc32_zero(&request,sizeof(request));request.size=64;request.version=1;
    request.owner_window=value.owner_window;request.owner_generation=value.owner_generation;
    request.anchor_x=value.anchor_x;request.anchor_y=value.anchor_y;request.anchor_width=value.anchor_width;request.anchor_height=value.anchor_height;
    request.preferred_width=value.preferred_width;request.preferred_height=value.preferred_height;request.kind=value.kind;request.flags=value.flags;
    /* 共用真正的CREATE/ACTIVATE与重入回滚，不在应用复制第二份过程表。
     * 新popup只有无标题的画布；实际外框由WM按owner/work area计算。 */
    ScWindowDesc desc;sc32_zero(&desc,sizeof(desc));desc.size=64;desc.version=1;desc.title="";
    desc.client_width=value.preferred_width;desc.client_height=value.preferred_height;
    desc.owner_window=value.owner_window;desc.owner_generation=value.owner_generation;
    desc.procedure=value.procedure;desc.context=value.context;
    return gui_create_common(&desc,out,&request);
}

ScStatus ScPointerSetCursor(ScWindow window,ScU32 shape)
{
    sc32_gui_record *record;ScStatus checked=gui_require(&record,window,SC32_GUI_CURSOR);if(checked)return checked;
    if(shape>SC32_CURSOR_DRAG_MOVE)return gui_error(SC32_E_RANGE,SC32_GUI_CURSOR);
    Sc32WindowAction request={24,1,window,record->generation,shape,0};
    return gui_result(gui_call(SC32_GUI_CURSOR,&request,0),SC32_GUI_CURSOR);
}

ScStatus ScPaintCancel(const ScPaintInfo *paint)
{
    ScStatus checked=sc32_record_in(paint,sizeof(ScPaintInfo));if(checked<0)return gui_error(checked,SC32_GUI_PAINT_CANCEL);
    sc32_gui_record *record;checked=gui_require(&record,paint->window,SC32_GUI_PAINT_CANCEL);if(checked)return checked;
    if(paint->generation!=record->generation)return gui_error(SC32_E_STALE,SC32_GUI_PAINT_CANCEL);
    if(paint->reserved[0] || paint->reserved[1])return gui_error(SC32_E_INVALID,SC32_GUI_PAINT_CANCEL);
    Sc32PaintEnd request={24,1,paint->window,paint->generation,paint->geometry_epoch,paint->paint_epoch};
    return gui_result(gui_call(SC32_GUI_PAINT_CANCEL,&request,0),SC32_GUI_PAINT_CANCEL);
}
