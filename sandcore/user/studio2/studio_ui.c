typedef struct {
    const char *label;
    U key,modifiers;
}
StCommand;
static const StCommand commands[]={
    {
        "新建文件…",'n',SC32_MOD_CONTROL
    },{
        "打开文件…",'o',SC32_MOD_CONTROL
    },{
        "保存",'s',SC32_MOD_CONTROL
    },{
        "另存为…",'s',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },{
        "全部保存",0,0
    },{
        "关闭文档",'w',SC32_MOD_CONTROL
    },
    {
        "新建工程",0,0
    },{
        "打开工程…",0,0
    },{
        "保存工程",0,0
    },{
        "添加文件…",0,0
    },{
        "从工程移除",0,0
    },{
        "工程配置…",0,0
    },
    {
        "撤销",'z',SC32_MOD_CONTROL
    },{
        "重做",'y',SC32_MOD_CONTROL
    },{
        "剪切",'x',SC32_MOD_CONTROL
    },{
        "复制",'c',SC32_MOD_CONTROL
    },{
        "粘贴",'v',SC32_MOD_CONTROL
    },{
        "全选",'a',SC32_MOD_CONTROL
    },
    {
        "查找 / 替换",'f',SC32_MOD_CONTROL
    },{
        "下一个",'g',SC32_MOD_CONTROL
    },{
        "上一个",'g',SC32_MOD_CONTROL|SC32_MOD_SHIFT
    },{
        "替换选中",0,0
    },{
        "全部替换",0,0
    },{
        "收起查找",0,0
    },
    {
        "构建",SC32_KEY_F1+4,0
    },{
        "取消作业",27,0
    },{
        "运行",SC32_KEY_F1+5,0
    },{
        "调试",SC32_KEY_F1+6,0
    },{
        "发布…",0,0
    },
    {
        "浮动输出",0,0
    },{
        "停靠输出",0,0
    },{
        "浮动工程树",0,0
    },{
        "停靠工程树",0,0
    },{
        "保存面板布局",0,0
    },{
        "恢复布局",0,0
    },{
        "SDK文档",SC32_KEY_F1,0
    },{
        "关于 Studio 2",0,0
    },{
        "退出",0,0
    }
};
static ScStatus layout(ScUiNode node,U mode,int width,int height,U flex,U dock)
{
    ScUiLayoutDesc l;
    rec(&l,sizeof(l));
    l.mode=mode;
    l.width=width;
    l.height=height;
    l.max_width=l.max_height=SC_UI_AUTO;
    l.flex_weight=flex;
    l.dock=dock;
    l.column_span=l.row_span=1;
    l.align_h=l.align_v=SC_UI_STRETCH;
    l.gap=4;
    return ScUiNodeSetLayout(st.ui,node,&l);
}
static ScStatus widget(ScUiNode parent,U kind,const char *label,U cmd,ScUiNode *out)
{
    ScUiWidgetDesc d;
    rec(&d,sizeof(d));
    d.parent=parent;
    d.kind=kind;
    d.command=cmd;
    d.text=text(label);
    if(kind==SC_UI_EDIT||kind==SC_UI_TEXTBOX||kind==SC_UI_BUTTON||kind==SC_UI_CHECKBOX)d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    if(kind==SC_UI_EDIT)d.flags|=SC_UI_EDITOR_TAB;
    return ScUiWidgetCreate(st.ui,&d,out);
}
static ScStatus doc_new(const char *name,const void *bytes,U count,U disk_revision,StDoc **out)
{
    StDoc *d=calloc(1,sizeof(*d));
    if(!d)return SC32_E_MEMORY;
    if(st.document_serial==0xffffffffu){
        free(d);
        return SC32_E_RANGE;
    }
    d->id=++st.document_serial;
    d->disk_revision=disk_revision;
    if(name)strcpy(d->path,name);
    ScUiDocumentDesc desc;
    rec(&desc,sizeof(desc));
    desc.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
    desc.undo_budget_bytes=1048576;
    desc.text.size=sizeof(desc.text);
    desc.text.version=1;
    desc.text.data=bytes;
    desc.text.bytes=count;
    ScStatus s=ScUiDocumentCreate(&desc,&d->text);
    if(s>=0)s=widget(st.tabs,SC_UI_PANEL,"",0,&d->page);
    if(s>=0)s=layout(d->page,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(s>=0)s=widget(d->page,SC_UI_EDIT,"",0,&d->edit);
    if(s>=0)s=layout(d->edit,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    if(s>=0)s=ScUiNodeSetFont(st.ui,d->edit,SC_UI_FONT_CODE);
    if(s>=0)s=ScUiEditBind(st.ui,d->edit,d->text);
    ScUiText title=text(*d->path?leaf(d->path):"未命名");
    if(s>=0)s=ScUiTabAdd(st.ui,st.tabs,d->page,&title);
    if(s<0){
        if(d->page)ScUiNodeDestroy(st.ui,d->page);
        ScUiDocumentRelease(d->text);
        free(d);
        return s;
    }
    ScUiDocumentInfo q;
    rec(&q,sizeof(q));
    ScUiDocumentQuery(d->text,&q);
    d->revision=q.revision;
    d->next=st.documents;
    st.documents=d;
    st.active=d;
    if(disk_revision)ScUiDocumentMarkSaved(d->text);
    ScUiTabSelect(st.ui,st.tabs,d->page);
    ScUiFocus(st.ui,d->edit);
    ScUiLayout(st.ui,st.root);
    refresh();
    if(out)*out=d;
    return 0;
}
static ScStatus provider(void *opaque,const ScUiDataRequest *q,ScUiDataReply *r)
{
    U diagnostics=(U)(unsigned long)opaque;
    U revision=r->revision;
    rec(r,sizeof(*r));
    rec(&r->value,sizeof(r->value));
    r->revision=revision;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(q->operation==SC_UI_DATA_COUNT||q->operation==SC_UI_DATA_CHILD_COUNT){
        r->total=diagnostics?st.error_count:q->parent==1u?st.member_count+1u:q->parent?0u:1u;
        return 0;
    }
    U index=q->index;
    if(q->operation==SC_UI_DATA_INDEX_OF){
        if(diagnostics){
            for(index=0;index<st.error_count;index++)if(st.errors[index].id==q->item)break;
            if(index==st.error_count)return SC_UI_E_NOT_FOUND;
        }
        else if(q->item==1u||q->item==2u){
            r->total=0;
            r->parent=q->item==2u?1u:0u;
            return 0;
        }
        else{
            for(index=0;index<st.member_count;index++)if(st.members[index].id+2u==q->item)break;
            if(index==st.member_count)return SC_UI_E_NOT_FOUND;
            index++;
            r->parent=1;
        }
        r->total=index;
        return 0;
    }
    if(q->operation==SC_UI_DATA_CHILD_AT){
        if(diagnostics){
            if(index>=st.error_count)return SC_UI_E_NOT_FOUND;
            r->item=st.errors[index].id;
        }
        else if(!q->parent){
            if(index)return SC_UI_E_NOT_FOUND;
            r->item=1;
        }
        else if(q->parent==1u&&index==0u){
            r->item=2;
            r->parent=1;
        }
        else if(q->parent==1u&&index-1u<st.member_count){
            r->item=st.members[index-1u].id+2u;
            r->parent=1;
        }
        else return SC_UI_E_NOT_FOUND;
        return 0;
    }
    const char *label=0;
    U number=0;
    if(diagnostics){
        StDiagnostic *d=0;
        for(U i=0;i<st.error_count;i++)if(st.errors[i].id==q->item)d=st.errors+i;
        if(!d)return SC_UI_E_NOT_FOUND;
        label=q->field==0?d->path:q->field==2?d->message:0;
        number=d->line;
        r->item=d->id;
    }
    else if(q->item==1){
        label=st.project_path[0]?leaf(st.project_path):"未保存工程";
        r->item=1;
        r->state|=SC_UI_EXPANDED;
    }
    else if(q->item==2){
        label=leaf(st.manifest_path);
        r->item=2;
        r->parent=1;
    }
    else{
        for(U i=0;i<st.member_count;i++)if(st.members[i].id+2u==q->item){
            label=leaf(st.members[i].path);
            r->item=q->item;
            r->parent=1;
            break;
        }
        if(!label)return SC_UI_E_NOT_FOUND;
    }
    rec(&r->value,sizeof(r->value));
    if(label){
        r->value.type=SC_VALUE_UTF8;
        r->value.data=label;
        r->value.bytes=(U)strlen(label);
    }
    else{
        r->value.type=SC_VALUE_U32;
        r->value.lo=number;
    }
    return 0;
}
static ScStatus menu_items(ScUiMenu *menu,const U *items,U count)
{
    for(U i=0;i<count;i++){
        ScUiMenuItem m;
        rec(&m,sizeof(m));
        if(!items[i])m.flags=SC_UI_MENU_SEPARATOR;
        else{
            U n=items[i]-S_NEW;
            m.command=items[i];
            m.label=text(commands[n].label);
            rec(&m.shortcut,sizeof(m.shortcut));
            m.shortcut.key=commands[n].key;
            m.shortcut.modifiers=commands[n].modifiers;
        }
        U id=0;
        ScStatus s=ScUiMenuInsert(menu,0,&m,&id);
        if(s<0)return s;
    }
    return 0;
}
static ScStatus menus(void)
{
    ScStatus s=0;
    for(U i=0;i<7&&s>=0;i++)s=ScUiMenuCreate(st.menus+i);
    const U file[]={
        S_NEW,S_OPEN,S_SAVE,S_SAVE_AS,S_SAVE_ALL,S_CLOSE,0,S_EXIT
    },project[]={
        S_PROJECT_NEW,S_PROJECT_OPEN,S_PROJECT_SAVE,0,S_ADD,S_REMOVE,S_CONFIG
    },edit[]={
        S_UNDO,S_REDO,S_CUT,S_COPY,S_PASTE,S_ALL,0,S_FIND,S_NEXT,S_PREVIOUS,S_REPLACE,S_REPLACE_ALL
    },
    build[]={
        S_BUILD,S_CANCEL,0,S_RUN,S_DEBUG,S_PUBLISH
    },view[]={
        S_FLOAT_TREE,S_DOCK_TREE,S_FLOAT_OUTPUT,S_DOCK_OUTPUT,0,S_LAYOUT_SAVE,S_LAYOUT_RESET,S_SDK,S_ABOUT
    },tools[]={
        S_NEW,S_OPEN,S_SAVE,0,S_UNDO,S_REDO,0,S_BUILD,S_CANCEL,S_RUN,S_DEBUG
    };
    if(s>=0)s=menu_items(st.menus[1],file,sizeof(file)/sizeof(file[0]));
    if(s>=0)s=menu_items(st.menus[2],project,sizeof(project)/sizeof(project[0]));
    if(s>=0)s=menu_items(st.menus[3],edit,sizeof(edit)/sizeof(edit[0]));
    if(s>=0)s=menu_items(st.menus[4],build,sizeof(build)/sizeof(build[0]));
    if(s>=0)s=menu_items(st.menus[5],view,sizeof(view)/sizeof(view[0]));
    if(s>=0)s=menu_items(st.menus[6],tools,sizeof(tools)/sizeof(tools[0]));
    const char *names[]={
        "文件 (&F)","工程 (&P)","编辑 (&E)","构建 (&B)","视图 (&V)"
    };
    for(U i=0;i<5&&s>=0;i++){
        ScUiMenuItem m;
        rec(&m,sizeof(m));
        m.label=text(names[i]);
        m.submenu=st.menus[i+1];
        U id=0;
        s=ScUiMenuInsert(st.menus[0],0,&m,&id);
    }
    if(s>=0)s=ScUiMenuBind(st.ui,st.menu,st.menus[0]);
    if(s>=0)s=ScUiToolbarBind(st.ui,st.toolbar,st.menus[6]);
    return s;
}
static void status(const char *message)
{
    if(message){
        strncpy(st.message,message,sizeof(st.message)-1);
        st.message[sizeof(st.message)-1]=0;
    }
    if(st.ui&&st.status){
        ScUiStatusPart p;
        rec(&p,sizeof(p));
        p.width=SC_UI_AUTO;
        p.text=text(st.message);
        ScUiStatusSetParts(st.ui,st.status,&p,1);
    }
}
static void refresh(void)
{
    if(!st.ui||st.busy_refresh)return;
    st.busy_refresh=1;
    ScUiDocumentInfo doc;
    rec(&doc,sizeof(doc));
    if(st.active)ScUiDocumentQuery(st.active->text,&doc);
    char title[192];
    snprintf(title,sizeof(title),"%s%s — Studio 2",st.active&&(doc.flags&SC_UI_DOCUMENT_DIRTY)?"* ":"",st.active?leaf(st.active->path):"工程工作区");
    if(strcmp(title,st.title)){
        strcpy(st.title,title);
        ScUiText t=text(title);
        ScUiWindowSetTitle(st.ui,st.root,&t);
    }
    for(StDoc *d=st.documents;d;d=d->next){
        ScUiDocumentInfo q;
        rec(&q,sizeof(q));
        if(ScUiDocumentQuery(d->text,&q)>=0){
            char label[96];
            snprintf(label,sizeof(label),"%s%s",q.flags&SC_UI_DOCUMENT_DIRTY?"* ":"",d->path[0]?leaf(d->path):"未命名");
            if(strcmp(label,d->label)){
                ScUiText tab=text(label);
                if(ScUiTabSetText(st.ui,st.tabs,d->page,&tab)>=0)strcpy(d->label,label);
            }
        }
    }
    for(U i=0;i<S_COUNT-S_NEW;i++){
        U cmd=S_NEW+i,disabled=0;
        if(cmd>=S_SAVE&&cmd<=S_CLOSE)disabled=!st.active;
        if(cmd>=S_UNDO&&cmd<=S_HIDE_FIND)disabled=!st.active;
        if(cmd==S_UNDO)disabled=!st.active||!doc.can_undo;
        if(cmd==S_REDO)disabled=!st.active||!doc.can_redo;
        if(cmd==S_BUILD)disabled=st.build.phase||st.io||!st.member_count||st.running.capture.job;
        if(cmd==S_CANCEL)disabled=!st.build.phase&&!st.io&&!st.running.capture.job&&!st.build.filejob;
        if(cmd==S_RUN||cmd==S_DEBUG||cmd==S_PUBLISH)disabled=!st.success||st.success_epoch!=st.epoch||st.build.phase||st.running.capture.job||st.io||st.build.filejob;
        if(cmd>=S_PROJECT_NEW&&cmd<=S_CONFIG)disabled=st.build.phase||st.io;
        U flags=disabled?SC_UI_MENU_DISABLED:0;
        if(st.command_flags[i]==flags+1u)continue;
        ScUiCommandDesc d;
        rec(&d,sizeof(d));
        d.command=cmd;
        d.flags=flags;
        d.label=text(commands[i].label);
        rec(&d.shortcut,sizeof(d.shortcut));
        d.shortcut.key=commands[i].key;
        d.shortcut.modifiers=commands[i].modifiers;
        if(ScUiCommandSet(st.ui,&d)>=0)st.command_flags[i]=flags+1u;
    }
    st.busy_refresh=0;
}
static ScStatus ui_build(void)
{
    ScUiContextDesc c;
    rec(&c,sizeof(c));
    c.cache_budget_bytes=2097152;
    c.undo_budget_bytes=1048576;
    c.scheduling_quantum=64;
    ScStatus s=ScUiContextCreate(&c,&st.ui);
    if(s<0)return s;
    ScUiWindowDesc w;
    rec(&w,sizeof(w));
    w.title=text("Studio 2");
    w.x=70;
    w.y=50;
    w.client_width=1280;
    w.client_height=840;
    w.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(st.ui,&w,&st.root);
    if(s>=0)s=layout(st.root,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,0,0);
    #define NODE(parent,kind,field,mode,width,height,flex,dock) do { if(s>=0)s=widget(parent,kind,"",0,&st.field);if(s>=0)s=layout(st.field,mode,width,height,flex,dock); } while(0)
    NODE(st.root,SC_UI_MENUBAR,menu,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_TOP);
    NODE(st.root,SC_UI_TOOLBAR,toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,32,0,SC_UI_DOCK_TOP);
    NODE(st.root,SC_UI_STATUS,status,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,SC_UI_DOCK_BOTTOM);
    NODE(st.root,SC_UI_PANEL,search,SC_UI_VBOX,SC_UI_AUTO,92,0,SC_UI_DOCK_TOP);
    ScUiNode findrow=0,replacerow=0,optionsrow=0;
    if(s>=0)s=widget(st.search,SC_UI_PANEL,"",0,&findrow);
    if(s>=0)s=layout(findrow,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    NODE(findrow,SC_UI_TEXTBOX,needle,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    const char *findlabels[]={
        "上一个","下一个"
    };
    U findids[]={
        S_PREVIOUS,S_NEXT
    };
    for(U i=0;i<2&&s>=0;i++){
        ScUiNode button=0;
        s=widget(findrow,SC_UI_BUTTON,findlabels[i],findids[i],&button);
        if(s>=0)s=layout(button,SC_UI_ABSOLUTE,70,SC_UI_AUTO,0,0);
    }
    if(s>=0)s=widget(st.search,SC_UI_PANEL,"",0,&replacerow);
    if(s>=0)s=layout(replacerow,SC_UI_HBOX,SC_UI_AUTO,28,0,0);
    NODE(replacerow,SC_UI_TEXTBOX,replacement,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    const char *labels[]={
        "替换","全部替换"
    };
    U ids[]={
        S_REPLACE,S_REPLACE_ALL
    };
    for(U i=0;i<2&&s>=0;i++){
        ScUiNode button=0;
        s=widget(replacerow,SC_UI_BUTTON,labels[i],ids[i],&button);
        if(s>=0)s=layout(button,SC_UI_ABSOLUTE,70,SC_UI_AUTO,0,0);
    }
    if(s>=0)s=widget(st.search,SC_UI_PANEL,"",0,&optionsrow);
    if(s>=0)s=layout(optionsrow,SC_UI_HBOX,SC_UI_AUTO,24,0,0);
    NODE(optionsrow,SC_UI_CHECKBOX,caseless,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    ScUiText hint=text("忽略大小写");
    if(s>=0)s=ScUiNodeSetText(st.ui,st.caseless,&hint);
    ScUiNode hide=0;
    if(s>=0)s=widget(optionsrow,SC_UI_BUTTON,"收起",S_HIDE_FIND,&hide);
    if(s>=0)s=layout(hide,SC_UI_ABSOLUTE,70,SC_UI_AUTO,0,0);
    if(s>=0)s=ScUiNodeSetState(st.ui,st.search,SC_UI_VISIBLE,0);
    NODE(st.root,SC_UI_DOCK,body,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    NODE(st.body,SC_UI_PANEL,upper,SC_UI_HBOX,SC_UI_AUTO,SC_UI_AUTO,1,0);
    NODE(st.upper,SC_UI_DOCK,tree_panel,SC_UI_VBOX,230,SC_UI_AUTO,0,0);
    if(s>=0)s=widget(st.tree_panel,SC_UI_TREE,"",S_OPEN,&st.tree);
    if(s>=0)s=layout(st.tree,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0);
    NODE(st.upper,SC_UI_SPLITTER,tree_split,SC_UI_ABSOLUTE,5,SC_UI_AUTO,0,0);
    NODE(st.upper,SC_UI_TAB,tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,0);
    NODE(st.body,SC_UI_SPLITTER,lower_split,SC_UI_ABSOLUTE,SC_UI_AUTO,5,0,0);
    NODE(st.body,SC_UI_DOCK,out_panel,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,220,0,0);
    NODE(st.out_panel,SC_UI_TAB,out_tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    NODE(st.out_tabs,SC_UI_PANEL,output_page,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0);
    NODE(st.output_page,SC_UI_EDIT,output,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    NODE(st.out_tabs,SC_UI_PANEL,diagnostic_page,SC_UI_DOCK_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0);
    if(s>=0)s=widget(st.diagnostic_page,SC_UI_TABLE,"",S_OPEN,&st.diagnostics);
    if(s>=0)s=layout(st.diagnostics,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,SC_UI_DOCK_FILL);
    #undef NODE
    ScUiText ot=text("输出"),dt=text("诊断");
    if(s>=0)s=ScUiTabAdd(st.ui,st.out_tabs,st.output_page,&ot);
    if(s>=0)s=ScUiTabAdd(st.ui,st.out_tabs,st.diagnostic_page,&dt);
    ScUiDocumentDesc d;
    rec(&d,sizeof(d));
    d.text=text("");
    if(s>=0)s=ScUiDocumentCreate(&d,&st.output_document);
    if(s>=0)s=ScUiEditBind(st.ui,st.output,st.output_document);
    if(s>=0)s=ScUiNodeSetState(st.ui,st.output,SC_UI_READONLY,SC_UI_READONLY);
    if(s>=0)s=ScUiNodeSetFont(st.ui,st.output,SC_UI_FONT_CODE);
    ScUiProviderDesc p;
    rec(&p,sizeof(p));
    p.flags=SC_UI_PROVIDER_TREE;
    p.procedure=provider;
    if(s>=0)s=ScUiProviderCreate(&p,&st.tree_provider);
    p.flags=0;
    p.context=(void *)1;
    if(s>=0)s=ScUiProviderCreate(&p,&st.diag_provider);
    if(s>=0)s=ScUiSelectionCreate(st.tree_provider,SC_UI_SELECTION_ALLOW_EMPTY,&st.tree_selection);
    if(s>=0)s=ScUiSelectionCreate(st.diag_provider,SC_UI_SELECTION_ALLOW_EMPTY,&st.diag_selection);
    if(s>=0)s=ScUiViewBind(st.ui,st.tree,st.tree_provider,st.tree_selection);
    if(s>=0)s=ScUiTreeExpand(st.ui,st.tree,1,1);
    if(s>=0)s=ScUiViewBind(st.ui,st.diagnostics,st.diag_provider,st.diag_selection);
    ScUiColumnDesc cols[3];
    memset(cols,0,sizeof(cols));
    const char *names[]={
        "文件","行","诊断"
    };
    for(U i=0;i<3;i++){
        rec(cols+i,sizeof(cols[i]));
        cols[i].column=i+1;
        cols[i].field=i;
        cols[i].title=text(names[i]);
        cols[i].width=i==0?240:i==1?64:640;
        cols[i].min_width=32;
        cols[i].max_width=4096;
        cols[i].format=i==1?SC_UI_FORMAT_U32:SC_UI_FORMAT_TEXT;
    }
    if(s>=0)s=ScUiTableSetColumns(st.ui,st.diagnostics,cols,3);
    if(s>=0)s=menus();
    ScUiSubscription sub;
    rec(&sub,sizeof(sub));
    sub.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    sub.procedure=event;
    U token=0;
    if(s>=0)s=ScUiListen(st.ui,st.root,&sub,&token);
    if(s>=0)s=ScUiLayout(st.ui,st.root);
    if(s>=0){
        status("就绪：添加独立C/头文件，配置manifest，再按F5构建");
        refresh();
    }
    return s;
}
