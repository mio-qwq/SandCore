static ScStatus n_notice_done(ScUiContext *ui,const ScUiEvent *event,void *opaque)
{
    (void)ui;(void)opaque;ScUiDialog *dialog=(ScUiDialog *)event->data;
    if(notes.notice==dialog)notes.notice=0;ScUiDialogRelease(dialog);return 0;
}
static void n_error(ScStatus status,const char *path)
{
    char message[320];snprintf(message,sizeof(message),"操作未完成（%d）。内存文档保持。\n%s",status,path?path:"");
    n_status(message);if(!notes.ui || notes.notice)return;
    ScUiMenuItem button;n_record(&button,sizeof(button));button.command=SC_UI_DIALOG_OK;button.label=n_text("确定");
    ScUiMessageBoxDesc desc;n_record(&desc,sizeof(desc));desc.owner=notes.root;desc.title=n_text("Notes 2");
    desc.message=n_text(message);desc.buttons=&button;desc.button_count=1;
    desc.default_command=desc.cancel_command=SC_UI_DIALOG_OK;desc.completion=n_notice_done;
    ScUiMessageBox(notes.ui,&desc,&notes.notice);
}
static ScStatus n_document_empty(void)
{
    ScUiDocumentDesc desc;n_record(&desc,sizeof(desc));desc.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
    desc.undo_budget_bytes=1048576;desc.text=n_text("");ScUiDocument *candidate=0;
    ScStatus status=ScUiDocumentCreate(&desc,&candidate);
    if(status>=0 && notes.edit)status=ScUiEditBind(notes.ui,notes.edit,candidate);
    if(status>=0){ScUiDocumentRelease(notes.document);notes.document=candidate;candidate=0;
        notes.path[0]=0;notes.loaded_revision=0;}
    ScUiDocumentRelease(candidate);return status;
}
static ScStatus n_dialog_done(ScUiContext *ui,const ScUiEvent *event,void *opaque)
{
    (void)ui;(void)opaque;ScUiDialog *dialog=(ScUiDialog *)event->data;
    ScUiDialogInfo info;n_record(&info,sizeof(info));ScStatus status=ScUiDialogQuery(dialog,&info);
    ScU32 kind=notes.dialog_kind;notes.dialog=0;notes.dialog_kind=0;
    if(status>=0 && info.stage==SC_UI_DIALOG_DONE){
        if(kind==N_DIALOG_CONFIRM){
            if(event->command==N_CONFIRM_DISCARD){ScU32 action=notes.pending_action;notes.pending_action=0;status=n_transition(action);}
            else if(event->command==N_CONFIRM_SAVE)status=notes.path[0]?n_save(notes.path):n_file_dialog(N_DIALOG_SAVE);
            else notes.pending_action=0;
        }else if(event->command==SC_UI_DIALOG_OK){
            ScString *selected=0;const char *path=0;status=ScUiDialogSelectedPath(dialog,0,&selected);
            if(status>=0)status=ScStringCStr(selected,&path);
            if(status>=0)status=kind==N_DIALOG_SAVE?n_save(path):n_load(path,0);
            ScStringRelease(selected);
        }else notes.pending_action=0;
    }else notes.pending_action=0;
    ScUiDialogRelease(dialog);
    if(status<0){notes.pending_action=0;n_error(status,notes.path);}n_status(0);return 0;
}
static ScStatus n_file_dialog(ScU32 kind)
{
    if(notes.dialog || notes.io)return SC32_E_BUSY;
    ScUiMenuItem filters[2];memset(filters,0,sizeof(filters));
    for(ScU32 i=0;i<2;i++){n_record(filters+i,sizeof(filters[i]));filters[i].command=i+1;}
    filters[0].label=n_text("文本文件");filters[0].help=n_text("*.TXT;*.txt;*.MD;*.md;*.C;*.H;*.c;*.h");
    filters[1].label=n_text("全部文件");filters[1].help=n_text("*");
    ScUiFileDialogDesc desc;n_record(&desc,sizeof(desc));desc.owner=notes.root;
    desc.flags=kind==N_DIALOG_SAVE?SC_UI_FILE_SAVE|SC_UI_FILE_OVERWRITE:SC_UI_FILE_OPEN;
    desc.title=n_text(kind==N_DIALOG_SAVE?"另存为":"打开文本");desc.directory="/HOME";
    desc.filters=filters;desc.filter_count=2;desc.completion=n_dialog_done;
    ScStatus status=ScUiFileDialog(notes.ui,&desc,&notes.dialog);
    if(status>=0)notes.dialog_kind=kind;return status;
}
static ScStatus n_transition(ScU32 action)
{
    if(action==N_ACTION_NEW){ScStatus status=n_document_empty();if(status>=0)n_status("新文档");return status;}
    if(action==N_ACTION_OPEN)return n_file_dialog(N_DIALOG_OPEN);
    if(action==N_ACTION_RELOAD)return notes.path[0]?n_load(notes.path,0):SC32_E_INVALID;
    if(action==N_ACTION_EXIT){notes.quit=1;return ScUiContextStop(notes.ui,0);}
    return 0;
}
static ScStatus n_request(ScU32 action)
{
    if(notes.io || notes.dialog)return SC32_E_BUSY;
    ScUiDocumentInfo document;n_record(&document,sizeof(document));
    ScStatus status=ScUiDocumentQuery(notes.document,&document);if(status<0)return status;
    if(!(document.flags&SC_UI_DOCUMENT_DIRTY))return n_transition(action);
    ScUiMenuItem buttons[3];memset(buttons,0,sizeof(buttons));
    const char *labels[]={"保存","不保存","取消"};
    for(ScU32 i=0;i<3;i++){n_record(buttons+i,sizeof(buttons[i]));buttons[i].command=N_CONFIRM_SAVE+i;buttons[i].label=n_text(labels[i]);}
    ScUiMessageBoxDesc desc;n_record(&desc,sizeof(desc));desc.owner=notes.root;desc.title=n_text("保留当前修改");
    desc.message=n_text("当前文档尚未保存。继续之前如何处理？");desc.buttons=buttons;desc.button_count=3;
    desc.default_command=N_CONFIRM_SAVE;desc.cancel_command=N_CONFIRM_CANCEL;desc.completion=n_dialog_done;
    status=ScUiMessageBox(notes.ui,&desc,&notes.dialog);
    if(status>=0){notes.dialog_kind=N_DIALOG_CONFIRM;notes.pending_action=action;}return status;
}
static ScStatus n_find(ScU32 command)
{
    ScString *needle=0,*replacement=0;const char *pattern=0,*substitute=0;
    ScStatus status=ScUiNodeCopyText(notes.ui,notes.needle,&needle);
    if(status>=0)status=ScStringCStr(needle,&pattern);
    if(status>=0 && !*pattern){ScUiNodeSetState(notes.ui,notes.search,SC_UI_VISIBLE,SC_UI_VISIBLE);
        ScUiFocus(notes.ui,notes.needle);ScStringRelease(needle);return 0;}
    ScUiDocumentInfo document;n_record(&document,sizeof(document));
    ScUiTextSelection selection;n_record(&selection,sizeof(selection));
    if(status>=0)status=ScUiDocumentQuery(notes.document,&document);
    if(status>=0)status=ScUiEditGetSelection(notes.ui,notes.edit,&selection);
    ScUiNodeInfo check;n_record(&check,sizeof(check));
    if(status>=0)status=ScUiNodeQuery(notes.ui,notes.caseless,&check);
    ScUiFindDesc find;n_record(&find,sizeof(find));find.flags=SC_UI_FIND_WRAP;
    if(check.state&SC_UI_CHECKED)find.flags|=SC_UI_FIND_ASCII_CASELESS;
    find.needle=n_text(pattern?pattern:"");find.end=document.bytes;
    ScU32 low=selection.anchor<selection.caret?selection.anchor:selection.caret;
    ScU32 high=selection.anchor>selection.caret?selection.anchor:selection.caret;
    if(command==N_REPLACE_ALL){
        if(status>=0)status=ScUiNodeCopyText(notes.ui,notes.replacement,&replacement);
        if(status>=0)status=ScStringCStr(replacement,&substitute);
        ScU32 changed=0;ScUiText text=n_text(substitute?substitute:"");
        if(status>=0)status=ScUiDocumentReplaceAll(notes.document,&find,&text,&changed);
        if(status>=0){char message[80];snprintf(message,sizeof(message),"已替换 %u 处，可整体撤销",changed);n_status(message);}
    }else if(command==N_REPLACE && high>low){
        /* 只替换与当前查找条件完整吻合的选区，避免选区是任意文本时
         * 直接破坏作品；不区分大小写判断也交同一Document Find语义。 */
        ScUiFindDesc exact=find;exact.flags&=~SC_UI_FIND_WRAP;exact.start=low;exact.end=high;
        ScUiTextSelection found;n_record(&found,sizeof(found));
        if(status>=0)status=ScUiDocumentFind(notes.document,&exact,&found);
        if(status>=0 && (found.anchor!=low || found.caret!=high))status=SC_UI_E_NOT_FOUND;
        if(status>=0)status=ScUiNodeCopyText(notes.ui,notes.replacement,&replacement);
        if(status>=0)status=ScStringCStr(replacement,&substitute);
        ScUiText text=n_text(substitute?substitute:"");
        if(status>=0)status=ScUiDocumentReplace(notes.document,low,high-low,&text,SC_UI_REPLACE_NEW_UNDO);
        if(status>=0){n_record(&selection,sizeof(selection));selection.anchor=low;selection.caret=low+text.bytes;
            ScUiDocumentInfo now;n_record(&now,sizeof(now));status=ScUiDocumentQuery(notes.document,&now);
            selection.revision=now.revision;if(status>=0)status=ScUiEditSetSelection(notes.ui,notes.edit,&selection);}
    }else{
        if(command==N_FIND_PREVIOUS){find.flags|=SC_UI_FIND_BACKWARD;find.end=low;}
        else find.start=high;
        ScUiTextSelection found;n_record(&found,sizeof(found));
        if(status>=0)status=ScUiDocumentFind(notes.document,&find,&found);
        if(status>=0)status=ScUiEditSetSelection(notes.ui,notes.edit,&found);
        if(status>=0){ScUiFocus(notes.ui,notes.edit);n_status("已定位匹配");}
    }
    ScStringRelease(needle);ScStringRelease(replacement);
    if(status==SC_UI_E_NOT_FOUND){n_status("没有找到匹配内容");return 0;}return status;
}
static ScStatus n_event(ScUiContext *ui,const ScUiEvent *event,void *opaque)
{
    (void)ui;(void)opaque;ScStatus status=0;ScU32 command=event->command;
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST){status=n_request(N_ACTION_EXIT);
        if(status<0)n_error(status,notes.path);return SC_UI_VETO;}
    if(event->type==SC_UI_EVENT_VALUE_CHANGED || event->type==SC_UI_EVENT_SELECTION_CHANGED){
        if(event->node==notes.edit)n_status(0);return 0;}
    if(event->type==SC_UI_EVENT_KEY_DOWN){
        if(event->key==SC32_KEY_F1+1)command=N_SAVE;
        else if(event->key==SC32_KEY_F1+2 && !(event->modifiers&SC32_MOD_CONTROL))command=event->modifiers&SC32_MOD_SHIFT?N_FIND_PREVIOUS:N_RELOAD;
        else if(event->key==27){ScUiNodeInfo search;n_record(&search,sizeof(search));ScUiNodeQuery(notes.ui,notes.search,&search);
            command=search.state&SC_UI_VISIBLE?N_FIND_CLOSE:N_EXIT;}
        else return 0;
    }else if(event->type!=SC_UI_EVENT_COMMAND)return 0;
    if(command==N_NEW)status=n_request(N_ACTION_NEW);
    else if(command==N_OPEN)status=n_request(N_ACTION_OPEN);
    else if(command==N_RELOAD)status=n_request(N_ACTION_RELOAD);
    else if(command==N_EXIT)status=n_request(N_ACTION_EXIT);
    else if(command==N_SAVE)status=notes.path[0]?n_save(notes.path):n_file_dialog(N_DIALOG_SAVE);
    else if(command==N_SAVE_AS)status=n_file_dialog(N_DIALOG_SAVE);
    else if(command==N_UNDO)status=ScUiDocumentUndo(notes.document);
    else if(command==N_REDO)status=ScUiDocumentRedo(notes.document);
    else if(command==N_CUT)status=ScUiEditCut(notes.ui,notes.edit);
    else if(command==N_COPY)status=ScUiEditCopy(notes.ui,notes.edit);
    else if(command==N_PASTE)status=ScUiEditPaste(notes.ui,notes.edit);
    else if(command==N_SELECT_ALL)status=ScUiEditSelectAll(notes.ui,notes.edit);
    else if(command==N_FIND){status=ScUiNodeSetState(notes.ui,notes.search,SC_UI_VISIBLE,SC_UI_VISIBLE);
        if(status>=0)status=ScUiFocus(notes.ui,notes.needle);}
    else if(command==N_FIND_CLOSE){status=ScUiNodeSetState(notes.ui,notes.search,SC_UI_VISIBLE,0);
        if(status>=0)status=ScUiFocus(notes.ui,notes.edit);}
    else if(command==N_FIND_NEXT || command==N_FIND_PREVIOUS || command==N_REPLACE || command==N_REPLACE_ALL)status=n_find(command);
    else if(command==N_CANCEL_IO){if(notes.io){notes.io->cancel=1;n_status("正在请求安全取消");}}
    else if(command==N_ABOUT){n_status("Notes 2 · SCUI共享文档 / UTF-8 / 原生动态系统库");}
    else return 0;
    if(status<0)n_error(status,notes.path);n_status(0);return SC_UI_HANDLED;
}
