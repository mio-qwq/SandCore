static ScStatus sh_argument(ScString *arguments,const char *value,ScU32 quoted) {
    ScStatus s;
    ScU32 i;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScStringQuery(arguments,&info);
    if(s<0)return sh_cause(s,60,0);
    if(info.length) {
        s=ScStringAppend(arguments," ",1);
        if(s<0)return sh_cause(s,60,0);
    }
    if(quoted) {
        s=ScStringAppend(arguments,"\"",1);
        if(s<0)return sh_cause(s,60,0);
    }
    for(i=0;value[i];++i) {
        unsigned char ch=(unsigned char)value[i];
        if(ch<32||ch==127)return SC_SHELL_E_INVALID;
        if(quoted&&(ch=='"'||ch=='\\')) {
            s=ScStringAppend(arguments,"\\",1);
            if(s<0)return sh_cause(s,60,0);
        }
        s=ScStringAppend(arguments,value+i,1);
        if(s<0)return sh_cause(s,60,0);
    }
    if(quoted) {
        s=ScStringAppend(arguments,"\"",1);
        if(s<0)return sh_cause(s,60,0);
    }
    return 0;
}
static ScStatus sh_launch_path(const char *path,const char *prefix,const char *document,ScU32 profile,ScLaunchInfo *out) {
    ScLaunchDesc d;
    ScLaunchInfo result;
    ScString *arguments=0;
    const char *text;
    char *exe=0,*doc=0;
    ScContainerInfo info;
    ScStatus s;
    if(sh_record(out,sizeof(*out))<0)return SC_SHELL_E_INVALID;
    if(profile!=SC_SHELL_PROFILE_MODERN&&profile!=SC_SHELL_PROFILE_LEGACY)return SC_SHELL_E_INVALID;
    s=sh_absolute(path,&exe);
    if(s<0)goto done;
    s=sh_text(prefix,profile==2?127:1023,1);
    if(s<0)goto done;
    s=ScStringCreate(prefix,prefix?(ScU32)strlen(prefix):0,&arguments);
    if(s<0)goto done;
    if(document&&*document) {
        s=sh_absolute(document,&doc);
        if(s<0)goto done;
        s=sh_argument(arguments,doc,profile==SC_SHELL_PROFILE_MODERN);
        if(s<0)goto done;
    }
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScStringQuery(arguments,&info);
    if(s<0)goto done;
    if(info.length>(profile==SC_SHELL_PROFILE_LEGACY?127u:1023u)) {
        s=SC_SHELL_E_RANGE;
        goto done;
    }
    s=ScStringCStr(arguments,&text);
    if(s<0)goto done;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.path=exe;
    d.arguments=text;
    memset(&result,0,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    s=ScProcessLaunch(&d,&result);
    if(s>=0)s=sh_output(out,&result,sizeof(result));
    done:free(exe);
    free(doc);
    ScStringRelease(arguments);
    return sh_cause(s,60,0);
}
ScStatus ScShellLaunch(const ScShellApplication *a,ScU32 verb_index,const char *document,ScLaunchInfo *out) {
    const ScShellVerbDesc *v;
    if(!sh_valid(a,SH_APP))return SC_SHELL_E_STALE;
    if(verb_index>=a->desc.verb_count)return SC_SHELL_E_RANGE;
    if(a->info.flags&SC_SHELL_APP_DISABLED)return SC_SHELL_E_ACCESS;
    v=a->verbs+verb_index;
    return sh_launch_path(*v->executable?v->executable:a->desc.executable, *v->arguments?v->arguments:a->desc.arguments,(v->flags&SC_SHELL_VERB_NO_DOCUMENT)?0:document,v->profile,out);
}
ScStatus ScShellOpen(const ScShellCatalog *catalog,const char *path,const char *verb,ScLaunchInfo *out) {
    ScShellApplication *app=0;
    ScHash *visited=0;
    ScHashDesc hd;
    ScU32 index,marker=1;
    ScPathParts parts;
    ScPathInfo stat;
    ScStatus s;
    char *current=0;
    if(sh_record(out,sizeof(*out))<0)return SC_SHELL_E_INVALID;
    s=sh_absolute(path,&current);
    if(s<0)return s;
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=4;
    s=ScHashCreate(&hd,&visited);
    if(s<0)goto done;
    for(;;) {
        ScU32 standard=!verb||!*verb||!strcmp(verb,"open");
        s=ScHashSet(visited,current,(ScU32)strlen(current),&marker,SC_HASH_INSERT_ONLY);
        if(s<0) {
            s=s==SC_UTIL_E_EXISTS?SC_SHELL_E_INVALID:s;
            break;
        }
        memset(&stat,0,sizeof(stat));
        stat.size=sizeof(stat);
        stat.version=1;
        s=ScPathQuery(current,&stat);
        if(s<0)break;
        memset(&parts,0,sizeof(parts));
        parts.size=sizeof(parts);
        parts.version=1;
        s=ScPathSplit(current,&parts);
        if(s<0)break;
        if(stat.kind==SC32_PATH_FILE&&standard&&parts.extension_bytes==4&&tolower((unsigned char)current[parts.extension_offset+1])=='l'&& tolower((unsigned char)current[parts.extension_offset+2])=='n'&&tolower((unsigned char)current[parts.extension_offset+3])=='k') {
            ScShellShortcut *shortcut=0;
            ScShellShortcutDesc d;
            s=ScShellShortcutRead(current,&shortcut);
            if(s<0)break;
            memset(&d,0,sizeof(d));
            d.size=sizeof(d);
            d.version=1;
            s=ScShellShortcutDescribe(shortcut,&d);
            if(s>=0&&d.kind==SC_SHELL_SHORTCUT_PATH) {
                char *next=0;
                s=sh_copy_text(d.target,64,0,&next);
                ScShellShortcutRelease(shortcut);
                if(s<0)break;
                free(current);
                current=next;
                continue;
            }
            if(s>=0)s=ScShellShortcutLaunch(shortcut,catalog,out);
            ScShellShortcutRelease(shortcut);
            break;
        }
        s=ScShellCatalogResolve(catalog,current,verb,&app,&index);
        if(s==SC_SHELL_E_NOT_FOUND&&stat.kind==SC32_PATH_FILE&&standard&&parts.extension_bytes==4&& tolower((unsigned char)current[parts.extension_offset+1])=='s'&&tolower((unsigned char)current[parts.extension_offset+2])=='c'&&tolower((unsigned char)current[parts.extension_offset+3])=='x') s=sh_launch_path(current,"",0,SC_SHELL_PROFILE_MODERN,out);
        else if(s>=0)s=ScShellLaunch(app,index,current,out);
        break;
    }
    done:ScShellApplicationRelease(app);
    ScHashRelease(visited);
    free(current);
    return sh_cause(s,60,0);
}
