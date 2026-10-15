typedef struct {
    ScU32 command;
    ScShellApplication *application;
    ScU32 verb;
}
ShMenuCommand;
typedef struct ShCategory ShCategory;
struct ShCategory {
    ShCategory *next;
    char *name;
    ScUiMenu *menu;
};
static void sh_menu_provider_release(ShProvider *p) {
    if(!p||!p->references)return;
    if(--p->references)return;
    if(!sh_quiet&&p->desc.context_drop)p->desc.context_drop(p->desc.context,0);
    free(p);
}
ScStatus ScShellMenuRegisterProvider(const ScShellMenuProviderDesc *d,ScU32 *out) {
    ShProvider *p;
    ScStatus s=sh_record(d,sizeof(*d));
    ScU32 i;
    if(s<0)return s;
    if(!out||!d->build||!d->execute||d->scope<1||d->scope>3||d->flags||!sh_provider_next||sh_provider_next==0xFFFFFFFFu)return SC_SHELL_E_INVALID;
    for(i=0;i<4;++i)if(d->reserved[i])return SC_SHELL_E_INVALID;
    p=(ShProvider *)calloc(1,sizeof(*p));
    if(!p)return SC_SHELL_E_MEMORY;
    s=ScCallbackPin((const void *)d->build);
    if(s>=0)s=sh_pin(d->execute,d->context_drop);
    if(s<0) {
        free(p);
        return sh_cause(s,100,0);
    }
    p->desc=*d;
    p->token=sh_provider_next++;
    p->references=1;
    p->next=sh_providers;
    sh_providers=p;
    *out=p->token;
    return 0;
}
ScStatus ScShellMenuUnregisterProvider(ScU32 token) {
    ShProvider **link=&sh_providers;
    while(*link&&(*link)->token!=token)link=&(*link)->next;
    if(!*link)return SC_SHELL_E_NOT_FOUND;
    {
        ShProvider *p=*link;
        *link=p->next;
        p->next=0;
        p->removed=1;
        sh_menu_provider_release(p);
    }
    return 0;
}
static void sh_menu_dispose(ShObject *base) {
    ScShellMenu *m=(ScShellMenu *)base;
    ScU32 i;
    if(m->dialog)ScShellDialogClose(m->dialog);
    if(m->job)sh_release(&m->job->base);
    if(!sh_quiet) {
        if(m->prompt)ScUiDialogRelease(m->prompt);
        if(m->conflict_prompt)ScUiDialogRelease(m->conflict_prompt);
        ScUiMenuRelease(m->menu);
        if(m->desc.ui)ScUiContextRelease(m->desc.ui);
    }
    if(m->commands) {
        ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        if(ScArrayQuery(m->commands,&info)>=0)for(i=0;i<info.length;++i) {
            ShMenuCommand c;
            if(ScArrayGet(m->commands,i,&c)>=0)ScShellApplicationRelease(c.application);
        }
    }
    ScArrayRelease(m->commands);
    for(i=0;i<m->provider_count;++i)sh_menu_provider_release(m->providers[i]);
    free(m->providers);
    ScShellCatalogRelease(m->desc.catalog);
    sh_file_items_free(m->items,m->desc.count);
    free(m->directory);
    free(m->recovery);
    if(!sh_quiet&&m->desc.context_drop)m->desc.context_drop(m->desc.context,0);
}
static ScStatus sh_menu_item(ScUiMenu *menu,const char *label,ScU32 command,ScU32 flags,ScUiIcon *icon,ScUiMenu *submenu) {
    ScUiMenuItem item;
    ScU32 token;
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.command=command;
    item.flags=flags;
    item.icon=icon;
    item.submenu=submenu;
    sh_ui_text(label,&item.label);
    return sh_cause(ScUiMenuInsert(menu,0,&item,&token),101,0);
}
static ScStatus sh_categories(ScHash *lookup,ShCategory **owned,ScUiMenu *root,const char *path,ScUiMenu **out) {
    ScUiMenu *parent=root;
    ScU32 at=0,start;
    char prefix[256],label[256];
    ScStatus s;
    ScHashEntry entry;
    while(path[at]) {
        start=at;
        while(path[at]&&path[at]!='/')++at;
        if(at==start||at>=sizeof(prefix))return SC_SHELL_E_INVALID;
        memcpy(prefix,path,at);
        prefix[at]=0;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        s=ScHashGet(lookup,prefix,at,&entry);
        if(s>=0) {
            ShCategory *found=*(ShCategory *const *)entry.value;
            parent=found->menu;
        } else if(s==SC_UTIL_E_NOT_FOUND) {
            ShCategory *category=(ShCategory *)calloc(1,sizeof(*category));
            if(!category)return SC_SHELL_E_MEMORY;
            memcpy(label,path+start,at-start);
            label[at-start]=0;
            s=sh_copy_text(prefix,255,0,&category->name);
            if(s>=0)s=ScUiMenuCreate(&category->menu);
            if(s>=0)s=sh_menu_item(parent,label,0,0,0,category->menu);
            if(s>=0)s=ScHashSet(lookup,prefix,at,&category,SC_HASH_INSERT_ONLY);
            if(s<0) {
                ScUiMenuRelease(category->menu);
                free(category->name);
                free(category);
                return sh_cause(s,101,0);
            }
            category->next=*owned;
            *owned=category;
            parent=category->menu;
        } else return sh_cause(s,101,0);
        if(path[at]) {
            ++at;
            if(!path[at])return SC_SHELL_E_INVALID;
        }
    }
    *out=parent;
    return 0;
}
static int sh_application_order(const void *left,const void *right) {
    const ScShellApplication *a=*(ScShellApplication *const *)left,*b=*(ScShellApplication *const *)right;
    int c=strcmp(a->desc.label,b->desc.label);
    return c?c:strcmp(a->desc.id,b->desc.id);
}
static ScStatus sh_menu_applications(ScShellMenu *m,ScUiMenu *root,ScU32 categorized,ScU32 document) {
    ScHash *shadow=0,*categories=0;
    ScHashDesc hd;
    ShCategory *owned=0;
    ScShellApplication **apps=0;
    ScU32 count=0,i;
    ScStatus s=0;
    ScShellCatalog *catalog=m->desc.catalog;
    if(!catalog)return 0;
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=sizeof(void *);
    s=ScHashCreate(&hd,&shadow);
    if(s>=0)s=ScHashCreate(&hd,&categories);
    if(s<0)goto done;
    if(catalog->app_count) {
        apps=(ScShellApplication **)malloc(catalog->app_count*sizeof(*apps));
        if(!apps) {
            s=SC_SHELL_E_MEMORY;
            goto done;
        }
    }
    for(i=0;i<catalog->app_count;++i) {
        ScShellApplication *a=catalog->apps[i];
        ScHashEntry e;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        s=ScHashGet(shadow,a->desc.id,(ScU32)strlen(a->desc.id),&e);
        if(s==SC_UTIL_E_NOT_FOUND||(s>=0&&(*(ScShellApplication *const *)e.value)->info.layer<a->info.layer)) s=ScHashSet(shadow,a->desc.id,(ScU32)strlen(a->desc.id),&a,0);
        if(s<0)goto done;
    }
    for(i=0;i<catalog->app_count;++i) {
        ScShellApplication *a=catalog->apps[i];
        ScHashEntry e;
        memset(&e,0,sizeof(e));
        e.size=sizeof(e);
        e.version=1;
        s=ScHashGet(shadow,a->desc.id,(ScU32)strlen(a->desc.id),&e);
        if(s<0)goto done;
        if(*(ScShellApplication *const *)e.value==a&&!(a->info.flags&SC_SHELL_APP_HIDDEN))apps[count++]=a;
    }
    qsort(apps,count,sizeof(*apps),sh_application_order);
    for(i=0;i<count;++i) {
        ScShellApplication *a=apps[i];
        ScUiMenu *parent=root;
        ScUiIcon *icon=0;
        ShMenuCommand command;
        ScContainerInfo info;
        ScU32 verb;
        if(!sh_verb_index(a,"",&verb))continue;
        if(document&&(a->verbs[verb].flags&SC_SHELL_VERB_NO_DOCUMENT))continue;
        if(categorized) {
            s=sh_categories(categories,&owned,root,a->desc.category,&parent);
            if(s<0)goto done;
        }
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScArrayQuery(m->commands,&info);
        if(s<0)goto done;
        if(info.length>0x7FFF0000u) {
            s=SC_SHELL_E_RANGE;
            goto done;
        }
        command.command=0x10000u+info.length;
        command.application=a;
        command.verb=verb;
        s=ScShellApplicationRetain(a);
        if(s<0)goto done;
        s=ScArrayInsert(m->commands,info.length,&command,1);
        if(s<0) {
            ScShellApplicationRelease(a);
            goto done;
        }
        s=ScShellIconLoad(m->desc.ui,a->desc.icon,a->desc.executable,&icon);
        if(s<0)goto done;
        s=sh_menu_item(parent,a->desc.label,command.command,(a->info.flags&SC_SHELL_APP_DISABLED)?SC_UI_MENU_DISABLED:0,icon,0);
        ScUiIconRelease(icon);
        if(s<0)goto done;
    }
    done:while(owned) {
        ShCategory *next=owned->next;
        ScUiMenuRelease(owned->menu);
        free(owned->name);
        free(owned);
        owned=next;
    }
    ScHashRelease(shadow);
    ScHashRelease(categories);
    free(apps);
    return sh_cause(s,102,0);
}
ScStatus ScShellMenuBuild(const ScShellMenuDesc *d,ScShellMenu **out) {
    ScShellMenu *m=0;
    ScArrayDesc ad;
    ScUiMenu *open_with=0;
    ScStatus s=sh_record(d,sizeof(*d));
    ShProvider *p;
    ScU32 count=0,i;
    if(s<0)return s;
    if(!out||!d->ui||!d->owner||d->kind<1||d->kind>3||d->flags||d->reserved)return SC_SHELL_E_INVALID;
    if(d->catalog&&!sh_valid(d->catalog,SH_CATALOG))return SC_SHELL_E_STALE;
    s=sh_pin(d->procedure,d->context_drop);
    if(s<0)return s;
    m=(ScShellMenu *)sh_new(sizeof(*m),SH_MENU,sh_menu_dispose);
    if(!m)return SC_SHELL_E_MEMORY;
    m->desc=*d;
    m->desc.ui=0;
    m->desc.catalog=0;
    m->desc.items=0;
    m->desc.count=0;
    m->desc.context_drop=0;
    s=ScUiContextRetain(d->ui);
    if(s<0)goto fail;
    m->desc.ui=d->ui;
    if(d->catalog) {
        s=ScShellCatalogRetain(d->catalog);
        if(s<0)goto fail;
        m->desc.catalog=d->catalog;
        m->catalog_epoch=d->catalog->epoch;
    }
    if(d->count) {
        s=sh_file_items(d->items,d->count,&m->items);
        if(s<0)goto fail;
        m->desc.items=m->items;
        m->desc.count=d->count;
        for(i=0;i<d->count;++i)if(!m->items[i].source) {
            s=SC_SHELL_E_INVALID;
            goto fail;
        }
    }
    if(d->directory&&*d->directory) {
        s=sh_absolute(d->directory,&m->directory);
        if(s<0)goto fail;
        m->desc.directory=m->directory;
    }
    if(d->recovery_directory&&*d->recovery_directory) {
        s=sh_absolute(d->recovery_directory,&m->recovery);
        if(s<0)goto fail;
        m->desc.recovery_directory=m->recovery;
    }
    s=ScUiMenuCreate(&m->menu);
    if(s<0)goto fail;
    memset(&ad,0,sizeof(ad));
    ad.size=sizeof(ad);
    ad.version=1;
    ad.item_bytes=sizeof(ShMenuCommand);
    s=ScArrayCreate(&ad,&m->commands);
    if(s<0)goto fail;
    if(d->kind==SC_SHELL_MENU_START) {
        s=sh_menu_applications(m,m->menu,1,0);
        if(s<0)goto fail;
    } else {
        ScU32 selection=d->count?0:SC_UI_MENU_DISABLED;
        s=sh_menu_item(m->menu,"Open",SC_SHELL_COMMAND_OPEN,selection,0,0);
        if(s<0)goto fail;
        s=ScUiMenuCreate(&open_with);
        if(s>=0)s=sh_menu_applications(m,open_with,1,1);
        if(s>=0)s=sh_menu_item(m->menu,"Open with",0,selection,0,open_with);
        ScUiMenuRelease(open_with);
        open_with=0;
        if(s<0)goto fail;
        s=sh_menu_item(m->menu,"",0,SC_UI_MENU_SEPARATOR,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Copy",SC_SHELL_COMMAND_COPY,selection,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Cut",SC_SHELL_COMMAND_CUT,selection,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Paste",SC_SHELL_COMMAND_PASTE,m->directory?0:SC_UI_MENU_DISABLED,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"",0,SC_UI_MENU_SEPARATOR,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Rename",SC_SHELL_COMMAND_RENAME,d->count==1?0:SC_UI_MENU_DISABLED,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Delete",SC_SHELL_COMMAND_DELETE,selection,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"New directory",SC_SHELL_COMMAND_NEW_DIRECTORY,m->directory?0:SC_UI_MENU_DISABLED,0,0);
        if(s>=0)s=sh_menu_item(m->menu,"Properties",SC_SHELL_COMMAND_PROPERTIES,d->count==1?0:SC_UI_MENU_DISABLED,0,0);
        if(s<0)goto fail;
    }
    /* 入口水位快照持provider引用；build内增删只影响下次菜单。 */ for(p=sh_providers;p;p=p->next)if(!p->removed&&p->desc.scope==d->kind)++count;
    if(count) {
        m->providers=(ShProvider **)calloc(count,sizeof(*m->providers));
        if(!m->providers) {
            s=SC_SHELL_E_MEMORY;
            goto fail;
        }
    }
    for(p=sh_providers;p;p=p->next)if(!p->removed&&p->desc.scope==d->kind) {
        if(p->references==0xFFFFFFFFu) {
            s=SC_SHELL_E_RANGE;
            goto fail;
        }
        ++p->references;
        m->providers[m->provider_count++]=p;
    }
    for(i=0;i<m->provider_count;++i) {
        p=m->providers[i];
        s=p->desc.build(&m->desc,m->menu,p->desc.context);
        if(s<0)goto fail;
    }
    m->desc.context_drop=d->context_drop;
    *out=m;
    return 0;
    fail:ScUiMenuRelease(open_with);
    sh_release(&m->base);
    return sh_cause(s,103,0);
}
ScStatus ScShellMenuNative(ScShellMenu *m,ScUiMenu **out) {
    ScStatus s;
    if(!sh_valid(m,SH_MENU))return SC_SHELL_E_STALE;
    if(!out)return SC_SHELL_E_INVALID;
    s=ScUiMenuRetain(m->menu);
    if(s<0)return sh_cause(s,104,0);
    *out=m->menu;
    return 0;
}
static ScStatus sh_menu_notice(ScShellMenu *m,ScU32 command,ScStatus status) {
    ScShellEvent e;
    if(!m->desc.procedure||sh_quiet)return 0;
    memset(&e,0,sizeof(e));
    e.size=sizeof(e);
    e.version=1;
    e.type=SC_SHELL_EVENT_COMMAND;
    e.command=command;
    e.status=status;
    e.epoch=m->catalog_epoch;
    e.data=m->items;
    e.bytes=m->desc.count*sizeof(*m->items);
    return m->desc.procedure(&e,m->desc.context);
}
static ScStatus sh_menu_job_event(const ScShellEvent *event,void *context) {
    ScShellMenu *m=(ScShellMenu *)context;
    ScStatus s=0;
    if(!sh_valid(m,SH_MENU)||sh_quiet)return 0;
    sh_enter(&m->base);
    if(m->desc.procedure)s=m->desc.procedure(event,m->desc.context);
    sh_leave(&m->base);
    return s;
}
static ScStatus sh_menu_recovery(ScShellMenu *m,char **out) {
    char leaf[32];
    ScString *joined=0;
    const char *text;
    ScStatus s;
    if(!m->recovery)return SC_SHELL_E_INVALID;
    snprintf(leaf,sizeof(leaf),"J%08X%08X.REC",sh_job_next.hi,sh_job_next.lo);
    s=ScPathJoin(m->recovery,leaf,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&joined);
    if(s>=0)s=ScStringCStr(joined,&text);
    if(s>=0)s=sh_absolute(text,out);
    ScStringRelease(joined);
    return sh_cause(s,105,0);
}
static ScStatus sh_menu_job_start(ScShellMenu *m,ScU32 operation,const ScShellFileItem *items,ScU32 count,const char *destination) {
    ScShellFileJobDesc d;
    char *recovery=0;
    ScStatus s=sh_menu_recovery(m,&recovery);
    if(s<0)return s;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.operation=operation;
    d.items=items;
    d.count=count;
    d.destination=destination;
    d.flags=SC_SHELL_RECURSIVE;
    d.final_mode=3;
    d.recovery_path=recovery;
    d.procedure=sh_menu_job_event;
    d.context=m;
    s=ScShellFileOperationBegin(&d,&m->job);
    free(recovery);
    if(s<0)return s;
    s=ScShellProgress(m->desc.ui,m->desc.owner,m->job,&m->dialog);
    if(s<0) {
        ScShellFileOperationClose(m->job);
        m->job=0;
    }
    return s;
}
static ScStatus sh_menu_name_prompt(ScShellMenu *m,ScU32 command) {
    ScConfig *initial=0;
    ScValue v;
    ScUiFieldDesc field;
    ScUiPropertySheetDesc d;
    ScStatus s;
    const char *name="New directory";
    if(command==SC_SHELL_COMMAND_RENAME) {
        name=strrchr(m->items[0].source,'/');
        name=name?name+1:m->items[0].source;
    } else if(command==0x7300) {
        name=strrchr(m->conflict_snapshot.destination,'/');
        name=name?name+1:m->conflict_snapshot.destination;
    }
    s=ScConfigCreate(&initial);
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.type=SC_VALUE_UTF8;
    v.data=name;
    v.bytes=(ScU32)strlen(name);
    if(s>=0)s=ScConfigSet(initial,"operation","name",&v);
    if(s<0)goto done;
    memset(&field,0,sizeof(field));
    field.size=sizeof(field);
    field.version=1;
    field.field=1;
    field.kind=SC_UI_TEXTBOX;
    field.flags=SC_UI_FIELD_REQUIRED;
    field.page=1;
    field.section="operation";
    field.key="name";
    sh_ui_text("Name",&field.label);
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=m->dialog&&m->dialog->root?m->dialog->root:m->desc.owner;
    d.initial=initial;
    sh_ui_text(command==SC_SHELL_COMMAND_RENAME||command==0x7300?"Rename":"New directory",&d.title);
    s=ScUiPropertySheet(m->desc.ui,&d,&field,1,&m->prompt);
    if(s>=0)m->prompt_kind=command;
    done:ScConfigRelease(initial);
    return sh_cause(s,106,0);
}
static ScStatus sh_menu_name_commit(ScShellMenu *m) {
    ScConfig *values=0;
    ScValue v;
    ScString *joined=0;
    const char *text;
    char *name=0,*base=0;
    ScShellFileItem item;
    ScU32 i;
    ScStatus s;
    s=ScUiDialogCopyValues(m->prompt,&values);
    if(s<0)goto done;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    s=ScConfigGet(values,"operation","name",&v);
    if(s<0)goto done;
    if(v.type!=SC_VALUE_UTF8||!v.bytes||v.bytes>63||memchr(v.data,0,v.bytes)) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    name=(char *)malloc(v.bytes+1);
    if(!name) {
        s=SC_SHELL_E_MEMORY;
        goto done;
    }
    memcpy(name,v.data,v.bytes);
    name[v.bytes]=0;
    if(!strcmp(name,".")||!strcmp(name,"..")) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    for(i=0;i<v.bytes;++i)if((unsigned char)name[i]<33||name[i]=='/'||name[i]=='\\'||name[i]==':') {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.item=1;
    if(m->prompt_kind==SC_SHELL_COMMAND_RENAME||m->prompt_kind==0x7300) {
        char *slash;
        s=sh_copy_text(m->prompt_kind==0x7300?m->conflict_snapshot.destination:m->items[0].source,64,0,&base);
        if(s<0)goto done;
        slash=strrchr(base,'/');
        if(!slash) {
            s=SC_SHELL_E_INVALID;
            goto done;
        }
        slash[1]=0;
        if(m->prompt_kind!=0x7300)item=m->items[0];
    } else {
        if(!m->directory) {
            s=SC_SHELL_E_INVALID;
            goto done;
        }
        s=sh_copy_text(m->directory,64,0,&base);
        if(s<0)goto done;
        item.kind=SC32_PATH_DIRECTORY;
    }
    s=ScPathJoin(base,name,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&joined);
    if(s>=0)s=ScStringCStr(joined,&text);
    if(s<0)goto done;
    item.destination=text;
    if(m->prompt_kind==0x7300) {
        ScShellConflictDecision decision;
        memset(&decision,0,sizeof(decision));
        decision.size=sizeof(decision);
        decision.version=1;
        decision.item=m->conflict_snapshot.item;
        decision.conflict_epoch=m->conflict_snapshot.conflict_epoch;
        decision.decision=SC_SHELL_RENAME_TARGET;
        decision.expected_source_revision=m->conflict_snapshot.source_revision;
        decision.expected_destination_revision=m->conflict_snapshot.destination_revision;
        decision.new_path=text;
        s=ScShellFileOperationResolveConflict(m->job,&decision);
    } else s=sh_menu_job_start(m,m->prompt_kind==SC_SHELL_COMMAND_RENAME?SC_SHELL_RENAME:SC_SHELL_MKDIR,&item,1,0);
    done:free(name);
    free(base);
    ScStringRelease(joined);
    ScConfigRelease(values);
    return sh_cause(s,107,0);
}
ScStatus ScShellMenuExecute(ScShellMenu *m,ScU32 command) {
    ScStatus s=0;
    ScU32 i;
    ScContainerInfo info;
    if(!sh_valid(m,SH_MENU))return SC_SHELL_E_STALE;
    if(m->base.depth)return SC_SHELL_E_BUSY;
    if(m->job||m->dialog||m->prompt)return SC_SHELL_E_BUSY;
    sh_enter(&m->base);
    s=sh_menu_notice(m,command,0);
    if(s||m->base.closing)goto done;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScArrayQuery(m->commands,&info);
    if(s<0)goto done;
    for(i=0;i<info.length;++i) {
        ShMenuCommand c;
        s=ScArrayGet(m->commands,i,&c);
        if(s<0)goto done;
        if(c.command==command) {
            ScLaunchInfo launched;
            if(m->desc.catalog&&m->catalog_epoch!=m->desc.catalog->epoch) {
                s=SC_SHELL_E_STALE;
                goto done;
            }
            memset(&launched,0,sizeof(launched));
            launched.size=sizeof(launched);
            launched.version=1;
            if(m->desc.kind==SC_SHELL_MENU_START)s=ScShellLaunch(c.application,c.verb,0,&launched);
            else {
                for(i=0;i<m->desc.count;++i) {
                    s=ScShellLaunch(c.application,c.verb,m->items[i].source,&launched);
                    if(s<0)break;
                }
            }
            goto done;
        }
    }
    if(command==SC_SHELL_COMMAND_OPEN) {
        ScLaunchInfo launched;
        if(!m->desc.count) {
            s=SC_SHELL_E_INVALID;
            goto done;
        }
        if(!m->desc.catalog) {
            s=SC_SHELL_E_NOT_FOUND;
            goto done;
        }
        for(i=0;i<m->desc.count;++i) {
            memset(&launched,0,sizeof(launched));
            launched.size=sizeof(launched);
            launched.version=1;
            s=ScShellOpen(m->desc.catalog,m->items[i].source,"open",&launched);
            if(s<0)break;
        }
    } else if(command==SC_SHELL_COMMAND_COPY||command==SC_SHELL_COMMAND_CUT) {
        ScClipboardInfo clip;
        memset(&clip,0,sizeof(clip));
        clip.size=sizeof(clip);
        clip.version=1;
        s=ScClipboardQuery(&clip);
        if(s>=0)s=command==SC_SHELL_COMMAND_COPY?ScShellClipboardCopyFiles(m->items,m->desc.count,clip.epoch,&clip):ScShellClipboardCutFiles(m->items,m->desc.count,clip.epoch,&clip);
    } else if(command==SC_SHELL_COMMAND_PASTE) {
        char *recovery=0;
        s=sh_menu_recovery(m,&recovery);
        if(s>=0)s=ScShellClipboardPaste(m->directory,recovery,sh_menu_job_event,m,&m->job);
        free(recovery);
        if(s>=0)s=ScShellProgress(m->desc.ui,m->desc.owner,m->job,&m->dialog);
    } else if(command==SC_SHELL_COMMAND_DELETE) {
        if(!m->desc.count)s=SC_SHELL_E_INVALID;
        else s=sh_menu_confirm_delete(m);
    } else if(command==SC_SHELL_COMMAND_RENAME) {
        if(m->desc.count!=1)s=SC_SHELL_E_INVALID;
        else s=sh_menu_name_prompt(m,command);
    } else if(command==SC_SHELL_COMMAND_NEW_DIRECTORY)s=m->directory?sh_menu_name_prompt(m,command):SC_SHELL_E_INVALID;
    else if(command==SC_SHELL_COMMAND_PROPERTIES) {
        s=m->desc.count==1?ScShellProperties(m->desc.ui,m->desc.owner,m->items[0].source,&m->dialog):SC_SHELL_E_INVALID;
    } else {
        s=SC_SHELL_E_NOT_FOUND;
        for(i=0;i<m->provider_count;++i) {
            ShProvider *p=m->providers[i];
            ScShellEvent e;
            ScStatus x;
            if(p->removed)continue;
            memset(&e,0,sizeof(e));
            e.size=sizeof(e);
            e.version=1;
            e.type=SC_SHELL_EVENT_COMMAND;
            e.command=command;
            e.data=m->items;
            e.bytes=m->desc.count*sizeof(*m->items);
            x=p->desc.execute(&e,p->desc.context);
            if(x<0) {
                s=x;
                break;
            }
            if(x>0) {
                s=0;
                break;
            }
            if(m->base.closing)break;
        }
    }
    done:if(s>0)s=0;
    if(s<0)s=sh_cause(s,108,command);
    sh_leave(&m->base);
    return s;
}
static ScStatus sh_menu_conflict_open(ScShellMenu *m) {
    ScUiMessageBoxDesc d;
    ScUiMenuItem buttons[6];
    ScString *message=0;
    const char *text;
    ScStatus s;
    ScU32 i;
    static const char *labels[6]= {
        "Replace","Skip","Rename","Merge","Retry","Cancel job"
    };
    if(!m->job||!m->job->conflict_valid||m->conflict_prompt||m->prompt)return 0;
    m->conflict_snapshot=m->job->conflict;
    s=ScStringCreate(0,0,&message);
    if(s>=0)s=ScStringFormat(message,"Source: %s\nDestination: %s\nError %d  Cause %d  Raw %d\nThe selected action applies only to this observed conflict.",m->conflict_snapshot.source,m->conflict_snapshot.destination,m->conflict_snapshot.status,m->conflict_snapshot.cause,m->conflict_snapshot.raw);
    if(s>=0)s=ScStringCStr(message,&text);
    if(s<0) {
        ScStringRelease(message);
        return sh_cause(s,109,0);
    }
    memset(&d,0,sizeof(d));
    memset(buttons,0,sizeof(buttons));
    for(i=0;i<6;++i) {
        buttons[i].size=sizeof(*buttons);
        buttons[i].version=1;
        buttons[i].command=i+1;
        sh_ui_text(labels[i],&buttons[i].label);
    }
    {
        ScPathInfo source,target;
        memset(&source,0,sizeof(source));
        source.size=sizeof(source);
        source.version=1;
        memset(&target,0,sizeof(target));
        target.size=sizeof(target);
        target.version=1;
        if(ScPathQuery(m->conflict_snapshot.source,&source)<0||ScPathQuery(m->conflict_snapshot.destination,&target)<0||source.kind!=SC32_PATH_DIRECTORY||target.kind!=SC32_PATH_DIRECTORY)buttons[3].flags=SC_UI_MENU_DISABLED;
    }
    d.size=sizeof(d);
    d.version=1;
    d.owner=m->dialog&&m->dialog->root?m->dialog->root:m->desc.owner;
    sh_ui_text("File operation conflict",&d.title);
    sh_ui_text(text,&d.message);
    d.buttons=buttons;
    d.button_count=6;
    d.default_command=SC_SHELL_SKIP;
    d.cancel_command=SC_SHELL_CANCEL;
    s=ScUiMessageBox(m->desc.ui,&d,&m->conflict_prompt);
    ScStringRelease(message);
    return sh_cause(s,109,0);
}
static ScStatus sh_menu_conflict_step(ScShellMenu *m) {
    ScUiDialogInfo info;
    ScShellConflictDecision decision;
    ScStatus s;
    if(!m->conflict_prompt)return 0;
    if(!m->job||m->job->closed||!m->job->conflict_valid) {
        ScUiDialogRelease(m->conflict_prompt);
        m->conflict_prompt=0;
        return 0;
    }
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScUiDialogQuery(m->conflict_prompt,&info);
    if(s<0)return sh_cause(s,110,0);
    if(info.stage!=SC_UI_DIALOG_DONE&&info.stage!=SC_UI_DIALOG_CANCELLED)return 0;
    if(info.command==SC_SHELL_RENAME_TARGET) {
        s=sh_menu_name_prompt(m,0x7300);
        if(s<0)return s;
    } else {
        memset(&decision,0,sizeof(decision));
        decision.size=sizeof(decision);
        decision.version=1;
        decision.item=m->conflict_snapshot.item;
        decision.conflict_epoch=m->conflict_snapshot.conflict_epoch;
        decision.decision=info.stage==SC_UI_DIALOG_CANCELLED?SC_SHELL_CANCEL:info.command;
        decision.expected_source_revision=m->conflict_snapshot.source_revision;
        decision.expected_destination_revision=m->conflict_snapshot.destination_revision;
        s=ScShellFileOperationResolveConflict(m->job,&decision);
        if(s==SC_SHELL_E_BUSY)return 0;
        if(s<0)return s;
    }
    ScUiDialogRelease(m->conflict_prompt);
    m->conflict_prompt=0;
    return 0;
}
ScStatus ScShellMenuStep(ScShellMenu *m,ScU32 timeout) {
    ScStatus s=0;
    ScU32 ready=0;
    if(!sh_valid(m,SH_MENU))return SC_SHELL_E_STALE;
    if(m->base.depth)return SC_SHELL_E_BUSY;
    sh_enter(&m->base);
    if(m->prompt) {
        ScUiDialogInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScUiDialogQuery(m->prompt,&info);
        if(s>=0&&(info.stage==SC_UI_DIALOG_DONE||info.stage==SC_UI_DIALOG_CANCELLED)) {
            if(info.stage==SC_UI_DIALOG_DONE&&info.command==SC_UI_DIALOG_OK)s=sh_menu_name_commit(m);
            ScUiDialogRelease(m->prompt);
            m->prompt=0;
            m->prompt_kind=0;
        }
    }
    if(s>=0&&m->dialog) {
        ScStatus x=ScShellDialogStep(m->dialog);
        if(x==SC_SHELL_READY)ready=1;
        if(x<0)s=x;
        else if(x==SC_SHELL_END) {
            ScU32 chosen=m->dialog->command,kind=m->dialog->kind;
            ScShellDialogClose(m->dialog);
            m->dialog=0;
            if(kind==1&&chosen==SC_UI_DIALOG_OK)s=sh_menu_job_start(m,SC_SHELL_DELETE,m->items,m->desc.count,0);
        }
    }
    if(s>=0&&m->job&&!m->job->closed) {
        s=ScShellFileOperationStep(m->job,timeout);
        if(s==SC_SHELL_READY)ready=1;
    }
    if(s>=0&&!m->base.closing)s=sh_menu_conflict_step(m);
    if(s>=0&&!m->base.closing)s=sh_menu_conflict_open(m);
    if(s>=0&&m->job&&m->job->closed) {
        if(m->job->clip_operation==SC32_CLIPBOARD_CUT) {
            ScClipboardInfo clip;
            memset(&clip,0,sizeof(clip));
            clip.size=sizeof(clip);
            clip.version=1;
            ScStatus x=ScShellClipboardReconcile(m->job,&clip);
            if(x<0&&x!=SC_SHELL_E_STALE)s=x;
        }
        if(m->dialog&&m->dialog->kind==2&&m->job->progress.state==SC_SHELL_JOB_FINISHED&&!m->job->progress.items_failed&&m->job->progress.status>=0) {
            ScShellDialogClose(m->dialog);
            m->dialog=0;
        }
        if(!m->dialog) {
            ScShellFileOperationClose(m->job);
            m->job=0;
        }
    }
    if(s>=0&&m->job&&!m->job->closed&&sh_io_ready(m->job->channel?m->job->channel:m->job->listener,m->job->outgoing,m->job->offset,!m->job->channel))ready=1;
    if(s>=0)s=m->prompt||m->dialog||m->job||m->conflict_prompt?(ready?SC_SHELL_READY:SC_SHELL_PENDING):SC_SHELL_END;
    else s=sh_cause(s,110,0);
    sh_leave(&m->base);
    return s;
}
void ScShellMenuRelease(ScShellMenu *m) {
    if(sh_valid(m,SH_MENU))sh_release(&m->base);
}
