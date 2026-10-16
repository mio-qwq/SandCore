static ScStatus f2_drop_reply(ScUiContext *,const ScUiEvent *,void *);
static void f2_drop_forget(void)
{free(f2.drop_bytes);f2.drop_bytes=0;memset(&f2.pending_drop,0,sizeof(f2.pending_drop));}
static void f2_drop_abort(ScStatus cause)
{
    if(!f2.drop_receipt.id)return;
    ScUiDropResult result;memset(&result,0,sizeof(result));result.size=sizeof(result);result.version=1;
    result.status=cause<0?cause:SC_UI_E_CANCELLED;
    ScStatus s=ScUiDropReply(f2.ui,&f2.drop_receipt,&result);
    f2.drop_stage=3;
    if(s<0){ScUiDropRelease(f2.ui,&f2.drop_receipt);f2.drop_stage=0;}
}
static ScStatus f2_drop_open_dialog(void)
{
    ScUiMenuItem buttons[3];memset(buttons,0,sizeof(buttons));
    const char *labels[]={"取消","复制到目标","移动到目标"};
    const ScU32 commands[]={SC_UI_DIALOG_CANCEL,F2_DROP_COPY,F2_DROP_MOVE};
    for(ScU32 i=0;i<3;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;buttons[i].command=commands[i];buttons[i].label=f2_text(labels[i]);}
    if(!(f2.pending_drop.actions&SC_UI_DRAG_COPY))buttons[1].flags=SC_UI_MENU_DISABLED;
    if(!(f2.pending_drop.actions&SC_UI_DRAG_MOVE))buttons[2].flags=SC_UI_MENU_DISABLED;
    char message[180];snprintf(message,sizeof(message),"目标：%s\n只有真实目标提交后才允许移动删源。",f2.drop_destination);
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2.root;
    desc.title=f2_text("放置文件");desc.message=f2_text(message);desc.buttons=buttons;desc.button_count=3;
    desc.default_command=SC_UI_DIALOG_CANCEL;desc.cancel_command=SC_UI_DIALOG_CANCEL;desc.completion=f2_drop_reply;
    ScStatus s=ScUiMessageBox(f2.ui,&desc,&f2.dialog);if(s==0)f2.drop_stage=2;return s;
}
static ScStatus f2_drop_step(ScU32 *ready)
{
    if(!f2.drop_receipt.id)return 0;
    ScUiDropReceiptInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDropQuery(f2.ui,&f2.drop_receipt,&info);
    if(s<0 || info.state==SC_UI_DROP_FAILED){
        /* EOF在确认之前取消；不能因为传输过正文就偷偷开始文件job。 */
        if(f2.drop_stage==2 && f2.dialog)ScUiDialogClose(f2.dialog,SC_UI_DIALOG_CANCEL);
        if(f2.drop_receipt.id)ScUiDropRelease(f2.ui,&f2.drop_receipt);
        f2.drop_stage=0;f2_drop_forget();return s<0?s:0;
    }
    if(f2.drop_stage==1 && info.state==SC_UI_DROP_READY && f2.visible && !f2.dialog){
        s=f2_drop_open_dialog();if(s<0){f2_drop_abort(s);f2_drop_forget();}*ready=1;return s;
    }
    if(info.state==SC_UI_DROP_SENT){ScUiDropRelease(f2.ui,&f2.drop_receipt);f2.drop_stage=0;f2_drop_forget();}
    return 0;
}
static ScU32 f2_drop_notice(const ScUiEvent *event,const ScUiDragData *data)
{
    if(event->type!=SC_UI_EVENT_DROP)return 0;
    if(data->phase!=SC_UI_DRAG_HANDOFF && data->phase!=SC32_DRAG_COMPLETE && data->phase!=SC32_DRAG_CANCEL)return 0;
    if(!data->bytes || event->command==f2.drag_token)f2.drag_token=0;
    if(data->phase!=SC_UI_DRAG_HANDOFF && data->bytes==sizeof(ScUiDropResult) && data->data){
        const ScUiDropResult *result=(const ScUiDropResult *)data->data;
        if(result->size==sizeof(*result) && result->version==1){
            /* 只失效，下一次分页枚举才决定行是否仍存在；任何peer声明都不是删源许可。 */
            if((result->flags&SC_UI_DROP_SOURCE_CHANGED) && result->source_changed)f2.refresh_pending=1;
            if(result->uncertain)f2.refresh_pending=1;
            if(result->flags&SC_UI_DROP_COMMITTED)f2_status("目标已报告拖放终态；来源视图正在复核");
            else if(result->status<0)f2_status("拖放已结束；请核对真实逐项结果，未提交来源保留");
        }
    }
    return 1;
}
