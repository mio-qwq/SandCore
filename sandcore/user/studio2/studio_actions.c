static ScStatus notice_done(ScUiContext *ui,const ScUiEvent *e,void *opaque)
{
    (void)ui;
    (void)opaque;
    ScUiDialog *d=(ScUiDialog *)e->data;
    if(st.notice==d)st.notice=0;
    ScUiDialogRelease(d);
    return 0;
}
static void error(ScStatus code,const char *message)
{
    char body[1536];
    snprintf(body,sizeof(body),"%s\n状态：%d",message?message:"操作未完成",code);
    status(body);
    if(!st.ui||st.notice)return;
    ScUiMenuItem button;
    rec(&button,sizeof(button));
    button.command=SC_UI_DIALOG_OK;
    button.label=text("确定");
    ScUiMessageBoxDesc d;
    rec(&d,sizeof(d));
    d.owner=st.root;
    d.title=text("Studio 2");
    d.message=text(body);
    d.buttons=&button;
    d.button_count=1;
    d.default_command=d.cancel_command=SC_UI_DIALOG_OK;
    d.completion=notice_done;
    ScUiMessageBox(st.ui,&d,&st.notice);
}
static void doc_close(U id)
{
    StDoc *d=document(id);
    if(!d)return;
    ScStatus s=ScUiTabRemove(st.ui,st.tabs,d->page);
    if(s<0){
        error(s,"无法关闭文档页");
        return;
    }
    StDoc **slot=&st.documents;
    while(*slot&&*slot!=d)slot=&(*slot)->next;
    if(*slot)*slot=d->next;
    if(st.active==d)st.active=st.documents;
    syntax_free(d);
    ScUiDocumentRelease(d->text);
    free(d);
    if(st.active)ScUiTabSelect(st.ui,st.tabs,st.active->page);
    st.pending_doc=0;
    refresh();
}
static ScStatus confirm(U action,U owner)
{
    if(st.dialog)return SC32_E_BUSY;
    ScUiMenuItem buttons[3];
    const char *labels[]={
        "保存","不保存","取消"
    };
    memset(buttons,0,sizeof(buttons));
    for(U i=0;i<3;i++){
        rec(buttons+i,sizeof(buttons[i]));
        buttons[i].command=S_CONFIRM_SAVE+i;
        buttons[i].label=text(labels[i]);
    }
    ScUiMessageBoxDesc d;
    rec(&d,sizeof(d));
    d.owner=st.root;
    d.title=text(owner?"文档尚未保存":"工程配置尚未保存");
    d.message=text("继续之前如何处理当前修改？");
    d.buttons=buttons;
    d.button_count=3;
    d.default_command=S_CONFIRM_SAVE;
    d.cancel_command=S_CONFIRM_CANCEL;
    d.completion=dialog_done;
    ScStatus s=ScUiMessageBox(st.ui,&d,&st.dialog);
    if(s>=0){
        st.dialog_kind=S_DIALOG_CONFIRM;
        st.dialog_owner=owner;
        st.pending_doc=owner;
        st.pending_action=action;
    }
    return s;
}
static void perform(U action)
{
    if(action==S_ACTION_CLOSE){
        doc_close(st.pending_doc?st.pending_doc:st.active?st.active->id:0);
        return;
    }
    if(action==S_ACTION_PROJECT_OPEN){
        ScStatus s=file_dialog(S_DIALOG_PROJECT_OPEN);
        if(s<0)error(s,"无法打开工程对话框");
        return;
    }
    if(action==S_ACTION_PROJECT_NEW){
        ScStatus s=new_project_dialog();
        if(s<0)error(s,"无法创建工程");
        return;
    }
    if(action==S_ACTION_EXIT){
        if(st.io||st.build.phase||st.running.capture.job||st.running.artifact.live||st.build.filejob){
            st.quit_requested=1;
            if(st.io)st.io->cancel=1;
            if(st.build.phase)st.build.cancel=1;
            st.running.cancel=1;
            if(st.build.filejob)ScShellFileOperationCancel(st.build.filejob);
            return;
        }
        for(StDoc *d=st.documents;d;d=d->next){
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            if(ScUiDocumentQuery(d->text,&q)>=0&&(q.flags&SC_UI_DOCUMENT_DIRTY)){
                confirm(action,d->id);
                return;
            }
        }
        if(st.project_dirty&&(st.simple!=1u||st.project_path[0])){
            confirm(action,0);
            return;
        }
        st.stopping=1;
        ScUiContextStop(st.ui,0);
    }
}
static ScStatus request(U action)
{
    if(st.dialog)return SC32_E_BUSY;
    if(action==S_ACTION_EXIT){
        st.quit_requested=1;
        perform(action);
        return 0;
    }
    if(st.io||st.build.phase)return SC32_E_BUSY;
    if(action==S_ACTION_CLOSE&&st.active){
        ScUiDocumentInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScUiDocumentQuery(st.active->text,&q);
        if(s<0)return s;
        st.pending_doc=st.active->id;
        if(q.flags&SC_UI_DOCUMENT_DIRTY)return confirm(action,st.active->id);
    }
    if((action==S_ACTION_PROJECT_NEW||action==S_ACTION_PROJECT_OPEN)&&st.project_dirty&&(st.simple!=1u||st.project_path[0]))return confirm(action,0);
    perform(action);
    return 0;
}
static ScStatus dialog_done(ScUiContext *ui,const ScUiEvent *e,void *opaque)
{
    (void)ui;
    (void)opaque;
    ScUiDialog *dialog=(ScUiDialog *)e->data;
    if(dialog!=st.dialog)return 0;
    U kind=st.dialog_kind,owner=st.dialog_owner,action=st.pending_action;
    ScUiDialogInfo info;
    rec(&info,sizeof(info));
    ScStatus s=ScUiDialogQuery(dialog,&info);
    st.dialog=0;
    st.dialog_kind=0;
    st.dialog_owner=0;
    if(s>=0&&info.stage==SC_UI_DIALOG_DONE){
        if(kind==S_DIALOG_CONFIRM){
            if(e->command==S_CONFIRM_DISCARD){
                if(owner){
                    StDoc *d=document(owner);
                    if(d)ScUiDocumentMarkSaved(d->text);
                }
                else{
                    st.project_saved=st.epoch;
                    st.project_dirty=0;
                }
                st.pending_action=0;
                perform(action);
            }
            else if(e->command==S_CONFIRM_SAVE){
                if(owner){
                    StDoc *d=document(owner);
                    s=d?d->path[0]?io_save(d,d->path):file_dialog(S_DIALOG_SAVE):SC32_E_STALE;
                }
                else s=st.project_path[0]?io_project_save(st.project_path):file_dialog(S_DIALOG_PROJECT_SAVE);
            }
            else{
                st.pending_action=0;
                st.quit_requested=0;
            }
        }
        else if(kind==S_DIALOG_PROJECT_CREATE){
            if(e->command==SC_UI_DIALOG_OK){ScConfig *values=0;s=ScUiDialogCopyValues(dialog,&values);if(s>=0)s=new_project_create(values);ScConfigRelease(values);}
        }
        else if(kind==S_DIALOG_CONFIG){
            ScConfig *candidate=0;
            s=ScUiDialogCopyValues(dialog,&candidate);
            if(s>=0)s=config_read(candidate);
            if(s>=0){
                ScConfigRelease(st.config);
                st.config=candidate;
                candidate=0;
                st.project_dirty=1;
                st.project_revision++;
                changed();
                project_notify();
                status("项目设置已更新；下次运行前将重新构建");
            }
            ScConfigRelease(candidate);
        }
        else if(e->command==SC_UI_DIALOG_OK){
            ScString *selected=0;
            const char *p=0;
            s=ScUiDialogSelectedPath(dialog,0,&selected);
            if(s>=0)s=ScStringCStr(selected,&p);
            if(s>=0){
                char absolute[68];
                s=kind==S_DIALOG_NEW||kind==S_DIALOG_NEW_HEADER?source_save_name(p,absolute,kind==S_DIALOG_NEW_HEADER):path(p,absolute);
                if(s>=0&&kind==S_DIALOG_OPEN)s=io_open(absolute,S_IO_OPEN,0);
                else if(s>=0&&(kind==S_DIALOG_NEW||kind==S_DIALOG_NEW_HEADER)){
                    ScPathInfo q;
                    rec(&q,sizeof(q));
                    ScStatus exists=ScPathQuery(absolute,&q);
                    if(exists!=SC32_E_NOT_FOUND)s=exists<0?exists:SC32_E_EXISTS;
                    else{
                        const char *initial=kind==S_DIALOG_NEW_HEADER?"/* 在这里声明共享的函数和类型。 */\n":"#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n";
                        s=doc_new(absolute,initial,(U)strlen(initial),0,0);
                        if(s>=0)s=st.simple?project_single(absolute):project_add(absolute);
                    }
                }
                else if(s>=0&&kind==S_DIALOG_SAVE){
                    StDoc *d=document(owner?owner:st.pending_doc?st.pending_doc:st.active?st.active->id:0);
                    s=d?io_save(d,absolute):SC32_E_STALE;
                }
                else if(s>=0&&kind==S_DIALOG_PROJECT_LOCATION){strcpy(st.project_location,absolute);st.project_create_pending=1;}
                else if(s>=0&&kind==S_DIALOG_PROJECT_OPEN)s=io_open(absolute,S_IO_PROJECT_OPEN,0);
                else if(s>=0&&kind==S_DIALOG_PROJECT_SAVE)s=io_project_save(absolute);
                else if(s>=0&&kind==S_DIALOG_ADD){
                    if(st.simple==1u)st.simple=2u;
                    for(U i=0;i<info.selected_count&&s>=0;i++){
                        ScString *item=0;
                        const char *name=0;
                        s=ScUiDialogSelectedPath(dialog,i,&item);
                        if(s>=0)s=ScStringCStr(item,&name);
                        if(s>=0)s=project_add(name);
                        ScStringRelease(item);
                    }
                }
                else if(s>=0&&kind==S_DIALOG_PUBLISH)s=publish_begin(absolute,1);
            }
            ScStringRelease(selected);
        }
        else{
            st.pending_action=0;
            st.quit_requested=0;
            st.save_all=0;
        }
    }
    else{
        st.pending_action=0;
        st.quit_requested=0;
        st.save_all=0;
    }
    ScUiDialogRelease(dialog);
    if(s<0){
        st.pending_action=0;
        st.save_all=0;
        error(s,kind==S_DIALOG_PROJECT_CREATE?
            (s==SC32_E_EXISTS?"同名项目文件夹已存在；请选择其他项目名称。":s==SC32_E_RANGE?"项目名称或保存路径过长；名称最多 20 个字母、数字或下划线，请选择较短的保存位置。":s==SC32_E_INVALID?"项目名称请使用字母、数字或下划线。":"项目创建失败；请检查保存位置的写入权限。"):
            kind==S_DIALOG_NEW||kind==S_DIALOG_NEW_HEADER?
            (s==SC32_E_EXISTS?"同名文件已存在；请选择其他名称，或使用“打开文件”编辑已有文件。":"无法创建文件；请检查名称和保存位置的写入权限。"):
            kind==S_DIALOG_PROJECT_SAVE?"项目未保存；文件类型为 SCW（.SCW），无后缀时自动补全。":
            kind==S_DIALOG_CONFIG?"项目设置无效；检查工具、输出和文件夹路径。关闭自动链接时需指定链接清单；SCL 库目标需关闭自动链接。":"文件操作失败；请检查所选路径、文件类型和访问权限。");
    }
    refresh();
    return 0;
}
static ScStatus file_dialog(U kind)
{
    if(st.dialog||st.io)return SC32_E_BUSY;
    ScUiFileDialogDesc d;
    ScUiMenuItem filter;
    rec(&d,sizeof(d));
    d.owner=st.root;
    d.directory="/HOME";
    d.title=text(kind==S_DIALOG_PROJECT_LOCATION?"新建项目：选择保存文件夹":kind==S_DIALOG_SAVE?"另存文档":kind==S_DIALOG_NEW?"新建 C 源文件（自动补 .C）":kind==S_DIALOG_NEW_HEADER?"新建头文件（自动补 .H）":kind==S_DIALOG_PROJECT_OPEN?"打开项目（.SCW）":kind==S_DIALOG_PROJECT_SAVE?"保存项目（自动补 .SCW）":kind==S_DIALOG_PUBLISH?"选择发布文件夹":"打开文件");
    d.flags=kind==S_DIALOG_SAVE||kind==S_DIALOG_PROJECT_SAVE?SC_UI_FILE_SAVE|SC_UI_FILE_OVERWRITE:kind==S_DIALOG_NEW||kind==S_DIALOG_NEW_HEADER?SC_UI_FILE_SAVE:kind==S_DIALOG_PUBLISH||kind==S_DIALOG_PROJECT_LOCATION?SC_UI_FILE_DIRECTORY:SC_UI_FILE_OPEN;
    if(kind==S_DIALOG_ADD)d.flags|=SC_UI_FILE_MULTI;
    if(kind==S_DIALOG_NEW||kind==S_DIALOG_NEW_HEADER){
        rec(&filter,sizeof(filter));filter.item=1;filter.command=1;
        filter.label=text(kind==S_DIALOG_NEW_HEADER?"头文件（*.H）":"C 源文件（*.C）");
        filter.help=text(kind==S_DIALOG_NEW_HEADER?"*.H;*.h":"*.C;*.c");
        d.filters=&filter;d.filter_count=1;d.selected_filter=1;
    }
    if(kind==S_DIALOG_PROJECT_OPEN||kind==S_DIALOG_PROJECT_SAVE){
        rec(&filter,sizeof(filter));
        filter.item=1;filter.command=1;
        filter.label=text("SCW 工作区（*.SCW）");
        /* 公共glob按字节匹配；列齐3个ASCII字母的大小写，不假定FS返回大写。 */
        filter.help=text("*.SCW;*.scw;*.Scw;*.sCW;*.SCw;*.ScW;*.sCw;*.scW");
        d.filters=&filter;d.filter_count=1;d.selected_filter=1;
    }
    d.completion=dialog_done;
    ScStatus s=ScShellFileDialog(st.ui,&d,&st.dialog);
    if(s>=0){
        st.dialog_kind=kind;
        st.dialog_owner=kind==S_DIALOG_SAVE?(st.pending_doc?st.pending_doc:st.active?st.active->id:0):0;
    }
    return s;
}
static ScStatus config_dialog(void)
{
    if(st.dialog||st.io||st.build.phase)return SC32_E_BUSY;
    ScUiPropertySheetDesc d;
    rec(&d,sizeof(d));
    d.owner=st.root;
    d.title=text("项目设置（高级）");
    d.initial=st.config;
    d.completion=dialog_done;
    const char *keys[]={
        "kind","output","auto_link","manifest","arguments","compiler","linker","include_dirs","workspace","debug_host","publish_dir"
    };
    const char *labels[]={
        "程序类型（SCX / SCL）","输出文件","自动链接系统库","自定义链接清单（可选）","运行参数","C 编译器","链接器","头文件目录（分号分隔）","构建临时文件夹","库调试宿主（SCX）","发布文件夹"
    };
    ScUiFieldDesc fields[11];
    memset(fields,0,sizeof(fields));
    for(U i=0;i<11;i++){
        rec(fields+i,sizeof(fields[i]));
        fields[i].field=i+1;
        fields[i].kind=i==2?SC_UI_CHECKBOX:SC_UI_TEXTBOX;
        fields[i].page=i<5?0u:1u;
        fields[i].label=text(labels[i]);
        fields[i].section="project";
        fields[i].key=keys[i];
        if(i<2||i==5||i==6||i==8)fields[i].flags=SC_UI_FIELD_REQUIRED;
    }
    ScStatus s=ScUiPropertySheet(st.ui,&d,fields,11,&st.dialog);
    if(s>=0)st.dialog_kind=S_DIALOG_CONFIG;
    return s;
}
static ScStatus find(U cmd)
{
    if(!st.active)return SC32_E_INVALID;
    ScString *needle=0,*replacement=0;
    const char *query=0,*substitute=0;
    ScStatus s=ScUiNodeCopyText(st.ui,st.needle,&needle);
    if(s>=0)s=ScStringCStr(needle,&query);
    if(s>=0&&!*query)s=SC32_E_INVALID;
    ScUiDocumentInfo q;
    rec(&q,sizeof(q));
    if(s>=0)s=ScUiDocumentQuery(st.active->text,&q);
    ScUiTextSelection selected;
    rec(&selected,sizeof(selected));
    if(s>=0)s=ScUiEditGetSelection(st.ui,st.active->edit,&selected);
    U low=selected.anchor<selected.caret?selected.anchor:selected.caret,high=selected.anchor>selected.caret?selected.anchor:selected.caret;
    ScUiFindDesc d;
    rec(&d,sizeof(d));
    d.start=0;
    d.end=q.bytes;
    d.flags=SC_UI_FIND_WRAP;
    d.needle=text(query?query:"");
    ScUiNodeInfo state;
    rec(&state,sizeof(state));
    if(ScUiNodeQuery(st.ui,st.caseless,&state)>=0&&(state.state&SC_UI_CHECKED))d.flags|=SC_UI_FIND_ASCII_CASELESS;
    if(cmd==S_REPLACE||cmd==S_REPLACE_ALL){
        if(s>=0)s=ScUiNodeCopyText(st.ui,st.replacement,&replacement);
        if(s>=0)s=ScStringCStr(replacement,&substitute);
        ScUiText t=text(substitute?substitute:"");
        if(cmd==S_REPLACE_ALL){
            U count=0;
            if(s>=0)s=ScUiDocumentReplaceAll(st.active->text,&d,&t,&count);
            if(s>=0)status("批量替换已作为共享Undo操作完成");
        }
        else{
            d.start=low;
            d.end=high;
            d.flags&=~SC_UI_FIND_WRAP;
            ScUiTextSelection matched;
            rec(&matched,sizeof(matched));
            if(s>=0)s=ScUiDocumentFind(st.active->text,&d,&matched);
            if(s>=0&&(matched.anchor!=low||matched.caret!=high))s=SC_UI_E_NOT_FOUND;
            if(s>=0)s=ScUiDocumentReplace(st.active->text,low,high-low,&t,SC_UI_REPLACE_NEW_UNDO);
        }
    }
    else{
        if(cmd==S_PREVIOUS){
            d.flags|=SC_UI_FIND_BACKWARD;
            d.end=low;
        }
        else d.start=high;
        ScUiTextSelection found;
        rec(&found,sizeof(found));
        if(s>=0)s=ScUiDocumentFind(st.active->text,&d,&found);
        if(s>=0)s=ScUiEditSetSelection(st.ui,st.active->edit,&found);
        if(s>=0)ScUiFocus(st.ui,st.active->edit);
    }
    ScStringRelease(needle);
    ScStringRelease(replacement);
    if(s==SC_UI_E_NOT_FOUND){
        status("未找到匹配内容");
        return 0;
    }
    return s;
}
static void navigate(U id)
{
    if(id){
        StDiagnostic *r=0;
        for(U i=0;i<st.error_count;i++)if(st.errors[i].id==id)r=st.errors+i;
        if(!r)return;
        if(r->generation!=st.build.generation){
            status("该诊断已属于旧构建");
            return;
        }
        st.jump_line=r->line?r->line:1;
        StDoc *d=document_path(r->path);
        if(!d){
            ScStatus s=io_open(r->path,S_IO_OPEN,0);
            if(s<0)error(s,"无法打开诊断源文件");
            return;
        }
        st.jump_doc=d->id;
    }
    StDoc *d=document(st.jump_doc);
    if(!d||!st.jump_line)return;
    ScUiDocumentInfo q;
    rec(&q,sizeof(q));
    ScStatus s=ScUiDocumentQuery(d->text,&q);
    ScString *copy=0;
    if(s>=0)s=ScUiDocumentCopy(d->text,0,q.bytes,&copy);
    ScView v;
    rec(&v,sizeof(v));
    if(s>=0)s=ScStringView(copy,&v);
    U at=0,line=1;
    if(s>=0){
        while(at<v.bytes&&line<st.jump_line)if(((const char *)v.data)[at++]=='\n')line++;
        ScUiTextSelection cursor;
        rec(&cursor,sizeof(cursor));
        cursor.anchor=cursor.caret=at;
        cursor.revision=q.revision;
        st.active=d;
        ScUiTabSelect(st.ui,st.tabs,d->page);
        ScUiEditSetSelection(st.ui,d->edit,&cursor);
        ScUiFocus(st.ui,d->edit);
        status(st.epoch==st.build.epoch?"已定位本次诊断":"诊断来自旧源修订，已打开当前文件");
    }
    ScStringRelease(copy);
    st.jump_line=st.jump_doc=0;
    refresh();
}
static void dock_panel(U tree,U floating)
{
    if(!tree)st.output_forced=1;
    ScUiNode panel=tree?st.tree_panel:st.out_panel,*root=tree?&st.float_tree:&st.float_output;
    ScStatus s=0;
    if(floating){
        if(*root)return;
        ScUiWindowDesc w;
        rec(&w,sizeof(w));
        w.title=text(tree?"Studio 2 · 工程":"Studio 2 · 输出");
        w.x=150;
        w.y=140;
        w.client_width=tree?320:900;
        w.client_height=420;
        w.style=SC32_STYLE_STANDARD;
        s=ScUiDockFloat(st.ui,panel,&w,root);
        if(s>=0){
            ScUiNodeSetState(st.ui,tree?st.tree_split:st.lower_split,SC_UI_VISIBLE,0);
            ScUiNodeSetState(st.ui,panel,SC_UI_VISIBLE,SC_UI_VISIBLE);
            layout(*root,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,0,0);
            ScUiSubscription sub;
            rec(&sub,sizeof(sub));
            sub.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
            sub.procedure=event;
            U token=0;
            ScUiListen(st.ui,*root,&sub,&token);
            ScUiLayout(st.ui,*root);
        }
    }
    else{
        if(!*root)return;
        ScUiNode old=*root;
        s=ScUiNodeMove(st.ui,panel,tree?st.upper:st.body,tree?st.tree_split:0);
        if(s>=0){
            layout(panel,tree?SC_UI_VBOX:SC_UI_DOCK_LAYOUT,tree?230:SC_UI_AUTO,tree?SC_UI_AUTO:140,0,0);
            U visible=(st.compact&(tree?1u:2u))?0:SC_UI_VISIBLE;
            ScUiNodeSetState(st.ui,panel,SC_UI_VISIBLE,visible);
            ScUiNodeSetState(st.ui,tree?st.tree_split:st.lower_split,SC_UI_VISIBLE,visible);
            *root=0;
            ScUiNodeDestroy(st.ui,old);
        }
    }
    if(s<0)error(s,"无法移动面板");
    ScUiLayout(st.ui,st.root);
}
static void command(U cmd)
{
    if(cmd>=S_NEW&&cmd<S_COUNT&&st.command_flags[cmd-S_NEW]==SC_UI_MENU_DISABLED+1u)return;
    ScStatus s=0;
    StDoc *d=st.active;
    if(cmd==S_NEW)s=file_dialog(S_DIALOG_NEW);
    else if(cmd==S_OPEN)s=file_dialog(S_DIALOG_OPEN);
    else if(cmd==S_SAVE)s=d?io_save(d,d->path):SC32_E_INVALID;
    else if(cmd==S_SAVE_AS)s=file_dialog(S_DIALOG_SAVE);
    else if(cmd==S_SAVE_ALL)st.save_all=1;
    else if(cmd==S_CLOSE)s=request(S_ACTION_CLOSE);
    else if(cmd==S_PROJECT_NEW)s=request(S_ACTION_PROJECT_NEW);
    else if(cmd==S_PROJECT_OPEN)s=request(S_ACTION_PROJECT_OPEN);
    else if(cmd==S_PROJECT_SAVE)s=st.project_path[0]?io_project_save(st.project_path):file_dialog(S_DIALOG_PROJECT_SAVE);
    else if(cmd==S_ADD)s=file_dialog(S_DIALOG_ADD);
    else if(cmd==S_CONFIG)s=config_dialog();
    else if(cmd==S_REMOVE){
        ScUiSelectionInfo q;
        rec(&q,sizeof(q));
        s=ScUiSelectionQuery(st.tree_selection,&q);
        if(s>=0){
            for(U i=0;i<st.member_count;i++)if(st.members[i].id+2u==q.current){
                memmove(st.members+i,st.members+i+1,(st.member_count-i-1u)*sizeof(StMember));
                st.member_count--;
                st.project_dirty=1;
                st.project_revision++;
                changed();
                project_notify();
                break;
            }
        }
    }
    else if(cmd==S_UNDO)s=d?ScUiDocumentUndo(d->text):SC32_E_INVALID;
    else if(cmd==S_REDO)s=d?ScUiDocumentRedo(d->text):SC32_E_INVALID;
    else if(cmd==S_CUT)s=d?ScUiEditCut(st.ui,d->edit):SC32_E_INVALID;
    else if(cmd==S_COPY)s=d?ScUiEditCopy(st.ui,d->edit):SC32_E_INVALID;
    else if(cmd==S_PASTE)s=d?ScUiEditPaste(st.ui,d->edit):SC32_E_INVALID;
    else if(cmd==S_ALL)s=d?ScUiEditSelectAll(st.ui,d->edit):SC32_E_INVALID;
    else if(cmd==S_FIND){
        ScUiNodeSetState(st.ui,st.search,SC_UI_VISIBLE,SC_UI_VISIBLE);
        ScUiLayout(st.ui,st.root);
        ScUiFocus(st.ui,st.needle);
    }
    else if(cmd==S_HIDE_FIND){
        ScUiNodeSetState(st.ui,st.search,SC_UI_VISIBLE,0);
        ScUiLayout(st.ui,st.root);
        if(d)ScUiFocus(st.ui,d->edit);
    }
    else if(cmd>=S_NEXT&&cmd<=S_REPLACE_ALL)s=find(cmd);
    else if(cmd==S_BUILD){st.after_build=0;s=build_begin();}
    else if(cmd==S_CANCEL){
        if(st.io)st.io->cancel=1;
        if(st.build.phase)st.build.cancel=1;
        st.running.cancel=1;
        if(st.build.filejob)ScShellFileOperationCancel(st.build.filejob);
    }
    else if(cmd==S_RUN||cmd==S_DEBUG){
        if(!st.success||st.success_epoch!=st.epoch){s=build_begin();if(s>=0&&st.build.phase)st.after_build=cmd;}
        else s=run(cmd==S_DEBUG);
    }
    else if(cmd==S_NEW_HEADER)s=file_dialog(S_DIALOG_NEW_HEADER);
    else if(cmd==S_PUBLISH)s=file_dialog(S_DIALOG_PUBLISH);
    else if(cmd==S_FLOAT_TREE||cmd==S_DOCK_TREE)dock_panel(1,cmd==S_FLOAT_TREE);
    else if(cmd==S_FLOAT_OUTPUT||cmd==S_DOCK_OUTPUT)dock_panel(0,cmd==S_FLOAT_OUTPUT);
    else if(cmd==S_LAYOUT_SAVE){
        st.project_dirty=1;
        st.project_revision++;
        changed();
        s=st.project_path[0]?io_project_save(st.project_path):file_dialog(S_DIALOG_PROJECT_SAVE);
    }
    else if(cmd==S_LAYOUT_RESET){
        dock_panel(0,0);
        dock_panel(1,0);
        ScUiSplitterSet(st.ui,st.tree_split,230);
        ScUiNodeInfo area;
        rec(&area,sizeof(area));
        if(ScUiNodeQuery(st.ui,st.body,&area)>=0)ScUiSplitterSet(st.ui,st.lower_split,area.rectangle.height>180?area.rectangle.height-140:80);
        ScUiLayout(st.ui,st.root);
    }
    else if(cmd==S_SDK)s=io_open("/SYS/SDK/DOC/M10A2-SDK.md",S_IO_OPEN,0);
    else if(cmd==S_ABOUT)status("Studio 2 · 独立SCO / 原生SCLD / SCUI多文档 / Debug 2");
    else if(cmd==S_EXIT)s=request(S_ACTION_EXIT);
    if(s<0)error(s,cmd==S_BUILD?"无法开始构建，请查看下方具体原因":cmd==S_RUN||cmd==S_DEBUG?"无法启动程序，请查看构建输出":"操作未完成");
    refresh();
}
static void ui_tick(void)
{
    if(st.project_create_pending&&!st.dialog&&!st.io){st.project_create_pending=0;ScStatus s=new_project_details();if(s<0)error(s,"无法打开新建项目对话框");}
    if(st.after_build&&!st.build.phase&&!st.io&&!st.dialog){
        U action=st.after_build;st.after_build=0;
        if(st.success){ScStatus s=run(action==S_DEBUG);if(s<0)error(s,"程序未能启动，构建输出已保留");}
    }
    if(st.save_all&&!st.io&&!st.dialog&&!st.build.phase){
        for(StDoc *d=st.documents;d;d=d->next){
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            ScStatus s=ScUiDocumentQuery(d->text,&q);
            if(s>=0&&((q.flags&SC_UI_DOCUMENT_DIRTY)||!d->disk_revision)){
                s=io_save(d,d->path);
                if(s<0){
                    st.save_all=0;
                    error(s,"全部保存未完成");
                }
                return;
            }
        }
        st.save_all=0;
        status("所有文档已保存");
    }
    if(st.config_save_requested&&!st.save_all&&!st.io&&!st.dialog&&!st.build.phase){
        st.config_save_requested=0;
        ScStatus s=io_project_save(st.project_path);if(s<0)error(s,"项目未能保存，源码保留在编辑器中");
    }
}
static ScStatus event(ScUiContext *ui,const ScUiEvent *e,void *opaque)
{
    (void)ui;
    (void)opaque;
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST){
        if(e->root==st.float_output){
            dock_panel(0,0);
            return SC_UI_VETO;
        }
        if(e->root==st.float_tree){
            dock_panel(1,0);
            return SC_UI_VETO;
        }
        request(S_ACTION_EXIT);
        return SC_UI_VETO;
    }
    if(e->type==SC_UI_EVENT_LAYOUT&&e->node==st.root){
        ScWindowInfo window;
        ScUiMetrics metrics;
        rec(&window,sizeof(window));
        rec(&metrics,sizeof(metrics));
        if(ScUiWindowNative(st.ui,st.root,&window)>=0&&ScUiMetricsQuery(st.ui,st.root,&metrics)>=0){
            U scale=metrics.scale_percent?metrics.scale_percent:100u;
            U compact=(window.client_width*100u/scale<800u?1u:0u)|(window.client_height*100u/scale<550u?2u:0u);
            if(compact!=st.compact){
                st.compact=compact;
                if(!st.float_tree){
                    ScUiNodeSetState(st.ui,st.tree_panel,SC_UI_VISIBLE,compact&1u?0:SC_UI_VISIBLE);
                    ScUiNodeSetState(st.ui,st.tree_split,SC_UI_VISIBLE,compact&1u?0:SC_UI_VISIBLE);
                }
                if(!st.float_output){
                    ScUiNodeSetState(st.ui,st.out_panel,SC_UI_VISIBLE,st.output_forced?SC_UI_VISIBLE:0);
                    ScUiNodeSetState(st.ui,st.lower_split,SC_UI_VISIBLE,st.output_forced?SC_UI_VISIBLE:0);
                }
            }
        }
        if(!(st.compact&1u)&&!st.float_tree){
            ScUiNodeInfo area;
            ScUiLayoutDesc l;
            rec(&area,sizeof(area));
            rec(&l,sizeof(l));
            if(ScUiNodeQuery(st.ui,st.upper,&area)>=0&&ScUiNodeGetLayout(st.ui,st.tree_panel,&l)>=0&&l.width>=0){
                U scale=metrics.scale_percent?metrics.scale_percent:100u;
                ScI32 available=area.rectangle.width-(ScI32)(200u*scale/100u);
                if(available>0&&(U)l.width*scale/100u>(U)available)ScUiSplitterSet(st.ui,st.tree_split,available);
            }
        }
        if(!(st.compact&2u)&&!st.float_output){
            ScUiNodeInfo area;
            ScUiLayoutDesc l;
            rec(&area,sizeof(area));
            rec(&l,sizeof(l));
            if(ScUiNodeQuery(st.ui,st.body,&area)>=0&&ScUiNodeGetLayout(st.ui,st.upper,&l)>=0&&l.height>=0){
                U scale=metrics.scale_percent?metrics.scale_percent:100u;
                ScI32 available=area.rectangle.height-(ScI32)(80u*scale/100u);
                if(available>0&&(U)l.height*scale/100u>(U)available)ScUiSplitterSet(st.ui,st.lower_split,available);
            }
        }
        return 0;
    }
    if(e->type==SC_UI_EVENT_VALUE_CHANGED){
        for(StDoc *d=st.documents;d;d=d->next)if(e->node==d->edit){
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            if(ScUiDocumentQuery(d->text,&q)>=0&&d->revision!=q.revision){
                d->revision=q.revision;
                changed();
            }
            break;
        }
        return 0;
    }
    if(e->type==SC_UI_EVENT_SELECTION_CHANGED&&e->node==st.tabs&&e->bytes==sizeof(ScUiNode)){
        ScUiNode page=*(const ScUiNode *)e->data;
        if(page==st.welcome)st.active=0;
        for(StDoc *d=st.documents;d;d=d->next)if(d->page==page)st.active=d;
        refresh();
        return 0;
    }
    if(e->type==SC_UI_EVENT_POINTER_UP&&e->node==st.diagnostics){
        ScUiSelectionInfo q;
        rec(&q,sizeof(q));
        if(ScUiSelectionQuery(st.diag_selection,&q)>=0&&q.current){
            if(st.click_id==q.current&&e->tick-st.click_tick<40u){
                navigate(q.current);
                st.click_id=0;
            }
            else{
                st.click_id=q.current;
                st.click_tick=e->tick;
            }
        }
        return 0;
    }
    if(e->type==SC_UI_EVENT_COMMAND){
        if(e->command==S_OPEN&&(e->node==st.tree||e->node==st.diagnostics)){
            ScUiSelectionInfo q;
            rec(&q,sizeof(q));
            ScUiSelectionQuery(e->node==st.tree?st.tree_selection:st.diag_selection,&q);
            if(e->node==st.diagnostics)navigate(q.current);
            else if(q.current==2u){
                ScStatus s=io_open(st.manifest_path,S_IO_OPEN,0);
                if(s<0)error(s,"无法打开链接清单");
            }
            else for(U i=0;i<st.member_count;i++)if(st.members[i].id+2u==q.current){
                ScStatus s=io_open(st.members[i].path,S_IO_OPEN,0);
                if(s<0)error(s,"无法打开工程成员");
                break;
            }
            return SC_UI_HANDLED;
        }
        command(e->command);
        return SC_UI_HANDLED;
    }
    return 0;
}
