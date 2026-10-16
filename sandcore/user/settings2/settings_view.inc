static const ScU32 display_widths[]={320,640,800,1024,1280,1920};
static const ScU32 display_heights[]={200,480,600,768,720,1080};
static const ScU32 display_scales[]={100,150,200};
static ScStatus button(ScUiNode parent,const char *z,ScU32 command,ScUiNode *out)
{ScUiNode node;
    ScStatus s=widget(parent,SC_UI_BUTTON,z,command,112,28,0,&node);
    if(out)*out=node;
    return s;
}
static ScStatus label(ScUiNode parent,const char *z)
{ScUiNode node;
    return widget(parent,SC_UI_LABEL,z,0,SC_UI_AUTO,28,0,&node);
}
static ScStatus bar(ScUiNode parent,ScUiNode *out)
{return responsive_band(parent,118,out);
}
static ScStatus display_refresh(void)
{
    ScDisplayInfo d;
    INIT(d);
    ScStatus s=ScDisplayQuery(&d);
    if(s<0)return s;
    settings.preview=d.preview_seconds;
    if(!settings.preview)settings.owns_preview=0;
    char line[192];
    snprintf(line,sizeof(line),"当前 %u × %u · %u%% · %s%u 秒",d.width,d.height,d.scale_percent,d.preview_seconds?"预览剩余 ":"预览 ",d.preview_seconds);
    set_text(settings.display_note,line);
    return 0;
}
static ScStatus display_preview(void)
{
    ScStatus s=ScDisplayApply(settings.width,settings.height,settings.scale,SC32_DISPLAY_PREVIEW);
    if(s<0)return s;
    settings.owns_preview=1;
    if(settings.timer)ScUiTimerCancel(app.ui,settings.timer);
    ScMonotonicClock now;
    INIT(now);
    s=ScSystemMonotonic(&now);
    if(s<0)return s;
    ScUiTimerDesc timer;
    INIT(timer);
    timer.node=app.root;
    timer.flags=SC_UI_TIMER_BACKGROUND;
    timer.deadline=now.ticks;
    timer.period_ticks=now.hz;
    timer.deadline.lo+=now.hz;
    if(timer.deadline.lo<now.ticks.lo)timer.deadline.hi++;
    s=ScUiTimerSet(app.ui,&timer,&settings.timer);
    if(s<0){ScDisplayApply(0,0,0,SC32_DISPLAY_CANCEL);
        return s;
    }
    status("显示预览已开始；Keep才保存，倒计时由系统负责");
    return display_refresh();
}
static ScStatus settings_command(ScU32 command)
{
    if(command>=S2_RESOLUTION && command<S2_RESOLUTION+6){settings.width=display_widths[command-S2_RESOLUTION];
        settings.height=display_heights[command-S2_RESOLUTION];
        char note[96];
        snprintf(note,sizeof(note),"待预览 %u × %u · %u%%",settings.width,settings.height,settings.scale);
        status(note);
        return 0;
    }
    if(command>=S2_SCALE && command<S2_SCALE+3){settings.scale=display_scales[command-S2_SCALE];
        return 0;
    }
    if(command>=S2_PROGRAM_ADD && command<=S2_SHORTCUT)return program_command(command);
    if(command>=S2_OPEN_USER && command<=S2_OPEN_WALL)return settings_document_open(settings.paths[command-S2_OPEN_USER],0);
    if(command>=S2_WALL_DUNES && command<=S2_WALL_SAND){if(settings.save.phase || settings.image)return SC32_E_BUSY;
        return ScWallpaperSet(command-S2_WALL_DUNES);
    }
    switch(command){
        case S2_PREVIEW:return display_preview();
        case S2_KEEP:case S2_REVERT:{ScStatus s=ScDisplayApply(0,0,0,command==S2_KEEP?SC32_DISPLAY_CONFIRM:SC32_DISPLAY_CANCEL);
            if(settings.timer)ScUiTimerCancel(app.ui,settings.timer);
            settings.timer=0;
            display_refresh();
            return s;
        }
        case S2_AURORA:strcpy(settings.theme,"/SYS/THEMES/AURORA.CFG");
        set_text(settings.theme_path,settings.theme);
        return 0;
        case S2_CLASSIC:strcpy(settings.theme,"/SYS/THEMES/CLASSIC.CFG");
        set_text(settings.theme_path,settings.theme);
        return 0;
        case S2_THEME_BROWSE:return settings_browse(S2_BROWSE_THEME);
        case S2_THEME_APPLY:{if(settings.save.phase)return SC32_E_BUSY;
            ScStatus s=node_text(settings.theme_path,settings.theme,sizeof(settings.theme));
            if(s==0)s=ScThemeApply(settings.theme,SC32_THEME_SAVE);
            if(s==0)status("主题已由系统持久保存并应用");
            return s;
        }
        case S2_THEME_RELOAD:if(settings.save.phase)return SC32_E_BUSY;
        return ScThemeApply(0,SC32_THEME_RELOAD);
        case S2_WALL_IMAGE:return settings_browse(S2_BROWSE_IMAGE);
        case S2_WALL_CLEAR:if(settings.save.phase)return SC32_E_BUSY;
        settings_image_cancel();
        return settings_wallpaper("");
        case S2_USER_SAVE:return user_save();
        case S2_USER_RELOAD:return settings.save.phase?SC32_E_BUSY:settings_load_user();
        case S2_LOGIN:{ScIdentityInfo self;
            INIT(self);
            ScStatus s=ScIdentityQuery(&self);
            return s<0?s:ScSessionControl(self.session,SC32_SESSION_LOGIN_PANEL);
        }
        case S2_ENV_ADD:case S2_ENV_EDIT:return environment_edit(command==S2_ENV_ADD);
        case S2_ENV_REMOVE:{ScUiSelectionInfo info;
            INIT(info);
            ScStatus s=ScUiSelectionQuery(settings.env_selection,&info);
            if(s<0)return s;
            if(!info.current || info.current>settings.env_count)return SC32_E_NOT_FOUND;
            ScU32 index=info.current-1;
            memmove(settings.env+index,settings.env+index+1,(settings.env_count-index-1)*sizeof(EnvironmentRow));
            settings.env_count--;
            settings.env_dirty=1;
            settings_model_changed(settings.env_provider,&settings.env_model_revision);
            return 0;
        }
        case S2_ENV_SAVE:return environment_save();
        case S2_ENV_RELOAD:return settings.save.phase?SC32_E_BUSY:settings_load_env();
        case S2_OPEN:return settings_browse(S2_BROWSE_DOCUMENT);
        case S2_SAVE:return *settings.path?settings_document_save(settings.path):settings_browse(S2_BROWSE_SAVE);
        case S2_SAVE_AS:return settings_browse(S2_BROWSE_SAVE);
        case S2_RELOAD:return *settings.path?settings_document_open(settings.path,0):SC32_E_NOT_FOUND;
        case S2_NEW:if(settings.save.phase || settings.loading)return SC32_E_BUSY;
        if(document_dirty())return message("文档草稿保留","请先保存或另存当前文档，再创建新文档。");
        {ScUiDocumentInfo info;
            INIT(info);
            ScStatus s=ScUiDocumentQuery(settings.document,&info);
            ScUiText empty=text("");
            if(s==0)s=ScUiDocumentReplace(settings.document,0,info.bytes,&empty,0);
            if(s==0){settings.path[0]=0;
                settings.document_revision=0;
                set_text(settings.document_path,"未命名");
                ScUiTabSelect(app.ui,settings.tabs,settings.pages[5]);
            }return s;
        }
        case S2_UNDO:return ScUiDocumentUndo(settings.document);
        case S2_REDO:return ScUiDocumentRedo(settings.document);
        case S2_CUT:return ScUiEditCut(app.ui,settings.editor);
        case S2_COPY:return ScUiEditCopy(app.ui,settings.editor);
        case S2_PASTE:return ScUiEditPaste(app.ui,settings.editor);
        case S2_ALL:return ScUiEditSelectAll(app.ui,settings.editor);
        case S2_FIND:case S2_REPLACE:return find_dialog(command==S2_REPLACE);
        case S2_CANCEL:settings_image_cancel();
        if(settings.save.phase)settings.save.cancel=1;
        status("已请求取消；已持久内容不会被谎称撤销");
        return 0;
        case S2_CLOSE:{ScUiEvent event;
            INIT(event);
            event.type=SC_UI_EVENT_CLOSE_REQUEST;
            return settings_event(app.ui,&event,0);
        }
        case S2_ABOUT:return message("Settings 2","显示 · 桌面 · 程序 · 显示资料 · 环境 · 配置\n真实SCUI控件与系统公开动态库。USER.CFG只改显示资料，不修改登录认证主体。文件路径暂不支持空格。");
    }return SC32_E_NOT_SUPPORTED;
}
static ScStatus settings_event(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)ui;
    (void)unused;
    if(e->type==SC_UI_EVENT_TIMER && settings.timer){display_refresh();
        if(!settings.preview){ScUiTimerCancel(app.ui,settings.timer);
            settings.timer=0;
        }}
    if(e->type==SC_UI_EVENT_VALUE_CHANGED && (e->node==settings.user_name || e->node==settings.user_home))settings.user_dirty=1;
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST){
        if(settings.request && settings.request_kind==2){status("账户目录仍在提交；等待真实ACK或明确失败后再关闭");
            return SC_UI_VETO;
        }
        if(settings.save.phase){status("保存正在提交；可取消后等待真实终态再关闭");
            return SC_UI_VETO;
        }
        ScU32 dirty=settings.user_dirty||settings.env_dirty||document_dirty();
        for(ScU32 i=0;i<settings.row_count;i++)dirty|=settings.rows[i].dirty;
        if(dirty){discard_prompt(S2_DISCARD_EXIT);
            return SC_UI_VETO;
        }
        settings.closing=1;
        return SC_UI_VETO;
    }
    if(e->type==SC_UI_EVENT_POINTER_DOWN && e->node==settings.editor && (e->buttons&2)){ScU32 popup;
        return ScUiMenuOpen(app.ui,app.root,settings.context_menu,e->x,e->y,0,&popup);
    }
    return 0;
}
static ScStatus settings_table(ScUiNode page,ScUiNode *node,ScUiDataProc procedure,ScUiProvider **provider,ScUiSelection **selection,const char **titles,ScU32 count)
{
    ScStatus s=widget(page,SC_UI_TABLE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,node);
    ScUiProviderDesc d;
    INIT(d);
    d.procedure=procedure;
    if(s==0)s=ScUiProviderCreate(&d,provider);
    if(s==0)s=ScUiSelectionCreate(*provider,SC_UI_SELECTION_ALLOW_EMPTY,selection);
    if(s==0)s=ScUiViewBind(app.ui,*node,*provider,*selection);
    ScUiColumnDesc columns[4];
    for(ScU32 i=0;i<count;i++){INIT(columns[i]);
        columns[i].column=i;
        columns[i].field=i;
        columns[i].title=text(titles[i]);
        columns[i].width=i==1?280:150;
        columns[i].min_width=64;
        columns[i].max_width=1000;
    }
    if(s==0)s=ScUiTableSetColumns(app.ui,*node,columns,count);
    return s;
}
static ScStatus settings_page(ScU32 index,const char *title)
{
    ScStatus s=widget(settings.tabs,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,settings.pages+index);
    if(s==0)s=layout(settings.pages[index],SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1);
    ScUiText t=text(title);
    if(s==0)s=ScUiTabAdd(app.ui,settings.tabs,settings.pages[index],&t);
    return s;
}
static ScStatus settings_create(void)
{
    memset(&settings,0,sizeof(settings));
    settings.env_model_revision=settings.program_model_revision=1;
    ScStatus s=app_create("Settings 2",settings_command,settings_event);
    if(s<0)return s;
    const char *leaves[]={"USER.CFG","ENV.CFG","MENU.CFG","THEME.CFG","DISPLAY.CFG","WALL.CFG"};
    for(ScU32 i=0;s==0 && i<6;i++){ScU32 length;
        s=ScSessionPath(leaves[i],settings.paths[i],65,&length);
    }
    /* DISPLAY.CFG沿旧系统显示配置入口，不能误写账户不存在的同名文件。 */
    strcpy(settings.paths[4],"/SYS/DISPLAY.CFG");
    AppMenuEntry file[]={{"打开配置",S2_OPEN,'o',SC32_MOD_CONTROL},{"保存",S2_SAVE,'s',SC32_MOD_CONTROL},{"另存为",S2_SAVE_AS,'s',SC32_MOD_CONTROL|SC32_MOD_SHIFT},{"重读",S2_RELOAD,0,0},{"新建文档",S2_NEW,'n',SC32_MOD_CONTROL},{"关闭",S2_CLOSE,'w',SC32_MOD_CONTROL}};
    AppMenuEntry edit[]={{"撤销",S2_UNDO,'z',SC32_MOD_CONTROL},{"重做",S2_REDO,'y',SC32_MOD_CONTROL},{"剪切",S2_CUT,'x',SC32_MOD_CONTROL},{"复制",S2_COPY,'c',SC32_MOD_CONTROL},{"粘贴",S2_PASTE,'v',SC32_MOD_CONTROL},{"全选",S2_ALL,'a',SC32_MOD_CONTROL},{"查找",S2_FIND,'f',SC32_MOD_CONTROL},{"替换全部",S2_REPLACE,'h',SC32_MOD_CONTROL}};
    AppMenuEntry help[]={{"取消当前操作",S2_CANCEL,27,0},{"关于",S2_ABOUT,SC32_KEY_F1,0}};
    if(s==0)s=menu_part("文件",file,sizeof(file)/sizeof(*file));
    if(s==0)s=menu_part("编辑",edit,sizeof(edit)/sizeof(*edit));
    if(s==0)s=menu_part("帮助",help,2);
    if(s==0)s=widget(app.body,SC_UI_TAB,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&settings.tabs);
    if(s==0)s=layout(settings.tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1);
    const char *titles[]={"显示","桌面","程序","显示资料","环境","配置"};
    for(ScU32 i=0;s==0 && i<6;i++)s=settings_page(i,titles[i]);
    ScUiNode page=settings.pages[0],row,node;
    ScDisplayInfo display;
    INIT(display);
    if(s==0)s=ScDisplayQuery(&display);
    settings.width=display.width;
    settings.height=display.height;
    settings.scale=display.scale_percent;
    if(s==0)s=label(page,"先选择，再预览。Keep确认；超时或关闭自动回退。");
    if(s==0)s=widget(page,SC_UI_LABEL,"",0,SC_UI_AUTO,32,0,&settings.display_note);
    if(s==0)s=bar(page,&row);
    for(ScU32 i=0;s==0 && i<6;i++){char z[40];
        snprintf(z,sizeof(z),"%u × %u",display_widths[i],display_heights[i]);
        s=button(row,z,S2_RESOLUTION+i,&node);
        if(s==0 && !(display.supported_modes&(1u<<i)))ScUiNodeSetState(app.ui,node,SC_UI_ENABLED,0);
    }
    if(s==0)s=bar(page,&row);
    for(ScU32 i=0;s==0 && i<3;i++){char z[24];
        snprintf(z,sizeof(z),"缩放 %u%%",display_scales[i]);
        s=button(row,z,S2_SCALE+i,0);
    }
    if(s==0)s=bar(page,&row);
    if(s==0)s=button(row,"预览",S2_PREVIEW,0);
    if(s==0)s=button(row,"Keep",S2_KEEP,0);
    if(s==0)s=button(row,"Revert",S2_REVERT,0);
    display_refresh();
    page=settings.pages[1];
    if(s==0)s=label(page,"主题与背景使用实际账户配置；图片完整验证后才提交。");
    if(s==0)s=widget(page,SC_UI_TEXTBOX,"/SYS/THEMES/AURORA.CFG",0,SC_UI_AUTO,28,0,&settings.theme_path);
    if(s==0)s=bar(page,&row);
    if(s==0)s=button(row,"Aurora",S2_AURORA,0);
    if(s==0)s=button(row,"Classic",S2_CLASSIC,0);
    if(s==0)s=button(row,"选择主题",S2_THEME_BROWSE,0);
    if(s==0)s=button(row,"保存主题",S2_THEME_APPLY,0);
    if(s==0)s=button(row,"重载主题",S2_THEME_RELOAD,0);
    if(s==0)s=bar(page,&row);
    if(s==0)s=button(row,"沙丘",S2_WALL_DUNES,0);
    if(s==0)s=button(row,"夜色",S2_WALL_NIGHT,0);
    if(s==0)s=button(row,"沙纹",S2_WALL_SAND,0);
    if(s==0)s=button(row,"选择图片",S2_WALL_IMAGE,0);
    if(s==0)s=button(row,"清除图片",S2_WALL_CLEAR,0);
    if(s==0)s=widget(page,SC_UI_LABEL,"SCB / BMP / PNG / JPEG / WebP · 最多1920 × 1080",0,SC_UI_AUTO,36,0,&settings.wall_note);
    page=settings.pages[2];
    if(s==0)s=bar(page,&row);
    const char *program_actions[]={"添加","编辑","移除","保存草稿","刷新目录","快捷方式"};
    for(ScU32 i=0;s==0 && i<6;i++)s=button(row,program_actions[i],S2_PROGRAM_ADD+i,0);
    const char *program_titles[]={"名称","可执行文件","来源","状态"};
    if(s==0)s=settings_table(page,&settings.programs,program_provider,&settings.program_provider,&settings.program_selection,program_titles,4);
    page=settings.pages[3];
    if(s==0)s=label(page,"USER.CFG是会话显示资料；不会更改登录账户、UID或认证home。");
    if(s==0)s=label(page,"显示名称");
    if(s==0)s=widget(page,SC_UI_TEXTBOX,"",0,SC_UI_AUTO,28,0,&settings.user_name);
    if(s==0)s=label(page,"资料home（绝对路径）");
    if(s==0)s=widget(page,SC_UI_TEXTBOX,"",0,SC_UI_AUTO,28,0,&settings.user_home);
    if(s==0)s=bar(page,&row);
    if(s==0)s=button(row,"保存资料",S2_USER_SAVE,0);
    if(s==0)s=button(row,"重读资料",S2_USER_RELOAD,0);
    if(s==0)s=button(row,"登录/会话",S2_LOGIN,0);
    page=settings.pages[4];
    if(s==0)s=label(page,"保留全部16项及未知键。PATH最多4个绝对目录；保存由系统真实校验。");
    if(s==0)s=bar(page,&row);
    const char *env_actions[]={"添加","编辑","移除","保存环境","重读环境"};
    for(ScU32 i=0;s==0 && i<5;i++)s=button(row,env_actions[i],S2_ENV_ADD+i,0);
    const char *env_titles[]={"名称","值"};
    if(s==0)s=settings_table(page,&settings.environment,environment_provider,&settings.env_provider,&settings.env_selection,env_titles,2);
    page=settings.pages[5];
    if(s==0)s=bar(page,&row);
    const char *config_actions[]={"USER","ENV","MENU","THEME","DISPLAY","WALL"};
    for(ScU32 i=0;s==0 && i<6;i++)s=button(row,config_actions[i],S2_OPEN_USER+i,0);
    if(s==0)s=widget(page,SC_UI_LABEL,"未命名 · UTF-8 · 上限1MiB",0,SC_UI_AUTO,28,0,&settings.document_path);
    if(s==0)s=widget(page,SC_UI_EDIT,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&settings.editor);
    ScUiDocumentDesc document;
    INIT(document);
    document.text=text("");
    document.undo_budget_bytes=1048576;
    if(s==0)s=ScUiDocumentCreate(&document,&settings.document);
    if(s==0)s=ScUiEditBind(app.ui,settings.editor,settings.document);
    if(s==0)s=ScUiNodeSetFont(app.ui,settings.editor,SC_UI_FONT_CODE);
    if(s==0)s=ScUiMenuCreate(&settings.context_menu);
    for(ScU32 i=2;s==0 && i<8;i++){ScUiMenuItem m;
        INIT(m);
        m.label=text(edit[i].label);
        m.command=edit[i].command;
        ScU32 token;
        s=ScUiMenuInsert(settings.context_menu,0,&m,&token);
    }
    ScShellConnectDesc connect;
    INIT(connect);
    connect.budget_bytes=1048576;
    if(s==0)s=ScShellConnect(&connect,&settings.shell);
    if(s==0){ScStatus loaded=settings_load_user();
        if(loaded<0)fprintf(stderr,"USER.CFG unavailable %d; editor remains available\n",loaded);
    }
    if(s==0){ScStatus loaded=settings_load_env();
        if(loaded<0)fprintf(stderr,"ENV.CFG unavailable %d; editor remains available\n",loaded);
    }
    if(s==0){ScStatus loaded=program_request_refresh();
        if(loaded<0)fprintf(stderr,"Catalog unavailable %d; other pages remain available\n",loaded);
    }
    if(s==0)s=ScUiTabSelect(app.ui,settings.tabs,settings.pages[0]);
    if(s==0)s=ScUiLayout(app.ui,app.root);
    if(s==0){ScUiDocumentMarkSaved(settings.document);
        settings.user_dirty=0;
        status(settings.user_loaded && settings.env_loaded?"就绪 · 草稿跨页保留 · 普通操作沿原系统权限":"部分配置不可读取；表单保存禁用，可用配置编辑器修复");
    }return s;
}
