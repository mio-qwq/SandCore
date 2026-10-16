#include "DESKDEFAULTS.H"

typedef struct {const char *path;ScU32 profile;ScStatus status;} DefChoice;
static DefChoice def_choices[DEF_APP_COUNT];
static int def_same(const char *a,const char *b){return a && b && !strcmp(a,b);}
static ScStatus def_choose(ScU32 index)
{
    const DefApp *app=def_apps+index;DefChoice *choice=def_choices+index;ScU32 revision;
    choice->path=app->modern;choice->profile=SC_SHELL_PROFILE_MODERN;
    choice->status=desk_file_revision(choice->path,&revision);
    if(choice->status==SC32_E_NOT_FOUND && *app->legacy){
        choice->path=app->legacy;choice->profile=SC_SHELL_PROFILE_LEGACY;
        choice->status=desk_file_revision(choice->path,&revision);
    }
    return choice->status;
}
static ScStatus def_seed(ScShellCatalog *catalog)
{
    for(ScU32 i=0;i<DEF_APP_COUNT;i++){
        ScStatus s=def_choose(i);if(s==SC32_E_NOT_FOUND)continue;if(s<0)return s;
        const DefApp *row=def_apps+i;DefChoice *choice=def_choices+i;
        ScShellCatalogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        s=ScShellCatalogQuery(catalog,&info);if(s<0)return s;
        ScShellVerbDesc verb;memset(&verb,0,sizeof(verb));verb.size=sizeof(verb);verb.version=1;
        verb.verb="open";verb.label="打开";verb.flags=SC_SHELL_VERB_DEFAULT;
        verb.profile=choice->profile;verb.executable=choice->path;verb.arguments="";verb.types=i==0?"directory":"";verb.icon=row->icon;
        ScShellApplicationDesc app;memset(&app,0,sizeof(app));app.size=sizeof(app);app.version=1;
        app.layer=SC_SHELL_LAYER_SYSTEM;app.id=row->id;app.label=row->label;app.executable=choice->path;
        app.arguments="";app.icon=row->icon;app.category=row->category;app.profile=choice->profile;
        app.verbs=&verb;app.verb_count=1;ScU32 token;
        s=ScShellCatalogRegister(catalog,&app,info.epoch,&token);if(s<0)return s;
        const char *types=row->types;
        while(*types){char type[16];ScU32 n=0;
            while(*types && *types!=' '){if(n+1>=sizeof(type))return SC_SHELL_E_RANGE;type[n++]=*types++;}
            type[n]=0;while(*types==' ')types++;
            s=ScShellCatalogQuery(catalog,&info);if(s<0)return s;
            ScShellAssociationDesc association;memset(&association,0,sizeof(association));
            association.size=sizeof(association);association.version=1;
            association.layer=SC_SHELL_LAYER_SYSTEM;association.type=type;association.application=row->id;association.verb="open";
            s=ScShellCatalogSetAssociation(catalog,&association,info.epoch);if(s<0)return s;
        }
    }
    return 0;
}
static int def_menu_bytes(const char *bytes,ScU32 count)
{
    ScU32 at=0;
    for(ScU32 i=0;i<sizeof(def_menu_lines)/sizeof(def_menu_lines[0]);i++){
        const DefMenuLine *line=def_menu_lines+i;
        ScU32 a=(ScU32)strlen(line->old_line),b=(ScU32)strlen(line->new_line);
        if(a<=count-at && !memcmp(bytes+at,line->old_line,a))at+=a;
        else if(b<=count-at && !memcmp(bytes+at,line->new_line,b))at+=b;
        else return 0;
    }
    return at==count;
}
static ScStatus def_menu_drop(ScLeaseInfo *pin)
{
    if(!pin->lease)return 0;
    ScFile file=pin->file;
    ScLeaseControl control;memset(&control,0,sizeof(control));control.size=sizeof(control);control.version=1;
    control.lease=pin->lease;control.generation=pin->generation;
    ScStatus s=ScFileLeaseRelease(&control,pin);
    while(s==SC32_PENDING){control.operation_sequence=pin->operation_sequence;ScProcessYield();s=ScFileLeaseAdvance(&control,pin);}
    ScStatus closed=ScFileClose(file);pin->lease=0;
    return s<0?s:closed;
}
/* 只为已知SYS原文件持一个小READ_STABLE；比对到实际旧导入结束均不
 * 释放，不能让“校验默认”与“导入用户修改”之间出现可替换来源的空窗。 */
