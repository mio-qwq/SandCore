static void program_drop(ProgramRow *row)
{
    ScShellApplicationRelease(row->original);
    ScStringRelease(row->id);
    ScStringRelease(row->label);
    ScStringRelease(row->executable);
    ScStringRelease(row->arguments);
    ScStringRelease(row->icon);
    ScStringRelease(row->category);
    memset(row,0,sizeof(*row));
}
static const char *program_string(ScString *string)
{const char *z="";
    if(string)ScStringCStr(string,&z);
    return z;
}
static ScStatus program_clone(ProgramRow *row,const ScShellApplicationDesc *d)
{
    ScStatus s=ScStringCreate(d->id,(ScU32)strlen(d->id),&row->id);
    if(s==0)s=ScStringCreate(d->label,(ScU32)strlen(d->label),&row->label);
    if(s==0)s=ScStringCreate(d->executable,(ScU32)strlen(d->executable),&row->executable);
    if(s==0)s=ScStringCreate(d->arguments?d->arguments:"",d->arguments?(ScU32)strlen(d->arguments):0,&row->arguments);
    if(s==0)s=ScStringCreate(d->icon?d->icon:"",d->icon?(ScU32)strlen(d->icon):0,&row->icon);
    if(s==0)s=ScStringCreate(d->category?d->category:"",d->category?(ScU32)strlen(d->category):0,&row->category);
    row->layer=d->layer;
    row->flags=d->flags;
    row->profile=d->profile;
    return s;
}
static ScStatus program_reserve(ScU32 count)
{
    if(count<=settings.row_capacity)return 0;
    ScU32 n=settings.row_capacity?settings.row_capacity*2:32;
    if(n<count)n=count;
    if(n>UINT_MAX/sizeof(ProgramRow))return SC32_E_RANGE;
    void *p=realloc(settings.rows,n*sizeof(ProgramRow));
    if(!p)return SC32_E_MEMORY;
    settings.rows=p;
    settings.row_capacity=n;
    return 0;
}
static ScStatus program_provider(void *unused,const ScUiDataRequest *q,ScUiDataReply *r)
{
    (void)unused;
    ScU32 index=0;
    ScStatus parsed=flat_provider(q,r,settings.row_count,settings.program_model_revision,&index);
    if(parsed)return parsed==SC_UI_HANDLED?0:parsed;
    if(q->operation==SC_UI_DATA_CELL){ProgramRow *row=settings.rows+index;
        const char *z="";
        /* field保留模型字段编号；column是表头身份，不能借其数值解释行数据。 */
        if(q->field==0)z=program_string(row->label);
        else if(q->field==1)z=program_string(row->executable);
        else if(q->field==2)z=row->layer==SC_SHELL_LAYER_SYSTEM?"系统默认":row->layer==SC_SHELL_LAYER_ACCOUNT?"账户":"临时";
        else z=row->removed?"待移除":row->dirty?"草稿":row->flags&SC_SHELL_APP_DISABLED?"禁用":"已登记";
        r->value.type=SC_VALUE_UTF8;
        r->value.data=z;
        r->value.bytes=(ScU32)strlen(z);
    }return 0;
}
static ProgramRow *program_current(void)
{
    ScUiSelectionInfo info;
    INIT(info);
    if(ScUiSelectionQuery(settings.program_selection,&info)<0 || !info.current || info.current>settings.row_count)return 0;
    return settings.rows+info.current-1;
}
static ScStatus program_request_refresh(void)
{
    if(settings.request || settings.program_loading)return SC32_E_BUSY;
    ScStatus s=ScShellQueryCatalog(settings.shell,0,&settings.request);
    if(s>=0){settings.request_kind=1;
        status("正在读取桌面实际程序目录");
        return 0;
    }return s;
}
static ScStatus program_send_next(void)
{
    while(settings.save_cursor<settings.row_count && !settings.rows[settings.save_cursor].dirty)settings.save_cursor++;
    if(settings.save_cursor==settings.row_count){status("程序草稿均已收到持久提交ACK");
        settings.request_kind=0;
        return 0;
    }
    ProgramRow *row=settings.rows+settings.save_cursor;
    ScStatus s;
    if(row->removed && row->layer!=SC_SHELL_LAYER_SYSTEM){
        s=ScShellUnregisterApplication(settings.shell,program_string(row->id),SC_SHELL_LAYER_ACCOUNT,settings.catalog_epoch,&settings.request);
    }else {
        ScShellApplicationDesc d;
        INIT(d);
        if(row->original){ScStatus describe=ScShellApplicationDescribe(row->original,&d);
            if(describe<0)return describe;
        }
        d.layer=SC_SHELL_LAYER_ACCOUNT;
        d.id=program_string(row->id);
        d.label=program_string(row->label);
        d.executable=program_string(row->executable);
        d.arguments=program_string(row->arguments);
        d.icon=program_string(row->icon);
        d.category=program_string(row->category);
        d.flags=row->flags|(row->removed?SC_SHELL_APP_DISABLED:0);
        d.profile=row->profile;
        d.flags&=~SC_SHELL_APP_TRANSIENT;
        ScShellVerbDesc fallback;
        INIT(fallback);
        fallback.verb="open";
        fallback.label="打开";
        fallback.flags=SC_SHELL_VERB_DEFAULT|SC_SHELL_VERB_NO_DOCUMENT;
        fallback.executable=d.executable;
        fallback.arguments=d.arguments;
        fallback.profile=d.profile;
        fallback.types="";
        fallback.icon=d.icon;
        ScShellVerbDesc *verbs=0;
        if(!d.verb_count){d.verbs=&fallback;
            d.verb_count=1;
        }
        else {if(d.verb_count>UINT_MAX/sizeof(*verbs))return SC32_E_RANGE;
            verbs=malloc(d.verb_count*sizeof(*verbs));
            if(!verbs)return SC32_E_MEMORY;
            memcpy(verbs,d.verbs,d.verb_count*sizeof(*verbs));
            ScU32 selected=0;
            for(ScU32 i=0;i<d.verb_count;i++)if(verbs[i].flags&SC_SHELL_VERB_DEFAULT){selected=i;
                break;
            }
            verbs[selected].executable=d.executable;
            verbs[selected].arguments=d.arguments;
            verbs[selected].profile=d.profile;
            d.verbs=verbs;
        }
        /* 只更新默认启动verb，其余edit/debug/type路由保留原完整描述。 */
        s=ScShellRegisterApplication(settings.shell,&d,settings.catalog_epoch,&settings.request);
        free(verbs);
    }
    if(s>=0){settings.request_kind=2;
        status("正在提交账户程序草稿；等待服务持久ACK");
        return 0;
    }return s;
}
static ScStatus program_step(void)
{
    if(settings.shell)ScShellStep(settings.shell,0);
    if(settings.request){ScShellRequestInfo info;
        INIT(info);
        ScStatus s=ScShellRequestQuery(settings.request,&info);
        if(s<0){ScShellRequestClose(settings.request);
            settings.request=0;
            settings.request_kind=0;
            return s;
        }
        if(info.state==SC_SHELL_REQUEST_QUEUED || info.state==SC_SHELL_REQUEST_SENT)return SC32_PENDING;
        if(info.state!=SC_SHELL_REQUEST_COMPLETE || info.status<0){
            status(info.state==SC_SHELL_REQUEST_UNKNOWN?"提交结果不明；草稿保留，请刷新实际目录后决定":"程序提交失败；草稿保留");
            ScShellRequestClose(settings.request);
            settings.request=0;
            settings.request_kind=0;
            return info.status<0?info.status:SC_SHELL_E_UNKNOWN;
        }
        if(settings.request_kind==1){
            ScShellCatalog *catalog=0;
            s=ScShellRequestCatalog(settings.request,&catalog);
            if(s<0){ScShellRequestClose(settings.request);
                settings.request=0;
                return s;
            }
            ScShellCatalogInfo ci;
            INIT(ci);
            s=ScShellCatalogQuery(catalog,&ci);
            if(s<0){ScShellCatalogRelease(catalog);
                return s;
            }
            ScU32 kept=0;
            for(ScU32 i=0;i<settings.row_count;i++){if(settings.rows[i].dirty)settings.rows[kept++]=settings.rows[i];
                else program_drop(settings.rows+i);
            }
            settings.row_count=kept;
            ScShellCatalogRelease(settings.catalog);
            settings.catalog=catalog;
            settings.catalog_epoch=ci.epoch;
            settings.program_cursor=0;
            settings.program_loading=1;
        }else {
            ProgramRow *row=settings.rows+settings.save_cursor;
            row->dirty=0;
            row->layer=SC_SHELL_LAYER_ACCOUNT;
            if(row->removed){row->flags|=SC_SHELL_APP_DISABLED;
                row->removed=0;
            }
            settings.catalog_epoch=info.epoch;
            settings.save_cursor++;
        }
        ScU32 kind=settings.request_kind;
        ScShellRequestClose(settings.request);
        settings.request=0;
        settings_model_changed(settings.program_provider,&settings.program_model_revision);
        if(kind==2)return program_send_next();
    }
    if(settings.program_loading){
        ScShellApplicationInfo rows[24];
        ScShellPage page;
        INIT(page);
        ScStatus s=ScShellCatalogApplications(settings.catalog,settings.program_cursor,settings.catalog_epoch,rows,24,&page);
        if(s<0){settings.program_loading=0;
            return s;
        }
        for(ScU32 i=0;i<page.count;i++){
            ProgramRow row;
            memset(&row,0,sizeof(row));
            ScShellApplicationDesc d;
            INIT(d);
            s=ScShellCatalogApplication(settings.catalog,rows[i].token,&row.original);
            if(s==0)s=ScShellApplicationDescribe(row.original,&d);
            if(s==0)s=program_clone(&row,&d);
            ScU32 shadow=0;
            if(s==0)for(ScU32 j=0;j<settings.row_count;j++)if(settings.rows[j].dirty && !strcmp(program_string(settings.rows[j].id),d.id)){shadow=1;
                break;
            }
            if(shadow){program_drop(&row);
                continue;
            }
            if(s==0)s=program_reserve(settings.row_count+1);
            if(s<0){program_drop(&row);
                settings.program_loading=0;
                return s;
            }
            settings.rows[settings.row_count++]=row;
        }
        settings.program_cursor=page.cursor;
        settings_model_changed(settings.program_provider,&settings.program_model_revision);
        if(page.flags&SC_SHELL_PAGE_FINISHED){settings.program_loading=0;
            settings.request_kind=0;
            status("已读取真实程序目录；修改先进入账户草稿");
            return 0;
        }return SC32_PENDING;
    }
    return 0;
}
static ScStatus program_apply(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    ScU32 index=(ScU32)context;
    if(settings.request || settings.program_loading)return SC32_E_BUSY;
    char id[256],label[1024],executable[65],arguments[1024],icon[128],category[256],profile[16];
    ScConfig *cfg=(ScConfig *)e->data;
    ScStatus s=config_get(cfg,"id",id,sizeof(id));
    if(s==0)s=config_get(cfg,"label",label,sizeof(label));
    if(s==0)s=config_get(cfg,"executable",executable,sizeof(executable));
    if(s==0)s=config_get(cfg,"arguments",arguments,sizeof(arguments));
    if(s==0)s=config_get(cfg,"icon",icon,sizeof(icon));
    if(s==0)s=config_get(cfg,"category",category,sizeof(category));
    if(s==0)s=config_get(cfg,"profile",profile,sizeof(profile));
    if(s<0)return s;
    if(!*id || !*label || (strcmp(profile,"modern") && strcmp(profile,"legacy")))return SC32_E_INVALID;
    char normal[65];
    s=normalize(executable,"/",normal);
    if(s<0)return s;
    ProgramRow candidate;
    memset(&candidate,0,sizeof(candidate));
    ScShellApplicationDesc d;
    INIT(d);
    d.id=id;
    d.label=label;
    d.executable=normal;
    d.arguments=arguments;
    d.icon=icon;
    d.category=category;
    d.layer=SC_SHELL_LAYER_ACCOUNT;
    d.profile=!strcmp(profile,"legacy")?SC_SHELL_PROFILE_LEGACY:SC_SHELL_PROFILE_MODERN;
    if(index<settings.row_count){ProgramRow *old=settings.rows+index;
        d.flags=old->flags;
        if(strcmp(id,program_string(old->id)))return SC32_E_INVALID;
        candidate.original=old->original;
        if(candidate.original)ScShellApplicationRetain(candidate.original);
    }
    s=program_clone(&candidate,&d);
    if(s<0){program_drop(&candidate);
        return s;
    }
    if(index==settings.row_count){s=program_reserve(index+1);
        if(s<0){program_drop(&candidate);
            return s;
        }settings.row_count++;
    }
    else program_drop(settings.rows+index);
    candidate.dirty=1;
    settings.rows[index]=candidate;
    settings_model_changed(settings.program_provider,&settings.program_model_revision);
    status("程序草稿已修改；点击保存才提交账户目录");
    return 0;
}
static ScStatus program_edit(ScU32 add)
{
    if(settings.request || settings.program_loading)return SC32_E_BUSY;
    ProgramRow *row=add?0:program_current();
    if(!add && !row)return SC32_E_NOT_FOUND;
    ScU32 index=row?(ScU32)(row-settings.rows):settings.row_count;
    ScConfig *cfg=0;
    ScStatus s=ScConfigCreate(&cfg);
    const char *keys[]={"id","label","executable","arguments","icon","category","profile"};
    const char *values[]={row?program_string(row->id):"",row?program_string(row->label):"",row?program_string(row->executable):"/APPS/",
        row?program_string(row->arguments):"",row?program_string(row->icon):"",row?program_string(row->category):"Applications",row && row->profile==SC_SHELL_PROFILE_LEGACY?"legacy":"modern"};
    for(ScU32 i=0;s==0 && i<7;i++)s=config_string(cfg,keys[i],values[i]);
    ScUiFieldDesc fields[]={field(1,"稳定ID","id",SC_UI_TEXTBOX,0,255),field(2,"名称","label",SC_UI_TEXTBOX,0,1023),field(3,"可执行文件","executable",SC_UI_TEXTBOX,0,64),
        field(4,"参数（legacy最多127B）","arguments",SC_UI_TEXTBOX,0,1023),field(5,"图标","icon",SC_UI_TEXTBOX,0,127),field(6,"分类","category",SC_UI_TEXTBOX,0,255),field(7,"modern / legacy","profile",SC_UI_TEXTBOX,0,15)};
    if(row)fields[0].flags=SC_UI_FIELD_READONLY;
    if(s==0)s=property("程序账户草稿",cfg,fields,7,program_apply,(void *)index);
    ScConfigRelease(cfg);
    return s;
}
static ScStatus program_command(ScU32 command)
{
    if(command==S2_PROGRAM_RELOAD)return program_request_refresh();
    if(command==S2_PROGRAM_SAVE){if(settings.request || settings.program_loading)return SC32_E_BUSY;
        settings.save_cursor=0;
        return program_send_next();
    }
    if(command==S2_PROGRAM_ADD || command==S2_PROGRAM_EDIT)return program_edit(command==S2_PROGRAM_ADD);
    ProgramRow *row=program_current();
    if(!row)return SC32_E_NOT_FOUND;
    if(command==S2_PROGRAM_REMOVE){if(settings.request || settings.program_loading)return SC32_E_BUSY;
        row->removed=1;
        row->dirty=1;
        settings_model_changed(settings.program_provider,&settings.program_model_revision);
        return 0;
    }
    if(command==S2_SHORTCUT)return settings_browse(S2_BROWSE_SHORTCUT);
    return SC32_E_INVALID;
}
