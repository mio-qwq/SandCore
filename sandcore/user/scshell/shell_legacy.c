static ScStatus sh_legacy_fields(const char *command,char **executable,char **arguments) {
    ScU32 at=0,start,n;
    unsigned char quote=0;
    char *exe=0,*args=0;
    ScStatus s;
    s=sh_text(command,127,0);
    if(s<0)return s;
    while(command[at]==' '||command[at]=='\t')++at;
    if(command[at]=='"')quote=(unsigned char)command[at++];
    start=at;
    while(command[at]&&(quote?command[at]!=quote:(command[at]!=' '&&command[at]!='\t')))++at;
    n=at-start;
    if(!n||(quote&&command[at]!=quote))return SC_SHELL_E_INVALID;
    exe=(char *)malloc(n+2);
    if(!exe)return SC_SHELL_E_MEMORY;
    if(command[start]=='/') {
        memcpy(exe,command+start,n);
        exe[n]=0;
    } else {
        exe[0]='/';
        memcpy(exe+1,command+start,n);
        exe[n+1]=0;
    }
    if(quote)++at;
    while(command[at]==' '||command[at]=='\t')++at;
    s=sh_copy_text(command+at,127,1,&args);
    if(s<0) {
        free(exe);
        return s;
    }
    *executable=exe;
    *arguments=args;
    return 0;
}
ScStatus ScShellCatalogImportLegacyMenu(ScShellCatalog *catalog,const char *path,ScU32 epoch) {
    ScBuffer *body=0;
    ScShellCatalog *candidate=0;
    const unsigned char *data;
    ScU32 bytes=0,at=0,count=0,i;
    ScStatus s;
    char *absolute=0,prefix[64];
    if(!sh_valid(catalog,SH_CATALOG))return SC_SHELL_E_STALE;
    if(epoch!=catalog->epoch)return SC_SHELL_E_STALE;
    if(catalog->commit_lock)return SC_SHELL_E_BUSY;
    if(catalog->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    s=sh_absolute(path,&absolute);
    if(s<0)goto done;
    s=sh_read_file(absolute,&body);
    if(s>=0)s=sh_buffer_data(body,&data,&bytes);
    if(s<0)goto done;
    if(bytes<9||bytes>8191||memchr(data,0,bytes)||ScUtf8Validate(data,bytes,0)<0) {
        s=SC_SHELL_E_PROTOCOL;
        goto done;
    }
    {
        char header[16];
        ScU32 n=0;
        while(at<bytes&&data[at]!='\n') {
            if(data[at]!='\r') {
                if(n==15) {
                    s=SC_SHELL_E_PROTOCOL;
                    goto done;
                }
                header[n++]=(char)data[at];
            }
            ++at;
        }
        if(at<bytes)++at;
        header[n]=0;
        if(n!=9) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        for(i=0;i<n;++i)if(toupper((unsigned char)header[i])!="SMENU1MIO"[i]) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
    }
    s=sh_catalog_clone(catalog,&candidate);
    if(s<0)goto done;
    snprintf(prefix,sizeof(prefix),"legacy.menu.%08x.",ScHashBytes(absolute,(ScU32)strlen(absolute)));
    for(i=candidate->app_count;i;--i)if(candidate->apps[i-1]->info.layer==SC_SHELL_LAYER_ACCOUNT&& !strncmp(candidate->apps[i-1]->desc.id,prefix,strlen(prefix))) {
        s=ScShellCatalogUnregister(candidate,candidate->apps[i-1]->desc.id,SC_SHELL_LAYER_ACCOUNT,candidate->epoch);
        if(s<0)goto done;
    } while(at<bytes) {
        char row[256],*fields[3],id[96],*exe=0,*arguments=0;
        ScU32 n=0,field=0,token;
        ScShellApplicationDesc app;
        ScShellVerbDesc verb;
        while(at<bytes&&data[at]!='\n') {
            if(data[at]!='\r') {
                if(n==255) {
                    s=SC_SHELL_E_PROTOCOL;
                    goto done;
                }
                row[n++]=(char)data[at];
            }
            ++at;
        }
        if(at<bytes)++at;
        row[n]=0;
        if(!n||row[0]=='#')continue;
        if(count==32) {
            s=SC_SHELL_E_RANGE;
            goto done;
        }
        fields[0]=row;
        for(i=0;i<n;++i) {
            if((unsigned char)row[i]<32) {
                s=SC_SHELL_E_PROTOCOL;
                goto done;
            }
            if(row[i]=='|') {
                if(field==2) {
                    s=SC_SHELL_E_PROTOCOL;
                    goto done;
                }
                row[i]=0;
                fields[++field]=row+i+1;
            }
        }
        if(field!=2||!*fields[0]||!*fields[1]||strlen(fields[0])>31||strlen(fields[1])>127||strlen(fields[2])>63) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        s=sh_legacy_fields(fields[1],&exe,&arguments);
        if(s<0)goto done;
        snprintf(id,sizeof(id),"%s%u",prefix,count+1);
        memset(&verb,0,sizeof(verb));
        verb.size=sizeof(verb);
        verb.version=1;
        verb.verb="open";
        verb.label=fields[0];
        verb.flags=SC_SHELL_VERB_DEFAULT|SC_SHELL_VERB_NO_DOCUMENT;
        verb.profile=SC_SHELL_PROFILE_LEGACY;
        verb.executable=exe;
        verb.arguments=arguments;
        verb.types="";
        verb.icon=fields[2];
        memset(&app,0,sizeof(app));
        app.size=sizeof(app);
        app.version=1;
        app.layer=SC_SHELL_LAYER_ACCOUNT;
        app.id=id;
        app.label=fields[0];
        app.executable=exe;
        app.arguments=arguments;
        app.icon=fields[2];
        app.category="Imported";
        app.profile=SC_SHELL_PROFILE_LEGACY;
        app.verbs=&verb;
        app.verb_count=1;
        s=ScShellCatalogRegister(candidate,&app,candidate->epoch,&token);
        free(exe);
        free(arguments);
        if(s<0)goto done;
        ++count;
    }
    /* 旧格式的32/8191约束不进入新catalog；完整候选才替换这一来源。 */ candidate->epoch=catalog->epoch+1;
    sh_catalog_swap(catalog,candidate);
    s=0;
    done:free(absolute);
    ScBufferRelease(body);
    ScShellCatalogRelease(candidate);
    return sh_cause(s,64,0);
}
