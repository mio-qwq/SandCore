/* 业务结果只能从本receipt创建并认证过worker的job里取，不接受应用传一个
 * 普通job及成功标志来伪造来源改变。source UI仍只失效并重读FS，绝不再删源。 */
ScStatus ScShellDropFilesDeferred(ScUiContext *ui,ScUiDropReceipt *receipt,ScU32 action,const char *destination,const char *recovery,ScShellFileJob **out)
{
    if(!ui || !receipt || !out || (action!=SC_UI_DRAG_COPY&&action!=SC_UI_DRAG_MOVE))return SC_SHELL_E_INVALID;
    ScUiDropReceiptInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDropQuery(ui,receipt,&info);if(s<0)return sh_cause(s,86,0);
    if(info.state!=SC_UI_DROP_READY || info.transfer.format!=SC32_CLIPBOARD_FILES || !(info.transfer.actions&action))return SC_SHELL_E_INVALID;
    for(ShObject *o=sh_objects;o;o=o->next)if(o->kind==SH_JOB&&!o->closing){ScShellFileJob *other=(ScShellFileJob *)o;
        if(other->drop_ui==ui && other->drop_receipt.id==receipt->id)return SC_SHELL_E_BUSY;}
    s=ScUiContextRetain(ui);if(s<0)return sh_cause(s,86,0);
    info.transfer.chosen_action=action;
    ScShellFileJob *job=0;s=ScShellDropFiles(&info.transfer,destination,recovery,&job);
    if(s<0){ScUiContextRelease(ui);return s;}
    job->drop_ui=ui;job->drop_receipt=*receipt;
    memset(&job->drop_result,0,sizeof(job->drop_result));job->drop_result.size=sizeof(job->drop_result);job->drop_result.version=1;
    job->drop_result.action=action;job->drop_result.status=SC_SHELL_E_UNKNOWN;
    receipt->id=receipt->generation=0;*out=job;return 0;
}
static void sh_drop_poll(ScShellFileJob *job)
{
    if(!job->drop_ui || job->drop_cancelled || job->closed)return;
    ScUiDropReceiptInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDropQuery(job->drop_ui,&job->drop_receipt,&info);
    if(s<0 || info.state==SC_UI_DROP_FAILED){
        job->drop_cancelled=1;
        if(job->stage<4){
            /* 已知对端退出且COMMIT尚未完整发送，不能先发COMMIT再补CANCEL。
             * 这一路worker只有规划/握手，直接停止本人managed child，没有持久文件可撤销。 */
            if(job->managed)ScJobCancel(job->managed);
            if(job->channel){ScIpcClose(job->channel);job->channel=0;}
            if(job->listener){ScIpcClose(job->listener);job->listener=0;}
            job->closed=1;job->progress.state=SC_SHELL_JOB_CANCELLED;job->progress.status=SC_SHELL_E_CANCELLED;
        }else ScShellFileOperationCancel(job);
    }
}
static void sh_drop_release(ScShellFileJob *job)
{
    if(job->drop_ui){
        if(!sh_quiet){if(job->drop_receipt.id)ScUiDropRelease(job->drop_ui,&job->drop_receipt);ScUiContextRelease(job->drop_ui);}
        job->drop_ui=0;job->drop_receipt.id=job->drop_receipt.generation=0;
    }
}
ScStatus ScShellDropReply(ScShellFileJob *job)
{
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(job->drop_done)return SC_SHELL_END;
    if(!job->drop_ui)return SC_SHELL_E_INVALID;
    if(!job->closed || job->progress.state<SC_SHELL_JOB_FINISHED)return SC_SHELL_PENDING;
    ScStatus s=0;ScUiDropResult *r=&job->drop_result;
    if(!job->drop_reviewed){
        for(ScU32 budget=0;budget<16;budget++){
            ScShellFileResult item;memset(&item,0,sizeof(item));item.size=sizeof(item);item.version=1;
            s=ScShellFileOperationResult(job,job->drop_index,&item);
            if(s==SC_SHELL_END){job->drop_reviewed=1;break;}
            if(s<0){r->uncertain++;job->drop_reviewed=1;break;}
            job->drop_index++;r->items_total++;
            if(item.state==SC_SHELL_RESULT_DONE && item.status>=0 && (item.flags&SC_SHELL_DURABLE)){
                r->items_durable++;
                if(r->action==SC_UI_DRAG_MOVE && (item.stage==SC_SHELL_STAGE_RENAME || item.stage==SC_SHELL_STAGE_SOURCE_REMOVED))r->source_changed++;
            }else if(item.state==SC_SHELL_RESULT_SKIPPED || item.state==SC_SHELL_RESULT_CANCELLED)r->skipped++;
            else if(item.state==SC_SHELL_RESULT_SOURCE_RETAINED)r->retained++;
            else if(item.state==SC_SHELL_RESULT_FAILED)r->failed++;
            else r->uncertain++;
            if((item.flags&SC_SHELL_SOURCE_RETAINED) && item.state!=SC_SHELL_RESULT_SOURCE_RETAINED)r->retained++;
        }
        if(!job->drop_reviewed)return SC_SHELL_READY;
        if(job->progress.items_total>r->items_total){r->items_total=job->progress.items_total;r->uncertain++;}
        if(job->progress.state==SC_SHELL_JOB_UNKNOWN && !r->uncertain)r->uncertain++;
        if(job->progress.state==SC_SHELL_JOB_FAILED && !r->failed)r->failed++;
        if(job->progress.state==SC_SHELL_JOB_CANCELLED && !r->skipped)r->skipped++;
        if(r->source_changed)r->flags|=SC_UI_DROP_SOURCE_CHANGED;
        if(job->progress.state==SC_SHELL_JOB_FINISHED && r->items_total && r->items_durable==r->items_total &&
            !r->retained&&!r->failed&&!r->skipped&&!r->uncertain && (r->action!=SC_UI_DRAG_MOVE||r->source_changed)){
            r->flags|=SC_UI_DROP_COMMITTED;r->status=0;
        }else r->status=r->uncertain?SC_SHELL_E_UNKNOWN:job->progress.state==SC_SHELL_JOB_CANCELLED?SC_SHELL_E_CANCELLED:SC_SHELL_E_IO;
    }
    s=ScUiDropReply(job->drop_ui,&job->drop_receipt,r);
    if(s==SC_UI_PENDING || s==SC_UI_E_BUSY)return SC_SHELL_PENDING;
    if(s<0){job->drop_done=1;sh_drop_release(job);return sh_cause(s,86,0);}
    job->drop_done=1;sh_drop_release(job);return SC_SHELL_END;
}
