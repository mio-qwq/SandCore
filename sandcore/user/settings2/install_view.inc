static ScStatus install_directory_apply(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    char path[65];
    ScStatus s=config_get((ScConfig *)e->data,"directory",path,sizeof(path));
    return s<0?s:install_add(path,SC32_PATH_DIRECTORY);
}
static ScStatus install_directory_dialog(void)
{
    ScConfig *cfg=0;
    ScStatus s=ScConfigCreate(&cfg);
    char path[65]="/HOME/NEW";
    node_text(installer.target,path,sizeof(path));
    if(s==0)s=config_string(cfg,"directory",path);
    ScUiFieldDesc f=field(1,"新目录绝对路径","directory",SC_UI_TEXTBOX,0,64);
    if(s==0)s=property("加入建目录计划",cfg,&f,1,install_directory_apply,0);
    ScConfigRelease(cfg);
    return s;
}
static ScStatus install_browse_complete(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    ScU32 kind=installer.browse;
    installer.browse=0;
    ScStatus s=0;
    if(e->command==SC_UI_DIALOG_OK){
        ScUiDialogInfo info;
        INIT(info);
        s=ScUiDialogQuery((ScUiDialog *)e->data,&info);
        for(ScU32 i=0;s==0 && i<info.selected_count;i++){
            ScString *selected=0;
            const char *z=0;
            s=ScUiDialogSelectedPath((ScUiDialog *)e->data,i,&selected);
            if(s==0)s=ScStringCStr(selected,&z);
            if(s==0 && kind==I2_BROWSE_SOURCE)s=install_add(z,SC32_PATH_FILE);
            else if(s==0 && kind==I2_BROWSE_TARGET)set_text(installer.target,z);
            else if(s==0 && kind==I2_BROWSE_REPORT){ScString *body=0;
                s=install_report(&body);
                if(s==0)s=io_start_memory(&installer.report,z,body,0,23,0);
                ScStringRelease(body);
                if(s==0)installer.reporting=1;
            }
            ScStringRelease(selected);
        }
    }
    ScUiDialogRelease(installer.dialog);
    installer.dialog=0;
    return s;
}
static ScStatus install_browse(ScU32 kind)
{
    if(installer.dialog || installer.phase || installer.reporting)return SC32_E_BUSY;
    ScUiFileDialogDesc d;
    INIT(d);
    d.owner=app.root;
    d.directory="/";
    d.completion=install_browse_complete;
    d.flags=kind==I2_BROWSE_SOURCE?SC_UI_FILE_OPEN|SC_UI_FILE_MULTI:kind==I2_BROWSE_TARGET?SC_UI_FILE_DIRECTORY:SC_UI_FILE_SAVE;
    d.title=text(kind==I2_BROWSE_SOURCE?"选择安装源文件":kind==I2_BROWSE_TARGET?"选择现有目标目录":"保存逐项结果（不覆盖）");
    ScStatus s=ScShellFileDialog(app.ui,&d,&installer.dialog);
    if(s==0)installer.browse=kind;
    return s;
}
static ScStatus installer_command(ScU32 command)
{
    switch(command){
        case I2_ADD:return install_browse(I2_BROWSE_SOURCE);
        case I2_DIRECTORY:return install_directory_dialog();
        case I2_TARGET:return install_browse(I2_BROWSE_TARGET);
        case I2_PREFLIGHT:return install_begin(0);
        case I2_START:return install_begin(1);
        case I2_CANCEL:installer.cancel=1;
        if(installer.writer.phase)installer.writer.cancel=1;
        status("正在取消未发布候选；已持久项与所有来源保留");
        return 0;
        case I2_RETRY:return install_retry();
        case I2_REMOVE:{if(installer.phase)return SC32_E_BUSY;
            InstallItem *item=install_current();
            if(!item)return SC32_E_NOT_FOUND;
            ScU32 index=(ScU32)(item-installer.items);
            char key[66];
            ScU32 key_bytes=install_key(item->kind==SC32_PATH_FILE?item->source:item->destination,item->kind,key);
            ScStatus removed=ScHashRemove(installer.identities,key,key_bytes);
            if(removed<0)return removed;
            memmove(installer.items+index,installer.items+index+1,(installer.count-index-1)*sizeof(InstallItem));
            installer.count--;
            install_changed();
            status("已从计划移除；没有删除任何磁盘对象");
            return 0;
        }
        case I2_DETAILS:{InstallItem *item=install_current();
            if(!item)return SC32_E_NOT_FOUND;
            char body[640];
            snprintf(body,sizeof(body),"源 %s\n目标 %s\n%s\n字节 %u · 错误 %d\n源revision %u · 目标revision %u · DURABLE %u\n%s",item->source,item->destination,install_state(item->state),item->length,item->error,item->source_revision,item->result_revision,item->durable,item->detail);
            return message("文件安装结果",body);
        }
        case I2_COPY:{ScString *body=0;
            const char *z=0;
            ScStatus s=install_report(&body);
            if(s==0)s=ScStringCStr(body,&z);
            ScClipboardInfo clip;
            INIT(clip);
            if(s==0)s=ScClipboardQuery(&clip);
            ScUiText t=text(z?z:"");
            if(s==0)s=ScShellClipboardText(&t,clip.epoch,&clip);
            ScStringRelease(body);
            return s;
        }
        case I2_REPORT:return install_browse(I2_BROWSE_REPORT);
        case I2_CLOSE:if(installer.phase || installer.reporting)return message("工作尚未结束","请取消并等待真实终态，再关闭。已持久项不会被误报为已撤销。");
        app.quit=1;
        return 0;
        case I2_ABOUT:return message("文件安装","旧INSTALL命令原样保留。此界面只安装文件和目录，沿同一UID/GID/rw权限。\n默认不覆盖既有文件；既有目录保留复用。-2表示所有者不改，-1表示SYSTEM（仍需原权限）。路径不支持空格。");
    }return SC32_E_NOT_SUPPORTED;
}
static ScStatus install_event(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST && (installer.phase || installer.reporting)){status("工作尚未结束；先取消并等待真实终态");
        return SC_UI_VETO;
    }return 0;
}
static ScStatus install_labeled(ScUiNode row,const char *label_text,const char *initial,ScI32 width,ScUiNode *out)
{
    ScUiNode label;
    ScStatus s=widget(row,SC_UI_LABEL,label_text,0,90,28,0,&label);
    if(s==0)s=widget(row,SC_UI_TEXTBOX,initial,0,width,28,width==SC_UI_AUTO?1:0,out);
    if(s==0)s=ScUiLabelAssociate(app.ui,label,*out);
    return s;
}
static ScStatus install_toolbar(ScUiNode parent,const char **labels,ScU32 first,ScU32 count)
{
    ScUiNode row;
    ScStatus s=responsive_band(parent,118,&row);
    for(ScU32 i=0;s==0 && i<count;i++){ScUiNode b;
        s=widget(row,SC_UI_BUTTON,labels[i],first+i,112,28,0,&b);
    }return s;
}
static ScStatus installer_create(void)
{
    memset(&installer,0,sizeof(installer));
    installer.revision=1;
    ScStatus s=app_create("文件安装",installer_command,install_event);
    ScHashDesc hash;
    INIT(hash);
    hash.value_bytes=4;
    if(s==0)s=ScHashCreate(&hash,&installer.identities);
    AppMenuEntry file[]={{"添加源文件",I2_ADD,'o',SC32_MOD_CONTROL},{"加入建目录计划",I2_DIRECTORY,0,0},{"选择目标目录",I2_TARGET,0,0},{"只读预检",I2_PREFLIGHT,0,0},{"开始安装",I2_START,0,0},{"关闭",I2_CLOSE,'w',SC32_MOD_CONTROL}};
    AppMenuEntry action[]={{"从计划移除",I2_REMOVE,SC32_KEY_DELETE,0},{"取消",I2_CANCEL,27,0},{"重试选中项",I2_RETRY,0,0},{"详情",I2_DETAILS,0,0},{"复制逐项结果",I2_COPY,'c',SC32_MOD_CONTROL},{"保存结果",I2_REPORT,'s',SC32_MOD_CONTROL},{"关于",I2_ABOUT,SC32_KEY_F1,0}};
    if(s==0)s=menu_part("文件",file,sizeof(file)/sizeof(*file));
    if(s==0)s=menu_part("操作",action,sizeof(action)/sizeof(*action));
    ScUiNode label,row;
    if(s==0)s=widget(app.body,SC_UI_LABEL,"文件与目录安装 · 先预检 · 不覆盖用户文件 · 来源始终保留",0,SC_UI_AUTO,28,0,&label);
    char home[65]="/HOME";
    ScU32 length;
    ScProcessEnvironment("HOME",home,sizeof(home),&length);
    if(s==0)s=widget(app.body,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,0,&row);
    if(s==0)s=install_labeled(row,"目标",home,SC_UI_AUTO,&installer.target);
    if(s==0)s=widget(app.body,SC_UI_PANEL,"",0,SC_UI_AUTO,28,0,&row);
    if(s==0)s=install_labeled(row,"rw(0..3)","311",SC_UI_AUTO,&installer.mode);
    if(s==0)s=widget(app.body,SC_UI_PANEL,"",0,SC_UI_AUTO,28,0,&row);
    if(s==0)s=install_labeled(row,"UID(-2不改)","-2",SC_UI_AUTO,&installer.uid);
    if(s==0)s=widget(app.body,SC_UI_PANEL,"",0,SC_UI_AUTO,28,0,&row);
    if(s==0)s=install_labeled(row,"GID(-2不改)","-2",SC_UI_AUTO,&installer.gid);
    if(s==0)s=widget(app.body,SC_UI_CHECKBOX,"创建缺失父目录",0,SC_UI_AUTO,28,0,&installer.parents);
    if(s==0)s=widget(app.body,SC_UI_CHECKBOX,"明确对已存在目录应用所填权限（默认保留）",0,SC_UI_AUTO,28,0,&installer.repermission);
    const char *plan_actions[]={"添加源文件","加入目录","选择目标","移除计划","只读预检","开始安装","取消","重试选中项","详情","复制结果","保存结果"};
    if(s==0)s=install_toolbar(app.body,plan_actions,I2_ADD,11);
    if(s==0)s=widget(app.body,SC_UI_TABLE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&installer.table);
    ScUiProviderDesc provider;
    INIT(provider);
    provider.procedure=install_provider;
    if(s==0)s=ScUiProviderCreate(&provider,&installer.provider);
    if(s==0)s=ScUiSelectionCreate(installer.provider,SC_UI_SELECTION_ALLOW_EMPTY,&installer.selection);
    if(s==0)s=ScUiViewBind(app.ui,installer.table,installer.provider,installer.selection);
    const char *titles[]={"来源","目标","字节","结果"};
    ScUiColumnDesc columns[4];
    for(ScU32 i=0;i<4;i++){INIT(columns[i]);
        columns[i].column=i;
        columns[i].field=i;
        columns[i].title=text(titles[i]);
        columns[i].width=i==2?90:i==3?230:230;
        columns[i].min_width=64;
        columns[i].max_width=1500;
        if(i==2){columns[i].format=SC_UI_FORMAT_BYTES;
            columns[i].flags=SC_UI_COLUMN_RIGHT;
        }}
    if(s==0)s=ScUiTableSetColumns(app.ui,installer.table,columns,4);
    if(s==0)s=widget(app.body,SC_UI_PROGRESS,"",0,SC_UI_AUTO,18,0,&installer.progress);
    if(s==0)s=ScUiLayout(app.ui,app.root);
    if(s==0)status("就绪 · 模式311=owner读写/组读/其它读 · CLI install 保留");
    return s;
}
static void installer_close(void)
{
    if(installer.dialog){ScUiDialogClose(installer.dialog,SC_UI_DIALOG_CANCEL);
        ScUiDialogRelease(installer.dialog);
        installer.dialog=0;
    }
    io_dispose(&installer.writer);
    io_dispose(&installer.report);
    app_close();
    ScUiSelectionRelease(installer.selection);
    ScUiProviderRelease(installer.provider);
    ScHashRelease(installer.identities);
    free(installer.items);
}
