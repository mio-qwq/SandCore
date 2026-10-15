static ScStatus desk_error(ScStatus status,const char *path)
{
    if(status>=0 || status==SC_UI_E_CANCELLED || status==SC_SHELL_E_CANCELLED)return status;
    ScShellErrorInfo error;memset(&error,0,sizeof(error));error.size=sizeof(error);error.version=1;
    if(ScShellErrorQuery(&error)<0 || error.code!=status){error.code=status;error.cause=status;}
    if(desk.ui && desk.desktop){ScUiDialog *dialog=0;
        if(ScShellErrorDialog(desk.ui,desk.desktop,&error,path?path:"",&dialog)==0)ScUiDialogRelease(dialog);}
    fprintf(stderr,"SCSHELL error=%d path=%s\n",status,path?path:"");return status;
}
static ScStatus desk_open(ScU32 index)
{
    if(index>=desk.count || desk.items[index].removed)return SC_UI_E_STALE;
    DeskItem *item=desk.items+index;ScLaunchInfo out;memset(&out,0,sizeof(out));out.size=sizeof(out);out.version=1;
    ScPathInfo current;memset(&current,0,sizeof(current));current.size=sizeof(current);current.version=1;
    ScStatus checked=ScPathQuery(item->path,&current);if(checked<0)return checked;
    if(current.generation!=item->observed_revision || current.kind!=item->kind){desk.refresh_pending=1;return SC_SHELL_E_STALE;}
    if(item->shortcut)return ScShellShortcutLaunch(item->shortcut,desk.catalog,&out);
    return ScShellOpen(desk.catalog,item->path,"open",&out);
}
static ScStatus desk_selected(ScShellFileItem **out,ScU32 *count)
{
    ScU32 n=0;for(ScU32 i=0;i<desk.count;i++)if(desk.items[i].selected && !desk.items[i].removed)n++;
    ScShellFileItem *items=n?calloc(n,sizeof(*items)):0;if(n && !items)return SC_UI_E_MEMORY;
    ScU32 at=0;for(ScU32 i=0;i<desk.count;i++)if(desk.items[i].selected && !desk.items[i].removed){
        items[at].size=sizeof(*items);items[at].version=1;items[at].item=i+1;
        items[at].source=desk.items[i].path;items[at].kind=desk.items[i].kind;at++;
        items[at-1].expected_source_revision=desk.items[i].observed_revision;
        items[at-1].flags=SC_SHELL_ITEM_EXPECT_SOURCE;
    }
    *out=items;*count=n;return 0;
}
static ScStatus desk_begin_transfer(void)
{
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=desk_selected(&items,&count);
    /* 保留本次真实选择，撤的只是尚未提交的布局预览。ScUiDragBegin会
     * 接管同一源capture；失败时显式释放，绝不在UP误提交图标坐标。 */
    desk.gesture=desk.moved=0;
    if(s==0)s=ScShellDragBegin(desk.ui,desk.canvas,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,&desk.external_drag);
    free(items);if(s<0){desk.external_drag=0;ScUiCapture(desk.ui,desk.canvas,0);}
    ScUiInvalidate(desk.ui,desk.canvas,0);return s;
}
static ScStatus desk_recovery(void)
{
    ScMonotonicClock now;memset(&now,0,sizeof(now));now.size=sizeof(now);now.version=1;
    ScStatus s=ScSystemMonotonic(&now);if(s<0)return s;
    if(desk.recovery_nonce==0xffffffffu)return SC_SHELL_E_RANGE;
    ScU32 serial=++desk.recovery_nonce;
    int length=snprintf(desk.recovery_path,sizeof(desk.recovery_path),"%s/%x-%x-%x.REC",
        desk.recovery_directory,desk.identity.task_generation,now.ticks.lo,serial);
    return length<0 || length>=65?SC_SHELL_E_RANGE:0;
}
static ScStatus desk_menu_extra(ScUiMenu *menu,ScU32 command,const char *label,ScU32 flags)
{
    ScUiMenuItem item;memset(&item,0,sizeof(item));item.size=sizeof(item);item.version=1;
    item.command=command;item.label=desk_text(label);item.flags=flags;ScU32 id;
    return ScUiMenuInsert(menu,0,&item,&id);
}
static ScStatus desk_menu_prepare(ScU32 kind,ScI32 x,ScI32 y,ScU32 show)
{
    if(desk.popup){ScUiMenuClose(desk.ui,desk.popup);desk.popup=0;}
    if(desk.shell_menu){
        ScStatus active=ScShellMenuStep(desk.shell_menu,0);
        if(active==SC_SHELL_END){ScShellMenuRelease(desk.shell_menu);desk.shell_menu=0;}
        else {DeskMenuWork *work=malloc(sizeof(*work));if(!work)return SC_UI_E_MEMORY;
            work->menu=desk.shell_menu;work->next=desk.menu_work;desk.menu_work=work;desk.shell_menu=0;}
    }
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=desk_selected(&items,&count);
    ScShellMenuDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.kind=kind;desc.items=items;desc.count=count;desc.directory=desk.desktop_path;
    desc.catalog=desk.catalog;desc.ui=desk.ui;desc.owner=kind==SC_SHELL_MENU_START?desk.bar:desk.desktop;
    desc.procedure=desk_job_event;
    desc.recovery_directory=desk.recovery_directory;
    if(s==0)s=ScShellMenuBuild(&desc,&desk.shell_menu);
    free(items);
    ScUiMenu *menu=0;if(s==0)s=ScShellMenuNative(desk.shell_menu,&menu);
    if(s==0 && kind!=SC_SHELL_MENU_SELECTION){
        s=desk_menu_extra(menu,0,"",SC_UI_MENU_SEPARATOR);
        if(s==0)s=desk_menu_extra(menu,DESK_REFRESH,"刷新",0);
        if(s==0)s=desk_menu_extra(menu,DESK_FREE,"自由放置",desk.layout_mode==SC_SHELL_LAYOUT_FREE?SC_UI_MENU_RADIO|SC_UI_MENU_CHECKED:SC_UI_MENU_RADIO);
        if(s==0)s=desk_menu_extra(menu,DESK_GRID,"对齐到网格",desk.layout_mode==SC_SHELL_LAYOUT_GRID?SC_UI_MENU_RADIO|SC_UI_MENU_CHECKED:SC_UI_MENU_RADIO);
        if(s==0)s=desk_menu_extra(menu,DESK_AUTO,"自动排列",desk.layout_mode==SC_SHELL_LAYOUT_AUTO?SC_UI_MENU_RADIO|SC_UI_MENU_CHECKED:SC_UI_MENU_RADIO);
        if(s==0)s=desk_menu_extra(menu,DESK_CASCADE,"层叠窗口",desk.task_count?0:SC_UI_MENU_DISABLED);
        if(s==0)s=desk_menu_extra(menu,DESK_TILE,"平铺窗口",desk.task_count?0:SC_UI_MENU_DISABLED);
        if(s==0)s=desk_menu_extra(menu,DESK_SETTINGS,"设置",0);
        if(s==0)s=desk_menu_extra(menu,DESK_NEW_SHORTCUT,"新建快捷方式",0);
        if(s==0 && kind==SC_SHELL_MENU_START)s=desk_menu_extra(menu,DESK_LOGOUT,"注销会话",0);
        if(s==0 && kind==SC_SHELL_MENU_START)s=desk_menu_extra(menu,DESK_EXIT,"退出桌面",0);
    }
    if(s==0 && show)s=ScUiMenuOpen(desk.ui,desc.owner,menu,x,y,0,&desk.popup);
    ScUiMenuRelease(menu);return s;
}
static ScStatus desk_menu(ScU32 kind,ScI32 x,ScI32 y)
{return desk_menu_prepare(kind,x,y,1);}
static ScStatus desk_job_event(const ScShellEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_SHELL_EVENT_ITEM && event->data && event->bytes==sizeof(ScShellFileResult)){
        const ScShellFileResult *result=(const ScShellFileResult *)event->data;
        if(result->state==SC_SHELL_RESULT_DONE && (result->flags&SC_SHELL_DURABLE)
            && !(result->flags&SC_SHELL_SOURCE_RETAINED) && (result->operation==SC_SHELL_RENAME
                || result->operation==SC_SHELL_MOVE || result->operation==SC_SHELL_DELETE) && desk.item_index){
            ScHashEntry found;memset(&found,0,sizeof(found));found.size=sizeof(found);found.version=1;
            char source_key[68],destination_key[68];ScU32 source_bytes=desk_path_key(result->source,source_key);
            ScU32 destination_bytes=desk_path_key(result->destination,destination_key);
            if(ScHashGet(desk.item_index,source_key,source_bytes,&found)==0){
                ScU32 i;memcpy(&i,found.value,sizeof(i));DeskItem *item=desk.items+i;
                char base_key[68];ScU32 base=desk_path_key(desk.desktop_path,base_key);
                int local=result->operation!=SC_SHELL_DELETE && !strncmp(destination_key,base_key,base)
                    && destination_key[base]=='/' && !strchr(destination_key+base+1,'/');
                desk_gesture_cancel();
                if(local){
                    char *identity=desk_copy(result->destination),*path=desk_copy(result->destination);
                    const char *leaf=result->destination+base+1;char *label=item->shortcut?0:desk_copy(leaf);
                    if(!identity || !path || (!item->shortcut && !label)){free(identity);free(path);free(label);return SC_UI_E_MEMORY;}
                    ScU32 replaced=DESK_NONE;
                    if(ScHashGet(desk.item_index,destination_key,destination_bytes,&found)==0)
                        memcpy(&replaced,found.value,sizeof(replaced));
                    ScStatus changed=ScHashSet(desk.item_index,destination_key,destination_bytes,&i,0);
                    if(changed<0){free(identity);free(path);free(label);return changed;}
                    if(strcmp(source_key,destination_key))ScHashRemove(desk.item_index,source_key,source_bytes);
                    if(replaced<desk.count && replaced!=i){desk_invalidate_item(replaced);desk.items[replaced].removed=1;desk.items[replaced].selected=0;
                        if(desk.focus==replaced)desk.focus=i;
                        if(desk.anchor==replaced)desk.anchor=i;}
                    free(item->identity);free(item->path);item->identity=identity;item->path=path;
                    if(label){free(item->label);item->label=label;}item->observed_revision=result->destination_revision;
                }else {desk_invalidate_item(i);ScHashRemove(desk.item_index,source_key,source_bytes);
                    item->removed=1;item->selected=0;desk_icon_unlink(i);ScUiIconRelease(item->icon);item->icon=0;
                    if(desk.focus==i){desk.focus=DESK_NONE;
                        for(ScU32 n=i+1;n<desk.count;n++)if(!desk.items[n].removed){desk.focus=n;break;}
                        if(desk.focus==DESK_NONE)for(ScU32 n=0;n<i;n++)if(!desk.items[n].removed){desk.focus=n;break;}
                        desk.anchor=desk.focus;}
                }
                /* 只迁移真实已提交结果；删掉的layout身份不留给同名重建
                 * 文件。数组压缩交给下一完整目录候选，避免每项搬动整表。 */
                desk.save_epoch=desk.layout_epoch;desk_layout_changed();
            }
        }
    }
    if(event->type==SC_SHELL_EVENT_ITEM || event->type==SC_SHELL_EVENT_JOB)desk.refresh_pending=1;
    return 0;
}
static ScStatus desk_job_display(ScShellFileJob *job)
{
    if(desk.job){ScShellFileOperationClose(job);return SC_SHELL_E_BUSY;}
    desk.job=job;desk.job_done=0;ScStatus s=ScShellProgress(desk.ui,desk.desktop,job,&desk.progress);
    if(s<0)ScShellFileOperationCancel(job);
    return s;
}
static ScStatus desk_clipboard(ScU32 command)
{
    if(command==SC_SHELL_COMMAND_PASTE){
        if(desk.job)return SC_SHELL_E_BUSY;
        ScShellFileJob *job=0;
        ScStatus s=desk_recovery();
        if(s==0)s=ScShellClipboardPaste(desk.desktop_path,desk.recovery_path,desk_job_event,0,&job);
        return s<0?s:desk_job_display(job);
    }
    ScShellFileItem *items=0;ScU32 count=0;ScStatus s=desk_selected(&items,&count);
    ScClipboardInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(s==0)s=ScClipboardQuery(&info);
    if(s==0)s=command==SC_SHELL_COMMAND_CUT?ScShellClipboardCutFiles(items,count,info.epoch,&info):
        ScShellClipboardCopyFiles(items,count,info.epoch,&info);
    free(items);return s;
}
static ScStatus desk_logout_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;
    memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.command==SC_UI_DIALOG_OK){
        if(context)desk.quit=1;else s=ScSessionControl(desk.identity.session,SC32_SESSION_LOGOUT);
    }
    if(desk.dialog==dialog){desk.dialog=0;ScUiDialogRelease(dialog);}return s;
}
static ScStatus desk_edit_done(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type==SC_UI_EVENT_DIALOG_COMPLETE && desk.dialog==(ScUiDialog *)event->data){
        ScUiDialog *dialog=desk.dialog;desk.dialog=0;ScUiDialogRelease(dialog);desk.edit_item=DESK_NONE;desk.edit_new=0;desk.edit_path[0]=0;
    }
    return 0;
}
static ScStatus desk_layout_conflict_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;
    memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.command==DESK_LAYOUT_RELOAD){
        ScShellCatalogInfo catalog;memset(&catalog,0,sizeof(catalog));catalog.size=sizeof(catalog);catalog.version=1;
        s=ScShellCatalogQuery(desk.catalog,&catalog);
        if(s==0)s=ScShellCatalogLoad(desk.catalog,desk.store_path,catalog.epoch);
        if(s==0){desk.refresh_pending=1;desk.save_pending=0;desk.layout_unsaved=0;desk.clock_dirty=1;}
    }
    if(desk.dialog==dialog){desk.dialog=0;ScUiDialogRelease(dialog);}if(s<0)desk_error(s,desk.store_path);return s;
}
static ScStatus desk_layout_conflict(void)
{
    if(desk.dialog)return SC_SHELL_E_BUSY;
    ScUiMenuItem buttons[2];memset(buttons,0,sizeof(buttons));
    const char *labels[2]={"保留画面，暂不保存","重新加载磁盘布局"};
    for(ScU32 i=0;i<2;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;
        buttons[i].command=i?DESK_LAYOUT_RELOAD:SC_UI_DIALOG_CANCEL;buttons[i].label=desk_text(labels[i]);}
    ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.owner=desk.desktop;desc.title=desk_text("布局发生冲突");
    desc.message=desk_text("另一个操作或登录已改变配置。当前图标位置尚未保存，原磁盘配置保持。");
    desc.buttons=buttons;desc.button_count=2;desc.default_command=SC_UI_DIALOG_CANCEL;
    desc.cancel_command=SC_UI_DIALOG_CANCEL;desc.completion=desk_layout_conflict_reply;
    return ScUiMessageBox(desk.ui,&desc,&desk.dialog);
}
static ScStatus desk_edit_apply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type!=SC_UI_EVENT_APPLY || (!desk.edit_new && desk.edit_item>=desk.count))return SC_UI_E_STALE;
    DeskItem *item=desk.edit_new?0:desk.items+desk.edit_item;ScShellShortcutDesc desc;
    memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    ScStatus s=0;
    if(item)s=ScShellShortcutDescribe(item->shortcut,&desc);
    else {desc.kind=SC_SHELL_SHORTCUT_PATH;desc.profile=0;}
    if(s<0)return s;
    static const char *keys[]={"label","target","arguments","icon","application"};char *strings[5]={0,0,0,0,0};
    ScConfig *config=(ScConfig *)event->data;
    for(ScU32 i=0;s==0 && i<5;i++){
        ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        s=ScConfigGet(config,"shortcut",keys[i],&value);
        if(s==0 && (value.type!=SC_VALUE_UTF8 || value.bytes>1023u || (value.bytes && memchr(value.data,0,value.bytes))))s=SC_UI_E_INVALID;
        if(s==0){strings[i]=malloc(value.bytes+1);if(!strings[i])s=SC_UI_E_MEMORY;
            else {memcpy(strings[i],value.data,value.bytes);strings[i][value.bytes]=0;}}
    }
    ScShellShortcut *candidate=0;ScUiIcon *icon=0;char *label=0;
    if(s==0 && desk.edit_new){ScValue kind,profile;
        memset(&kind,0,sizeof(kind));kind.size=sizeof(kind);kind.version=1;
        memset(&profile,0,sizeof(profile));profile.size=sizeof(profile);profile.version=1;
        s=ScConfigGet(config,"shortcut","kind",&kind);
        if(s==0)s=ScConfigGet(config,"shortcut","profile",&profile);
        if(s==0 && (kind.type!=SC_VALUE_UTF8 || profile.type!=SC_VALUE_UTF8))s=SC_UI_E_INVALID;
        if(s==0){
            if(kind.bytes==strlen("路径") && !memcmp(kind.data,"路径",kind.bytes)){desc.kind=SC_SHELL_SHORTCUT_PATH;desc.profile=0;}
            else if(kind.bytes==strlen("程序") && !memcmp(kind.data,"程序",kind.bytes))desc.kind=SC_SHELL_SHORTCUT_EXECUTABLE;
            else if(kind.bytes==strlen("应用") && !memcmp(kind.data,"应用",kind.bytes))desc.kind=SC_SHELL_SHORTCUT_APPLICATION;
            else s=SC_UI_E_INVALID;
            if(s==0 && desc.kind!=SC_SHELL_SHORTCUT_PATH){
                if(profile.bytes==strlen("新版") && !memcmp(profile.data,"新版",profile.bytes))desc.profile=SC_SHELL_PROFILE_MODERN;
                else if(profile.bytes==strlen("旧版") && !memcmp(profile.data,"旧版",profile.bytes))desc.profile=SC_SHELL_PROFILE_LEGACY;
                else s=SC_UI_E_INVALID;}
        }
    }
    if(s==0){desc.label=strings[0];desc.target=strings[1];desc.arguments=strings[2];desc.icon=strings[3];desc.application=strings[4];
        s=ScShellShortcutCreate(&desc,&candidate);}
    if(s==0){label=desk_copy(desc.label);if(!label)s=SC_UI_E_MEMORY;}
    if(s==0 && item)ScShellIconLoad(desk.ui,desc.icon,desc.target,&icon);
    if(s==0 && desk.edit_new==1){
        char leaf[68];ScU32 n=(ScU32)strlen(desc.label);
        if(!n || n>=sizeof(leaf))s=SC_SHELL_E_RANGE;
        else {for(ScU32 i=0;i<n;i++){unsigned char c=(unsigned char)desc.label[i];
                leaf[i]=(c<32 || c=='/' || c=='\\' || c==':' || c=='*' || c=='?')?'_':(char)c;}leaf[n]=0;
            int length=snprintf(desk.edit_path,sizeof(desk.edit_path),"%s/%s.LNK",desk.desktop_path,leaf);
            if(length<0 || length>=65)s=SC_SHELL_E_RANGE;}
    }
    ScU32 revision=0;if(s==0)s=ScShellShortcutWrite(candidate,item?item->path:desk.edit_path,desk.edit_revision,&revision);
    if(s==0 && item){ScShellShortcutRelease(item->shortcut);item->shortcut=candidate;candidate=0;
        free(item->label);item->label=label;label=0;desk_icon_unlink(desk.edit_item);
        ScUiIconRelease(item->icon);item->icon=icon;icon=0;item->icon_failed=0;
        if(item->icon){item->next_icon=desk.loaded_icons;desk.loaded_icons=desk.edit_item;}
        desk.edit_revision=revision;item->observed_revision=revision;desk_invalidate_item(desk.edit_item);}
    else if(s==0){desk.edit_revision=revision;desk.edit_new=2;desk.refresh_pending=1;}
    ScShellShortcutRelease(candidate);ScUiIconRelease(icon);free(label);
    for(ScU32 i=0;i<5;i++)free(strings[i]);
    return s;
}
static ScStatus desk_shortcut_properties(ScU32 index,ScU32 rename_only)
{
    if(desk.dialog)return SC_SHELL_E_BUSY;
    if(index>=desk.count || !desk.items[index].shortcut)return SC_SHELL_E_INVALID;
    ScShellShortcutDesc shortcut;memset(&shortcut,0,sizeof(shortcut));shortcut.size=sizeof(shortcut);shortcut.version=1;
    ScStatus s=ScShellShortcutDescribe(desk.items[index].shortcut,&shortcut);
    ScPathInfo metadata;memset(&metadata,0,sizeof(metadata));metadata.size=sizeof(metadata);metadata.version=1;
    if(s==0)s=ScPathQuery(desk.items[index].path,&metadata);
    if(s==0 && metadata.generation!=desk.items[index].observed_revision){desk.refresh_pending=1;s=SC_SHELL_E_STALE;}
    ScConfig *initial=0;if(s==0)s=ScConfigCreate(&initial);
    const char *values[5]={shortcut.label,shortcut.target,shortcut.arguments,shortcut.icon,shortcut.application};
    const char *keys[5]={"label","target","arguments","icon","application"};
    const char *labels[5]={"显示名称","目标路径","参数","图标","应用标识"};
    ScUiFieldDesc fields[5];memset(fields,0,sizeof(fields));
    for(ScU32 i=0;s==0 && i<5;i++){
        ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        value.type=SC_VALUE_UTF8;value.data=values[i]?values[i]:"";value.bytes=(ScU32)strlen((const char *)value.data);
        s=ScConfigSet(initial,"shortcut",keys[i],&value);
        fields[i].size=sizeof(fields[i]);fields[i].version=1;fields[i].field=i+1;fields[i].kind=SC_UI_TEXTBOX;
        fields[i].label=desk_text(labels[i]);fields[i].section="shortcut";fields[i].key=keys[i];
        if(i==0)fields[i].flags=SC_UI_FIELD_REQUIRED;
        if(shortcut.kind==SC_SHELL_SHORTCUT_PATH && (i==2 || i==4))fields[i].flags=SC_UI_FIELD_READONLY;
        if(shortcut.kind==SC_SHELL_SHORTCUT_APPLICATION && i==1)fields[i].flags=SC_UI_FIELD_READONLY;
    }
    ScUiPropertySheetDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.owner=desk.desktop;desc.title=desk_text(rename_only?"重命名快捷方式":"快捷方式属性");desc.initial=initial;
    desc.apply=desk_edit_apply;desc.completion=desk_edit_done;
    if(s==0){desk.edit_item=index;desk.edit_revision=metadata.generation;
        s=ScUiPropertySheet(desk.ui,&desc,fields,rename_only?1:5,&desk.dialog);}
    ScConfigRelease(initial);return s;
}
static ScStatus desk_shortcut_create(void)
{
    if(desk.dialog)return SC_SHELL_E_BUSY;
    ScConfig *initial=0;ScStatus s=ScConfigCreate(&initial);
    const char *keys[7]={"label","target","arguments","icon","application","kind","profile"};
    const char *values[7]={"新快捷方式","/","","","","路径","新版"};
    const char *labels[7]={"显示名称","目标路径","参数","图标","应用标识","目标类型","兼容模式"};
    ScUiFieldDesc fields[7];memset(fields,0,sizeof(fields));
    for(ScU32 i=0;s==0 && i<7;i++){
        ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        value.type=SC_VALUE_UTF8;value.data=values[i];value.bytes=(ScU32)strlen(values[i]);
        s=ScConfigSet(initial,"shortcut",keys[i],&value);
        fields[i].size=sizeof(fields[i]);fields[i].version=1;fields[i].field=i+1;fields[i].kind=i<5?SC_UI_TEXTBOX:SC_UI_COMBOBOX;
        fields[i].label=desk_text(labels[i]);fields[i].section="shortcut";fields[i].key=keys[i];
        if(i==0)fields[i].flags=SC_UI_FIELD_REQUIRED;
    }
    const char *choices[2]={"路径\n程序\n应用","新版\n旧版"};
    const char *choice_keys[2]={"kind.choices","profile.choices"};
    for(ScU32 i=0;s==0 && i<2;i++){ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        value.type=SC_VALUE_UTF8;value.data=choices[i];value.bytes=(ScU32)strlen(choices[i]);s=ScConfigSet(initial,"shortcut",choice_keys[i],&value);}
    ScUiPropertySheetDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.owner=desk.desktop;desc.title=desk_text("新建快捷方式");desc.initial=initial;
    desc.apply=desk_edit_apply;desc.completion=desk_edit_done;
    if(s==0){desk.edit_item=DESK_NONE;desk.edit_new=1;desk.edit_revision=0;desk.edit_path[0]=0;
        s=ScUiPropertySheet(desk.ui,&desc,fields,7,&desk.dialog);if(s<0)desk.edit_new=0;}
    ScConfigRelease(initial);return s;
}
static ScStatus desk_command(ScU32 command)
{
    ScStatus s=0;
    if(command==DESK_OPEN)s=desk_open(desk.focus);
    else if(command==DESK_NEW_SHORTCUT)s=desk_shortcut_create();
    else if(command==DESK_REFRESH)desk.refresh_pending=1;
    else if(command==SC_SHELL_COMMAND_PROPERTIES && desk.focus<desk.count && desk.items[desk.focus].shortcut)
        s=desk_shortcut_properties(desk.focus,0);
    else if(command==SC_SHELL_COMMAND_RENAME && desk.focus<desk.count && desk.items[desk.focus].shortcut)
        s=desk_shortcut_properties(desk.focus,1);
    else if(command==DESK_FREE || command==DESK_GRID || command==DESK_AUTO)
        s=desk_layout_apply(command==DESK_FREE?SC_SHELL_LAYOUT_FREE:command==DESK_GRID?SC_SHELL_LAYOUT_GRID:SC_SHELL_LAYOUT_AUTO);
    else if(command==DESK_SELECT_ALL){for(ScU32 i=0;i<desk.count;i++)desk_select(i,1);}
    else if(command==DESK_CASCADE || command==DESK_TILE)s=desk_tasks_arrange(command==DESK_TILE);
    else if(command>=DESK_TASK_FOCUS && command<=DESK_TASK_CLOSE){
        ScU32 action=command==DESK_TASK_FOCUS?SC32_SHELL_ACTION_FOCUS:command==DESK_TASK_MINIMIZE?SC32_SHELL_ACTION_MINIMIZE:
            command==DESK_TASK_MAXIMIZE?SC32_SHELL_ACTION_MAXIMIZE:command==DESK_TASK_RESTORE?SC32_SHELL_ACTION_RESTORE:SC32_SHELL_ACTION_CLOSE;
        s=desk_tasks_action(desk.pressed,action);
    }else if(command==DESK_SETTINGS){
        ScShellApplication *app=0;s=ScShellCatalogFind(desk.catalog,"sandcore.settings",&app);
        ScLaunchInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        if(s==0)s=ScShellLaunch(app,0,0,&info);
        ScShellApplicationRelease(app);
    }else if(command==DESK_LOGOUT || command==DESK_EXIT){
        ScUiText title=desk_text(command==DESK_EXIT?"退出桌面":"注销会话");
        ScUiText message=desk_text(command==DESK_EXIT?"桌面将关闭，已打开的程序继续运行。":"本会话中的程序将关闭。请先保存文档。");
        if(desk.dialog)s=SC_SHELL_E_BUSY;
        else s=ScShellConfirm(desk.ui,desk.desktop,&title,&message,desk_logout_reply,command==DESK_EXIT?(void *)1:0,&desk.dialog);
    }else if(command==SC_SHELL_COMMAND_COPY || command==SC_SHELL_COMMAND_CUT || command==SC_SHELL_COMMAND_PASTE)
        s=desk_clipboard(command);
    else if(command>=SC_SHELL_COMMAND_OPEN && command<=SC_SHELL_COMMAND_NEW_DIRECTORY){
        s=desk_menu_prepare(command==SC_SHELL_COMMAND_NEW_DIRECTORY?SC_SHELL_MENU_BACKGROUND:SC_SHELL_MENU_SELECTION,0,0,0);
        if(s==0)s=ScShellMenuExecute(desk.shell_menu,command);
    }else if(desk.shell_menu)s=ScShellMenuExecute(desk.shell_menu,command);
    else s=SC_SHELL_E_NOT_FOUND;
    desk_error(s,"");return s<0?s:SC_UI_HANDLED;
}
static ScStatus desk_drop_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;
    memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && (info.command==DESK_DROP_COPY || info.command==DESK_DROP_MOVE)){
        desk.pending_drop.chosen_action=info.command==DESK_DROP_COPY?SC_UI_DRAG_COPY:SC_UI_DRAG_MOVE;
        ScShellFileJob *job=0;s=desk_recovery();
        if(s==0)s=ScShellDropFiles(&desk.pending_drop,desk.drop_destination,desk.recovery_path,&job);
        if(s==0)s=desk_job_display(job);
    }else if(s==0 && info.command==DESK_DROP_LINK){
        /* 值记录来自实际已验Drop；这里仍独立核公开FILES布局及整数边界，
         * 快捷方式只记路径，不借其声称的object/revision授予任何权限。 */
        ScClipboardFiles header;
        if(desk.pending_drop.bytes<sizeof(header))s=SC_SHELL_E_PROTOCOL;
        else {memcpy(&header,desk.drop_bytes,sizeof(header));
            if(header.size!=sizeof(header) || header.version!=1)s=SC_SHELL_E_PROTOCOL;
            ScU32 offset=sizeof(header);
            for(ScU32 i=0;s==0 && i<header.count;i++){
                ScClipboardFileRow row;
                if(offset>desk.pending_drop.bytes || sizeof(row)>desk.pending_drop.bytes-offset){s=SC_SHELL_E_PROTOCOL;break;}
                memcpy(&row,(char *)desk.drop_bytes+offset,sizeof(row));
                if(row.record_bytes<sizeof(row) || row.record_bytes>desk.pending_drop.bytes-offset
                    || row.path_bytes<2 || row.path_bytes>65 || row.path_bytes>row.record_bytes-sizeof(row)){
                    s=SC_SHELL_E_PROTOCOL;break;}
                const char *source=(const char *)desk.drop_bytes+offset+sizeof(row);
                if(source[0]!='/' || source[row.path_bytes-1] || strlen(source)+1!=row.path_bytes){s=SC_SHELL_E_PROTOCOL;break;}
                const char *leaf=strrchr(source,'/');leaf=leaf?leaf+1:source;
                char path[68];int length=snprintf(path,sizeof(path),"%s/%s.LNK",desk.drop_destination,leaf);
                if(length<0 || (ScU32)length>=sizeof(path)){s=SC_SHELL_E_RANGE;break;}
                ScShellShortcutDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
                desc.kind=SC_SHELL_SHORTCUT_PATH;desc.label=leaf;desc.target=source;desc.arguments="";
                desc.icon="";desc.application="";desc.profile=0;
                ScShellShortcut *link=0;s=ScShellShortcutCreate(&desc,&link);
                ScU32 revision=0;if(s==0)s=ScShellShortcutWrite(link,path,0,&revision);
                ScShellShortcutRelease(link);if(s==0)desk.refresh_pending=1;offset+=row.record_bytes;
            }
            if(s==0 && offset!=desk.pending_drop.bytes)s=SC_SHELL_E_PROTOCOL;
        }
    }
    free(desk.drop_bytes);desk.drop_bytes=0;memset(&desk.pending_drop,0,sizeof(desk.pending_drop));
    if(desk.dialog==dialog){desk.dialog=0;ScUiDialogRelease(dialog);}
    desk_error(s,desk.drop_destination);return s;
}
static ScStatus desk_drop(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(!event->data || event->bytes!=sizeof(ScUiDragData))return SC_SHELL_E_PROTOCOL;
    ScUiDragData *data=(ScUiDragData *)event->data;
    if(data->format!=SC32_CLIPBOARD_FILES)return 0;
    if(event->type==SC_UI_EVENT_DRAG_LEAVE){ScU32 previous=desk.drop_target;desk.drop_target=DESK_NONE;desk_invalidate_item(previous);return 0;}
    if(event->type==SC_UI_EVENT_DRAG_ENTER || event->type==SC_UI_EVENT_DRAG_OVER){
        if(desk.job || desk.dialog){data->chosen_action=0;return SC_UI_HANDLED;}
        ScU32 hit=desk_hit(event->x,event->y);if(hit!=DESK_NONE && desk.items[hit].kind!=SC32_PATH_DIRECTORY)hit=DESK_NONE;
        if(hit!=desk.drop_target){ScU32 previous=desk.drop_target;desk.drop_target=hit;desk_invalidate_item(previous);desk_invalidate_item(hit);}
        data->chosen_action=(data->actions&SC_UI_DRAG_COPY)?SC_UI_DRAG_COPY:(data->actions&SC_UI_DRAG_MOVE)?SC_UI_DRAG_MOVE:0;return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_DROP){
        if(desk.job || desk.dialog)return SC_SHELL_E_BUSY;
        ScU32 hit=desk_hit(event->x,event->y);
        strcpy(desk.drop_destination,desk.desktop_path);
        if(hit!=DESK_NONE && desk.items[hit].kind==SC32_PATH_DIRECTORY){
            ScPathInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
            ScStatus checked=ScPathQuery(desk.items[hit].path,&info);if(checked<0)return checked;
            if(info.kind!=SC32_PATH_DIRECTORY || info.generation!=desk.items[hit].observed_revision)return SC_SHELL_E_STALE;
            strcpy(desk.drop_destination,desk.items[hit].path);
        }
        ScU32 previous=desk.drop_target;desk.drop_target=DESK_NONE;desk_invalidate_item(previous);
        void *owned=malloc(data->bytes);if(!owned)return SC_UI_E_MEMORY;
        memcpy(owned,data->data,data->bytes);desk.pending_drop=*data;desk.pending_drop.data=owned;desk.drop_bytes=owned;
        ScUiMenuItem buttons[4];memset(buttons,0,sizeof(buttons));
        const char *labels[4]={"取消","复制到目标","移动到目标","创建快捷方式"};
        const ScU32 commands[4]={SC_UI_DIALOG_CANCEL,DESK_DROP_COPY,DESK_DROP_MOVE,DESK_DROP_LINK};
        for(ScU32 i=0;i<4;i++){buttons[i].size=sizeof(buttons[i]);buttons[i].version=1;
            buttons[i].command=commands[i];buttons[i].label=desk_text(labels[i]);}
        if(!(data->actions&SC_UI_DRAG_COPY))buttons[1].flags=SC_UI_MENU_DISABLED;
        if(!(data->actions&SC_UI_DRAG_MOVE))buttons[2].flags=SC_UI_MENU_DISABLED;
        ScUiMessageBoxDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
        desc.owner=desk.desktop;desc.title=desk_text("放置文件");
        char message[200];snprintf(message,sizeof(message),"目标：%s\n请选择文件操作。快捷方式只保存目标路径。",desk.drop_destination);
        desc.message=desk_text(message);
        desc.buttons=buttons;desc.button_count=4;desc.default_command=SC_UI_DIALOG_CANCEL;
        desc.cancel_command=SC_UI_DIALOG_CANCEL;desc.completion=desk_drop_reply;
        ScStatus s=ScUiMessageBox(desk.ui,&desc,&desk.dialog);
        if(s<0){free(desk.drop_bytes);desk.drop_bytes=0;memset(&desk.pending_drop,0,sizeof(desk.pending_drop));}
        desk_error(s,desk.desktop_path);return s<0?s:SC_UI_HANDLED;
    }
    return 0;
}
