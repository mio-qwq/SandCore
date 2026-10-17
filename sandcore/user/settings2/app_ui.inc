/* 本文件只组合公开控件。布局、文字、命中、绘制和对话框均由SCUI
* 管理，主题/缩放切换不会留下应用私有像素或旧命中矩形。 */
static ScUiText text(const char *z)
{ScUiText t={sizeof(t),1,z,(ScU32)strlen(z)};
    return t;
}
static void status(const char *z)
{
    if(!strcmp(app.message,z))return;
    snprintf(app.message,sizeof(app.message),"%s",z);
    ScUiStatusPart p;
    INIT(p);
    p.part=1;
    p.width=-1;
    p.text=text(app.message);
    if(app.status)ScUiStatusSetParts(app.ui,app.status,&p,1);
}
static ScStatus error(ScStatus code,const char *operation)
{
    if(code>=0 || code==SC_UI_E_CANCELLED)return code;
    char body[384],line[384];
    snprintf(body,sizeof(body),"%s\n错误 %d",operation?operation:"操作未完成",code);
    snprintf(line,sizeof(line),"%s · 错误 %d",operation?operation:"操作未完成",code);
    status(line);
    if(app.ui && app.root)message("操作未完成",body);
    fprintf(stderr,"setup error=%d %s\n",code,operation?operation:"");
    return code;
}
static ScStatus layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex)
{
    ScUiLayoutDesc d;
    INIT(d);
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.column_span=d.row_span=1;
    d.align_h=d.align_v=SC_UI_STRETCH;
    d.flex_weight=flex;
    d.gap=6;
    return ScUiNodeSetLayout(app.ui,node,&d);
}
static ScStatus widget(ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScI32 width,ScI32 height,ScU32 flex,ScUiNode *out)
{
    ScUiWidgetDesc d;
    INIT(d);
    d.kind=kind;
    d.parent=parent;
    d.text=text(label);
    d.command=command;
    if(kind==SC_UI_BUTTON || kind==SC_UI_TEXTBOX || kind==SC_UI_EDIT || kind==SC_UI_TABLE || kind==SC_UI_SPINBOX || kind==SC_UI_CHECKBOX || kind==SC_UI_RADIO)
    d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScStatus s=ScUiWidgetCreate(app.ui,&d,out);
    return s<0?s:layout(*out,kind==SC_UI_PANEL?SC_UI_HBOX:SC_UI_ABSOLUTE,width,height,flex);
}
static ScStatus listen(ScUiNode node,ScU32 phases,AppEvent procedure,void *context)
{
    ScUiSubscription d;
    INIT(d);
    d.phases=phases;
    d.procedure=procedure;
    d.context=context;
    ScU32 token;
    return ScUiListen(app.ui,node,&d,&token);
}
static void set_text(ScUiNode node,const char *z)
{ScUiText t=text(z);
    ScUiNodeSetText(app.ui,node,&t);
}
static ScStatus node_text(ScUiNode node,char *out,ScU32 capacity)
{
    ScString *s=0;
    ScView v;
    INIT(v);
    ScStatus code=ScUiNodeCopyText(app.ui,node,&s);
    if(code==0)code=ScStringView(s,&v);
    if(code==0 && v.bytes>=capacity)code=SC32_E_RANGE;
    if(code==0){memcpy(out,v.data,v.bytes);
        out[v.bytes]=0;
    }
    ScStringRelease(s);
    return code;
}
static ScStatus menu_part(const char *title,const AppMenuEntry *entries,ScU32 count)
{
    ScUiMenu *part=0;
    ScStatus s=ScUiMenuCreate(&part);
    for(ScU32 i=0;s==0 && i<count;i++){
        ScUiMenuItem m;
        INIT(m);
        m.label=text(entries[i].label);
        m.command=entries[i].command;
        m.shortcut.size=sizeof(m.shortcut);
        m.shortcut.version=1;
        m.shortcut.key=entries[i].key;
        m.shortcut.modifiers=entries[i].modifiers;
        if(!m.command)m.flags=SC_UI_MENU_SEPARATOR;
        if(m.command && m.shortcut.key){
            ScUiCommandDesc binding;INIT(binding);binding.command=m.command;binding.label=m.label;binding.shortcut=m.shortcut;
            s=ScUiCommandSet(app.ui,&binding);if(s<0)break;
        }
        ScU32 token;
        s=ScUiMenuInsert(part,0,&m,&token);
    }
    ScUiMenuItem top;
    INIT(top);
    top.label=text(title);
    top.submenu=part;
    ScU32 token;
    if(s==0)s=ScUiMenuInsert(app.menus,0,&top,&token);
    ScUiMenuRelease(part);
    return s;
}
static ScStatus notice_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type==SC_UI_EVENT_DIALOG_COMPLETE && event->data==app.notice){
        ScUiDialog *done=app.notice;
        app.notice=0;
        /* 完成回调仍在库的活跃引用中；先脱开应用指针再领取唯一持有。 */
        ScUiDialogRelease(done);
    }
    return 0;
}
static ScStatus message(const char *title,const char *body)
{
    if(app.notice)return SC_UI_E_BUSY;
    ScUiMessageBoxDesc d;
    INIT(d);
    d.owner=app.root;
    d.title=text(title);
    d.message=text(body);
    d.completion=notice_complete;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(app.ui,&d,&dialog);
    /* OPEN最后Release会取消窗口；持有到完成或app_close，不能创建即丢弃。 */
    if(s==0)app.notice=dialog;
    else ScUiDialogRelease(dialog);
    return s;
}
static ScStatus app_event(ScUiContext *ui,const ScUiEvent *e,void *unused)
{
    (void)unused;
    if(e->type==SC_UI_EVENT_LAYOUT && e->node==app.root)responsive_update();
    if(e->type==SC_UI_EVENT_COMMAND){ScStatus s=app.command(e->command);
        if(s<0)error(s,"命令");
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_VISIBILITY){ScWindowInfo info;
        INIT(info);
        if(ScUiWindowNative(ui,app.root,&info)==0)app.visible=(info.state&SC32_WINDOW_VISIBLE)!=0;
    }
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST){ScStatus s=app.event?app.event(ui,e,0):0;
        if(s==SC_UI_VETO)return s;
        app.quit=1;
        return SC_UI_VETO;
    }
    return app.event?app.event(ui,e,0):0;
}
static ScStatus app_create(const char *title,AppCommand command,AppEvent event)
{
    memset(&app,0,sizeof(app));
    app.command=command;
    app.event=event;
    app.visible=1;
    ScUiContextDesc c;
    INIT(c);
    c.cache_budget_bytes=1048576;
    c.undo_budget_bytes=1048576;
    c.scheduling_quantum=64;
    ScStatus s=ScUiContextCreate(&c,&app.ui);
    ScDisplayInfo display;
    INIT(display);
    if(s==0)s=ScDisplayQuery(&display);
    ScUiWindowDesc w;
    INIT(w);
    w.title=text(title);
    w.x=24;
    w.y=16;
    w.style=SC32_STYLE_STANDARD;
    w.client_width=display.width>840?840:display.width;
    w.client_height=display.height>620?556:display.height>96?display.height-64:display.height;
    if(s==0)s=ScUiWindowCreate(app.ui,&w,&app.root);
    if(s==0)s=layout(app.root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0);
    if(s==0)s=widget(app.root,SC_UI_MENUBAR,"",0,SC_UI_AUTO,26,0,&app.menu);
    if(s==0)s=widget(app.root,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&app.body);
    if(s==0)s=layout(app.body,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1);
    if(s==0)s=widget(app.root,SC_UI_STATUS,"",0,SC_UI_AUTO,26,0,&app.status);
    if(s==0)s=ScUiMenuCreate(&app.menus);
    if(s==0)s=ScUiMenuBind(app.ui,app.menu,app.menus);
    if(s==0)s=listen(app.root,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE,app_event,0);
    return s;
}
static void app_close(void)
{
    ScUiDialog *notice=app.notice;
    app.notice=0;
    if(notice)ScUiDialogClose(notice,SC_UI_DIALOG_CANCEL);
    ScUiDialogRelease(notice);
    if(app.ui && app.root)ScUiNodeDestroy(app.ui,app.root);
    ScUiMenuRelease(app.menus);
    ScUiContextRelease(app.ui);
    free(app.bands);
    memset(&app,0,sizeof(app));
}
static ScStatus responsive_band(ScUiNode parent,ScU32 minimum_width,ScUiNode *out)
{
    if(app.band_count==app.band_capacity){ScU32 n=app.band_capacity?app.band_capacity*2:16;
        void *p=realloc(app.bands,n*sizeof(AppBand));
        if(!p)return SC32_E_MEMORY;
        app.bands=p;
        app.band_capacity=n;
    }
    ScStatus s=widget(parent,SC_UI_PANEL,"",0,SC_UI_AUTO,34,0,out);
    if(s<0)return s;
    app.bands[app.band_count].node=*out;
    app.bands[app.band_count].minimum_width=minimum_width;
    app.band_count++;
    return layout(*out,SC_UI_GRID_LAYOUT,SC_UI_AUTO,34,0);
}
static ScStatus responsive_update(void)
{
    ScUiMetrics metrics;
    INIT(metrics);
    if(ScUiMetricsQuery(app.ui,app.root,&metrics)<0 || !metrics.scale_percent)return 0;
    for(ScU32 band=0;band<app.band_count;band++){
        ScUiNodeInfo n;
        INIT(n);
        if(ScUiNodeQuery(app.ui,app.bands[band].node,&n)<0 || n.rectangle.width<=0)continue;
        ScU32 width=(ScU32)n.rectangle.width*100/metrics.scale_percent;
        ScU32 columns=width/app.bands[band].minimum_width;
        if(!columns)columns=1;
        ScU32 count=0;
        for(ScUiNode child=n.first_child;child;){ScUiNodeInfo info;
            INIT(info);
            if(ScUiNodeQuery(app.ui,child,&info)<0)break;
            count++;
            child=info.next_sibling;
        }
        if(!count)continue;
        if(columns>count)columns=count;
        ScU32 rows=(count+columns-1)/columns;
        ScUiLayoutDesc d;
        INIT(d);
        if(ScUiNodeGetLayout(app.ui,n.node,&d)<0)continue;
        if(d.columns!=columns || d.height!=(ScI32)(rows*34)){app.relayout=1;
            d.columns=columns;
            d.height=(ScI32)(rows*34);
            ScUiNodeSetLayout(app.ui,n.node,&d);
        }
        ScU32 index=0;
        for(ScUiNode child=n.first_child;child;index++){
            ScUiNodeInfo info;
            INIT(info);
            if(ScUiNodeQuery(app.ui,child,&info)<0)break;
            INIT(d);
            if(ScUiNodeGetLayout(app.ui,child,&d)==0 && (d.row!=index/columns || d.column!=index%columns)){app.relayout=1;
                d.row=index/columns;
                d.column=index%columns;
                ScUiNodeSetLayout(app.ui,child,&d);
            }
            child=info.next_sibling;
        }
    }return 0;
}
static ScStatus app_step(void)
{
    /* LAYOUT通知在真实布局发布后发出。GRID参数有变化时下一轮再
    * 正式发布，不能让它在没有下一条输入时一直停留在旧矩形。 */
    if(app.relayout){app.relayout=0;
        ScStatus s=ScUiLayout(app.ui,app.root);
        if(s<0)return s;
    }
    return ScUiContextStep(app.ui,0);
}
static ScStatus config_string(ScConfig *cfg,const char *key,const char *value)
{ScValue v;
    INIT(v);
    v.type=SC_VALUE_UTF8;
    v.data=value;
    v.bytes=(ScU32)strlen(value);
    return ScConfigSet(cfg,"draft",key,&v);
}
static ScStatus config_get(ScConfig *cfg,const char *key,char *out,ScU32 capacity)
{
    ScValue v;
    INIT(v);
    ScStatus s=ScConfigGet(cfg,"draft",key,&v);
    if(s<0)return s;
    if(v.type!=SC_VALUE_UTF8 || v.bytes>=capacity)return SC32_E_RANGE;
    memcpy(out,v.data,v.bytes);
    out[v.bytes]=0;
    return 0;
}
static ScUiFieldDesc field(ScU32 id,const char *label,const char *key,ScU32 kind,ScI32 minimum,ScI32 maximum)
{
    ScUiFieldDesc d;
    INIT(d);
    d.field=id;
    d.label=text(label);
    d.key=key;
    d.section="draft";
    d.kind=kind;
    d.minimum=minimum;
    d.maximum=maximum;
    return d;
}
static ScStatus property(const char *title,ScConfig *initial,const ScUiFieldDesc *fields,ScU32 count,AppEvent apply,void *context)
{
    if(app.notice)return SC_UI_E_BUSY;
    ScUiPropertySheetDesc d;
    INIT(d);
    d.owner=app.root;
    d.title=text(title);
    d.initial=initial;
    d.apply=apply;
    d.context=context;
    d.completion=notice_complete;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiPropertySheet(app.ui,&d,fields,count,&dialog);
    if(s==0)app.notice=dialog;
    else ScUiDialogRelease(dialog);
    return s;
}
