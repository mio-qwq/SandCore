static int sh_name(const char *s) {
    ScU32 i;
    if(sh_text(s,255,0)<0)return 0;
    for(i=0;s[i];++i)if(!isalnum((unsigned char)s[i])&&s[i]!='.'&&s[i]!='_'&&s[i]!='-')return 0;
    return 1;
}
static void sh_application_dispose(ShObject *base) {
    ScShellApplication *a=(ScShellApplication *)base;
    ScU32 i;
    ScHashRelease(a->verb_index);
    for(i=0;i<a->desc.verb_count;++i) {
        ScShellVerbDesc *v=a->verbs+i;
        free((void *)v->verb);
        free((void *)v->label);
        free((void *)v->executable);
        free((void *)v->arguments);
        free((void *)v->types);
        free((void *)v->icon);
    }
    free(a->verbs);
    for(i=0;i<6;++i)free(a->strings[i]);
}
static ScStatus sh_application_new(const ScShellApplicationDesc *d,ScShellApplication **out) {
    ScShellApplication *a;
    ScStatus s=sh_record(d,sizeof(*d));
    ScU32 i,has_default=0;
    ScHashDesc hd;
    const char *src[6];
    ScU32 bounds[6]= {
        255,1023,127,1023,127,255
    };
    if(s<0)return s;
    if(d->layer>2||(d->flags&~7u)||(d->profile!=1&&d->profile!=2)||!sh_name(d->id)|| !d->verb_count||!d->verbs||d->verb_count>0xFFFFFFFFu/sizeof(ScShellVerbDesc)|| d->reserved[0]||d->reserved[1]||d->reserved[2])return SC_SHELL_E_INVALID;
    if(d->layer!=SC_SHELL_LAYER_SESSION&&(d->flags&SC_SHELL_APP_TRANSIENT))return SC_SHELL_E_INVALID;
    src[0]=d->id;
    src[1]=d->label;
    src[2]=d->executable;
    src[3]=d->arguments;
    src[4]=d->icon;
    src[5]=d->category;
    a=(ScShellApplication *)sh_new(sizeof(*a),SH_APP,sh_application_dispose);
    if(!a)return SC_SHELL_E_MEMORY;
    a->desc=*d;
    a->desc.verb_count=0;
    a->verbs=(ScShellVerbDesc *)calloc(d->verb_count,sizeof(*a->verbs));
    if(!a->verbs) {
        sh_release(&a->base);
        return SC_SHELL_E_MEMORY;
    }
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=4;
    s=ScHashCreate(&hd,&a->verb_index);
    if(s<0)goto fail;
    for(i=0;i<6;++i) {
        if(i==2)s=sh_absolute(src[i],a->strings+i);
        else s=sh_copy_text(src[i],bounds[i],i>=3,a->strings+i);
        if(s<0)goto fail;
    }
    a->desc.id=a->strings[0];
    a->desc.label=a->strings[1];
    a->desc.executable=a->strings[2];
    a->desc.arguments=a->strings[3];
    a->desc.icon=a->strings[4];
    a->desc.category=a->strings[5];
    a->desc.verbs=a->verbs;
    a->desc.size=sizeof(a->desc);
    a->desc.version=1;
    {
        ScU32 k;
        const char *category=a->desc.category;
        for(k=0;category[k];++k) {
            if((unsigned char)category[k]<32||category[k]==127||(category[k]=='/'&&(!k||category[k-1]=='/'||!category[k+1]))) {
                s=SC_SHELL_E_INVALID;
                goto fail;
            }
        }
        for(k=0;a->desc.label[k];++k)if((unsigned char)a->desc.label[k]<32||a->desc.label[k]==127) {
            s=SC_SHELL_E_INVALID;
            goto fail;
        }
    }
    if((ScU32)strlen(a->desc.arguments)>(d->profile==2?127u:1023u)) {
        s=SC_SHELL_E_RANGE;
        goto fail;
    }
    for(i=0;i<d->verb_count;++i) {
        const ScShellVerbDesc *v=d->verbs+i;
        ScShellVerbDesc *n=a->verbs+i;
        s=sh_record(v,sizeof(*v));
        if(s<0)goto fail;
        if(!sh_name(v->verb)||(v->flags&~3u)||(v->profile&&v->profile!=1&&v->profile!=2)||v->reserved[0]||v->reserved[1]) {
            s=SC_SHELL_E_INVALID;
            goto fail;
        }
        s=ScHashSet(a->verb_index,v->verb,(ScU32)strlen(v->verb),&i,SC_HASH_INSERT_ONLY);
        if(s<0)goto fail;
        if(!has_default&&(v->flags&SC_SHELL_VERB_DEFAULT)) {
            a->default_verb=i;
            has_default=1;
        }
        memset(n,0,sizeof(*n));
        n->size=sizeof(*n);
        n->version=1;
        n->flags=v->flags;
        n->profile=v->profile?v->profile:d->profile;
        ++a->desc.verb_count;
        s=sh_copy_text(v->verb,255,0,(char **)&n->verb);
        if(s<0)goto fail;
        s=sh_copy_text(v->label,1023,0,(char **)&n->label);
        if(s<0)goto fail;
        if(v->executable&&*v->executable)s=sh_absolute(v->executable,(char **)&n->executable);
        else s=sh_copy_text("",0,1,(char **)&n->executable);
        if(s<0)goto fail;
        s=sh_copy_text(v->arguments,n->profile==2?127:1023,1,(char **)&n->arguments);
        if(s<0)goto fail;
        s=sh_copy_text(v->types,4095,1,(char **)&n->types);
        if(s<0)goto fail;
        s=sh_copy_text(v->icon,127,1,(char **)&n->icon);
        if(s<0)goto fail;
    }
    memset(&a->info,0,sizeof(a->info));
    a->info.size=sizeof(a->info);
    a->info.version=1;
    a->info.layer=d->layer;
    a->info.flags=d->flags;
    a->info.verb_count=d->verb_count;
    for(i=0;i<6;++i)a->info.string_bytes+=(ScU32)strlen(a->strings[i])+1;
    for(i=0;i<d->verb_count;++i) {
        const ScShellVerbDesc *v=a->verbs+i;
        ScU32 n=(ScU32)(strlen(v->verb)+strlen(v->label)+strlen(v->executable)+strlen(v->arguments)+strlen(v->types)+strlen(v->icon))+6;
        if(a->info.string_bytes>0xFFFFFFFFu-n) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        a->info.string_bytes+=n;
    }
    *out=a;
    return 0;
    fail:sh_release(&a->base);
    return sh_cause(s,10,0);
}
ScStatus ScShellApplicationRetain(ScShellApplication *a) {
    return sh_retain((ShObject *)a,SH_APP);
}
void ScShellApplicationRelease(ScShellApplication *a) {
    if(a&&a->base.kind==SH_APP)sh_release(&a->base);
}
ScStatus ScShellApplicationQuery(const ScShellApplication *a,ScShellApplicationInfo *out) {
    if(!sh_valid(a,SH_APP))return SC_SHELL_E_STALE;
    return sh_output(out,&a->info,sizeof(a->info));
}
ScStatus ScShellApplicationDescribe(const ScShellApplication *a,ScShellApplicationDesc *out) {
    if(!sh_valid(a,SH_APP))return SC_SHELL_E_STALE;
    return sh_output(out,&a->desc,sizeof(a->desc));
}
static void sh_association_release(ShAssociation *a) {
    if(!a||!--a->references) {
        if(a) {
            free(a->type);
            free(a->application);
            free(a->verb);
            free(a);
        }
    }
}
static void sh_catalog_contents(ScShellCatalog *c) {
    ScU32 i;
    for(i=0;i<c->app_count;++i)ScShellApplicationRelease(c->apps[i]);
    free(c->apps);
    for(i=0;i<c->association_count;++i)sh_association_release(c->associations[i]);
    free(c->associations);
    ScShellLayoutRelease(c->layout);
    c->apps=0;
    c->associations=0;
    c->layout=0;
    c->app_count=0;
    c->association_count=0;
}
static void sh_catalog_dispose(ShObject *base) {
    sh_catalog_contents((ScShellCatalog *)base);
}
ScStatus ScShellCatalogCreate(ScShellCatalog **out) {
    ScShellCatalog *c;
    ScStatus s;
    if(!out)return SC_SHELL_E_INVALID;
    c=(ScShellCatalog *)sh_new(sizeof(*c),SH_CATALOG,sh_catalog_dispose);
    if(!c)return SC_SHELL_E_MEMORY;
    c->epoch=1;
    c->next_token=1;
    s=ScShellLayoutCreate(0,0,0,80,80,&c->layout);
    if(s<0) {
        sh_release(&c->base);
        return s;
    }
    *out=c;
    return 0;
}
ScStatus ScShellCatalogRetain(ScShellCatalog *c) {
    return sh_retain((ShObject *)c,SH_CATALOG);
}
void ScShellCatalogRelease(ScShellCatalog *c) {
    if(c&&c->base.kind==SH_CATALOG)sh_release(&c->base);
}
static ScStatus sh_catalog_clone(const ScShellCatalog *c,ScShellCatalog **out) {
    ScShellCatalog *n;
    ScU32 i;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    n=(ScShellCatalog *)sh_new(sizeof(*n),SH_CATALOG,sh_catalog_dispose);
    if(!n)return SC_SHELL_E_MEMORY;
    n->epoch=c->epoch;
    n->next_token=c->next_token;
    n->store_revision=c->store_revision;
    if(c->app_count) {
        n->apps=(ScShellApplication **)calloc(c->app_count,sizeof(*n->apps));
        if(!n->apps) {
            s=SC_SHELL_E_MEMORY;
            goto fail;
        }
    }
    if(c->association_count) {
        n->associations=(ShAssociation **)calloc(c->association_count,sizeof(*n->associations));
        if(!n->associations) {
            s=SC_SHELL_E_MEMORY;
            goto fail;
        }
    }
    for(i=0;i<c->app_count;++i) {
        s=ScShellApplicationRetain(c->apps[i]);
        if(s<0)goto fail;
        n->apps[n->app_count++]=c->apps[i];
    }
    for(i=0;i<c->association_count;++i) {
        if(c->associations[i]->references==0xFFFFFFFFu) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        ++c->associations[i]->references;
        n->associations[n->association_count++]=c->associations[i];
    }
    s=ScShellLayoutRetain(c->layout);
    if(s<0)goto fail;
    n->layout=c->layout;
    *out=n;
    return 0;
    fail:sh_release(&n->base);
    return s;
}
static void sh_catalog_swap(ScShellCatalog *a,ScShellCatalog *b) {
    ScU32 e=a->epoch,t=a->next_token,r=a->store_revision,ac=a->app_count,nc=a->association_count;
    ScShellApplication **ap=a->apps;
    ShAssociation **as=a->associations;
    ScShellLayout *l=a->layout;
    a->epoch=b->epoch;
    a->next_token=b->next_token;
    a->store_revision=b->store_revision;
    a->app_count=b->app_count;
    a->apps=b->apps;
    a->association_count=b->association_count;
    a->associations=b->associations;
    a->layout=b->layout;
    b->epoch=e;
    b->next_token=t;
    b->store_revision=r;
    b->app_count=ac;
    b->apps=ap;
    b->association_count=nc;
    b->associations=as;
    b->layout=l;
}
ScStatus ScShellCatalogQuery(const ScShellCatalog *c,ScShellCatalogInfo *out) {
    ScShellCatalogInfo v;
    ScU32 i;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.epoch=c->epoch;
    v.applications=c->app_count;
    v.associations=c->association_count;
    v.layout_items=c->layout?c->layout->info.count:0;
    for(i=0;i<c->app_count;++i) {
        ScU32 n=c->apps[i]->info.string_bytes;
        if(v.bytes>0xFFFFFFFFu-n)return SC_SHELL_E_RANGE;
        v.bytes+=n;
    }
    return sh_output(out,&v,sizeof(v));
}
ScStatus ScShellCatalogStoreRevision(const ScShellCatalog *c,ScU32 *out) {
    if(!out)return SC_SHELL_E_INVALID;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    *out=c->store_revision;
    return 0;
}
ScStatus ScShellCatalogRegister(ScShellCatalog *c,const ScShellApplicationDesc *d,ScU32 epoch,ScU32 *out) {
    ScShellApplication *a=0,**next;
    ScU32 i,index,count,token;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(c->commit_lock)return SC_SHELL_E_BUSY;
    if(!out)return SC_SHELL_E_INVALID;
    if(epoch!=c->epoch)return SC_SHELL_E_STALE;
    if(c->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    s=sh_application_new(d,&a);
    if(s<0)return sh_cause(s,10,0);
    index=c->app_count;
    for(i=0;i<c->app_count;++i)if(c->apps[i]->info.layer==d->layer&&!strcmp(c->apps[i]->desc.id,d->id)) {
        index=i;
        break;
    }
    count=c->app_count+(index==c->app_count);
    if(count<c->app_count||count>0xFFFFFFFFu/sizeof(*next)) {
        s=SC_SHELL_E_RANGE;
        goto fail;
    }
    if(index==c->app_count) {
        if(!c->next_token||c->next_token==0xFFFFFFFFu) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        token=c->next_token;
        a->info.revision=1;
    } else {
        token=c->apps[index]->info.token;
        if(c->apps[index]->info.revision==0xFFFFFFFFu) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        a->info.revision=c->apps[index]->info.revision+1;
    }
    next=(ScShellApplication **)malloc(count*sizeof(*next));
    if(!next) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    if(c->app_count)memcpy(next,c->apps,c->app_count*sizeof(*next));
    next[index]=a;
    a->info.token=token;
    a->info.epoch=c->epoch+1;
    if(d->layer==SC_SHELL_LAYER_SESSION) {
        ScBrokerIdentity id;
        s=sh_self(&id);
        if(s<0) {
            free(next);
            goto fail;
        }
        a->info.owner_pid=id.pid;
        a->info.owner_generation=id.generation;
        a->info.uid=id.uid;
        a->info.gid=id.gid;
        a->info.flags|=SC_SHELL_APP_TRANSIENT;
        a->desc.flags=a->info.flags;
    } else {
        ScIdentityInfo id;
        memset(&id,0,sizeof(id));
        id.size=sizeof(id);
        id.version=1;
        s=ScIdentityQuery(&id);
        if(s<0) {
            free(next);
            goto fail;
        }
        a->info.uid=id.uid;
        a->info.gid=id.gid;
    }
    if(index<c->app_count)ScShellApplicationRelease(c->apps[index]);
    else ++c->next_token;
    free(c->apps);
    c->apps=next;
    c->app_count=count;
    ++c->epoch;
    *out=token;
    return 0;
    fail:ScShellApplicationRelease(a);
    return sh_cause(s,10,0);
}
ScStatus ScShellCatalogUnregister(ScShellCatalog *c,const char *id,ScU32 layer,ScU32 epoch) {
    ScU32 i;
    ScShellApplication *a;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(c->commit_lock)return SC_SHELL_E_BUSY;
    if(!sh_name(id)||layer>2)return SC_SHELL_E_INVALID;
    if(c->epoch!=epoch)return SC_SHELL_E_STALE;
    if(c->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    for(i=0;i<c->app_count;++i)if(c->apps[i]->info.layer==layer&&!strcmp(c->apps[i]->desc.id,id))break;
    if(i==c->app_count)return SC_SHELL_E_NOT_FOUND;
    a=c->apps[i];
    memmove(c->apps+i,c->apps+i+1,(c->app_count-i-1)*sizeof(*c->apps));
    --c->app_count;
    ++c->epoch;
    ScShellApplicationRelease(a);
    return 0;
}
ScStatus ScShellCatalogApplication(const ScShellCatalog *c,ScU32 token,ScShellApplication **out) {
    ScU32 i;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(!out||!token)return SC_SHELL_E_INVALID;
    for(i=0;i<c->app_count;++i)if(c->apps[i]->info.token==token) {
        s=ScShellApplicationRetain(c->apps[i]);
        if(s<0)return s;
        *out=c->apps[i];
        return 0;
    }
    return SC_SHELL_E_NOT_FOUND;
}
ScStatus ScShellCatalogFind(const ScShellCatalog *c,const char *id,ScShellApplication **out) {
    ScU32 i;
    ScShellApplication *best=0;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(!out||!sh_name(id))return SC_SHELL_E_INVALID;
    for(i=0;i<c->app_count;++i)if(!strcmp(c->apps[i]->desc.id,id)&&(!best||c->apps[i]->info.layer>best->info.layer))best=c->apps[i];
    if(!best)return SC_SHELL_E_NOT_FOUND;
    s=ScShellApplicationRetain(best);
    if(s<0)return s;
    *out=best;
    return 0;
}
ScStatus ScShellCatalogApplications(const ScShellCatalog *c,ScU32 after,ScU32 epoch,ScShellApplicationInfo *rows,ScU32 capacity,ScShellPage *out) {
    ScShellPage p;
    ScU32 i;
    ScStatus s=sh_record(out,sizeof(*out));
    if(s<0)return s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(epoch&&epoch!=c->epoch)return SC_SHELL_E_STALE;
    if(!capacity||capacity>64||!rows)return SC_SHELL_E_RANGE;
    memset(&p,0,sizeof(p));
    p.size=sizeof(p);
    p.version=1;
    p.epoch=c->epoch;
    p.cursor=after;
    /* 注册序token单调，但replacement保token；page只读有限64个匹配项。 */ for(i=0;i<c->app_count;++i) {
        if(c->apps[i]->info.token<=after)continue;
        if(p.count==capacity)break;
        rows[p.count]=c->apps[i]->info;
        rows[p.count].epoch=c->epoch;
        p.cursor=c->apps[i]->info.token;
        ++p.count;
        ++p.examined;
    }
    if(i==c->app_count)p.flags=SC_SHELL_PAGE_FINISHED;
    return sh_output(out,&p,sizeof(p));
}
static int sh_verb_index(const ScShellApplication *a,const char *name,ScU32 *out) {
    ScHashEntry entry;
    ScStatus s;
    if(!name||!*name) {
        if(!a->desc.verb_count)return 0;
        *out=a->default_verb;
        return 1;
    }
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    s=ScHashGet(a->verb_index,name,(ScU32)strlen(name),&entry);
    if(s<0)return 0;
    memcpy(out,entry.value,4);
    return 1;
}
static ScStatus sh_type(const char *type,char **out) {
    ScStatus s;
    ScU32 i;
    if(type&&type[0]=='.')++type;
    s=sh_copy_text(type,255,0,out);
    if(s<0)return s;
    for(i=0;(*out)[i];++i) {
        unsigned char ch=(unsigned char)(*out)[i];
        if(ch>=128||(!isalnum(ch)&&ch!='_'&&ch!='-'&&ch!='+'&&ch!='.')) {
            free(*out);
            *out=0;
            return SC_SHELL_E_INVALID;
        }
        (*out)[i]=(char)tolower(ch);
    }
    return 0;
}
ScStatus ScShellCatalogSetAssociation(ScShellCatalog *c,const ScShellAssociationDesc *d,ScU32 epoch) {
    ShAssociation *a=0,**next;
    ScShellApplication *app=0;
    ScU32 i,index,count,verb;
    ScStatus s=sh_record(d,sizeof(*d));
    if(s<0)return s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    if(c->commit_lock)return SC_SHELL_E_BUSY;
    if(epoch!=c->epoch)return SC_SHELL_E_STALE;
    if(c->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    if(d->layer>2||d->flags||d->reserved[0]||d->reserved[1]||d->reserved[2]||d->reserved[3])return SC_SHELL_E_INVALID;
    s=ScShellCatalogFind(c,d->application,&app);
    if(s<0)return s;
    if(!sh_verb_index(app,d->verb,&verb)) {
        ScShellApplicationRelease(app);
        return SC_SHELL_E_NOT_FOUND;
    }
    ScShellApplicationRelease(app);
    a=(ShAssociation *)calloc(1,sizeof(*a));
    if(!a)return SC_SHELL_E_MEMORY;
    a->references=1;
    s=sh_type(d->type,&a->type);
    if(s<0)goto fail;
    s=sh_copy_text(d->application,255,0,&a->application);
    if(s<0)goto fail;
    s=sh_copy_text(d->verb,255,1,&a->verb);
    if(s<0)goto fail;
    a->layer=d->layer;
    a->flags=d->flags;
    if(a->layer==SC_SHELL_LAYER_SESSION) {
        ScBrokerIdentity self;
        s=sh_self(&self);
        if(s<0)goto fail;
        a->owner_pid=self.pid;
        a->owner_generation=self.generation;
    }
    index=c->association_count;
    for(i=0;i<c->association_count;++i)if(c->associations[i]->layer==a->layer&&!strcmp(c->associations[i]->type,a->type)&&!strcmp(c->associations[i]->verb,a->verb)) {
        index=i;
        break;
    }
    if(index==c->association_count) {
        if(d->expected_revision) {
            s=SC_SHELL_E_STALE;
            goto fail;
        }
        a->revision=1;
    } else {
        if(d->expected_revision!=c->associations[index]->revision) {
            s=SC_SHELL_E_STALE;
            goto fail;
        }
        if(c->associations[index]->revision==0xFFFFFFFFu) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        a->revision=c->associations[index]->revision+1;
    }
    count=c->association_count+(index==c->association_count);
    if(count<c->association_count||count>0xFFFFFFFFu/sizeof(*next)) {
        s=SC_SHELL_E_RANGE;
        goto fail;
    }
    next=(ShAssociation **)malloc(count*sizeof(*next));
    if(!next) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    if(c->association_count)memcpy(next,c->associations,c->association_count*sizeof(*next));
    next[index]=a;
    if(index<c->association_count)sh_association_release(c->associations[index]);
    free(c->associations);
    c->associations=next;
    c->association_count=count;
    ++c->epoch;
    return 0;
    fail:sh_association_release(a);
    return s;
}
ScStatus ScShellCatalogAssociation(const ScShellCatalog *c,const char *type,const char *verb,ScShellAssociationInfo *out) {
    char *t=0;
    ShAssociation *best=0;
    ScShellApplication *app=0;
    ScShellAssociationInfo info;
    ScU32 i,index;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    s=sh_record(out,sizeof(*out));
    if(s<0)return s;
    s=sh_type(type,&t);
    if(s<0)return s;
    if(!verb)verb="";
    for(i=0;i<c->association_count;++i) {
        ShAssociation *a=c->associations[i];
        if(!strcmp(a->type,t)&&!strcmp(a->verb,verb)&&(!best||a->layer>best->layer))best=a;
    }
    free(t);
    if(!best)return SC_SHELL_E_NOT_FOUND;
    s=ScShellCatalogFind(c,best->application,&app);
    if(s<0)return s;
    if(!sh_verb_index(app,best->verb,&index)) {
        ScShellApplicationRelease(app);
        return SC_SHELL_E_NOT_FOUND;
    }
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    info.revision=best->revision;
    info.layer=best->layer;
    info.flags=best->flags;
    info.application_token=app->info.token;
    info.verb_index=index;
    info.epoch=c->epoch;
    ScShellApplicationRelease(app);
    return sh_output(out,&info,sizeof(info));
}
ScStatus ScShellCatalogResolve(const ScShellCatalog *c,const char *path,const char *verb,ScShellApplication **out,ScU32 *verb_index) {
    ScPathStatInfo st;
    ScPathParts parts;
    ScShellAssociationInfo a;
    char type[256];
    ScU32 i;
    ScStatus s;
    if(!out||!verb_index)return SC_SHELL_E_INVALID;
    memset(&st,0,sizeof(st));
    st.size=sizeof(st);
    st.version=1;
    s=ScPathStat(path,&st);
    if(s<0)return sh_cause(s,11,0);
    if(st.kind==SC32_PATH_DIRECTORY)strcpy(type,"directory");
    else {
        memset(&parts,0,sizeof(parts));
        parts.size=sizeof(parts);
        parts.version=1;
        s=ScPathSplit(path,&parts);
        if(s<0)return sh_cause(s,11,0);
        if(!parts.extension_bytes||parts.extension_bytes>=sizeof(type))return SC_SHELL_E_NOT_FOUND;
        if(path[parts.extension_offset]=='.') {
            ++parts.extension_offset;
            --parts.extension_bytes;
        }
        for(i=0;i<parts.extension_bytes;++i)type[i]=(char)tolower((unsigned char)path[parts.extension_offset+i]);
        type[i]=0;
    }
    memset(&a,0,sizeof(a));
    a.size=sizeof(a);
    a.version=1;
    s=ScShellCatalogAssociation(c,type,verb,&a);
    if(s<0)return s;
    s=ScShellCatalogApplication(c,a.application_token,out);
    if(s>=0)*verb_index=a.verb_index;
    return s;
}
static void sh_layout_dispose(ShObject *base) {
    ScShellLayout *l=(ScShellLayout *)base;
    ScU32 i;
    for(i=0;i<l->info.count;++i)free((void *)l->items[i].identity);
    free(l->items);
}
ScStatus ScShellLayoutCreate(const ScShellLayoutItem *items,ScU32 count,ScU32 mode,ScU32 gw,ScU32 gh,ScShellLayout **out) {
    ScShellLayout *l;
    ScU32 i,marker=1;
    ScStatus s;
    ScHash *ids=0,*names=0;
    ScHashDesc hd;
    if(!out||(count&&!items)||count>0xFFFFFFFFu/sizeof(*items)||mode>2||!gw||!gh||gw>INT_MAX||gh>INT_MAX)return SC_SHELL_E_INVALID;
    l=(ScShellLayout *)sh_new(sizeof(*l),SH_LAYOUT,sh_layout_dispose);
    if(!l)return SC_SHELL_E_MEMORY;
    l->info.size=sizeof(l->info);
    l->info.version=1;
    l->info.revision=1;
    l->info.mode=mode;
    l->info.grid_width=gw;
    l->info.grid_height=gh;
    if(count) {
        l->items=(ScShellLayoutItem *)calloc(count,sizeof(*l->items));
        if(!l->items) {
            sh_release(&l->base);
            return SC_SHELL_E_MEMORY;
        }
    }
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=4;
    s=ScHashCreate(&hd,&ids);
    if(s>=0)s=ScHashCreate(&hd,&names);
    if(s<0)goto fail;
    for(i=0;i<count;++i) {
        s=sh_record(items+i,sizeof(*items));
        if(s<0)goto fail;
        if(!items[i].item||items[i].flags&~3u||items[i].width<=0||items[i].height<=0||items[i].reserved[0]||items[i].reserved[1]) {
            s=SC_SHELL_E_INVALID;
            goto fail;
        }
        s=sh_text(items[i].identity,1023,0);
        if(s<0)goto fail;
        s=ScHashSet(ids,&items[i].item,4,&marker,SC_HASH_INSERT_ONLY);
        if(s<0)goto fail;
        s=ScHashSet(names,items[i].identity,(ScU32)strlen(items[i].identity),&marker,SC_HASH_INSERT_ONLY);
        if(s<0)goto fail;
        l->items[i]=items[i];
        l->items[i].identity=0;
        ++l->info.count;
        s=sh_copy_text(items[i].identity,1023,0,(char **)&l->items[i].identity);
        if(s<0)goto fail;
        l->items[i].size=sizeof(*items);
        l->items[i].version=1;
    }
    ScHashRelease(ids);
    ScHashRelease(names);
    *out=l;
    return 0;
    fail:ScHashRelease(ids);
    ScHashRelease(names);
    sh_release(&l->base);
    return sh_cause(s,12,0);
}
ScStatus ScShellLayoutRetain(ScShellLayout *l) {
    return sh_retain((ShObject *)l,SH_LAYOUT);
}
void ScShellLayoutRelease(ScShellLayout *l) {
    if(l&&l->base.kind==SH_LAYOUT)sh_release(&l->base);
}
ScStatus ScShellLayoutQuery(const ScShellLayout *l,ScShellLayoutInfo *out) {
    if(!sh_valid(l,SH_LAYOUT))return SC_SHELL_E_STALE;
    return sh_output(out,&l->info,sizeof(l->info));
}
ScStatus ScShellLayoutGet(const ScShellLayout *l,ScU32 index,ScShellLayoutItem *out) {
    if(!sh_valid(l,SH_LAYOUT))return SC_SHELL_E_STALE;
    if(index>=l->info.count)return SC_SHELL_END;
    return sh_output(out,l->items+index,sizeof(*out));
}
static int sh_layout_order(const void *x,const void *y) {
    const ScShellLayoutItem *a=(const ScShellLayoutItem *)x,*b=(const ScShellLayoutItem *)y;
    if(a->order!=b->order)return a->order<b->order?-1:1;
    return a->item<b->item?-1:(a->item>b->item);
}
ScStatus ScShellLayoutArrange(const ScShellLayout *l,const ScUiRect *work,ScU32 mode,ScShellLayout **out) {
    ScShellLayout *n=0;
    ScU32 i,cols;
    ScStatus s;
    if(!sh_valid(l,SH_LAYOUT))return SC_SHELL_E_STALE;
    if(!work||work->width<=0||work->height<=0||mode>2||!out)return SC_SHELL_E_INVALID;
    s=ScShellLayoutCreate(l->items,l->info.count,mode,l->info.grid_width,l->info.grid_height,&n);
    if(s<0)return s;
    if(l->info.revision==0xFFFFFFFFu) {
        ScShellLayoutRelease(n);
        return SC_SHELL_E_RANGE;
    }
    n->info.revision=l->info.revision+1;
    if(mode==SC_SHELL_LAYOUT_AUTO) {
        qsort(n->items,n->info.count,sizeof(*n->items),sh_layout_order);
        cols=(ScU32)work->width/l->info.grid_width;
        if(!cols)cols=1;
        for(i=0;i<n->info.count;++i) {
            ScU32 col=i%cols,row=i/cols;
            if(row>(ScU32)INT_MAX/l->info.grid_height||col>(ScU32)INT_MAX/l->info.grid_width) {
                s=SC_SHELL_E_RANGE;
                goto fail;
            }
            if(work->x>INT_MAX-(ScI32)(col*l->info.grid_width)||work->y>INT_MAX-(ScI32)(row*l->info.grid_height)) {
                s=SC_SHELL_E_RANGE;
                goto fail;
            }
            n->items[i].x=work->x+(ScI32)(col*l->info.grid_width);
            n->items[i].y=work->y+(ScI32)(row*l->info.grid_height);
            n->items[i].order=i;
        }
    } else if(mode==SC_SHELL_LAYOUT_GRID) {
        for(i=0;i<n->info.count;++i) {
            ScI32 x=n->items[i].x,y=n->items[i].y;
            /* 整数除法向零；负坐标仍保持有效，不以显示clamp覆盖保存原值。 */ n->items[i].x=(x/(ScI32)l->info.grid_width)*(ScI32)l->info.grid_width;
            n->items[i].y=(y/(ScI32)l->info.grid_height)*(ScI32)l->info.grid_height;
        }
    }
    *out=n;
    return 0;
    fail:ScShellLayoutRelease(n);
    return s;
}
ScStatus ScShellCatalogLayout(const ScShellCatalog *c,ScShellLayout **out) {
    ScStatus s;
    if(!out)return SC_SHELL_E_INVALID;
    if(!sh_valid(c,SH_CATALOG))return SC_SHELL_E_STALE;
    s=ScShellLayoutRetain(c->layout);
    if(s<0)return s;
    *out=c->layout;
    return 0;
}
ScStatus ScShellCatalogApplyLayout(ScShellCatalog *c,ScShellLayout *l,ScU32 epoch) {
    ScShellLayout *old,*candidate=0;
    ScStatus s;
    if(!sh_valid(c,SH_CATALOG)||!sh_valid(l,SH_LAYOUT))return SC_SHELL_E_STALE;
    if(c->commit_lock)return SC_SHELL_E_BUSY;
    if(epoch!=c->epoch)return SC_SHELL_E_STALE;
    if(c->epoch==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    if(c->layout->info.revision==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    s=ScShellLayoutCreate(l->items,l->info.count,l->info.mode,l->info.grid_width,l->info.grid_height,&candidate);
    if(s<0)return s;
    candidate->info.revision=c->layout->info.revision+1;
    old=c->layout;
    c->layout=candidate;
    ++c->epoch;
    ScShellLayoutRelease(old);
    return 0;
}
