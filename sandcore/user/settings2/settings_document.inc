static ScU32 document_dirty(void)
{ScUiDocumentInfo info;
    INIT(info);
    return ScUiDocumentQuery(settings.document,&info)==0 && (info.flags&SC_UI_DOCUMENT_DIRTY);
}
static ScStatus discard_complete(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    ScU32 kind=settings.prompt_kind;
    settings.prompt_kind=0;
    if(e->command!=SC_UI_DIALOG_OK)return 0;
    if(kind==S2_DISCARD_EXIT){settings.closing=1;
        return 0;
    }
    return settings_document_open(settings.pending_path,1);
}
static ScStatus discard_prompt(ScU32 kind)
{
    if(settings.prompt_kind)return SC32_E_BUSY;
    settings.prompt_kind=kind;
    ScUiText title=text("尚有未保存草稿"),body=text(kind==S2_DISCARD_EXIT?"关闭会丢弃本窗口未保存草稿。继续关闭？":"打开文件会替换当前未保存文档。继续打开？");
    ScUiDialog *dialog=0;
    ScStatus s=ScShellConfirm(app.ui,app.root,&title,&body,discard_complete,0,&dialog);
    ScUiDialogRelease(dialog);
    if(s<0)settings.prompt_kind=0;
    return s;
}
static ScStatus settings_document_open(const char *path,ScU32 discard)
{
    if(settings.save.phase || settings.loading)return SC32_E_BUSY;
    char normal[65];
    ScStatus s=normalize(path,"/",normal);
    if(s<0)return s;
    if(!discard && document_dirty()){strcpy(settings.pending_path,normal);
        return discard_prompt(S2_DISCARD_OPEN);
    }
    s=read_start(&settings.reader,normal,1048576);
    if(s==0){settings.loading=1;
        status("正在完整读取配置文档；当前内容保留");
    }return s;
}
static ScStatus settings_document_step(void)
{
    if(!settings.loading)return 0;
    ScStatus s=read_step(&settings.reader);
    if(s==SC32_PENDING)return s;
    if(s==0){ScView view;
        INIT(view);
        s=ScStringView(settings.reader.text,&view);
        ScUiDocumentDesc desc;
        INIT(desc);
        desc.undo_budget_bytes=1048576;
        desc.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
        desc.text.size=sizeof(desc.text);
        desc.text.version=1;
        desc.text.data=view.data;
        desc.text.bytes=view.bytes;
        ScUiDocument *candidate=0;
        if(s==0)s=ScUiDocumentCreate(&desc,&candidate);
        if(s==0)s=ScUiEditBind(app.ui,settings.editor,candidate);
        if(s==0){ScUiDocumentRelease(settings.document);
            settings.document=candidate;
            candidate=0;
            strcpy(settings.path,settings.reader.path);
            settings.document_revision=settings.reader.revision;
            ScUiDocumentMarkSaved(settings.document);
            set_text(settings.document_path,settings.path);
            ScUiTabSelect(app.ui,settings.tabs,settings.pages[5]);
            status("文档已完整读入；保存使用原修订条件");
        }
        ScUiDocumentRelease(candidate);
    }
    settings.loading=0;
    read_dispose(&settings.reader);
    return s;
}
static ScStatus settings_document_save(const char *path)
{
    if(settings.loading || settings.save.phase)return SC32_E_BUSY;
    ScUiDocumentInfo info;
    INIT(info);
    ScStatus s=ScUiDocumentQuery(settings.document,&info);
    ScString *snapshot=0;
    if(s==0)s=ScUiDocumentCopy(settings.document,0,info.bytes,&snapshot);
    ScU32 expected=settings.document_revision;
    if(strcmp(path,settings.path)){ScPathInfo target;
        INIT(target);
        s=ScPathQuery(path,&target);
        if(s==SC32_E_NOT_FOUND){expected=0;
            s=0;
        }else if(s==0){if(target.kind!=SC32_PATH_FILE)s=SC32_E_INVALID;
            else expected=target.generation;
        }}
    if(s==0)s=settings_save_start(S2_SAVE_DOCUMENT,path,snapshot,expected,info.revision);
    ScStringRelease(snapshot);
    return s;
}
static ScStatus find_apply(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    char needle[1025],replacement[1025];
    ScConfig *cfg=(ScConfig *)e->data;
    ScStatus s=config_get(cfg,"needle",needle,sizeof(needle));
    if(s==0)s=config_get(cfg,"replacement",replacement,sizeof(replacement));
    if(s<0)return s;
    if(!*needle)return SC32_E_INVALID;
    ScUiFindDesc find;
    INIT(find);
    find.needle=text(needle);
    find.flags=SC_UI_FIND_WRAP;
    ScUiTextSelection current;
    INIT(current);
    ScUiEditGetSelection(app.ui,settings.editor,&current);
    find.start=current.caret;
    ScUiDocumentInfo document;
    INIT(document);
    s=ScUiDocumentQuery(settings.document,&document);
    if(s<0)return s;
    find.end=document.bytes;
    if((ScU32)context){ScU32 changed;
        find.start=0;
        ScUiText replacement_text;
        INIT(replacement_text);
        replacement_text.data=replacement;replacement_text.bytes=(ScU32)strlen(replacement);
        s=ScUiDocumentReplaceAll(settings.document,&find,&replacement_text,&changed);
        if(s==0){char note[80];
            snprintf(note,sizeof(note),"已替换 %u 处；尚未保存",changed);
            status(note);
        }}
    else {ScUiTextSelection result;
        INIT(result);
        s=ScUiDocumentFind(settings.document,&find,&result);
        if(s==0)s=ScUiEditSetSelection(app.ui,settings.editor,&result);
    }
    return s;
}
static ScStatus find_dialog(ScU32 replace)
{
    ScConfig *cfg=0;
    ScStatus s=ScConfigCreate(&cfg);
    if(s==0)s=config_string(cfg,"needle","");
    if(s==0)s=config_string(cfg,"replacement","");
    ScUiFieldDesc fields[]={field(1,"查找","needle",SC_UI_TEXTBOX,0,1024),field(2,"替换为","replacement",SC_UI_TEXTBOX,0,1024)};
    if(s==0)s=property(replace?"替换全部":"查找下一个",cfg,fields,replace?2:1,find_apply,(void *)replace);
    ScConfigRelease(cfg);
    return s;
}
static ScStatus settings_shortcut(const char *path)
{
    ProgramRow *row=program_current();
    if(!row)return SC32_E_NOT_FOUND;
    ScShellShortcutDesc d;
    INIT(d);
    d.kind=SC_SHELL_SHORTCUT_EXECUTABLE;
    d.label=program_string(row->label);
    d.target=program_string(row->executable);
    d.arguments=program_string(row->arguments);
    d.icon=program_string(row->icon);
    d.profile=row->profile;
    ScShellShortcut *shortcut=0;
    ScStatus s=ScShellShortcutCreate(&d,&shortcut);
    ScU32 revision;
    if(s==0)s=ScShellShortcutWrite(shortcut,path,0,&revision);
    ScShellShortcutRelease(shortcut);
    if(s==0){s=ScDesktopReload();
        status(s==0?"快捷方式已持久创建":"快捷方式已创建；桌面刷新失败");
    }return s;
}
static ScStatus browse_complete(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    ScU32 kind=settings.browse_kind;
    settings.browse_kind=0;
    if(e->command!=SC_UI_DIALOG_OK){ScUiDialogRelease(settings.dialog);
        settings.dialog=0;
        return 0;
    }
    ScString *selected=0;
    const char *z=0;
    ScStatus s=ScUiDialogSelectedPath((ScUiDialog *)e->data,0,&selected);
    if(s==0)s=ScStringCStr(selected,&z);
    char path[65];
    if(s==0)s=normalize(z,"/",path);
    if(s==0){if(kind==S2_BROWSE_DOCUMENT)s=settings_document_open(path,0);
        else if(kind==S2_BROWSE_SAVE)s=settings_document_save(path);
        else if(kind==S2_BROWSE_THEME){strcpy(settings.theme,path);
            set_text(settings.theme_path,path);
        }
        else if(kind==S2_BROWSE_IMAGE)s=settings_image_begin(path);
        else if(kind==S2_BROWSE_SHORTCUT)s=settings_shortcut(path);
    }
    ScStringRelease(selected);
    ScUiDialogRelease(settings.dialog);
    settings.dialog=0;
    return s;
}
static ScStatus settings_browse(ScU32 kind)
{
    if(settings.dialog)return SC32_E_BUSY;
    ScUiFileDialogDesc d;
    INIT(d);
    d.owner=app.root;
    d.directory="/";
    d.context=0;
    d.completion=browse_complete;
    d.flags=kind==S2_BROWSE_SAVE || kind==S2_BROWSE_SHORTCUT?SC_UI_FILE_SAVE:SC_UI_FILE_OPEN;
    if(kind==S2_BROWSE_SAVE)d.flags|=SC_UI_FILE_OVERWRITE;
    d.title=text(kind==S2_BROWSE_THEME?"选择主题配置":kind==S2_BROWSE_IMAGE?"选择背景图片":kind==S2_BROWSE_SAVE?"配置另存为":kind==S2_BROWSE_SHORTCUT?"创建快捷方式（不覆盖）":"打开配置文件");
    ScStatus s=ScShellFileDialog(app.ui,&d,&settings.dialog);
    if(s==0)settings.browse_kind=kind;
    return s;
}
