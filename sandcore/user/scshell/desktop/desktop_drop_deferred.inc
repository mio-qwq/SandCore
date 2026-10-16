static ScStatus desk_drop_reply(ScUiContext *,const ScUiEvent *,void *);
static void desk_drop_forget(void)
{free(desk.drop_bytes);desk.drop_bytes=0;memset(&desk.pending_drop,0,sizeof(desk.pending_drop));}
static void desk_drop_abort(ScStatus cause)
{
    if(!desk.drop_receipt.id)return;
    memset(&desk.drop_result,0,sizeof(desk.drop_result));desk.drop_result.size=sizeof(desk.drop_result);desk.drop_result.version=1;
    desk.drop_result.status=cause<0?cause:SC_UI_E_CANCELLED;
    ScStatus s=ScUiDropReply(desk.ui,&desk.drop_receipt,&desk.drop_result);desk.drop_stage=3;
    if(s<0){ScUiDropRelease(desk.ui,&desk.drop_receipt);desk.drop_stage=0;}
}
static ScStatus desk_drop_open_dialog(void)
{
    ScUiMenuItem buttons[4];memset(buttons,0,sizeof(buttons));
    const char *labels[]={"取消","复制到目标","移动到目标","创建快捷方式"};
    const ScU32 commands[]={SC_UI_DIALOG_CANCEL,DESK_DROP_COPY,DESK_DROP_MOVE,DESK_DROP_LINK};
    for(ScU32 i=0;i<4;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;buttons[i].label=desk_text(labels[i]);buttons[i].command=commands[i];}
    if(!(desk.pending_drop.actions&SC_UI_DRAG_COPY)){buttons[1].flags=SC_UI_MENU_DISABLED;buttons[3].flags=SC_UI_MENU_DISABLED;}
    if(!(desk.pending_drop.actions&SC_UI_DRAG_MOVE))buttons[2].flags=SC_UI_MENU_DISABLED;
    char message[200];snprintf(message,sizeof(message),"目标：%s\n请选择文件操作。快捷方式只保存目标路径。",desk.drop_destination);
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=desk.desktop;
    desc.title=desk_text("放置文件");desc.message=desk_text(message);desc.buttons=buttons;desc.button_count=4;
    desc.default_command=desc.cancel_command=SC_UI_DIALOG_CANCEL;desc.completion=desk_drop_reply;
    ScStatus s=ScUiMessageBox(desk.ui,&desc,&desk.dialog);if(s==0)desk.drop_stage=2;return s;
}
static ScStatus desk_drop_step(ScU32 *ready)
{
    if(!desk.drop_receipt.id)return 0;
    ScUiDropReceiptInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDropQuery(desk.ui,&desk.drop_receipt,&info);
    if(s<0 || info.state==SC_UI_DROP_FAILED){
        if(desk.drop_stage==2 && desk.dialog)ScUiDialogClose(desk.dialog,SC_UI_DIALOG_CANCEL);
        if(desk.drop_receipt.id)ScUiDropRelease(desk.ui,&desk.drop_receipt);
        desk.drop_stage=0;desk_drop_forget();return s<0?s:0;
    }
    if(desk.drop_stage==1 && info.state==SC_UI_DROP_READY && desk.visible && !desk.dialog){s=desk_drop_open_dialog();if(s<0){desk_drop_abort(s);desk_drop_forget();}*ready=1;return s;}
    if(desk.drop_stage==3 && info.state==SC_UI_DROP_READY){s=ScUiDropReply(desk.ui,&desk.drop_receipt,&desk.drop_result);if(s<0)return s;}
    if(info.state==SC_UI_DROP_SENT){ScUiDropRelease(desk.ui,&desk.drop_receipt);desk.drop_stage=0;desk_drop_forget();}
    return 0;
}
static ScU32 desk_drop_notice(const ScUiEvent *event,const ScUiDragData *data)
{
    if(event->type!=SC_UI_EVENT_DROP)return 0;
    if(data->phase!=SC_UI_DRAG_HANDOFF && data->phase!=SC32_DRAG_COMPLETE && data->phase!=SC32_DRAG_CANCEL)return 0;
    if(!data->bytes || event->command==desk.external_drag)desk.external_drag=0;
    if(data->phase!=SC_UI_DRAG_HANDOFF && data->data && data->bytes==sizeof(ScUiDropResult)){
        const ScUiDropResult *result=(const ScUiDropResult *)data->data;
        if(result->size==sizeof(*result) && result->version==1 &&
            (((result->flags&SC_UI_DROP_SOURCE_CHANGED) && result->source_changed) || result->uncertain))desk.refresh_pending=1;
    }
    return 1;
}
