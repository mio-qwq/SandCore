/* 与旧install一致：三位每组0..3，其中r=1/w=2；-2表示不改所有者，
* -1表示SYSTEM，授权由原ScPathPermissions真实gate决定。 */
static ScStatus install_mode(const char *z,ScU32 *out)
{
    if(strlen(z)!=3)return SC32_E_INVALID;
    ScU32 value=0;
    for(ScU32 i=0;i<3;i++){if(z[i]<'0' || z[i]>'3')return SC32_E_INVALID;
        value|=(ScU32)(z[i]-'0')<<(i*2);
    }
    *out=value;
    return 0;
}
static ScStatus install_owner(const char *z,ScI32 *out)
{
    if(!strcmp(z,"-2")){*out=-2;
        return 0;
    }if(!strcmp(z,"-1")){*out=-1;
        return 0;
    }
    if(!*z)return SC32_E_INVALID;
    ScU32 n=0;
    for(ScU32 i=0;z[i];i++){if(z[i]<'0' || z[i]>'9' || n>((ScU32)INT_MAX-(ScU32)(z[i]-'0'))/10)return SC32_E_RANGE;
        n=n*10+(ScU32)(z[i]-'0');
    }
    *out=(ScI32)n;
    return 0;
}
static ScU32 install_same(const char *a,const char *b)
{
    while(*a && *b){unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;
    }return !*a && !*b;
}
static const char *install_state(ScU32 state)
{
    const char *states[]={"待预检","预检通过","复制中","正文已持久/权限待完成","完成","目录保留复用","失败/来源保留","已取消/来源保留","结果不明/禁止自动重试"};
    return state<sizeof(states)/sizeof(*states)?states[state]:"未知状态";
}
static void install_changed(void)
{
    ScUiDataChange change;
    INIT(change);
    change.kind=SC_UI_DATA_RESET;
    change.revision=++installer.revision;
    ScUiProviderNotify(installer.provider,&change);
}
static ScU32 install_key(const char *path,ScU32 kind,char out[66])
{
    out[0]=kind==SC32_PATH_FILE?'F':'D';
    ScU32 i=0;
    for(;path[i];i++){unsigned char c=(unsigned char)path[i];
        out[i+1]=(char)(c>='a' && c<='z'?c-32:c);
    }out[i+1]=0;
    return i+1;
}
static ScStatus install_add(const char *source,ScU32 kind)
{
    if(installer.phase)return SC32_E_BUSY;
    char normal[65];
    ScStatus s=normalize(source,"/",normal);
    if(s<0)return s;
    char key[66];
    ScU32 key_bytes=install_key(normal,kind,key);
    ScHashEntry existing;
    INIT(existing);
    s=ScHashGet(installer.identities,key,key_bytes,&existing);
    if(s==0)return SC32_E_EXISTS;
    if(s!=SC_UTIL_E_NOT_FOUND)return s;
    if(installer.count==installer.capacity){ScU32 n=installer.capacity?installer.capacity*2:16;
        if(n>UINT_MAX/sizeof(InstallItem))return SC32_E_RANGE;
        void *p=realloc(installer.items,n*sizeof(InstallItem));
        if(!p)return SC32_E_MEMORY;
        installer.items=p;
        installer.capacity=n;
    }
    InstallItem row;
    memset(&row,0,sizeof(row));
    row.kind=kind;
    if(kind==SC32_PATH_FILE)strcpy(row.source,normal);
    else strcpy(row.destination,normal);
    ScU32 marker=1;
    s=ScHashSet(installer.identities,key,key_bytes,&marker,SC_HASH_INSERT_ONLY);
    if(s<0)return s;
    installer.items[installer.count++]=row;
    install_changed();
    return 0;
}
static InstallItem *install_current(void)
{
    ScUiSelectionInfo info;
    INIT(info);
    if(ScUiSelectionQuery(installer.selection,&info)<0 || !info.current || info.current>installer.count)return 0;
    return installer.items+info.current-1;
}
static ScStatus install_provider(void *unused,const ScUiDataRequest *q,ScUiDataReply *r)
{
    (void)unused;
    r->revision=installer.revision;
    r->total=installer.count;
    if(q->operation==SC_UI_DATA_COUNT)return 0;
    ScU32 index=q->operation==SC_UI_DATA_ITEM?q->index:q->item-1;
    if(index>=installer.count)return SC_UI_END;
    r->item=index+1;
    if(q->operation==SC_UI_DATA_INDEX_OF){r->value.type=SC_VALUE_U32;
        r->value.lo=index;
        return 0;
    }
    if(q->operation==SC_UI_DATA_CELL){InstallItem *row=installer.items+index;
        if(q->column==2){r->value.type=SC_VALUE_U32;
            r->value.lo=row->length;
        }
        else {const char *z=q->column==0?(row->kind==SC32_PATH_DIRECTORY?"<目录>":row->source):q->column==1?row->destination:install_state(row->state);
            r->value.type=SC_VALUE_UTF8;
            r->value.data=z;
            r->value.bytes=(ScU32)strlen(z);
        }}
    return 0;
}
static ScStatus install_options(void)
{
    char mode[8],uid[32],gid[32],target[65];
    ScStatus s=node_text(installer.mode,mode,sizeof(mode));
    if(s==0)s=node_text(installer.uid,uid,sizeof(uid));
    if(s==0)s=node_text(installer.gid,gid,sizeof(gid));
    if(s==0)s=node_text(installer.target,target,sizeof(target));
    if(s==0)s=install_mode(mode,&installer.final_mode);
    if(s==0)s=install_owner(uid,&installer.final_uid);
    if(s==0)s=install_owner(gid,&installer.final_gid);
    if(s==0)s=normalize(target,"/",installer.destination);
    ScValue value;
    INIT(value);
    if(s==0)s=ScUiNodeGetValue(app.ui,installer.parents,&value);
    installer.create_parents=value.lo!=0;
    INIT(value);
    if(s==0)s=ScUiNodeGetValue(app.ui,installer.repermission,&value);
    installer.change_existing=value.lo!=0;
    return s;
}
static ScStatus install_preflight_item(InstallItem *item)
{
    ScStatus s=0;
    ScPathInfo target;
    INIT(target);
    if(item->state==I2_DONE || item->state==I2_REUSED)return 0;
    if(item->durable){s=ScPathQuery(item->destination,&target);
        if(s<0)return s;
        if(target.generation!=item->result_revision)return SC32_E_STALE;
        item->state=I2_CONTENT;
        return 0;
    }
    if(item->kind==SC32_PATH_FILE){
        ScPathInfo source;
        INIT(source);
        s=ScPathQuery(item->source,&source);
        if(s<0)return s;
        if(source.kind!=SC32_PATH_FILE)return SC32_E_INVALID;
        item->source_revision=source.generation;
        item->length=source.bytes;
        IoPin read;
        memset(&read,0,sizeof(read));
        s=pin_open(item->source,SC32_PATH_FILE,SC32_LEASE_READ_STABLE,source.generation,0,&read);
        if(s<0)return s;
        pin_release(&read,0);
        s=ScPathQuery(installer.destination,&target);
        ScU32 directory=s==0 && target.kind==SC32_PATH_DIRECTORY;
        if(installer.file_count>1 && !directory)return s<0?s:SC32_E_INVALID;
        if(s<0 && s!=SC32_E_NOT_FOUND)return s;
        if(directory){const char *leaf=strrchr(item->source,'/');
            if(!leaf)return SC32_E_INVALID;
            ScString *joined=0;
            s=ScPathJoin(installer.destination,leaf+1,0,&joined);
            const char *z=0;
            if(s==0)s=ScStringCStr(joined,&z);
            if(s==0)s=normalize(z,"/",item->destination);
            ScStringRelease(joined);
            if(s<0)return s;
        }
        else strcpy(item->destination,installer.destination);
        if(install_same(item->source,item->destination))return SC32_E_INVALID;
    }
    INIT(target);
    s=ScPathQuery(item->destination,&target);
    if(s==0){if(item->kind==SC32_PATH_FILE || target.kind!=SC32_PATH_DIRECTORY)return SC32_E_EXISTS;
        item->result_revision=target.generation;
        item->state=installer.change_existing?I2_CONTENT:I2_REUSED;
        return 0;
    }
    if(s!=SC32_E_NOT_FOUND)return s;
    char parent[65];
    strcpy(parent,item->destination);
    char *slash=strrchr(parent,'/');
    if(!slash)return SC32_E_INVALID;
    if(slash==parent)slash[1]=0;
    else *slash=0;
    INIT(target);
    s=ScPathQuery(parent,&target);
    if(s==0 && target.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
    if(s==SC32_E_NOT_FOUND && installer.create_parents)s=0;
    if(s<0)return s;
    item->state=I2_CHECKED;
    snprintf(item->detail,sizeof(item->detail),"元数据与来源读取预检通过；目标写权限在真实创建/发布时复核");
    return 0;
}
static ScStatus install_begin(ScU32 run)
{
    if(installer.phase || installer.report.phase)return SC32_E_BUSY;
    if(!installer.count)return SC32_E_INVALID;
    ScStatus s=install_options();
    if(s<0)return s;
    installer.cursor=0;
    installer.cancel=0;
    installer.run_after_check=run;
    installer.checking=1;
    installer.phase=1;
    installer.file_count=0;
    for(ScU32 i=0;i<installer.count;i++)if(installer.items[i].kind==SC32_PATH_FILE)installer.file_count++;
    status("只读预检；任何既有文件都不会自动覆盖");
    return 0;
}
