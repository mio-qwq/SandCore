static char *trim(char *z)
{while(*z==' ' || *z=='\t')z++;
    ScU32 n=(ScU32)strlen(z);
    while(n && (z[n-1]=='\r' || z[n-1]==' ' || z[n-1]=='\t'))z[--n]=0;
    return z;
}
static ScStatus config_lines(ScString *body,EnvironmentRow rows[16],ScU32 *count,ScU32 user)
{
    const char *input=0;
    ScStatus s=ScStringCStr(body,&input);
    if(s<0)return s;
    char *copy=malloc(strlen(input)+1);
    if(!copy)return SC32_E_MEMORY;
    strcpy(copy,input);
    char *next=copy;
    ScU32 line=0,total=0;
    while(next && s==0){char *start=next;
        next=strchr(start,'\n');
        if(next)*next++=0;
        start=trim(start);
        if(!line++){if(strcmp(start,user?"SUSER1MIO":"SENV1MIO"))s=SC32_E_INVALID;
            continue;
        }
        if(!*start || *start=='#')continue;
        char *value=strchr(start,'=');
        if(!value || total==16){s=SC32_E_INVALID;
            break;
        }*value++=0;
        start=trim(start);
        value=trim(value);
        if(!*start || strlen(start)>31 || strlen(value)>255){s=SC32_E_RANGE;
            break;
        }
        for(ScU32 i=0;i<total;i++)if(!strcmp(rows[i].key,start))s=SC32_E_INVALID;
        if(s<0)break;
        strcpy(rows[total].key,start);
        strcpy(rows[total++].value,value);
    }
    free(copy);
    if(s==0)*count=total;
    return s;
}
static void settings_model_changed(ScUiProvider *provider,ScU32 *revision)
{
    ScUiDataChange change;
    INIT(change);
    change.kind=SC_UI_DATA_RESET;
    change.revision=++*revision;
    ScUiProviderNotify(provider,&change);
}
static ScStatus settings_load_user(void)
{
    ScString *body=0;
    ScU32 revision=0;
    ScStatus s=read_small(settings.paths[0],4095,&body,&revision);
    char name[32]="",home[65]="";
    ScU32 length;
    if(s==SC32_E_NOT_FOUND){s=ScProcessEnvironment("USER",name,sizeof(name),&length);
        if(s==0)s=ScProcessEnvironment("HOME",home,sizeof(home),&length);
    }
    else if(s==0){EnvironmentRow rows[16];
        ScU32 count=0;
        s=config_lines(body,rows,&count,1);
        for(ScU32 i=0;s==0 && i<count;i++){if(!strcmp(rows[i].key,"username"))snprintf(name,sizeof(name),"%s",rows[i].value);
            else if(!strcmp(rows[i].key,"home"))snprintf(home,sizeof(home),"%s",rows[i].value);
            else s=SC32_E_INVALID;
        }
        if(!*name || !*home)s=SC32_E_INVALID;
    }
    if(s==0){set_text(settings.user_name,name);
        set_text(settings.user_home,home);
        settings.user_revision=revision;
        settings.user_dirty=0;
        settings.user_loaded=1;
    }
    ScStringRelease(body);
    return s;
}
static ScStatus settings_load_env(void)
{
    ScString *body=0;
    ScU32 revision=0;
    ScStatus s=read_small(settings.paths[1],4095,&body,&revision);
    EnvironmentRow rows[16];
    ScU32 count=0;
    if(s==SC32_E_NOT_FOUND){strcpy(rows[0].key,"PATH");
        ScU32 length;
        s=ScProcessEnvironment("PATH",rows[0].value,sizeof(rows[0].value),&length);
        if(s<0){strcpy(rows[0].value,"/BIN:/APPS");
            s=0;
        }count=1;
    }else if(s==0)s=config_lines(body,rows,&count,0);
    if(s==0){memcpy(settings.env,rows,count*sizeof(*rows));
        settings.env_count=count;
        settings.env_revision=revision;
        settings.env_dirty=0;
        settings.env_loaded=1;
        settings_model_changed(settings.env_provider,&settings.env_model_revision);
    }
    ScStringRelease(body);
    return s;
}
static ScStatus environment_provider(void *unused,const ScUiDataRequest *q,ScUiDataReply *r)
{
    (void)unused;
    ScU32 index=0;
    ScStatus parsed=flat_provider(q,r,settings.env_count,settings.env_model_revision,&index);
    if(parsed)return parsed==SC_UI_HANDLED?0:parsed;
    /* CELL由SCUI按列描述的field取值，不把可重排的column身份当数组下标。 */
    if(q->operation==SC_UI_DATA_CELL){const char *z=q->field?settings.env[index].value:settings.env[index].key;
        r->value.type=SC_VALUE_UTF8;
        r->value.data=z;
        r->value.bytes=(ScU32)strlen(z);
    }
    return 0;
}
static ScStatus environment_apply(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    ScConfig *cfg=(ScConfig *)e->data;
    ScU32 index=(ScU32)context;
    EnvironmentRow row;
    ScStatus s=config_get(cfg,"key",row.key,sizeof(row.key));
    if(s==0)s=config_get(cfg,"value",row.value,sizeof(row.value));
    if(s<0)return s;
    if(!*row.key || strchr(row.key,'=') || strchr(row.key,'\n') || strchr(row.value,'\n') || strchr(row.value,'\r'))return SC32_E_INVALID;
    for(ScU32 i=0;row.key[i];i++)if(!((row.key[i]>='A' && row.key[i]<='Z') || (row.key[i]>='a' && row.key[i]<='z') || row.key[i]=='_' || (i && row.key[i]>='0' && row.key[i]<='9')))return SC32_E_INVALID;
    for(ScU32 i=0;i<settings.env_count;i++)if(i!=index && !strcmp(settings.env[i].key,row.key))return SC32_E_EXISTS;
    if(index>=settings.env_count){if(settings.env_count==16)return SC32_E_NO_SPACE;
        index=settings.env_count++;
    }
    settings.env[index]=row;
    settings.env_dirty=1;
    settings_model_changed(settings.env_provider,&settings.env_model_revision);
    status("环境草稿已修改；保存后由真实消费者校验");
    return 0;
}
static ScStatus environment_edit(ScU32 add)
{
    ScUiSelectionInfo select;
    INIT(select);
    ScU32 index=settings.env_count;
    if(!add){ScStatus s=ScUiSelectionQuery(settings.env_selection,&select);
        if(s<0)return s;
        if(!select.current)return SC32_E_NOT_FOUND;
        index=select.current-1;
    }
    if(index>=16)return SC32_E_NO_SPACE;
    ScConfig *cfg=0;
    ScStatus s=ScConfigCreate(&cfg);
    if(s==0)s=config_string(cfg,"key",index<settings.env_count?settings.env[index].key:"");
    if(s==0)s=config_string(cfg,"value",index<settings.env_count?settings.env[index].value:"");
    ScUiFieldDesc fields[]={field(1,"名称","key",SC_UI_TEXTBOX,0,31),field(2,"值","value",SC_UI_TEXTBOX,0,255)};
    if(s==0)s=property("环境变量草稿",cfg,fields,2,environment_apply,(void *)index);
    ScConfigRelease(cfg);
    return s;
}
static ScStatus environment_save(void)
{
    if(!settings.env_loaded)return SC32_E_INVALID;
    ScString *body=0;
    ScStatus s=ScStringCreate("SENV1MIO\n",9,&body);
    for(ScU32 i=0;s==0 && i<settings.env_count;i++)s=ScStringAppendFormat(body,"%s=%s\n",settings.env[i].key,settings.env[i].value);
    if(s==0)s=settings_save_start(S2_SAVE_ENV,settings.paths[1],body,settings.env_revision,settings.env_model_revision);
    ScStringRelease(body);
    return s;
}
static ScStatus user_save(void)
{
    if(!settings.user_loaded)return SC32_E_INVALID;
    char name[32],home[65];
    ScStatus s=node_text(settings.user_name,name,sizeof(name));
    if(s==0)s=node_text(settings.user_home,home,sizeof(home));
    if(s<0)return s;
    ScString *body=0;
    s=ScStringCreate("",0,&body);
    if(s==0)s=ScStringFormat(body,"SUSER1MIO\nusername=%s\nhome=%s\n",name,home);
    if(s==0)s=settings_save_start(S2_SAVE_USER,settings.paths[0],body,settings.user_revision,0);
    ScStringRelease(body);
    return s;
}
