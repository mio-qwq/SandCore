static ScU32 settings_validator(const char *path)
{
    if(path_equal(path,settings.paths[0]))return S2_VALIDATION_USER;
    if(path_equal(path,settings.paths[1]))return S2_VALIDATION_ENV;
    if(path_equal(path,settings.paths[2]))return SC32_CHECK_MENU;
    char prefix[13];
    memset(prefix,0,sizeof(prefix));
    if(strlen(path)>=12)memcpy(prefix,path,12);
    if(path_equal(path,settings.paths[3]) || path_equal(prefix,"/SYS/THEMES/"))return SC32_CHECK_THEME;
    if(path_equal(path,settings.paths[4]))return SC32_CHECK_DISPLAY;
    if(path_equal(path,settings.paths[5]))return SC32_CHECK_WALLPAPER;
    ScU32 n=(ScU32)strlen(path);
    if(n>4 && path_equal(path+n-4,".LNK"))return SC32_CHECK_SHORTCUT;
    return S2_VALIDATION_NONE;
}
static ScStatus settings_save_start(ScU32 kind,const char *path,ScString *body,ScU32 expected,ScU32 document_revision)
{
    SettingsSave *save=&settings.save;
    if(save->phase)return SC32_E_BUSY;
    memset(save,0,sizeof(*save));
    ScStatus s=normalize(path,"/",save->target);
    if(s<0)return s;
    ScView body_view;
    INIT(body_view);
    s=ScStringView(body,&body_view);
    ScU32 bad;
    if(s==0)s=ScUtf8Validate(body_view.data,body_view.bytes,&bad);
    if(s==0){const unsigned char *bytes=body_view.data;
        for(ScU32 i=0;i<body_view.bytes;i++)if((bytes[i]<32 && bytes[i]!='\n' && bytes[i]!='\r' && bytes[i]!='\t') || bytes[i]==127){s=SC32_E_INVALID;
            break;
        }}
    if(s<0)return s;
    save->kind=kind;
    save->expected=expected;
    save->document_revision=document_revision;
    save->validator=settings_validator(save->target);
    s=ScStringRetain(body);
    if(s<0)return s;
    save->body=body;
    if(save->validator==S2_VALIDATION_NONE){s=io_start_memory(&save->write,save->target,body,expected,23,0);
        save->phase=3;
    }
    else {
        char home[65];
        ScU32 length;
        ScProcessRef self;
        INIT(self);
        s=ScProcessSelf(&self);
        if(s==0)s=ScProcessEnvironment("HOME",home,sizeof(home),&length);
        if(s==0){int n=snprintf(save->temporary_path,sizeof(save->temporary_path),"%s/.C%X%X",home,self.pid,++temporary_serial);
            if(n<0 || (ScU32)n>=sizeof(save->temporary_path))s=SC32_E_RANGE;
        }
        if(s==0)s=io_start_memory(&save->write,save->temporary_path,body,0,3,1);
        save->phase=1;
    }
    if(s<0){io_dispose(&save->write);
        ScStringRelease(save->body);
        memset(save,0,sizeof(*save));
        return s;
    }
    status("保存准备中；当前草稿保留直到持久确认");
    if(kind==S2_SAVE_USER){ScUiNodeSetState(app.ui,settings.user_name,SC_UI_ENABLED,0);
        ScUiNodeSetState(app.ui,settings.user_home,SC_UI_ENABLED,0);
    }
    return 0;
}
static ScStatus settings_temp_remove(SettingsSave *save)
{
    if(!save->temporary.lease)return 0;
    ScLeaseInfo info;
    INIT(info);
    ScStatus s;
    if(!save->cleanup){s=pin_query(&save->temporary,&info);
        if(s<0)return s;
        ScLeaseRemoveDesc q;
        INIT(q);
        q.lease=info.lease;
        q.generation=info.generation;
        q.expected_object=info.object_id;
        q.expected_revision=info.revision;
        /* 此处只清理本轮独占的临时文件。文件条件由object/revision
         * 精确绑定；发布后Info可携当前目录epoch，但Remove的文件
         * 合同要求该字段为0，不能把目录快照条件混入文件操作。 */
        q.expected_snapshot_epoch=0;
        INIT(info);
        s=ScFileLeaseRemove(&q,&info);
        if(s==SC32_E_BUSY){ScLeaseControl c=pin_control(&save->temporary);
            INIT(info);
            ScFileLeaseAdvance(&c,&info);
            return SC32_PENDING;
        }
        if(s<0)return s;
        pin_store(&save->temporary,&info);
        save->cleanup=1;
        return SC32_PENDING;
    }
    ScLeaseControl q=pin_control(&save->temporary);
    s=ScFileLeaseAdvance(&q,&info);
    if(info.lease)pin_store(&save->temporary,&info);
    if(s==SC32_PENDING)return s;
    if(s<0)return s;
    if(!(info.flags&SC32_LEASE_FLAG_DURABLE))return SC32_E_IO;
    return pin_release(&save->temporary,0);
}
static ScStatus settings_save_step(void)
{
    SettingsSave *save=&settings.save;
    if(!save->phase)return 0;
    ScStatus s=0;
    if(save->cancel){
        if(save->phase==1 || save->phase==3)save->write.cancel=1;
        else if(save->phase==2 || save->phase==4){save->outcome=SC32_E_CANCELLED;
            save->phase=5;
        }
    }
    if(save->phase==1 || save->phase==3){
        s=io_step(&save->write);
        if(s==SC32_PENDING)return s;
        if(s<0){save->outcome=s;
            save->uncertain=save->write.uncertain;
            save->phase=5;
        }
        else if(save->phase==1){save->temporary=save->write.candidate;
            memset(&save->write.candidate,0,sizeof(IoPin));
            io_dispose(&save->write);
            save->phase=2;
        }
        else {save->durable=save->write.durable;
            save->phase=4;
        }
        return SC32_PENDING;
    }
    if(save->phase==2){
        if(save->validator==S2_VALIDATION_USER || save->validator==S2_VALIDATION_ENV){
            ScLeaseInfo candidate;
            INIT(candidate);
            s=pin_query(&save->temporary,&candidate);
            ScConfigurationDesc d;
            INIT(d);
            d.kind=save->validator==S2_VALIDATION_USER?SC32_CONFIG_USER:SC32_CONFIG_ENVIRONMENT;
            d.action=SC32_CONFIG_SAVE;
            d.candidate=save->temporary_path;
            d.expected_candidate_revision=candidate.revision;
            d.flags=save->expected?SC32_CONFIG_EXPECT_EXACT:SC32_CONFIG_EXPECT_ABSENT;
            d.expected_target_revision=save->expected;
            ScConfigurationResult result;
            INIT(result);
            if(s==0)s=ScConfigurationApply(&d,&result);
            save->durable=(result.flags&SC32_CONFIG_DURABLE)!=0;
            save->active=(result.flags&SC32_CONFIG_ACTIVE)!=0;
            save->uncertain=(result.flags&SC32_CONFIG_UNCERTAIN)!=0;
            if(s==0 && (!save->durable || !save->active))s=SC32_E_FAILED;
            save->outcome=s;
            save->phase=5;
        }else {
            s=ScConfigurationCheck(save->validator,save->temporary_path);
            if(s==0)s=io_start_memory(&save->write,save->target,save->body,save->expected,23,0);
            if(s<0){save->outcome=s;
                save->phase=5;
            }else save->phase=3;
        }
        return SC32_PENDING;
    }
    if(save->phase==4){
        if(!strcmp(save->target,settings.paths[3])){s=ScThemeApply(0,SC32_THEME_RELOAD);
            save->active=s==0;
        }
        else if(save->validator==SC32_CHECK_MENU || save->validator==SC32_CHECK_SHORTCUT){s=ScDesktopReload();
            save->active=s==0;
        }
        else if(save->validator==SC32_CHECK_WALLPAPER){const char *body=0;
            ScStringCStr(save->body,&body);
            s=ScWallpaperSet((ScU32)(body[0]-'0'));
            save->active=s==0;
        }
        save->outcome=s;
        save->phase=5;
        return SC32_PENDING;
    }
    if(save->phase==5){
        s=settings_temp_remove(save);
        if(s==SC32_PENDING)return s;
        if(s<0 && !save->outcome)save->outcome=s;
        if(save->durable){
            ScPathInfo meta;
            INIT(meta);
            ScStatus q=ScPathQuery(save->target,&meta);
            if(save->kind==S2_SAVE_DOCUMENT){
                strcpy(settings.path,save->target);
                set_text(settings.document_path,settings.path);
                if(q==0)settings.document_revision=meta.generation;
                ScUiDocumentInfo info;
                INIT(info);
                if(ScUiDocumentQuery(settings.document,&info)==0 && info.revision==save->document_revision)ScUiDocumentMarkSaved(settings.document);
            }
            if(save->kind==S2_SAVE_USER && !save->outcome){settings.user_revision=q==0?meta.generation:0;
                settings.user_dirty=0;
            }
            if(save->kind==S2_SAVE_ENV && !save->outcome){settings.env_revision=q==0?meta.generation:0;
                if(settings.env_model_revision==save->document_revision)settings.env_dirty=0;
            }
        }
        if(save->uncertain)status("磁盘提交结果不明；草稿保留，请检查卷与实际文件，不能自动重试");
        else if(save->outcome<0)status(save->durable?"文件已持久保存；激活或临时清理失败，请检查逐项错误":"保存失败；原文件与当前草稿保留");
        else status(save->kind==S2_SAVE_THEME?"主题配置已持久保存；画面资源由桌面异步接管":save->active?"已持久保存并应用":"已持久保存；文件消费者按自身规则读取");
        ScStatus outcome=save->outcome;
        if(save->kind==S2_SAVE_USER){ScUiNodeSetState(app.ui,settings.user_name,SC_UI_ENABLED,SC_UI_ENABLED);
            ScUiNodeSetState(app.ui,settings.user_home,SC_UI_ENABLED,SC_UI_ENABLED);
        }
        pin_release(&save->temporary,0);
        io_dispose(&save->write);
        ScStringRelease(save->body);
        memset(save,0,sizeof(*save));
        return outcome;
    }
    return SC32_E_INVALID;
}
static ScStatus settings_wallpaper(const char *path)
{
    ScString *original=0,*changed=0;
    ScU32 revision=0;
    ScStatus s=read_small(settings.paths[3],4095,&original,&revision);
    if(s==SC32_E_NOT_FOUND){ScThemeInfo info;
        INIT(info);
        s=ScThemeQuery(&info);
        if(s==0)s=read_small(info.style==SC32_STYLE_CLASSIC?"/SYS/THEMES/CLASSIC.CFG":"/SYS/THEMES/AURORA.CFG",4095,&original,0);
    }
    if(s==0)s=ScStringCreate("",0,&changed);
    const char *body=0;
    if(s==0)s=ScStringCStr(original,&body);
    ScU32 found=0;
    while(s==0 && *body){const char *end=strchr(body,'\n');
        ScU32 count=end?(ScU32)(end-body)+1:(ScU32)strlen(body);
        const char *key=body;
        while(key<body+count && (*key==' ' || *key=='\t'))key++;
        const char *equals=memchr(key,'=',count-(ScU32)(key-body));
        ScU32 wallpaper=0;
        if(equals){const char *last=equals;
            while(last>key && (last[-1]==' ' || last[-1]=='\t'))last--;
            if(last-key==9){char name[10];
                memcpy(name,key,9);
                name[9]=0;
                wallpaper=path_equal(name,"wallpaper");
            }}
        if(wallpaper){s=ScStringAppendFormat(changed,"wallpaper=%s\n",path);
            found=1;
        }
        else s=ScStringAppend(changed,body,count);
        body+=count;
    }
    if(s==0 && !found)s=ScStringAppendFormat(changed,"\nwallpaper=%s\n",path);
    if(s==0)s=settings_save_start(S2_SAVE_THEME,settings.paths[3],changed,revision,0);
    ScStringRelease(original);
    ScStringRelease(changed);
    return s;
}
static void settings_image_cancel(void)
{
    if(settings.image)ScImageDecodeCancel(settings.image);
    settings.image=0;
    settings.image_wait=settings.image_more=0;
    pin_release(&settings.image_pin,0);
}
static ScStatus settings_image_begin(const char *path)
{
    if(settings.save.phase)return SC32_E_BUSY;
    settings_image_cancel();
    ScPathInfo info;
    INIT(info);
    ScStatus s=ScPathQuery(path,&info);
    if(s==0 && info.kind!=SC32_PATH_FILE)s=SC32_E_INVALID;
    if(s==0)s=pin_open(path,SC32_PATH_FILE,SC32_LEASE_READ_STABLE,info.generation,0,&settings.image_pin);
    ScImageDecodeDesc d;
    INIT(d);
    d.path=path;
    if(s==0)s=ScImageDecodeBegin(&d,&settings.image);
    if(s<0){settings_image_cancel();
        return s;
    }strcpy(settings.image_path,path);
    settings.image_more=1;
    set_text(settings.wall_note,"正在完整验证图片；当前背景保留");
    return 0;
}
static ScStatus settings_image_step(void)
{
    if(!settings.image)return 0;
    ScImageInfo info;
    INIT(info);
    ScStatus s=ScImageDecodeQuery(settings.image,&info);
    if(s==SC32_PENDING){settings.image_more=(info.flags&SC32_IMAGE_MORE_WORK)!=0;
        settings.image_wait=info.wait_events;
        if(settings.image_more){char note[160];
            snprintf(note,sizeof(note),"正在验证SCB正文 %u / %u B",info.progress,info.total);
            set_text(settings.wall_note,note);
        }
        return SC32_PENDING;
    }
    if(s==0){ScLeaseInfo source;
        INIT(source);
        s=pin_query(&settings.image_pin,&source);
        ScPathInfo current;
        INIT(current);
        if(s==0)s=ScPathQuery(settings.image_path,&current);
        if(s==0 && current.generation!=source.revision)s=SC32_E_STALE;
        if(s==0)s=settings_wallpaper(settings.image_path);
        if(s==0){char note[160];
            snprintf(note,sizeof(note),"已完整验证 %u × %u；正在保存背景配置",info.width,info.height);
            set_text(settings.wall_note,note);
        }
    }
    /* Query成功不消费请求。失败、成功和退出均Cancel释放服务/SCB文件；
    * 不为“预览就绪”无意义复制全屏ARGB。稳定源保持到配置提交结束。 */
    if(settings.image)ScImageDecodeCancel(settings.image);
    settings.image=0;
    settings.image_more=settings.image_wait=0;
    if(s<0)pin_release(&settings.image_pin,0);
    return s;
}
