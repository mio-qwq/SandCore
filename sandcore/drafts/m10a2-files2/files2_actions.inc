static ScUiNode f2_dialog_owner(void)
{
    /* 作业进度是共享库拥有的模态root。用公开枚举选择最内层活模态，
     * 冲突窗口才能接收输入；不拆解ScShellDialog的私有布局。 */
    ScUiNode after=0,owner=f2.root;
    for(;;){ScUiNode root=0;ScStatus s=ScUiContextRootNext(f2.ui,after,&root);if(s!=0)break;after=root;
        ScWindowInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        if(ScUiWindowNative(f2.ui,root,&info)==0 && (info.state&SC32_WINDOW_VISIBLE) && (info.state&SC32_WINDOW_MODAL)){
            owner=root;if(info.state&SC32_WINDOW_FOCUSED)break;}}
    return owner;
}
static ScStatus f2_error(ScStatus status,const char *path)
{
    if(status>=0 || status==SC_UI_E_CANCELLED || status==SC32_E_CANCELLED || status==SC_SHELL_E_CANCELLED)return status;
    ScShellErrorInfo error;memset(&error,0,sizeof(error));error.size=sizeof(error);error.version=1;
    if(ScShellErrorQuery(&error)<0 || error.code!=status){error.code=status;error.cause=status;
        ScErrorInfo native;memset(&native,0,sizeof(native));native.size=sizeof(native);native.version=1;
        if(ScErrorQuery(&native)==0 && native.code==status){error.cause=native.code;error.domain=native.domain;error.raw=native.raw;}}
    char message[240];snprintf(message,sizeof(message),"操作未完成（%d） · %s",status,path?path:"");f2_status(message);
    if(f2.ui && f2.root){ScUiDialog *dialog=0;
        if(ScShellErrorDialog(f2.ui,f2_dialog_owner(),&error,path?path:"",&dialog)==0)ScUiDialogRelease(dialog);}
    fprintf(stderr,"Files2 error=%d path=%s\n",status,path?path:"");return status;
}
static ScStatus f2_selected(ScShellFileItem **out,ScU32 *count)
{
    *out=0;*count=0;if(!f2.current)return 0;
    ScArray *ids=0;ScStatus s=ScUiSelectionCopy(f2.selection,&ids);if(s<0)return s;
    ScContainerInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;s=ScArrayQuery(ids,&info);
    ScShellFileItem *items=0;
    if(s==0 && info.length){if(info.length>UINT_MAX/sizeof(*items))s=SC_UI_E_RANGE;
        else {items=calloc(info.length,sizeof(*items));if(!items)s=SC_UI_E_MEMORY;}}
    ScU32 n=0;
    for(ScU32 i=0;s==0 && i<info.length;i++){
        ScU32 id,index;s=ScArrayGet(ids,i,&id);if(s<0)break;
        if(f2_lookup(f2.current->ids,&id,4,&index)<0 || f2.current->rank[index]==F2_NONE)continue;
        F2Entry *row=f2.current->rows+index;ScShellFileItem *item=items+n++;
        item->size=sizeof(*item);item->version=1;item->item=row->id;item->kind=row->kind;item->source=row->path;
        item->expected_source_revision=row->revision;item->flags=SC_SHELL_ITEM_EXPECT_SOURCE;
    }
    ScArrayRelease(ids);if(s<0){free(items);return s;}*out=items;*count=n;return 0;
}
static ScStatus f2_launch(const char *executable,const char *document)
{
    ScLaunchDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.path=executable;desc.arguments=document?document:"";
    ScLaunchInfo launched;memset(&launched,0,sizeof(launched));launched.size=sizeof(launched);launched.version=1;
    return ScProcessLaunch(&desc,&launched);
}
static ScU32 f2_extension(const char *path,const char *extension)
{const char *actual=strrchr(f2_leaf(path),'.');return actual && !f2_compare_text(actual,extension);}
static ScStatus f2_open_one(const ScShellFileItem *item,ScU32 mode)
{
    ScStatus s=f2_check_item(item);if(s<0)return s;
    if(item->kind==SC32_PATH_DIRECTORY)return f2_navigate(item->source,F2_NAV_NEW,0,0);
    if(f2_extension(item->source,".LNK") && mode==0){ScShellShortcut *shortcut=0;ScLaunchInfo launched;
        memset(&launched,0,sizeof(launched));launched.size=sizeof(launched);launched.version=1;
        s=ScShellShortcutRead(item->source,&shortcut);
        if(s==0)s=ScShellShortcutLaunch(shortcut,f2.catalog,&launched);
        ScShellShortcutRelease(shortcut);return s;}
    if(f2_extension(item->source,".SCX") && mode!=1)return f2_launch(mode==2?"/APPS/DEBUGGER.SCX":item->source,mode==2?item->source:0);
    if(!mode && f2.catalog){ScLaunchInfo launched;memset(&launched,0,sizeof(launched));launched.size=sizeof(launched);launched.version=1;
        s=ScShellOpen(f2.catalog,item->source,"open",&launched);
        if(s!=SC_SHELL_E_NOT_FOUND && s!=SC_SHELL_E_NOT_SUPPORTED)return s;}
    static const char *pictures[]={".SCB",".BMP",".PNG",".JPG",".JPEG",".WEBP"};
    for(ScU32 i=0;i<sizeof(pictures)/sizeof(pictures[0]);i++)if(f2_extension(item->source,pictures[i]))return f2_launch("/APPS/LENS.SCX",item->source);
    static const char *texts[]={".C",".H",".INC",".ASM",".TXT",".CFG",".LOG"};
    for(ScU32 i=0;i<sizeof(texts)/sizeof(texts[0]);i++)if(f2_extension(item->source,texts[i]))return f2_launch("/APPS/IDE.SCX",item->source);
    f2_status("未知二进制；请在右键菜单中选择“Open with”");return SC_SHELL_E_NOT_SUPPORTED;
}
static ScStatus f2_open_items(const ScShellFileItem *items,ScU32 count,ScU32 mode)
{
    if(!count)return SC_UI_E_INVALID;
    for(ScU32 i=0;i<count;i++){
        /* 多选中的目录保留为单次导航，避免同一窗口连续导航时只留下最后一个。 */
        if(count>1 && items[i].kind==SC32_PATH_DIRECTORY){ScStatus checked=f2_check_item(items+i);if(checked<0)return checked;
            ScStatus s=f2_launch("/APPS/FILES.SCX",items[i].source);if(s<0)return s;}
        else {ScStatus s=f2_open_one(items+i,mode);if(s<0)return s;}
    }
    f2_status("已分派打开请求");return 0;
}
static ScStatus f2_shell_event(const ScShellEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_SHELL_EVENT_COMMAND){
        if(event->command==SC_SHELL_COMMAND_OPEN || event->command==F2_EDIT || event->command==F2_DEBUG){
            if(event->bytes%sizeof(ScShellFileItem))return SC_SHELL_E_PROTOCOL;
            ScStatus s=f2_open_items(event->data,event->bytes/sizeof(ScShellFileItem),event->command==F2_EDIT?1:event->command==F2_DEBUG?2:0);
            return s<0?s:SC_UI_HANDLED;
        }
        if(event->command==SC_SHELL_COMMAND_PASTE || event->command==SC_SHELL_COMMAND_DELETE
            || event->command==SC_SHELL_COMMAND_RENAME || event->command==SC_SHELL_COMMAND_NEW_DIRECTORY)return f2_recovery();
    }
    if(event->type==SC_SHELL_EVENT_ITEM && event->data && event->bytes==sizeof(ScShellFileResult)){
        const ScShellFileResult *result=event->data;
        if(result->state==SC_SHELL_RESULT_DONE && (result->flags&SC_SHELL_DURABLE)){
            f2_status(result->flags&SC_SHELL_SOURCE_RETAINED?"目标已提交；来源保留":"文件变更已持久提交");f2.refresh_pending=1;
        }else if(result->state==SC_SHELL_RESULT_UNCERTAIN)f2_status("结果不确定；来源保留，请检查进度和恢复记录");
        else if(result->state==SC_SHELL_RESULT_FAILED)f2_status("文件项失败；其它已提交项保留");
    }
    if(event->type==SC_SHELL_EVENT_CONNECTED || event->type==SC_SHELL_EVENT_CHANGED)f2.catalog_dirty=1;
    if(event->type==SC_SHELL_EVENT_DISCONNECTED)f2_status("桌面服务连接中断；当前目录和文件操作保留");
    return 0;
}
static ScStatus f2_menu_extra(ScUiMenu *menu,ScU32 command,const char *label,ScU32 flags)
{
    ScUiMenuItem item;memset(&item,0,sizeof(item));item.size=sizeof(item);item.version=1;item.command=command;
    item.label=f2_text(label);item.flags=flags;ScU32 id;return ScUiMenuInsert(menu,0,&item,&id);
}
static ScStatus f2_menu_prepare(ScU32 kind,ScI32 x,ScI32 y,ScU32 show)
{
    if(f2.popup){ScUiMenuClose(f2.ui,f2.popup);f2.popup=0;}
    if(f2.shell_menu){
        ScStatus active=ScShellMenuStep(f2.shell_menu,0);
        if(active==SC_SHELL_END){ScShellMenuRelease(f2.shell_menu);f2.shell_menu=0;}
        else {F2MenuWork *work=malloc(sizeof(*work));if(!work)return SC_UI_E_MEMORY;
            work->menu=f2.shell_menu;work->last_error=f2.menu_error;work->next=f2.menu_work;f2.menu_work=work;f2.shell_menu=0;}
    }
    f2.menu_error=0;
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=f2_selected(&items,&count);if(s<0)return s;
    if(kind==SC_SHELL_MENU_BACKGROUND)count=0;
    ScShellMenuDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.kind=kind;desc.items=items;desc.count=count;desc.directory=f2.current?f2.current->path:"/";
    desc.catalog=f2.catalog;desc.ui=f2.ui;desc.owner=f2.root;desc.procedure=f2_shell_event;desc.recovery_directory=f2.recovery_directory;
    s=ScShellMenuBuild(&desc,&f2.shell_menu);free(items);ScUiMenu *native=0;
    if(s==0)s=ScShellMenuNative(f2.shell_menu,&native);
    if(s==0)s=f2_menu_extra(native,0,"",SC_UI_MENU_SEPARATOR);
    if(s==0)s=f2_menu_extra(native,F2_EDIT,"在 Studio 编辑",count?0:SC_UI_MENU_DISABLED);
    if(s==0)s=f2_menu_extra(native,F2_DEBUG,"在 Debug 调试",count?0:SC_UI_MENU_DISABLED);
    if(s==0)s=f2_menu_extra(native,F2_NEW_FILE,"新建空文件",0);
    if(s==0)s=f2_menu_extra(native,F2_REFRESH,"刷新",0);
    if(s==0 && show)s=ScUiMenuOpen(f2.ui,f2.root,native,x,y,0,&f2.popup);
    ScUiMenuRelease(native);return s;
}
static ScStatus f2_clipboard(ScU32 command)
{
    if(command==SC_SHELL_COMMAND_PASTE){ScStatus s=f2_recovery();ScShellFileJob *job=0;
        if(s==0)s=ScShellClipboardPaste(f2.current?f2.current->path:"/",f2.recovery_path,f2_shell_event,0,&job);
        return s<0?s:f2_job_adopt(job);}
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=f2_selected(&items,&count);
    if(s==0 && !count)s=SC_UI_E_INVALID;
    ScClipboardInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(s==0)s=ScClipboardQuery(&info);
    if(s==0)s=command==SC_SHELL_COMMAND_CUT?ScShellClipboardCutFiles(items,count,info.epoch,&info):ScShellClipboardCopyFiles(items,count,info.epoch,&info);
    free(items);if(s==0)f2_status(command==SC_SHELL_COMMAND_CUT?"文件剪切意图已放入系统剪贴板；来源尚未删除":"文件已复制到系统剪贴板");return s;
}
static ScStatus f2_conflict_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;F2Job *job=context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(job->conflict==dialog){job->conflict=0;ScUiDialogRelease(dialog);}
    if(s<0)return s;
    if(info.command==SC_SHELL_RENAME_TARGET)return f2_prompt(F2_PROMPT_RENAME_CONFLICT,job);
    job->decision=info.stage==SC_UI_DIALOG_CANCELLED?SC_SHELL_CANCEL:info.command;return 0;
}
static ScStatus f2_conflict_step(F2Job *job,const ScShellFileProgress *progress)
{
    if(job->decision){ScShellConflictDecision decision;memset(&decision,0,sizeof(decision));decision.size=sizeof(decision);decision.version=1;
        decision.item=job->observation.item;decision.conflict_epoch=job->observation.conflict_epoch;decision.decision=job->decision;
        decision.expected_source_revision=job->observation.source_revision;decision.expected_destination_revision=job->observation.destination_revision;
        ScStatus s=ScShellFileOperationResolveConflict(job->handle,&decision);
        if(s==SC_SHELL_E_BUSY)return 0;
        job->decision=0;if(s<0){job->conflict_epoch=0;return s;}}
    if(progress->state!=SC_SHELL_JOB_CONFLICT || job->conflict || f2.dialog)return 0;
    ScShellFileResult conflict;memset(&conflict,0,sizeof(conflict));conflict.size=sizeof(conflict);conflict.version=1;
    ScStatus s=ScShellFileOperationConflict(job->handle,&conflict);if(s<0)return s;
    if(job->conflict_epoch==conflict.conflict_epoch)return 0;
    job->observation=conflict;job->conflict_epoch=conflict.conflict_epoch;
    ScUiMenuItem buttons[6];memset(buttons,0,sizeof(buttons));
    static const char *labels[]={"替换当前目标","跳过此项","改用其它名称","合并目录","重试此项","取消作业"};
    static const ScU32 commands[]={SC_SHELL_REPLACE,SC_SHELL_SKIP,SC_SHELL_RENAME_TARGET,SC_SHELL_MERGE,SC_SHELL_RETRY,SC_SHELL_CANCEL};
    for(ScU32 i=0;i<6;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;buttons[i].command=commands[i];buttons[i].label=f2_text(labels[i]);}
    ScPathInfo a,b;if(f2_query(conflict.source,&a)<0 || f2_query(conflict.destination,&b)<0
        || a.kind!=SC32_PATH_DIRECTORY || b.kind!=SC32_PATH_DIRECTORY)buttons[3].flags=SC_UI_MENU_DISABLED;
    char message[512];snprintf(message,sizeof(message),"来源：%s\n目标：%s\n错误 %d / %d。决定仅用于本次修订；默认保留来源并跳过。",conflict.source,conflict.destination,conflict.status,conflict.raw);
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.owner=f2_dialog_owner();desc.title=f2_text("文件操作冲突");desc.message=f2_text(message);desc.buttons=buttons;desc.button_count=6;
    desc.default_command=SC_SHELL_SKIP;desc.cancel_command=SC_SHELL_CANCEL;desc.completion=f2_conflict_reply;desc.context=job;
    return ScUiMessageBox(f2.ui,&desc,&job->conflict);
}
static ScStatus f2_job_adopt(ScShellFileJob *handle)
{
    F2Job *job=calloc(1,sizeof(*job));if(!job){ScShellFileOperationCancel(handle);ScShellFileOperationClose(handle);return SC_UI_E_MEMORY;}
    job->handle=handle;job->last_state=job->last_item=F2_NONE;
    ScStatus s=ScShellProgress(f2.ui,f2.root,handle,&job->progress);
    if(s<0){ScShellFileOperationCancel(handle);ScShellFileOperationClose(handle);free(job);return s;}
    job->next=f2.jobs;f2.jobs=job;f2_status("文件作业已启动；等待实际持久提交");return 0;
}
static void f2_job_close(F2Job *job)
{
    if(job->conflict)ScUiDialogRelease(job->conflict);
    ScShellDialogClose(job->progress);ScShellFileOperationClose(job->handle);free(job);
}
static ScStatus f2_jobs_step(ScU32 *ready)
{
    F2Job **link=&f2.jobs;ScStatus first=0;
    while(*link){F2Job *job=*link;ScStatus s=job->final?0:ScShellFileOperationStep(job->handle,0);
        if(s==SC_SHELL_READY)*ready=1;
        ScShellFileProgress progress;memset(&progress,0,sizeof(progress));progress.size=sizeof(progress);progress.version=1;
        ScStatus query=ScShellFileOperationQuery(job->handle,&progress);if(query<0){
            if(query!=job->last_error && !first)first=query;
            job->last_error=query;link=&job->next;continue;}
        ScStatus dialog=ScShellDialogStep(job->progress);if(dialog==SC_SHELL_READY)*ready=1;
        if(progress.state!=job->last_state || progress.current_item!=job->last_item){
            job->last_state=progress.state;job->last_item=progress.current_item;
            static const char *states[]={"等待","启动工作进程","规划文件","传输中","等待冲突决定","正在取消","已结束","已取消","操作失败","结果不确定"};
            const char *state=progress.state<sizeof(states)/sizeof(states[0])?states[progress.state]:states[0];
            char text[200];snprintf(text,sizeof(text),"文件作业：%u / %u 项 · %s",progress.items_done,progress.items_total,state);f2_status(text);}
        if(progress.state<SC_SHELL_JOB_FINISHED){ScStatus conflict=f2_conflict_step(job,&progress);if(conflict<0 && !first)first=conflict;}
        else if(!job->reviewed){
            job->final=1;*ready=1;
            /* 完成百分比不是成功凭据。逐项消费公开结果，限定每轮16项，
             * 只有DONE+DURABLE才计提交；移动保留来源和UNKNOWN单列。 */
            for(ScU32 budget=0;budget<16;budget++){ScShellFileResult result;memset(&result,0,sizeof(result));result.size=sizeof(result);result.version=1;
                ScStatus r=ScShellFileOperationResult(job->handle,job->read_index,&result);
                if(r==SC_SHELL_END){job->reviewed=1;break;}if(r<0){job->reviewed=1;job->uncertain++;if(!first)first=r;break;}
                job->read_index++;
                if(result.state==SC_SHELL_RESULT_DONE){if(result.flags&SC_SHELL_DURABLE)job->durable++;else job->uncertain++;}
                else if(result.state==SC_SHELL_RESULT_SKIPPED || result.state==SC_SHELL_RESULT_CANCELLED)job->skipped++;
                else if(result.state==SC_SHELL_RESULT_FAILED)job->failed++;
                else if(result.state==SC_SHELL_RESULT_UNCERTAIN)job->uncertain++;
                if((result.flags&SC_SHELL_SOURCE_RETAINED) || result.state==SC_SHELL_RESULT_SOURCE_RETAINED)job->retained++;
            }
            if(job->reviewed){ScClipboardInfo clipboard;memset(&clipboard,0,sizeof(clipboard));clipboard.size=sizeof(clipboard);clipboard.version=1;
                ScStatus reconcile=ScShellClipboardReconcile(job->handle,&clipboard);
                /* Reconcile只接受剪切来源；普通复制/Drop返回INVALID表示
                 * 没有可协调的CUT记录，不把成功复制误报为作业错误。 */
                if(reconcile<0 && reconcile!=SC_SHELL_E_STALE && reconcile!=SC_SHELL_E_INVALID && !first)first=reconcile;
                if(progress.state==SC_SHELL_JOB_UNKNOWN && !job->uncertain)job->uncertain++;
                if(progress.state==SC_SHELL_JOB_FAILED && !job->failed)job->failed++;
                char text[220];snprintf(text,sizeof(text),"已持久提交 %u 项 · 来源保留 %u · 跳过/取消 %u · 失败 %u · 不确定 %u",
                    job->durable,job->retained,job->skipped,job->failed,job->uncertain);f2_status(text);
                f2.refresh_pending=1;
            }
        }
        if(job->reviewed && !job->conflict && (dialog==SC_SHELL_END || (progress.state==SC_SHELL_JOB_FINISHED
            && !job->failed && !job->uncertain && !job->skipped && !job->retained))){
            *link=job->next;f2_job_close(job);continue;}
        if(s<0 && s!=job->last_error){job->last_error=s;if(!first)first=s;}link=&job->next;
    }
    return first;
}
static ScStatus f2_prompt_apply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_APPLY && event->type!=SC_UI_EVENT_VALIDATE)return 0;
    ScConfig *config=(ScConfig *)event->data;ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
    ScStatus s=ScConfigGet(config,"file","path",&value);char candidate[256],path[F2_PATH];
    if(s==0 && (value.type!=SC_VALUE_UTF8 || value.bytes>=sizeof(candidate) || !value.bytes || memchr(value.data,0,value.bytes)))s=SC_UI_E_RANGE;
    if(s==0){memcpy(candidate,value.data,value.bytes);candidate[value.bytes]=0;s=f2_absolute(candidate,f2.current?f2.current->path:"/",path);}
    if(s<0)return s;
    if(f2.prompt_kind==F2_PROMPT_NEW){ScPathInfo metadata;s=f2_query(path,&metadata);
        if(s!=SC32_E_NOT_FOUND)return s<0?s:SC32_E_EXISTS;
        s=0;
        if(event->type==SC_UI_EVENT_APPLY){ScString *empty=0;s=ScStringCreate(0,0,&empty);
            if(s==0)s=f2_writer_begin(path,empty,0,F2_WRITE_EMPTY);
            ScStringRelease(empty);}
    }else if(f2.prompt_kind==F2_PROMPT_RENAME_CONFLICT){F2Job *job=f2.prompt_job;if(!job)return SC_UI_E_STALE;
        if(event->type==SC_UI_EVENT_APPLY){ScShellConflictDecision decision;memset(&decision,0,sizeof(decision));decision.size=sizeof(decision);decision.version=1;
            decision.item=job->observation.item;decision.conflict_epoch=job->observation.conflict_epoch;decision.decision=SC_SHELL_RENAME_TARGET;
            decision.expected_source_revision=job->observation.source_revision;decision.expected_destination_revision=job->observation.destination_revision;
            decision.new_path=path;s=ScShellFileOperationResolveConflict(job->handle,&decision);}
    }else s=SC_UI_E_INVALID;
    return s;
}
static ScStatus f2_prompt(ScU32 kind,F2Job *job)
{
    if(f2.dialog)return SC_UI_E_BUSY;
    ScConfig *config=0;ScStatus s=ScConfigCreate(&config);if(s<0)return s;
    ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;value.type=SC_VALUE_UTF8;
    char candidate[160];snprintf(candidate,sizeof(candidate),"%s%sNEW.TXT",f2.current?f2.current->path:"/",f2.current && strcmp(f2.current->path,"/")?"/":"");
    const char *initial=kind==F2_PROMPT_NEW?candidate:job->observation.destination;
    value.data=initial;value.bytes=(ScU32)strlen(initial);s=ScConfigSet(config,"file","path",&value);
    ScUiFieldDesc field;memset(&field,0,sizeof(field));field.size=sizeof(field);field.version=1;field.flags=SC_UI_FIELD_REQUIRED;
    field.field=1;field.kind=SC_UI_TEXTBOX;field.label=f2_text("完整路径");field.section="file";field.key="path";
    ScUiPropertySheetDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2_dialog_owner();
    desc.title=f2_text(kind==F2_PROMPT_NEW?"新建空文件":"冲突项另存为");desc.initial=config;
    desc.validate=f2_prompt_apply;desc.apply=f2_prompt_apply;desc.completion=f2_dialog_complete;
    f2.prompt_kind=kind;f2.prompt_job=job;if(s==0)s=ScUiPropertySheet(f2.ui,&desc,&field,1,&f2.dialog);
    ScConfigRelease(config);if(s<0){f2.prompt_kind=0;f2.prompt_job=0;}return s;
}
static ScStatus f2_dialog_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;
    if(f2.dialog==dialog){f2.dialog=0;ScUiDialogRelease(dialog);f2.prompt_kind=0;f2.prompt_job=0;}return 0;
}
static ScStatus f2_file_chosen(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);ScString *selected=0;const char *z=0;
    if(s==0 && info.stage==SC_UI_DIALOG_DONE && info.command==SC_UI_DIALOG_OK){
        s=ScUiDialogSelectedPath(dialog,0,&selected);if(s==0)s=ScStringCStr(selected,&z);
        char path[F2_PATH];if(s==0)s=f2_absolute(z,f2.current?f2.current->path:"/",path);
        ScPathInfo metadata;if(s==0)s=f2_query(path,&metadata);
        if(s==0 && metadata.kind==SC32_PATH_DIRECTORY)s=f2_navigate(path,F2_NAV_NEW,0,0);
        else if(s==0){strcpy(f2.pending_file,path);char *slash=strrchr(path,'/');if(slash==path)path[1]=0;else if(slash)*slash=0;
            s=f2_navigate(path,F2_NAV_NEW,0,0);}
    }
    ScStringRelease(selected);if(f2.dialog==dialog){f2.dialog=0;ScUiDialogRelease(dialog);}if(s<0)f2_error(s,"");return 0;
}
static ScStatus f2_file_dialog(ScU32 directory)
{
    if(f2.dialog)return SC_UI_E_BUSY;
    ScUiFileDialogDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2.root;
    desc.flags=SC_UI_FILE_OPEN|(directory?SC_UI_FILE_DIRECTORY:0);desc.title=f2_text(directory?"选择目录":"定位文件");
    desc.directory=f2.current?f2.current->path:"/";desc.completion=f2_file_chosen;return ScShellFileDialog(f2.ui,&desc,&f2.dialog);
}
static void f2_gesture_cancel(ScU32 cancel_drag)
{
    if(f2.gesture && f2.ui)ScUiCapture(f2.ui,f2.table,0);
    f2.gesture=0;
    if(cancel_drag && f2.drag_token){ScUiDragCancel(f2.ui,f2.drag_token);f2.drag_token=0;}
}
static ScStatus f2_drag_begin(void)
{
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=f2_selected(&items,&count);f2.gesture=0;
    if(s==0 && !count)s=SC_UI_E_INVALID;
    if(s==0)s=ScShellDragBegin(f2.ui,f2.table,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,&f2.drag_token);
    free(items);if(s<0){f2.drag_token=0;ScUiCapture(f2.ui,f2.table,0);}return s;
}
static ScStatus f2_drop_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.stage==SC_UI_DIALOG_DONE && (info.command==F2_DROP_COPY || info.command==F2_DROP_MOVE)){
        f2.pending_drop.chosen_action=info.command==F2_DROP_COPY?SC_UI_DRAG_COPY:SC_UI_DRAG_MOVE;
        ScPathInfo target;s=f2_query(f2.drop_destination,&target);
        if(s==0 && (target.kind!=SC32_PATH_DIRECTORY || target.generation!=f2.drop_revision))s=SC_SHELL_E_STALE;
        ScShellFileJob *job=0;if(s==0)s=f2_recovery();if(s==0)s=ScShellDropFiles(&f2.pending_drop,f2.drop_destination,f2.recovery_path,&job);
        if(s==0)s=f2_job_adopt(job);
    }
    free(f2.drop_bytes);f2.drop_bytes=0;memset(&f2.pending_drop,0,sizeof(f2.pending_drop));
    if(f2.dialog==dialog){f2.dialog=0;ScUiDialogRelease(dialog);}if(s<0)f2_error(s,f2.drop_destination);return 0;
}
static ScStatus f2_drop(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(!event->data || event->bytes!=sizeof(ScUiDragData))return SC_SHELL_E_PROTOCOL;
    ScUiDragData *data=(ScUiDragData *)event->data;
    if(data->phase==SC32_DRAG_COMPLETE){f2.drag_token=0;return 0;}
    if(data->format!=SC32_CLIPBOARD_FILES)return 0;
    if(event->type==SC_UI_EVENT_DRAG_LEAVE){f2.drop_item=0;f2_status("拖放已离开当前窗口");return SC_UI_HANDLED;}
    if(event->type!=SC_UI_EVENT_DRAG_ENTER && event->type!=SC_UI_EVENT_DRAG_OVER && event->type!=SC_UI_EVENT_DROP)return 0;
    if(f2.dialog || f2.scan.active || f2.exit_pending || !f2.current){data->chosen_action=0;return SC_UI_HANDLED;}
    char destination[F2_PATH];strcpy(destination,f2.current->path);ScU32 item=0,column=0,index,expected=f2.current->directory_revision;
    if(event->node==f2.table && ScUiViewHitTest(f2.ui,f2.table,event->x,event->y,&item,&column)==0
        && f2_lookup(f2.current->ids,&item,4,&index)==0 && f2.current->rows[index].kind==SC32_PATH_DIRECTORY)
        {strcpy(destination,f2.current->rows[index].path);expected=f2.current->rows[index].revision;}
    else if(event->node==f2.tree && ScUiViewHitTest(f2.ui,f2.tree,event->x,event->y,&item,&column)==0
        && !(item&0x80000000u) && item && item<=f2.node_count){strcpy(destination,f2.nodes[item-1].path);expected=f2.nodes[item-1].revision;}
    else item=0;
    ScU32 action=(event->modifiers&SC32_MOD_SHIFT)?SC_UI_DRAG_MOVE:SC_UI_DRAG_COPY;
    if(!(data->actions&action))action=data->actions&SC_UI_DRAG_COPY?SC_UI_DRAG_COPY:data->actions&SC_UI_DRAG_MOVE?SC_UI_DRAG_MOVE:0;
    data->chosen_action=action;
    if(event->type!=SC_UI_EVENT_DROP){if(f2.drop_item!=item || strcmp(f2.drop_destination,destination)){
            f2.drop_item=item;strcpy(f2.drop_destination,destination);char message[150];snprintf(message,sizeof(message),"放置到 %s · %s",destination,action==SC_UI_DRAG_MOVE?"移动":"复制");f2_status(message);}return SC_UI_HANDLED;}
    ScPathInfo metadata;ScStatus s=f2_query(destination,&metadata);if(s<0)return s;
    if(metadata.kind!=SC32_PATH_DIRECTORY || (expected && metadata.generation!=expected))return SC_SHELL_E_STALE;
    f2.drop_revision=metadata.generation;
    if(!action || data->bytes>65536 || !data->bytes || !data->data)return SC_SHELL_E_PROTOCOL;
    void *owned=malloc(data->bytes);if(!owned)return SC_UI_E_MEMORY;memcpy(owned,data->data,data->bytes);
    f2.drop_bytes=owned;f2.pending_drop=*data;f2.pending_drop.data=owned;strcpy(f2.drop_destination,destination);
    ScUiMenuItem buttons[3];memset(buttons,0,sizeof(buttons));const char *labels[]={"取消","复制到目标","移动到目标"};
    const ScU32 commands[]={SC_UI_DIALOG_CANCEL,F2_DROP_COPY,F2_DROP_MOVE};
    for(ScU32 i=0;i<3;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;buttons[i].command=commands[i];buttons[i].label=f2_text(labels[i]);}
    if(!(data->actions&SC_UI_DRAG_COPY))buttons[1].flags=SC_UI_MENU_DISABLED;
    if(!(data->actions&SC_UI_DRAG_MOVE))buttons[2].flags=SC_UI_MENU_DISABLED;
    char message[180];snprintf(message,sizeof(message),"目标：%s\n只有真实目标提交后才允许移动删源。",destination);
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2.root;
    desc.title=f2_text("放置文件");desc.message=f2_text(message);desc.buttons=buttons;desc.button_count=3;
    desc.default_command=SC_UI_DIALOG_CANCEL;desc.cancel_command=SC_UI_DIALOG_CANCEL;desc.completion=f2_drop_reply;
    s=ScUiMessageBox(f2.ui,&desc,&f2.dialog);
    if(s<0){free(f2.drop_bytes);f2.drop_bytes=0;memset(&f2.pending_drop,0,sizeof(f2.pending_drop));}return s<0?s:SC_UI_HANDLED;
}
static ScStatus f2_exit_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.command==F2_EXIT_CANCEL){f2.exit_pending=1;f2.writer.cancel=1;
        for(F2Job *job=f2.jobs;job;job=job->next)ScShellFileOperationCancel(job->handle);
        if(f2.shell_menu){ScShellMenuRelease(f2.shell_menu);f2.shell_menu=0;}
        while(f2.menu_work){F2MenuWork *work=f2.menu_work;f2.menu_work=work->next;ScShellMenuRelease(work->menu);free(work);}
        f2_scan_cancel();f2_order_cancel();f2_gesture_cancel(1);f2_status("正在取消未完成作业并等待安全回收");}
    if(f2.dialog==dialog){f2.dialog=0;ScUiDialogRelease(dialog);}return 0;
}
static ScStatus f2_exit_request(void)
{
    if(f2.dialog)return SC_UI_E_BUSY;
    ScU32 work=f2.writer.active || f2.jobs || f2.menu_work;
    if(f2.shell_menu && ScShellMenuStep(f2.shell_menu,0)!=SC_SHELL_END)work=1;
    if(!work && f2.bookmark_serial==f2.saved_serial){f2.quit=1;return 0;}
    ScUiMenuItem buttons[2];memset(buttons,0,sizeof(buttons));
    buttons[0].size=buttons[1].size=sizeof(*buttons);buttons[0].version=buttons[1].version=1;
    buttons[0].command=F2_EXIT_WAIT;buttons[0].label=f2_text("继续等待 / 返回");
    buttons[1].command=F2_EXIT_CANCEL;buttons[1].label=f2_text("取消未完成操作并退出");
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2.root;
    desc.title=f2_text("仍有未完成工作");desc.message=f2_text("文件作业或书签尚未完成。已提交项保留；取消不会将不确定结果报告为成功。");
    desc.buttons=buttons;desc.button_count=2;desc.default_command=F2_EXIT_WAIT;desc.cancel_command=F2_EXIT_WAIT;desc.completion=f2_exit_reply;
    return ScUiMessageBox(f2.ui,&desc,&f2.dialog);
}
static ScStatus f2_read_node(ScUiNode node,char *out,ScU32 capacity)
{
    ScString *value=0;const char *z=0;ScStatus s=ScUiNodeCopyText(f2.ui,node,&value);
    if(s==0)s=ScStringCStr(value,&z);
    if(s==0 && strlen(z)>=capacity)s=SC_UI_E_RANGE;
    if(s==0)strcpy(out,z);
    ScStringRelease(value);return s;
}
static ScStatus f2_command(ScU32 command)
{
    if(command>=F2_BOOKMARK_FIRST && command<F2_HISTORY_FIRST){ScU32 i=command-F2_BOOKMARK_FIRST;
        return i<f2.bookmark_count?f2_navigate(f2.bookmarks[i].path,F2_NAV_NEW,0,0):SC_UI_E_STALE;}
    if(command>=F2_HISTORY_FIRST && command<F2_HISTORY_FIRST+f2.history_count){ScU32 i=command-F2_HISTORY_FIRST;
        return f2_navigate(f2.history[i].path,F2_NAV_HISTORY,i,0);}
    if(command==F2_BACK || command==F2_FORWARD){ScU32 index=f2.history_index;
        if(command==F2_BACK){if(!index)return 0;index--;}else {if(index+1>=f2.history_count)return 0;index++;}
        return f2_navigate(f2.history[index].path,F2_NAV_HISTORY,index,0);}
    if(command==F2_UP){char path[F2_PATH];strcpy(path,f2.current?f2.current->path:"/");
        char *slash=strrchr(path,'/');if(slash==path)path[1]=0;else if(slash)*slash=0;return f2_navigate(path,F2_NAV_NEW,0,0);}
    if(command==F2_REFRESH){f2.catalog_dirty=1;f2.reconnect_pending=1;f2_tree_invalidate();return f2_navigate(f2.current?f2.current->path:"/",F2_NAV_REFRESH,0,
            f2.current && f2.current->search?f2.current->query:0);}
    if(command==F2_GO){char path[1024];ScStatus s=f2_read_node(f2.address,path,sizeof(path));return s<0?s:f2_navigate(path,F2_NAV_NEW,0,0);}
    if(command==F2_ADDRESS)return ScUiFocus(f2.ui,f2.address);
    if(command==F2_FILTER){f2.filter_pending=1;return ScUiFocus(f2.ui,f2.filter);}
    if(command==F2_SEARCH){char query[F2_QUERY];ScStatus s=f2_read_node(f2.search,query,sizeof(query));
        return s<0?s:f2_navigate(f2.current?f2.current->path:"/",F2_NAV_SEARCH,0,query);}
    if(command==F2_CANCEL_SEARCH){f2_scan_cancel();f2_order_cancel();f2_table_notify(SC_UI_DATA_SORTED);
        ScUiNodeSetState(f2.ui,f2.scan_progress,SC_UI_VISIBLE,0);f2_status("已取消；当前结果保留");return 0;}
    if(command==F2_BROWSE || command==F2_OPEN_FILE)return f2_file_dialog(command==F2_BROWSE);
    if(command==F2_CLOSE)return f2_exit_request();
    if(command==F2_BOOKMARK_ADD){if(!f2.current)return SC_UI_E_INVALID;
        ScStatus s=f2_bookmark_add_path(f2.current->path);if(s<0)return s;
        if(f2.bookmark_serial==UINT_MAX)return SC_UI_E_RANGE;
        f2.bookmark_serial++;f2.bookmark_save_pending=1;return f2_places_refresh();}
    if(command==F2_BOOKMARK_REMOVE){if(!f2.current)return SC_UI_E_INVALID;
        for(ScU32 i=0;i<f2.bookmark_count;i++)if(!f2_compare_text(f2.current->path,f2.bookmarks[i].path)){
            memmove(f2.bookmarks+i,f2.bookmarks+i+1,(f2.bookmark_count-i-1)*sizeof(F2Bookmark));f2.bookmark_count--;
            if(f2.bookmark_serial==UINT_MAX)return SC_UI_E_RANGE;
            f2.bookmark_serial++;f2.bookmark_save_pending=1;return f2_places_refresh();}
        return SC_UI_E_NOT_FOUND;}
    if(command==F2_NEW_FILE)return f2_prompt(F2_PROMPT_NEW,0);
    if(command==SC_SHELL_COMMAND_COPY || command==SC_SHELL_COMMAND_CUT || command==SC_SHELL_COMMAND_PASTE)return f2_clipboard(command);
    if(command==F2_SELECT_ALL){if(!f2.current || !f2.current->visible)return 0;
        return ScUiSelectionRange(f2.selection,f2.current->rows[f2.current->order[0]].id,
            f2.current->rows[f2.current->order[f2.current->visible-1]].id,SC_UI_REPLACE);}
    if(command==F2_SELECT_NONE)return ScUiSelectionClear(f2.selection);
    if(command==F2_BOOKMARK_MENU){ScUiNodeInfo node;memset(&node,0,sizeof(node));node.size=sizeof(node);node.version=1;
        ScUiNodeQuery(f2.ui,f2.toolbar,&node);return ScUiMenuOpen(f2.ui,f2.root,f2.places,8,node.rectangle.y+node.rectangle.height,0,&f2.popup);}
    if(command==F2_OPEN_WITH){f2_status("在共享菜单的 Open with 子菜单选择应用");return f2_menu_prepare(SC_SHELL_MENU_SELECTION,32,100,1);}
    if(command==F2_ABOUT){ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;desc.owner=f2.root;
        desc.title=f2_text("Files 2");desc.message=f2_text("SandCore 原生文件管理器\n分页目录 · 虚拟详情 · 系统剪贴板 · 受管文件事务\n当前 SandFS 不支持空格路径；未提供时间戳字段。");
        ScUiDialog *dialog=0;ScStatus s=ScUiMessageBox(f2.ui,&desc,&dialog);ScUiDialogRelease(dialog);return s;}
    if(command==SC_SHELL_COMMAND_OPEN || command==F2_EDIT || command==F2_DEBUG){
        ScShellFileItem *items=0;ScU32 count=0;ScStatus s=f2_selected(&items,&count);
        if(s==0)s=f2_open_items(items,count,command==F2_EDIT?1:command==F2_DEBUG?2:0);
        free(items);return s;}
    if(command>=SC_SHELL_COMMAND_DELETE && command<=SC_SHELL_COMMAND_NEW_DIRECTORY){
        ScStatus s=command==SC_SHELL_COMMAND_PROPERTIES?0:f2_recovery();
        if(s==0)s=f2_menu_prepare(command==SC_SHELL_COMMAND_NEW_DIRECTORY?SC_SHELL_MENU_BACKGROUND:SC_SHELL_MENU_SELECTION,0,0,0);
        if(s==0)s=ScShellMenuExecute(f2.shell_menu,command);
        return s;}
    /* Open-with的动态命令由本次共享菜单快照解析；不能用显示标签猜程序。 */
    if(f2.shell_menu)return ScShellMenuExecute(f2.shell_menu,command);
    return SC_UI_E_NOT_FOUND;
}