static ScStatus def_menu_hold(const char *path,ScLeaseInfo *pin,ScU32 *eligible)
{
    *eligible=0;memset(pin,0,sizeof(*pin));pin->size=sizeof(*pin);pin->version=1;
    if(strcmp(path,"/SYS/MENU.CFG"))return 0;
    ScPathInfo metadata;memset(&metadata,0,sizeof(metadata));metadata.size=sizeof(metadata);metadata.version=1;
    ScStatus s=ScPathQuery(path,&metadata);if(s<0)return s;
    if(metadata.kind!=SC32_PATH_FILE || metadata.uid!=1000 || metadata.gid!=1000 || metadata.rw!=23 || metadata.bytes>DEF_MENU_BYTES)return 0;
    ScLeaseOpenDesc open;memset(&open,0,sizeof(open));open.size=sizeof(open);open.version=1;
    open.path=path;open.object_kind=SC32_PATH_FILE;open.lease_kind=SC32_LEASE_READ_STABLE;
    open.expected_revision=metadata.generation;open.open_flags=SC32_FILE_READ;
    s=ScFileLeaseOpenPinned(&open,pin);if(s<0)return s;
    char body[DEF_MENU_BYTES];ScU32 offset=0;
    while(offset<metadata.bytes){ScIoResult io;memset(&io,0,sizeof(io));io.size=sizeof(io);io.version=1;
        s=ScFileRead(pin->file,body+offset,metadata.bytes-offset,&io);if(s<0)break;
        if(!io.transferred || io.transferred>metadata.bytes-offset){s=SC_SHELL_E_IO;break;}
        offset+=io.transferred;
    }
    ScPathInfo after;memset(&after,0,sizeof(after));after.size=sizeof(after);after.version=1;
    if(s==0)s=ScPathQuery(path,&after);
    if(s==0 && (after.generation!=metadata.generation || after.bytes!=metadata.bytes
       || after.uid!=metadata.uid || after.gid!=metadata.gid || after.rw!=metadata.rw))s=SC_SHELL_E_STALE;
    if(s==0)*eligible=def_menu_bytes(body,offset);
    if(s<0 || !*eligible){ScStatus dropped=def_menu_drop(pin);if(s==0)s=dropped;}
    return s;
}
static ScStatus def_menu_finish(ScLeaseInfo *pin,ScU32 *eligible)
{
    ScStatus s=0;
    if(pin->lease && *eligible){ScPathInfo metadata;memset(&metadata,0,sizeof(metadata));metadata.size=sizeof(metadata);metadata.version=1;
        s=ScPathQuery("/SYS/MENU.CFG",&metadata);
        if(s==0 && (metadata.kind!=SC32_PATH_FILE || metadata.generation!=pin->revision
           || metadata.uid!=1000 || metadata.gid!=1000 || metadata.rw!=23))s=SC_SHELL_E_STALE;
    }
    ScStatus dropped=def_menu_drop(pin);if(s==0)s=dropped;if(s<0)*eligible=0;return s;
}
static ScStatus def_expected_path(const char *path,char out[65])
{
    ScU32 bytes=0;return ScPathResolve(path,out,65,&bytes);
}
/* Imported不是保留字或权限票。只接受已知来源ID和完整未改的描述；
 * 一个自定义参数、图标、标签、额外verb或flags都会退出这个窄匹配。 */
