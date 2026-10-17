#include "D2APP.H"

static ScStatus d2_ui_event(ScUiContext *,const ScUiEvent *,void *);
static ScStatus d2_layout(D2App *a,ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex,ScI32 min_w,ScI32 min_h)
{
    ScUiLayoutDesc d;
    D2Record(&d,sizeof(d));
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.flex_weight=flex;
    d.min_width=min_w;
    d.min_height=min_h;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.align_h=d.align_v=SC_UI_STRETCH;
    d.column_span=d.row_span=1;
    return ScUiNodeSetLayout(a->ui,node,&d);
}
static ScStatus d2_widget(D2App *a,ScUiNode parent,ScU32 kind,const char *text,ScU32 command,ScUiNode *out)
{
    ScUiWidgetDesc d;
    D2Record(&d,sizeof(d));
    d.kind=kind;
    d.parent=parent;
    d.command=command;
    d.text=D2Text(text);
    if(kind==SC_UI_TEXTBOX || kind==SC_UI_TABLE)d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScStatus s=ScUiWidgetCreate(a->ui,&d,out);
    if(s==0)s=d2_layout(a,*out,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,0,0,0);
    return s;
}
static ScStatus d2_button(D2App *a,ScUiNode parent,const char *text,ScU32 command,ScI32 width)
{ScUiNode n;
    ScStatus s=d2_widget(a,parent,SC_UI_BUTTON,text,command,&n);
    return s<0?s:d2_layout(a,n,SC_UI_ABSOLUTE,width,28,0,width,24);
}
static ScStatus d2_provider(void *opaque,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    D2Pane *pane=opaque;
    D2Frame *f=pane->app->frame;
    ScArray *array=f?f->rows[pane->kind]:0;
    ScU32 count=D2Count(array);
    reply->revision=pane->revision;
    if(request->operation==SC_UI_DATA_COUNT){reply->total=count;
        return 0;
    }
    if(request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=0;
        return 0;
    }
    ScU32 index=request->operation==SC_UI_DATA_CHILD_AT?request->index:request->item?request->item-1:UINT_MAX;
    if(index>=count)return request->operation==SC_UI_DATA_CHILD_AT?SC_UI_END:SC_UI_E_STALE;
    const D2Row *rows=D2Data(array);
    if(!rows)return SC_UI_E_STALE;
    const D2Row *row=rows+index;
    reply->item=row->id;
    reply->total=index;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF || request->operation==SC_UI_DATA_ITEM || request->operation==SC_UI_DATA_CHILD_AT)return 0;
    if(request->operation!=SC_UI_DATA_CELL || request->field>=5)return SC_UI_E_NOT_SUPPORTED;
    reply->value.type=SC_VALUE_UTF8;
    reply->value.data=row->text[request->field];
    reply->value.bytes=(ScU32)strlen(row->text[request->field]);
    return 0;
}
static ScStatus d2_pane(D2App *a,ScU32 kind,ScUiNode parent,const char *title,ScU32 command)
{
    D2Pane *p=&a->panes[kind];
    p->app=a;
    p->kind=kind;
    p->revision=1;
    ScStatus s=d2_widget(a,parent,SC_UI_PANEL,"",0,&p->container);
    if(s<0)return s;
    s=d2_layout(a,p->container,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
    if(s<0)return s;
    ScUiNode label;
    s=d2_widget(a,p->container,SC_UI_LABEL,title,0,&label);
    if(s==0)s=d2_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,20,0,0,18);
    if(s==0)s=ScUiNodeSetFont(a->ui,label,SC_UI_FONT_DENSE);
    if(s==0)s=d2_widget(a,p->container,SC_UI_TABLE,"",command,&p->table);
    if(s==0)s=d2_layout(a,p->table,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
    if(s==0)s=ScUiNodeSetFont(a->ui,p->table,kind==D2_MODULES?SC_UI_FONT_DENSE:SC_UI_FONT_CODE);
    ScUiProviderDesc provider;
    D2Record(&provider,sizeof(provider));
    provider.procedure=d2_provider;
    provider.context=p;
    if(s==0)s=ScUiProviderCreate(&provider,&p->provider);
    if(s==0)s=ScUiSelectionCreate(p->provider,SC_UI_SELECTION_ALLOW_EMPTY,&p->selection);
    if(s==0)s=ScUiViewBind(a->ui,p->table,p->provider,p->selection);
    static const char *names[D2_PANES][5]={
        {"","Address","Bytes","Instruction","Symbol / source"},{"Register","Value","Flags / identity","",""},
        {"Address","Hex bytes","ASCII","Access",""},{"Address","Value","Role","Symbol",""},
        {"Address","State","Hits","Identity",""},{"Module","Mapped range","Version","Source","State"},
        {"Module","Symbol","Address","Kind / size","Provenance"}};
    static const ScI32 widths[D2_PANES][5]={{28,88,148,286,330},{80,94,310,0,0},{90,405,155,150,0},
        {90,96,110,310,0},{100,150,70,120,0},{168,190,136,280,170},{145,235,90,130,245}};
    ScUiColumnDesc columns[5];
    ScU32 count=0;
    memset(columns,0,sizeof(columns));
    for(ScU32 i=0;i<5;i++)if(widths[kind][i]){D2Record(columns+count,sizeof(columns[count]));
        columns[count].column=count+1;
        columns[count].field=i;
        columns[count].title=D2Text(names[kind][i]);
        columns[count].width=widths[kind][i];
        columns[count].min_width=i?44:20;
        columns[count].max_width=1200;
        columns[count].format=SC_UI_FORMAT_TEXT;
        count++;
    }
    if(s==0)s=ScUiTableSetColumns(a->ui,p->table,columns,count);
    return s;
}
static ScStatus d2_menu_item(D2App *a,ScUiMenu *menu,ScU32 command,const char *label,ScU32 key,ScU32 modifiers)
{
    ScUiMenuItem item;
    D2Record(&item,sizeof(item));
    item.command=command;
    item.label=D2Text(label);
    item.help=D2Text(label);
    D2Record(&item.shortcut,sizeof(item.shortcut));
    item.shortcut.key=key;
    item.shortcut.modifiers=modifiers;
    ScU32 id;
    ScStatus s=ScUiMenuInsert(menu,0,&item,&id);
    if(s<0)return s;
    ScUiCommandDesc d;
    D2Record(&d,sizeof(d));
    d.command=command;
    d.label=item.label;
    d.shortcut=item.shortcut;
    return ScUiCommandSet(a->ui,&d);
}
static ScStatus d2_menus(D2App *a)
{
    ScStatus s=ScUiMenuCreate(&a->menu);
    if(s==0)s=ScUiMenuCreate(&a->file_menu);
    if(s==0)s=ScUiMenuCreate(&a->debug_menu);
    if(s==0)s=ScUiMenuCreate(&a->view_menu);
    if(s==0)s=ScUiMenuCreate(&a->tools);
    if(s<0)return s;
    struct {ScU32 command,group;
        const char *text;
        ScU32 key,mods;
    } items[]={
        {D2_BROWSE,0,"&Choose executable...",'o',SC32_MOD_CONTROL},{D2_LAUNCH,0,"&Launch target",SC32_KEY_F1+4,SC32_MOD_CONTROL},
        {D2_RESTART,0,"&Restart target",SC32_KEY_F1+1,0},{D2_STOP,0,"&Terminate target",SC32_KEY_F1+4,SC32_MOD_SHIFT},{D2_QUIT,0,"E&xit",'q',SC32_MOD_CONTROL},
        {D2_CONTINUE,1,"&Continue",SC32_KEY_F1+4,0},{D2_PAUSE,1,"&Pause",SC32_KEY_F1+3,0},{D2_STEP,1,"Single &step",SC32_KEY_F1+7,0},
        {D2_BREAK_ADD,1,"Add &breakpoint",SC32_KEY_F1+8,0},{D2_BREAK_REMOVE,1,"&Remove selected breakpoint",SC32_KEY_DELETE,SC32_MOD_CONTROL},
        {D2_FOLLOW_IP,1,"Follow instruction &pointer",'i',SC32_MOD_CONTROL},{D2_FOLLOW_BRANCH,1,"Follow selected &branch",SC32_KEY_F1+6,0},
        {D2_GOTO_CODE,2,"Go to &code address",'g',SC32_MOD_CONTROL},{D2_GOTO_MEMORY,2,"Go to &memory address",'m',SC32_MOD_CONTROL},
        {D2_PREVIOUS,2,"Previous memory page",SC32_KEY_PAGEUP,SC32_MOD_ALT},{D2_NEXT,2,"Next memory page",SC32_KEY_PAGEDOWN,SC32_MOD_ALT},
        {D2_FLOAT,2,"&Float information dock",0,0},{D2_DOCK,2,"&Dock information panels",0,0},{D2_RESET_LAYOUT,2,"&Reset layout",0,0},
        {D2_SAVE_LAYOUT,2,"&Save layout",0,0},{D2_LOAD_LAYOUT,2,"&Load layout",0,0},{D2_MODULE_DETAILS,2,"Module &properties...",0,0},{D2_ABOUT,2,"About Debug 2",SC32_KEY_F1,0}};
    for(ScU32 i=0;s==0 && i<sizeof(items)/sizeof(items[0]);i++)s=d2_menu_item(a,items[i].group==0?a->file_menu:items[i].group==1?a->debug_menu:a->view_menu,
    items[i].command,items[i].text,items[i].key,items[i].mods);
    ScUiMenu *menus[3]={a->file_menu,a->debug_menu,a->view_menu};
    const char *labels[3]={"&Target","&Debug","&View"};
    for(ScU32 i=0;s==0 && i<3;i++){ScUiMenuItem item;
        D2Record(&item,sizeof(item));
        item.label=D2Text(labels[i]);
        item.submenu=menus[i];
        D2Record(&item.shortcut,sizeof(item.shortcut));
        ScU32 id;
        s=ScUiMenuInsert(a->menu,0,&item,&id);
    }
    ScU32 commands[8]={D2_LAUNCH,D2_RESTART,D2_CONTINUE,D2_PAUSE,D2_STEP,D2_STOP,D2_BREAK_ADD,D2_FOLLOW_IP};
    const char *tooltext[8]={"Launch","Restart","Continue","Pause","Step","Stop","Breakpoint","Follow EIP"};
    for(ScU32 i=0;s==0 && i<8;i++){ScUiMenuItem item;
        D2Record(&item,sizeof(item));
        item.command=commands[i];
        item.label=D2Text(tooltext[i]);
        D2Record(&item.shortcut,sizeof(item.shortcut));
        ScU32 id;
        s=ScUiMenuInsert(a->tools,0,&item,&id);
    }
    if(s==0)s=ScUiMenuBind(a->ui,a->menubar,a->menu);
    if(s==0)s=ScUiToolbarBind(a->ui,a->toolbar,a->tools);
    return s;
}
void D2Status(D2App *a,const char *text)
{
    snprintf(a->message,sizeof(a->message),"%s",text);
    if(!a->status)return;
    ScUiStatusPart parts[2];
    memset(parts,0,sizeof(parts));
    D2Record(parts,sizeof(parts[0]));
    D2Record(parts+1,sizeof(parts[1]));
    parts[0].part=1;
    parts[0].width=780;
    parts[0].text=D2Text(a->message);
    char target[112];
    if(a->active)snprintf(target,sizeof(target),"PID %u / process %u / bind %u",a->launch.target.pid,a->launch.target.generation,a->launch.target.binding);
    else snprintf(target,sizeof(target),"Owned Ring 3 debugging");
    parts[1].part=2;
    parts[1].width=-1;
    parts[1].text=D2Text(target);
    ScUiStatusSetParts(a->ui,a->status,parts,2);
}
void D2Error(D2App *a,const char *operation,ScStatus status)
{char text[240],name[80];
    ScU32 bytes=0;
    if(ScErrorName(status,name,sizeof(name),&bytes)<0)snprintf(name,sizeof(name),"error %d",status);
    snprintf(text,sizeof(text),"%s: %s (%d)",operation,name,status);
    D2Status(a,text);
}
static void d2_notify(D2App *a)
{
    ScUiBatchBegin(a->ui);
    /* 只在调试状态发布时刷新有限命令，不让工具栏长期保留入口暂停
     * 或上一目标退出前的禁用位，也不在每帧轮询整个命令目录。 */
    static const ScU32 commands[]={D2_LAUNCH,D2_BROWSE,D2_RESTART,D2_STOP,
        D2_CONTINUE,D2_PAUSE,D2_STEP,D2_BREAK_ADD,D2_BREAK_REMOVE,D2_FOLLOW_IP,
        D2_FOLLOW_BRANCH,D2_GOTO_CODE,D2_GOTO_MEMORY,D2_PREVIOUS,D2_NEXT};
    for(ScU32 i=0;i<sizeof(commands)/sizeof(*commands);i++){
        ScUiCommandDesc state;D2Record(&state,sizeof(state));
        (void)ScUiCommandQuery(a->ui,commands[i],&state);
    }
    for(ScU32 i=0;i<D2_PANES;i++){D2Pane *p=a->panes+i;
        p->revision++;
        ScUiDataChange d;
        D2Record(&d,sizeof(d));
        d.kind=SC_UI_DATA_RESET;
        d.revision=p->revision;
        if(p->provider)ScUiProviderNotify(p->provider,&d);
    }
    ScUiBatchEnd(a->ui);
}
void D2Clear(D2App *a)
{D2Frame *previous=a->frame;
    a->frame=0;
    d2_notify(a);
    D2FrameDestroy(previous);
}
void D2Publish(D2App *a,D2Frame *f)
{
    D2Frame *previous=a->frame;
    a->frame=f;
    a->current=f->snapshot;
    a->code_address=f->code_address;
    a->memory_address=f->memory_address;
    d2_notify(a);
    D2FrameDestroy(previous);
    const char *reason=f->snapshot.reason==SC32_DEBUG_STOP_ENTRY?"entry":f->snapshot.reason==SC32_DEBUG_STOP_STEP?"single step":f->snapshot.reason==SC32_DEBUG_STOP_BREAKPOINT?"breakpoint":f->snapshot.reason==SC32_DEBUG_STOP_PAUSE?"pause":"exception";
    char text[240];
    snprintf(text,sizeof(text),"Paused %u (%s) / EIP %08X / %u modules, %u symbols%s%.70s",f->snapshot.stop_generation,reason,f->snapshot.registers.eip,
    D2Count(f->modules),D2Count(f->rows[D2_SYMBOLS]),f->sidecar_status[0]?" / ":"",f->sidecar_status);
    D2Status(a,text);
    const D2Row *rows=D2Data(f->rows[D2_CODE]);
    for(ScU32 i=0;i<D2Count(f->rows[D2_CODE]);i++)if(rows[i].address==f->snapshot.registers.eip){ScUiSelectionSet(a->panes[D2_CODE].selection,rows[i].id,SC_UI_REPLACE);
        ScUiViewEnsureItem(a->ui,a->panes[D2_CODE].table,rows[i].id);
        break;
    }
}
static ScStatus d2_copy_box(D2App *a,ScUiNode node,char *out,ScU32 capacity)
{
    ScString *s=0;
    ScStatus status=ScUiNodeCopyText(a->ui,node,&s);
    if(status<0)return status;
    ScView v;
    D2Record(&v,sizeof(v));
    status=ScStringView(s,&v);
    if(status==0){if(v.bytes>=capacity || (v.bytes && memchr(v.data,0,v.bytes)))status=SC32_E_RANGE;
        else{memcpy(out,v.data,v.bytes);
            out[v.bytes]=0;
    }}
    ScStringRelease(s);
    return status;
}
static ScStatus d2_address(D2App *a,ScU32 *out)
{
    char text[160];
    ScStatus s=d2_copy_box(a,a->addressbox,text,sizeof(text));
    if(s<0)return s;
    if(!strcmp(text,"eip")){if(!a->frame)return SC32_E_BUSY;
         *out=a->frame->snapshot.registers.eip;
        return 0;
    }
    if(!strcmp(text,"esp")){if(!a->frame)return SC32_E_BUSY;
         *out=a->frame->snapshot.registers.esp;
        return 0;
    }
    const char *p=text;
    if(p[0]=='0' && (p[1]=='x'||p[1]=='X'))p+=2;
    ScU32 value=0,digits=0;
    int valid=1;
    for(;*p;p++){int d=*p>='0'&&*p<='9'?*p-'0':*p>='a'&&*p<='f'?*p-'a'+10:*p>='A'&&*p<='F'?*p-'A'+10:-1;
        if(d<0 || digits==8){valid=0;
            break;
        }value=(value<<4)|(ScU32)d;
        digits++;
    }
    if(valid && digits){*out=value;
        return 0;
    }
    if(!a->frame)return SC32_E_BUSY;
    int found=0;
    const D2Row *rows=D2Data(a->frame->rows[D2_SYMBOLS]);
    for(ScU32 i=0;i<D2Count(a->frame->rows[D2_SYMBOLS]);i++){char qualified[208];
        snprintf(qualified,sizeof(qualified),"%s!%s",rows[i].text[0],rows[i].text[1]);
        if(!strcmp(text,rows[i].text[1]) || !strcmp(text,qualified)){if(found && value!=rows[i].address)return SC32_E_EXISTS;
            value=rows[i].address;
            found=1;
    }}
    if(!found)return SC32_E_NOT_FOUND;
     *out=value;
    return 0;
}
ScStatus D2Navigate(D2App *a,ScU32 kind)
{
    if(!a->frame || a->candidate)return SC32_E_BUSY;
    ScU32 address;
    ScStatus s=d2_address(a,&address);
    if(s<0)return s;
    if(kind==D2_GOTO_MEMORY)a->memory_address=address;
    else{a->code_address=address;
        a->follow_ip=0;
    }return D2CaptureStart(a);
}
static ScStatus d2_selected(D2App *a,ScU32 kind,D2Row *row)
{
    if(!a->frame)return SC32_E_BUSY;
    ScUiSelectionInfo selection;
    D2Record(&selection,sizeof(selection));
    ScStatus s=ScUiSelectionQuery(a->panes[kind].selection,&selection);
    if(s<0)return s;
    if(!selection.current)return SC32_E_NOT_FOUND;
    return ScArrayGet(a->frame->rows[kind],selection.current-1,row);
}
static ScStatus d2_dock(D2App *a)
{
    if(!a->float_root)return 0;
    ScStatus s=ScUiDockMove(a->ui,a->dock,a->workspace,SC_UI_DOCK_FILL);
    if(s<0)return s;
    ScUiNode floating=a->float_root;
    a->float_root=0;
    ScUiNodeDestroy(a->ui,floating);
    a->timer_period=0;
    D2Schedule(a);
    return d2_layout(a,a->dock,SC_UI_VBOX,SC_UI_AUTO,170,1,0,80);
}
static ScStatus d2_layout_file(D2App *a,int save)
{
    if(!a->layout_path[0]){ScU32 size=0;
        ScStatus s=ScSessionPath("DEBUG2.LAYOUT",a->layout_path,sizeof(a->layout_path),&size);
        if(s<0)return s;
    }
    ScStatus s=d2_dock(a);
    if(s<0)return s;
    ScConfig *config=0;
    s=ScConfigCreate(&config);
    if(s<0)return s;
    if(save){s=ScUiLayoutSave(a->ui,a->root,config);
        ScString *serialized=0;
        const char *text=0;
        ScContainerInfo info;
        D2Record(&info,sizeof(info));
        if(s==0)s=ScConfigSerialize(config,0,&serialized);
        if(s==0)s=ScStringCStr(serialized,&text);
        if(s==0)s=ScStringQuery(serialized,&info);
        ScU32 transferred=0;
        if(s==0)s=ScPathReplace(a->layout_path,text,info.length,&transferred);
        if(s==0 && transferred!=info.length)s=SC32_E_IO;
        ScStringRelease(serialized);
    }else{ScPathStatInfo stat;
        D2Record(&stat,sizeof(stat));
        s=ScPathStat(a->layout_path,&stat);
        if(s==0 && (stat.kind!=SC32_PATH_FILE || !stat.bytes || stat.bytes>65536))s=SC32_E_RANGE;
        unsigned char *bytes=s==0?malloc(stat.bytes):0;
        if(s==0 && !bytes)s=SC32_E_MEMORY;
        ScU32 got=0;
        if(s==0)s=ScPathRead(a->layout_path,0,bytes,stat.bytes,&got);
        if(s==0 && got!=stat.bytes)s=SC32_E_IO;
        ScParseIssue issue;
        D2Record(&issue,sizeof(issue));
        if(s==0)s=ScConfigParse(config,bytes,got,0,&issue);
        if(s==0)s=ScUiLayoutRestore(a->ui,a->root,config);
        free(bytes);
    }
    ScConfigRelease(config);
    return s;
}
static ScStatus d2_dialog_complete(ScUiContext *ui,const ScUiEvent *event,void *opaque)
{
    (void)ui;
    (void)event;
    D2App *a=opaque;
    if(!a->dialog)return 0;
    ScUiDialogInfo info;
    D2Record(&info,sizeof(info));
    if(a->dialog_kind==1 && ScUiDialogQuery(a->dialog,&info)==0 && info.stage==SC_UI_DIALOG_DONE && info.command==SC_UI_DIALOG_OK){ScString *path=0;
        const char *text=0;
        ScStatus s=ScUiDialogSelectedPath(a->dialog,0,&path);
        if(s==0)s=ScStringCStr(path,&text);
        if(s==0){ScUiText t=D2Text(text);
            s=ScUiNodeSetText(a->ui,a->commandbox,&t);
        }if(s<0)D2Error(a,"Choose executable",s);
        ScStringRelease(path);
    }
    ScUiDialog *old=a->dialog;
    a->dialog=0;
    a->dialog_kind=0;
    ScUiDialogRelease(old);
    return 0;
}
static ScStatus d2_module_properties(D2App *a)
{
    if(a->dialog)return SC32_E_BUSY;
    D2Row row;
    ScStatus s=d2_selected(a,D2_MODULES,&row);
    if(s<0)return s;
    ScDebugModule module;
    int found=0;
    for(ScU32 i=0;i<D2Count(a->frame->modules);i++){
        if(ScArrayGet(a->frame->modules,i,&module)==0 && module.id==row.aux && module.generation==row.generation){found=1;
            break;
    }}
    if(!found)return SC32_E_STALE;
    const char *keys[8]={"name","source","identity","mapping","version","file","sha256","permissions"};
    const char *labels[8]={"Module","Source path","Load identity","Mapped bytes","Library ABI","Source revision","Source SHA256","Current access"};
    char values[8][160];
    memset(values,0,sizeof(values));
    snprintf(values[0],160,"%s",module.name);
    snprintf(values[1],160,"%s",module.source);
    snprintf(values[2],160,"module %u / load generation %u",module.id,module.generation);
    snprintf(values[3],160,"%08X .. %08X / %uB (%uB allocation)",module.base,module.base+module.image_bytes-1,module.image_bytes,module.allocation_bytes);
    snprintf(values[4],160,module.kind==SC32_DEBUG_MAIN?"SCX main image":"%u.%u / release %u",module.abi_major,module.abi_minor,module.library_version);
    snprintf(values[5],160,"%uB / revision %u",module.source_bytes,module.source_revision);
    for(ScU32 i=0;i<32;i++)snprintf(values[6]+i*2,160-i*2,"%02X",module.sha256[i]);
    snprintf(values[7],160,"%.92s; X is declared, no hardware NX",row.text[4]);
    ScConfig *config=0;
    s=ScConfigCreate(&config);
    ScUiFieldDesc fields[8];
    memset(fields,0,sizeof(fields));
    for(ScU32 i=0;s==0 && i<8;i++){ScValue value={sizeof(value),1,SC_VALUE_UTF8,0,0,0,values[i],(ScU32)strlen(values[i])};
        s=ScConfigSet(config,"MODULE",keys[i],&value);
        D2Record(fields+i,sizeof(fields[i]));
        fields[i].field=i+1;
        fields[i].kind=SC_UI_TEXTBOX;
        fields[i].flags=SC_UI_FIELD_READONLY;
        fields[i].section="MODULE";
        fields[i].key=keys[i];
        fields[i].label=D2Text(labels[i]);
    }
    ScUiPropertySheetDesc d;
    D2Record(&d,sizeof(d));
    d.owner=a->root;
    d.title=D2Text("Verified runtime module properties");
    d.initial=config;
    d.context=a;
    d.completion=d2_dialog_complete;
    if(s==0){a->dialog_kind=2;
        s=ScUiPropertySheet(a->ui,&d,fields,8,&a->dialog);
        if(s<0)a->dialog_kind=0;
    }ScConfigRelease(config);
    return s;
}
static ScStatus d2_execute(D2App *a,ScU32 command)
{
    if(a->closing)return SC32_E_BUSY;
    if(command==D2_QUIT){D2Close(a);
        return 0;
    }
    if(command==D2_LAUNCH){char target[1088];
        ScStatus s=d2_copy_box(a,a->commandbox,target,sizeof(target));
        return s<0?s:D2Launch(a,target);
    }
    if(command==D2_BROWSE){if(a->dialog)return SC32_E_BUSY;
        ScUiFileDialogDesc d;
        D2Record(&d,sizeof(d));
        d.owner=a->root;
        d.flags=SC_UI_FILE_OPEN;
        d.title=D2Text("Choose executable");
        d.directory="/APPS";
        d.completion=d2_dialog_complete;
        d.context=a;
        a->dialog_kind=1;
        ScStatus s=ScUiFileDialog(a->ui,&d,&a->dialog);
        if(s<0)a->dialog_kind=0;
        return s;
    }
    if(command==D2_RESTART){if(a->active){a->restart_pending=1;
            ScStatus s=D2Control(a,SC32_DEBUG_TERMINATE);
            if(s<0)a->restart_pending=0;
            return s;
        }
        char target[1088];
        snprintf(target,sizeof(target),"%s",a->command);
        return D2Launch(a,target);
    }
    if(command==D2_CONTINUE || command==D2_PAUSE || command==D2_STEP || command==D2_STOP)return D2Control(a,command==D2_CONTINUE?SC32_DEBUG_CONTINUE:command==D2_PAUSE?SC32_DEBUG_PAUSE:command==D2_STEP?SC32_DEBUG_STEP:SC32_DEBUG_TERMINATE);
    if(command==D2_GOTO_CODE || command==D2_GOTO_MEMORY)return D2Navigate(a,command);
    if(command==D2_FOLLOW_IP){a->follow_ip=1;
        return D2CaptureStart(a);
    }
    if(command==D2_PREVIOUS || command==D2_NEXT){if(!a->frame)return SC32_E_BUSY;
        if(command==D2_PREVIOUS){if(a->memory_address<256)return SC32_E_RANGE;
            a->memory_address-=256;
        }
        else{if(a->memory_address>UINT_MAX-256)return SC32_E_RANGE;
            a->memory_address+=256;
        }return D2CaptureStart(a);
    }
    if(command==D2_BREAK_ADD){ScU32 address;
        ScStatus s=d2_address(a,&address);
        if(s<0){D2Row selected;
            s=d2_selected(a,D2_CODE,&selected);
            if(s<0)return s;
            address=selected.address;
        }
        ScDebugBreakpoint bp;
        D2Record(&bp,sizeof(bp));
        s=ScDebugBreakpointAdd(&a->launch.target,a->current.stop_generation,address,&bp);
        return s<0?s:D2CaptureStart(a);
    }
    if(command==D2_BREAK_REMOVE){D2Row row;
        ScStatus s=d2_selected(a,D2_BREAKPOINTS,&row);
        if(s<0)return s;
        s=ScDebugBreakpointRemove(&a->launch.target,a->current.stop_generation,row.aux,row.generation);
        return s<0?s:D2CaptureStart(a);
    }
    if(command==D2_FOLLOW_BRANCH || command==D2_ACTIVATE_CODE || command==D2_ACTIVATE_STACK || command==D2_ACTIVATE_MODULE || command==D2_ACTIVATE_SYMBOL){
        ScU32 pane=command==D2_ACTIVATE_STACK?D2_STACK:command==D2_ACTIVATE_MODULE?D2_MODULES:command==D2_ACTIVATE_SYMBOL?D2_SYMBOLS:D2_CODE;
        D2Row row;
        ScStatus s=d2_selected(a,pane,&row);
        if(s<0)return s;
        ScU32 address=row.address;
        if(pane==D2_STACK){if(!row.flags)return SC32_E_RANGE;
            address=row.aux;
        }
        if(command==D2_FOLLOW_BRANCH){if(!row.flags)return SC32_E_NOT_SUPPORTED;
            address=row.aux;
        }
        a->code_address=address;
        a->follow_ip=0;
        char value[16];
        snprintf(value,sizeof(value),"%08X",address);
        ScUiText t=D2Text(value);
        ScUiNodeSetText(a->ui,a->addressbox,&t);
        return D2CaptureStart(a);
    }
    if(command==D2_FLOAT){if(a->float_root)return SC32_E_BUSY;
        ScUiWindowDesc d;
        D2Record(&d,sizeof(d));
        d.title=D2Text("Debug 2 - modules, symbols and breakpoints");
        d.client_width=900;
        d.client_height=340;
        d.style=SC32_STYLE_STANDARD;
        d.owner=a->root;
        ScStatus s=ScUiDockFloat(a->ui,a->dock,&d,&a->float_root);
        if(s==0)s=d2_layout(a,a->float_root,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
        return s;
    }
    if(command==D2_DOCK)return d2_dock(a);
    if(command==D2_RESET_LAYOUT){ScStatus s=d2_dock(a);
        return s<0?s:ScUiLayoutRestore(a->ui,a->root,a->saved_layout);
    }
    if(command==D2_SAVE_LAYOUT || command==D2_LOAD_LAYOUT){ScStatus s=d2_layout_file(a,command==D2_SAVE_LAYOUT);
        if(s==0)D2Status(a,command==D2_SAVE_LAYOUT?"Layout saved":"Layout restored");
        return s;
    }
    if(command==D2_MODULE_DETAILS)return d2_module_properties(a);
    if(command==D2_ABOUT){if(a->dialog)return SC32_E_BUSY;
        ScUiMessageBoxDesc d;
        D2Record(&d,sizeof(d));
        d.owner=a->root;
        d.title=D2Text("SandCore Debug 2");
        d.message=D2Text("Owned processes, true TF / INT3, synchronized paused views, verified runtime SCL modules and SCDB1MIO symbols.\nF5 Continue - F4 Pause - F8 Step - F9 Breakpoint");
        d.completion=d2_dialog_complete;
        d.context=a;
        a->dialog_kind=2;
        ScStatus s=ScUiMessageBox(a->ui,&d,&a->dialog);
        if(s<0)a->dialog_kind=0;
        return s;
    }
    return SC32_E_NOT_SUPPORTED;
}
static ScStatus d2_ui_event(ScUiContext *ui,const ScUiEvent *e,void *opaque)
{
    D2App *a=opaque;
    (void)ui;
    if(e->type==SC_UI_EVENT_COMMAND_QUERY && e->phase==SC_UI_TARGET_PHASE && e->bytes==sizeof(ScUiCommandDesc)){
        ScUiCommandDesc *d=(ScUiCommandDesc *)e->data;
        int enabled=1;
        if(d->command==D2_LAUNCH || d->command==D2_BROWSE)enabled=!a->active && !a->closing;
        else if(d->command==D2_RESTART)enabled=a->command[0] && !a->closing && !a->restart_pending;
        else if(d->command==D2_PAUSE)enabled=a->active && a->current.state==SC32_DEBUG_RUNNING;
        else if(d->command==D2_STOP)enabled=a->active;
        else if(d->command==D2_CONTINUE || d->command==D2_STEP)enabled=a->active && a->current.state==SC32_DEBUG_PAUSED && !a->candidate;
        else if(d->command>=D2_CONTINUE && d->command<D2_FLOAT)enabled=a->active && a->frame && !a->candidate;
        if(!enabled)d->flags|=SC_UI_MENU_DISABLED;
        else d->flags&=~SC_UI_MENU_DISABLED;
        return 0;
    }
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST && e->phase==SC_UI_CAPTURE_PHASE){
        if(e->node==a->float_root){ScStatus s=d2_dock(a);
            if(s<0)D2Error(a,"Dock",s);
            return SC_UI_VETO;
        }
        if(e->node==a->root){D2Close(a);
            return SC_UI_VETO;
    }}
    if(e->type==SC_UI_EVENT_TIMER && e->phase==SC_UI_CAPTURE_PHASE){D2Poll(a);
        return 0;
    }
    if(e->type==SC_UI_EVENT_COMMAND && (e->phase==SC_UI_BUBBLE_PHASE || (!e->node && e->phase==SC_UI_TARGET_PHASE))){
        ScStatus s=d2_execute(a,e->command);
        D2Schedule(a);
        if(s<0)D2Error(a,"Command",s);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN && e->phase==SC_UI_CAPTURE_PHASE && e->key==13 && e->node==a->addressbox){ScStatus s=D2Navigate(a,D2_GOTO_CODE);
        if(s<0)D2Error(a,"Address",s);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN && e->phase==SC_UI_CAPTURE_PHASE && e->key==13 && e->node==a->commandbox && !a->active){ScStatus s=d2_execute(a,D2_LAUNCH);
        if(s<0)D2Error(a,"Launch",s);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_VISIBILITY && e->phase==SC_UI_CAPTURE_PHASE && e->node==a->root){
        ScWindowInfo info;
        D2Record(&info,sizeof(info));
        if(ScUiWindowNative(a->ui,a->root,&info)==0){a->visible=(info.state&SC32_WINDOW_VISIBLE)!=0 && !(info.state&SC32_WINDOW_MINIMIZED);
            if(a->visible)D2Poll(a);
        }return 0;
    }
    if(e->type==SC_UI_EVENT_POINTER_DOWN && e->phase==SC_UI_CAPTURE_PHASE && e->buttons&2u){
        for(ScU32 i=0;i<D2_PANES;i++)if(e->node==a->panes[i].table){ScUiNodeInfo n;
            D2Record(&n,sizeof(n));
            ScU32 item=0,column=0;
            if(ScUiNodeQuery(a->ui,e->node,&n)==0 && ScUiViewHitTest(a->ui,e->node,e->x-n.rectangle.x,e->y-n.rectangle.y,&item,&column)==0 && item)ScUiSelectionSet(a->panes[i].selection,item,SC_UI_REPLACE);
            ScU32 popup=0;
            ScUiMenuOpen(a->ui,e->root,a->debug_menu,e->x,e->y,0,&popup);
            return SC_UI_HANDLED;
    }}
    return 0;
}
ScStatus D2BuildUi(D2App *a)
{
    ScUiContextDesc c;
    D2Record(&c,sizeof(c));
    c.flags=SC_UI_STOP_LAST_ROOT;
    c.cache_budget_bytes=1024u*1024u;
    c.undo_budget_bytes=0;
    c.scheduling_quantum=32;
    ScStatus s=ScUiContextCreate(&c,&a->ui);
    if(s<0)return s;
    ScUiWindowDesc w;
    D2Record(&w,sizeof(w));
    w.title=D2Text("SandCore Debug 2");
    w.client_width=1220;
    w.client_height=840;
    w.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(a->ui,&w,&a->root);
    if(s<0)return s;
    s=d2_layout(a,a->root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
    if(s==0)s=d2_widget(a,a->root,SC_UI_MENUBAR,"",0,&a->menubar);
    if(s==0)s=d2_layout(a,a->menubar,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,0,22);
    if(s==0)s=d2_widget(a,a->root,SC_UI_TOOLBAR,"",0,&a->toolbar);
    if(s==0)s=d2_layout(a,a->toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,32,0,0,28);
    ScUiNode targetrow,label;
    if(s==0)s=d2_widget(a,a->root,SC_UI_PANEL,"",0,&targetrow);
    if(s==0)s=d2_layout(a,targetrow,SC_UI_HBOX,SC_UI_AUTO,30,0,0,28);
    if(s==0)s=d2_widget(a,targetrow,SC_UI_LABEL,"Target",0,&label);
    if(s==0)s=d2_layout(a,label,SC_UI_ABSOLUTE,54,28,0,0,0);
    if(s==0)s=d2_widget(a,targetrow,SC_UI_TEXTBOX,"",D2_LAUNCH,&a->commandbox);
    if(s==0)s=d2_layout(a,a->commandbox,SC_UI_ABSOLUTE,SC_UI_AUTO,28,1,80,24);
    if(s==0)s=d2_button(a,targetrow,"Browse...",D2_BROWSE,84);
    if(s==0)s=d2_button(a,targetrow,"Launch",D2_LAUNCH,76);
    ScUiNode nav;
    if(s==0)s=d2_widget(a,a->root,SC_UI_PANEL,"",0,&nav);
    if(s==0)s=d2_layout(a,nav,SC_UI_HBOX,SC_UI_AUTO,30,0,0,28);
    if(s==0)s=d2_widget(a,nav,SC_UI_LABEL,"Address",0,&label);
    if(s==0)s=d2_layout(a,label,SC_UI_ABSOLUTE,60,28,0,0,0);
    if(s==0)s=d2_widget(a,nav,SC_UI_TEXTBOX,"eip",D2_GOTO_CODE,&a->addressbox);
    if(s==0)s=d2_layout(a,a->addressbox,SC_UI_ABSOLUTE,SC_UI_AUTO,28,1,80,24);
    if(s==0)s=ScUiNodeSetFont(a->ui,a->addressbox,SC_UI_FONT_CODE);
    if(s==0)s=d2_button(a,nav,"Code",D2_GOTO_CODE,64);
    if(s==0)s=d2_button(a,nav,"Memory",D2_GOTO_MEMORY,76);
    if(s==0)s=d2_button(a,nav,"Add BP",D2_BREAK_ADD,76);
    if(s==0)s=d2_button(a,nav,"Remove BP",D2_BREAK_REMOVE,92);
    ScUiNode scroll;
    if(s==0)s=d2_widget(a,a->root,SC_UI_SCROLL,"",0,&scroll);
    if(s==0)s=d2_layout(a,scroll,SC_UI_SCROLL_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
    if(s==0)s=d2_widget(a,scroll,SC_UI_PANEL,"",0,&a->workspace);
    if(s==0)s=d2_layout(a,a->workspace,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,600,580);
    if(s==0)s=d2_widget(a,a->workspace,SC_UI_PANEL,"",0,&a->top);
    if(s==0)s=d2_layout(a,a->top,SC_UI_HBOX,SC_UI_AUTO,300,3,0,140);
    if(s==0)s=d2_pane(a,D2_CODE,a->top,"Disassembly - original code bytes",D2_ACTIVATE_CODE);
    if(s==0)s=d2_layout(a,a->panes[D2_CODE].container,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,3,260,0);
    ScUiNode split;
    if(s==0)s=d2_widget(a,a->top,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=d2_layout(a,split,SC_UI_ABSOLUTE,5,SC_UI_AUTO,0,5,0);
    if(s==0)s=d2_pane(a,D2_REGISTERS,a->top,"Registers - same paused generation",0);
    if(s==0)s=d2_layout(a,a->panes[D2_REGISTERS].container,SC_UI_VBOX,300,SC_UI_AUTO,0,190,0);
    if(s==0)s=d2_widget(a,a->workspace,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=d2_layout(a,split,SC_UI_ABSOLUTE,SC_UI_AUTO,5,0,0,5);
    if(s==0)s=d2_widget(a,a->workspace,SC_UI_PANEL,"",0,&a->bottom);
    if(s==0)s=d2_layout(a,a->bottom,SC_UI_HBOX,SC_UI_AUTO,170,2,0,100);
    if(s==0)s=d2_pane(a,D2_MEMORY,a->bottom,"Memory - ?? means unreadable",0);
    if(s==0)s=d2_layout(a,a->panes[D2_MEMORY].container,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,3,260,0);
    if(s==0)s=d2_widget(a,a->bottom,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=d2_layout(a,split,SC_UI_ABSOLUTE,5,SC_UI_AUTO,0,5,0);
    if(s==0)s=d2_pane(a,D2_STACK,a->bottom,"Stack - activate a readable value to follow",D2_ACTIVATE_STACK);
    if(s==0)s=d2_layout(a,a->panes[D2_STACK].container,SC_UI_VBOX,300,SC_UI_AUTO,2,190,0);
    if(s==0)s=d2_widget(a,a->workspace,SC_UI_SPLITTER,"",0,&split);
    if(s==0)s=d2_layout(a,split,SC_UI_ABSOLUTE,SC_UI_AUTO,5,0,0,5);
    if(s==0)s=d2_widget(a,a->workspace,SC_UI_DOCK,"",0,&a->dock);
    if(s==0)s=d2_layout(a,a->dock,SC_UI_VBOX,SC_UI_AUTO,170,1,0,80);
    if(s==0)s=d2_widget(a,a->dock,SC_UI_TAB,"",0,&a->tabs);
    if(s==0)s=d2_layout(a,a->tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
    const ScU32 order[3]={D2_MODULES,D2_SYMBOLS,D2_BREAKPOINTS};
    const char *titles[3]={"Modules","Symbols","Breakpoints"};
    for(ScU32 i=0;s==0 && i<3;i++){ScU32 pane=order[i];
        s=d2_pane(a,pane,a->tabs,titles[i],pane==D2_MODULES?D2_ACTIVATE_MODULE:pane==D2_SYMBOLS?D2_ACTIVATE_SYMBOL:0);
        ScUiText title=D2Text(titles[i]);
        if(s==0)s=ScUiTabAdd(a->ui,a->tabs,a->panes[pane].container,&title);
    }
    if(s==0)s=d2_widget(a,a->root,SC_UI_STATUS,"",0,&a->status);
    if(s==0)s=d2_layout(a,a->status,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,0,20);
    if(s==0)s=d2_menus(a);
    ScUiSubscription listener;
    D2Record(&listener,sizeof(listener));
    listener.phases=SC_UI_TARGET_PHASE|SC_UI_CAPTURE_PHASE|SC_UI_BUBBLE_PHASE;
    listener.procedure=d2_ui_event;
    listener.context=a;
    if(s==0)s=ScUiListen(a->ui,0,&listener,&a->listener);
    if(s==0)s=ScConfigCreate(&a->saved_layout);
    if(s==0)s=ScUiLayoutSave(a->ui,a->root,a->saved_layout);
    if(s==0)s=ScUiLayout(a->ui,a->root);
    if(s==0){D2Status(a,"Choose a real target or enter an executable command. Target starts paused.");
        a->visible=1;
    }return s;
}
