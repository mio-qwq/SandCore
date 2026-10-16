static void changed(void){
    if(st.epoch==0xffffffffu){
        st.success=0;
        status("工程修订耗尽，请重新打开工程");
        return;
    }
    st.epoch++;
    st.success=0;
    refresh();
}
static void config_set(ScConfig *c,const char *section,const char *key,const char *s)
{
    ScValue v;
    rec(&v,sizeof(v));
    v.type=SC_VALUE_UTF8;
    v.data=s;
    v.bytes=(U)strlen(s);
    ScStatus q=ScConfigSet(c,section,key,&v);
    if(q<0&&!st.config_error)st.config_error=q;
}
static ScStatus config_string(ScConfig *c,const char *key,char *out,U cap,U required)
{
    ScValue v;
    rec(&v,sizeof(v));
    ScStatus s=ScConfigGet(c,"project",key,&v);
    if(s<0)return s;
    if(v.type!=SC_VALUE_UTF8||v.bytes>=cap||memchr(v.data,0,v.bytes)||(required&&!v.bytes))return SC32_E_INVALID;
    memcpy(out,v.data,v.bytes);
    out[v.bytes]=0;
    return 0;
}
static ScStatus config_path(ScConfig *c,const char *key,char out[68],U optional)
{
    char value[68];
    ScStatus s=config_string(c,key,value,sizeof(value),!optional);
    if(s<0)return s;
    if(!*value){
        out[0]=0;
        return 0;
    }
    return path(value,out);
}
static ScStatus include_valid(const char *s)
{
    U length=(U)strlen(s);
    if(length>255u)return SC32_E_RANGE;
    if(!length)return 0;
    U start=0;
    for(U i=0;i<=length;i++)if(!s[i]||s[i]==';'){
        U n=i-start;
        if(!n||n>63u||s[start]!='/')return SC32_E_INVALID;
        char dir[68];
        memcpy(dir,s+start,n);
        dir[n]=0;
        char resolved[68];
        ScStatus q=path(dir,resolved);
        if(q<0)return q;
        ScPathInfo info;
        rec(&info,sizeof(info));
        q=ScPathQuery(resolved,&info);
        if(q<0)return q;
        if(info.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
        start=i+1;
    }
    return 0;
}
static ScStatus config_read(ScConfig *c)
{
    char compiler[68],linker[68],manifest[68],output[68],host[68],publish[68],workspace[68],includes[256],arguments[512],kind[16];
    ScStatus s=config_path(c,"compiler",compiler,0);
    if(s>=0)s=config_path(c,"linker",linker,0);
    if(s>=0)s=config_path(c,"manifest",manifest,0);
    if(s>=0)s=config_path(c,"output",output,0);
    if(s>=0)s=config_path(c,"debug_host",host,1);
    if(s>=0)s=config_path(c,"publish_dir",publish,1);
    if(s>=0)s=config_path(c,"workspace",workspace,0);
    if(s>=0)s=config_string(c,"include_dirs",includes,sizeof(includes),0);
    if(s>=0)s=include_valid(includes);
    if(s>=0)s=config_string(c,"arguments",arguments,sizeof(arguments),0);
    if(s>=0)s=config_string(c,"kind",kind,sizeof(kind),1);
    if(s<0)return s;
    if(strcmp(kind,"SCX")&&strcmp(kind,"SCL"))return SC32_E_INVALID;
    U n=(U)strlen(output);
    if(n<5u||n>59u||strcmp(output+n-4u,!strcmp(kind,"SCL")?".SCL":".SCX"))return SC32_E_RANGE;
    const char *suffix[3]={
        ".sdbg",".map",""
    };
    for(U i=0;i<3;i++){
        char artifact[68];
        snprintf(artifact,sizeof(artifact),"%s%s",output,suffix[i]);
        if(same_name(artifact,manifest)||same_name(artifact,compiler)||same_name(artifact,linker)||(st.project_path[0]&&same_name(artifact,st.project_path)))return SC32_E_INVALID;
        for(U k=0;k<st.member_count;k++)if(same_name(artifact,st.members[k].path))return SC32_E_INVALID;
        for(StDoc *d=st.documents;d;d=d->next)if(d->path[0]&&same_name(artifact,d->path))return SC32_E_INVALID;
    }
    strcpy(st.compiler,compiler);
    strcpy(st.linker,linker);
    strcpy(st.manifest_path,manifest);
    strcpy(st.output_path,output);
    strcpy(st.debug_host,host);
    strcpy(st.publish_dir,publish);
    strcpy(st.workspace,workspace);
    strcpy(st.includes,includes);
    strcpy(st.arguments,arguments);
    st.target_scl=!strcmp(kind,"SCL");
    return 0;
}
static void config_number(ScConfig *c,const char *section,const char *key,U value)
{
    ScValue v;
    rec(&v,sizeof(v));
    v.type=SC_VALUE_U32;
    v.lo=value;
    ScStatus q=ScConfigSet(c,section,key,&v);
    if(q<0&&!st.config_error)st.config_error=q;
}
static ScStatus project_encode(ScString **out)
{
    st.config_error=0;
    config_number(st.config,"project","schema",1);
    config_number(st.config,"project","file_count",st.member_count);
    for(U i=0;i<st.member_count;i++){
        char section[32];
        snprintf(section,sizeof(section),"file.%u",i);
        config_set(st.config,section,"path",st.members[i].path);
        config_number(st.config,section,"source",st.members[i].source);
    }
    if(st.ui){
        ScStatus q=ScUiLayoutSave(st.ui,st.root,st.config);
        if(q<0)return q;
    }
    if(st.config_error<0)return st.config_error;
    return ScConfigSerialize(st.config,SC_CONFIG_SORT_KEYS,out);
}
static ScStatus project_defaults(void)
{
    st.config_error=0;
    ScConfig *c=0;
    ScStatus s=ScConfigCreate(&c);
    if(s<0)return s;
    config_set(c,"project","compiler","/BIN/SCCC.SCX");
    config_set(c,"project","linker","/BIN/SCLD.SCX");
    config_set(c,"project","manifest","/HOME/HELLO.JSON");
    config_set(c,"project","output","/HOME/HELLO.SCX");
    config_set(c,"project","kind","SCX");
    config_set(c,"project","include_dirs","");
    config_set(c,"project","arguments","");
    config_set(c,"project","debug_host","");
    config_set(c,"project","publish_dir","/HOME");
    config_set(c,"project","workspace","/HOME");
    config_number(c,"project","schema",1);
    config_number(c,"project","file_count",0);
    s=st.config_error<0?st.config_error:config_read(c);
    if(s<0){
        ScConfigRelease(c);
        return s;
    }
    ScConfigRelease(st.config);
    st.config=c;
    st.member_count=0;
    st.project_path[0]=0;
    st.project_disk_revision=0;
    st.project_saved=0;
    st.project_dirty=1;
    st.project_revision++;
    changed();
    project_notify();
    return 0;
}
static ScStatus project_add(const char *name)
{
    if(st.build.phase)return SC32_E_BUSY;
    char absolute[68];
    ScStatus s=path(name,absolute);
    if(s<0)return s;
    for(U i=0;i<st.member_count;i++)if(same_name(st.members[i].path,absolute))return 0;
    const char *suffix[3]={
        ".sdbg",".map",""
    };
    for(U i=0;i<3;i++){
        char artifact[68];
        snprintf(artifact,sizeof(artifact),"%s%s",st.output_path,suffix[i]);
        if(same_name(artifact,absolute))return SC32_E_INVALID;
    }
    if(same_name(absolute,st.manifest_path))return SC32_E_INVALID;
    if(st.member_count==st.member_capacity){
        U next=st.member_capacity?st.member_capacity*2u:8u;
        if(next>0x1000000u/sizeof(StMember))return SC32_E_RANGE;
        StMember *p=realloc(st.members,next*sizeof(*p));
        if(!p)return SC32_E_MEMORY;
        st.members=p;
        st.member_capacity=next;
    }
    StMember *m=st.members+st.member_count++;
    memset(m,0,sizeof(*m));
    m->id=++st.member_serial;
    if(!m->id)return SC32_E_RANGE;
    strcpy(m->path,absolute);
    const char *ext=strrchr(absolute,'.');
    m->source=ext&&(!strcmp(ext,".C")||!strcmp(ext,".c"));
    st.project_dirty=1;
    st.project_revision++;
    changed();
    project_notify();
    return 0;
}
static ScStatus project_load(const void *bytes,U count,U revision,const char *name)
{
    if(st.build.phase)return SC32_E_BUSY;
    ScConfig *candidate=0;
    ScStatus s=ScConfigCreate(&candidate);
    ScParseIssue issue;
    rec(&issue,sizeof(issue));
    if(s>=0)s=ScConfigParse(candidate,bytes,count,0,&issue);
    ScValue schema,number;
    rec(&schema,sizeof(schema));
    rec(&number,sizeof(number));
    if(s>=0)s=ScConfigGet(candidate,"project","schema",&schema);
    if(s>=0)s=ScConfigGet(candidate,"project","file_count",&number);
    if(s>=0&&(schema.type!=SC_VALUE_U32||schema.lo!=1u||number.type!=SC_VALUE_U32||number.lo>1048576u/sizeof(StMember)))s=SC32_E_INVALID;
    U n=s>=0?number.lo:0;
    StMember *members=n?calloc(n,sizeof(*members)):0;
    if(n&&!members)s=SC32_E_MEMORY;
    for(U i=0;i<n&&s>=0;i++){
        char section[32];
        snprintf(section,sizeof(section),"file.%u",i);
        ScValue p,k;
        rec(&p,sizeof(p));
        rec(&k,sizeof(k));
        s=ScConfigGet(candidate,section,"path",&p);
        if(s>=0)s=ScConfigGet(candidate,section,"source",&k);
        if(s>=0&&(p.type!=SC_VALUE_UTF8||!p.bytes||p.bytes>64u||memchr(p.data,0,p.bytes)||k.type!=SC_VALUE_U32||k.lo>1u))s=SC32_E_INVALID;
        if(s>=0){
            char raw[68];
            memcpy(raw,p.data,p.bytes);
            raw[p.bytes]=0;
            s=path(raw,members[i].path);
            members[i].source=k.lo;
            members[i].id=i+1u;
            for(U j=0;j<i;j++)if(same_name(members[j].path,members[i].path))s=SC32_E_INVALID;
        }
    }
    if(s>=0){
        char output[68],manifest[68];
        s=config_path(candidate,"output",output,0);
        if(s>=0)s=config_path(candidate,"manifest",manifest,0);
        const char *suffix[3]={
            ".sdbg",".map",""
        };
        for(U i=0;i<3&&s>=0;i++){
            char artifact[68];
            int wrote=snprintf(artifact,sizeof(artifact),"%s%s",output,suffix[i]);
            if(wrote<0||(U)wrote>=sizeof(artifact)){
                s=SC32_E_RANGE;
                break;
            }
            if(same_name(artifact,name))s=SC32_E_INVALID;
            for(U k=0;k<n;k++)if(same_name(artifact,members[k].path)||same_name(manifest,members[k].path))s=SC32_E_INVALID;
        }
    }
    if(s>=0)s=config_read(candidate);
    if(s>=0){
        free(st.members);
        st.members=members;
        members=0;
        st.member_count=st.member_capacity=n;
        st.member_serial=n;
        ScConfigRelease(st.config);
        st.config=candidate;
        candidate=0;
        strcpy(st.project_path,name);
        st.project_disk_revision=revision;
        changed();
        st.project_saved=st.epoch;
        st.project_dirty=0;
        st.project_revision++;
        project_notify();
        ScUiLayoutRestore(st.ui,st.root,st.config);
        status("工程已打开；源文件分别编译为SCO");
    }
    free(members);
    ScConfigRelease(candidate);
    return s;
}
static void project_notify(void)
{
    st.tree_revision++;
    if(st.tree_provider){
        ScUiDataChange d;
        rec(&d,sizeof(d));
        d.kind=SC_UI_DATA_RESET;
        d.revision=st.tree_revision;
        ScUiProviderNotify(st.tree_provider,&d);
    }
}