static ScStatus def_imported_change(const ScShellApplicationInfo *info,const ScShellApplicationDesc *app,
                                   const char *prefix,ScShellApplicationDesc *updated,ScShellVerbDesc *verb,
                                   char executable[65])
{
    if(info->layer!=SC_SHELL_LAYER_ACCOUNT || info->owner_pid || info->owner_generation
       || app->layer!=SC_SHELL_LAYER_ACCOUNT || app->flags || app->verb_count!=1
       || !def_same(app->category,"Imported") || !def_same(app->arguments,"")
       || app->reserved[0] || app->reserved[1] || app->reserved[2])return 0;
    for(ScU32 i=0;i<sizeof(def_imported)/sizeof(def_imported[0]);i++){
        const DefImported *row=def_imported+i;char id[80];
        snprintf(id,sizeof(id),"%s%u",prefix,row->line);if(!def_same(id,app->id))continue;
        const ScShellVerbDesc *v=app->verbs;
        if(!def_same(app->label,row->label) || !def_same(app->icon,row->icon)
           || !def_same(v->verb,"open") || !def_same(v->label,row->label)
           || v->flags!=(SC_SHELL_VERB_DEFAULT|SC_SHELL_VERB_NO_DOCUMENT)
           || v->profile!=app->profile || !def_same(v->arguments,"")
           || !def_same(v->types,"") || !def_same(v->icon,row->icon)
           || !def_same(v->executable,app->executable) || v->reserved[0] || v->reserved[1])return 0;
        char old[65],modern[65];ScStatus s=def_expected_path(row->legacy,old);if(s<0)return s;
        s=def_expected_path(def_apps[row->app].modern,modern);if(s<0)return s;
        if(!(def_same(app->executable,old) && app->profile==SC_SHELL_PROFILE_LEGACY)
           && !(def_same(app->executable,modern) && (app->profile==SC_SHELL_PROFILE_LEGACY || app->profile==SC_SHELL_PROFILE_MODERN)))return 0;
        ScU32 revision=0,profile=SC_SHELL_PROFILE_MODERN;
        s=desk_file_revision(modern,&revision);
        if(s==SC32_E_NOT_FOUND){profile=SC_SHELL_PROFILE_LEGACY;s=desk_file_revision(old,&revision);}
        if(s==SC32_E_NOT_FOUND)return 0;
        if(s<0)return s;
        const char *selected=profile==SC_SHELL_PROFILE_MODERN?modern:old;
        if(app->profile==profile && def_same(app->executable,selected))return 0;
        strcpy(executable,selected);*updated=*app;*verb=*v;
        updated->executable=executable;updated->profile=profile;updated->verbs=verb;
        verb->executable=executable;verb->profile=profile;
        return 1;
    }
    return 0;
}
static ScStatus def_project(ScShellCatalog *catalog,ScU32 *changed)
{
    *changed=0;ScShellCatalogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScShellCatalogQuery(catalog,&info);if(s<0)return s;
    char source[65],prefix[64];s=def_expected_path("/SYS/MENU.CFG",source);if(s<0)return s;
    snprintf(prefix,sizeof(prefix),"legacy.menu.%08x.",ScHashBytes(source,(ScU32)strlen(source)));
    ScShellCatalog *patch=0;ScShellLayout *layout=0;ScBuffer *buffer=0;
    s=ScShellCatalogCreate(&patch);ScU32 count=0;
    /* 只查11个已知发行ID；不是截取前11个用户项。公共Applications当前
     * 每页从头扫描，反复分页大CAT会二次增长，固定候选直接Find为O(N)。 */
    for(ScU32 i=0;s==0 && i<sizeof(def_imported)/sizeof(def_imported[0]);i++){
        char id[80];snprintf(id,sizeof(id),"%s%u",prefix,def_imported[i].line);
        ScShellApplication *application=0;s=ScShellCatalogFind(catalog,id,&application);
        if(s==SC_SHELL_E_NOT_FOUND){s=0;continue;}if(s<0)break;
        ScShellApplicationInfo app_info;memset(&app_info,0,sizeof(app_info));app_info.size=sizeof(app_info);app_info.version=1;
        ScShellApplicationDesc description,updated;memset(&description,0,sizeof(description));description.size=sizeof(description);description.version=1;
        s=ScShellApplicationQuery(application,&app_info);
        if(s==0)s=ScShellApplicationDescribe(application,&description);
        if(s==0){ScShellVerbDesc verb;char executable[65];
            s=def_imported_change(&app_info,&description,prefix,&updated,&verb,executable);
            if(s>0){ScShellCatalogInfo patch_info;memset(&patch_info,0,sizeof(patch_info));patch_info.size=sizeof(patch_info);patch_info.version=1;
                s=ScShellCatalogQuery(patch,&patch_info);ScU32 token;
                if(s==0)s=ScShellCatalogRegister(patch,&updated,patch_info.epoch,&token);
                if(s==0)count++;
            }
        }
        ScShellApplicationRelease(application);
    }
    /* 只merge改变项，用户注册的revision/token/字段完全保留；携带同一
     * layout值使通用Import不会把坐标、顺序、选择和网格重置为空。 */
    if(s==0 && count){s=ScShellCatalogLayout(catalog,&layout);ScShellCatalogInfo patch_info;
        memset(&patch_info,0,sizeof(patch_info));patch_info.size=sizeof(patch_info);patch_info.version=1;
        if(s==0)s=ScShellCatalogQuery(patch,&patch_info);
        if(s==0)s=ScShellCatalogApplyLayout(patch,layout,patch_info.epoch);
        if(s==0)s=ScShellCatalogExport(patch,&buffer);
        ScView view;memset(&view,0,sizeof(view));view.size=sizeof(view);view.version=1;
        if(s==0)s=ScBufferView(buffer,&view);
        if(s==0)s=ScShellCatalogImport(catalog,view.data,view.bytes,info.epoch,0);
        if(s==0)*changed=count;
    }
    ScBufferRelease(buffer);ScShellLayoutRelease(layout);ScShellCatalogRelease(patch);return s;
}
