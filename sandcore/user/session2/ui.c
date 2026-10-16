#include "S2APP.H"
static ScStatus s2_layout(S2App *a,ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex,ScI32 min_width,ScI32 min_height)
{
    ScUiLayoutDesc d;
    s2_record(&d,sizeof(d));
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.min_width=min_width;
    d.min_height=min_height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.flex_weight=flex;
    d.align_h=d.align_v=SC_UI_STRETCH;
    d.column_span=d.row_span=1;
    return ScUiNodeSetLayout(a->ui,node,&d);
}
static ScStatus s2_widget(S2App *a,ScUiNode parent,ScU32 kind,const char *text,ScU32 command,ScUiNode *out)
{
    ScUiWidgetDesc d;
    s2_record(&d,sizeof(d));
    d.parent=parent;
    d.kind=kind;
    d.text=s2_text(text);
    d.command=command;
    ScStatus status=ScUiWidgetCreate(a->ui,&d,out);
    return status<0?status:s2_layout(a,*out,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,0,0,0);
}
static ScStatus s2_button(S2App *a,ScUiNode parent,const char *text,ScU32 command,ScUiNode *out)
{ScUiNode node;
    ScStatus status=s2_widget(a,parent,SC_UI_BUTTON,text,command,&node);
    if(status==0)status=s2_layout(a,node,SC_UI_ABSOLUTE,132,32,0,88,28);
    if(out)*out=node;
    return status;
}
static void s2_status(S2App *a,const char *message)
{
    snprintf(a->message,sizeof(a->message),"%s",message);
    if(!a->status)return;
    ScUiStatusPart p;
    s2_record(&p,sizeof(p));
    p.part=1;
    p.width=-1;
    p.text=s2_text(a->message);
    ScUiStatusSetParts(a->ui,a->status,&p,1);
}
static ScStatus s2_queue(S2App *a,ScU32 id)
{S2Command *c=malloc(sizeof(*c));
    if(!c)return SC_UI_E_MEMORY;
    c->next=0;
    c->id=id;
    if(a->last_command)a->last_command->next=c;
    else a->commands=c;
    a->last_command=c;
    return 0;
}
static ScStatus s2_dialog_complete(ScUiContext *ui,const ScUiEvent *e,void *context)
{(void)ui;
    S2App *a=context;
    if(e->type==SC_UI_EVENT_DIALOG_COMPLETE){a->dialog_done=1;
        a->dialog_command=e->command;
    }return 0;
}
static void s2_error(S2App *a,const char *action,ScStatus status)
{
    char message[240];
    snprintf(message,sizeof(message),"%s failed (%d). Existing sessions are retained.",action,status);
    s2_status(a,message);
    if(a->dialog || a->closed)return;
    ScUiMessageBoxDesc d;
    s2_record(&d,sizeof(d));
    d.owner=a->root;
    d.title=s2_text("Session operation");
    d.message=s2_text(message);
    d.completion=s2_dialog_complete;
    d.context=a;
    a->dialog_kind=S_DIALOG_INFO;
    if(ScUiMessageBox(a->ui,&d,&a->dialog)<0)a->dialog_kind=0;
}
static void s2_properties_update(S2App *a)
{
    ScSessionInfo row;
    char text[512];
    ScStatus status=s2_selected(a,&row);
    if(status==0)snprintf(text,sizeof(text),"%s\n\nSession  %u\nUID / GID  %d / %d\n\n%s\n%s\n\n%u members\nGeneration  %u\n\nSwitch keeps background work.\nLog out closes this session's processes.",row.name,row.id,row.uid,row.gid,row.kind==1?"External SYSTEM":row.kind==2?"Locked desktop":"Normal account",row.closing?"Closing":row.visible?"Visible":"Hidden",row.members,row.generation);
    else snprintf(text,sizeof(text),"SESSION DETAILS\n\nChoose a session to see its actual identity and state.\n\nOnly sessions permitted by the kernel are listed.");
    ScUiText value=s2_text(text);
    ScUiNodeSetText(a->ui,a->details,&value);
}
static ScStatus s2_provider(void *context,const ScUiDataRequest *q,ScUiDataReply *r)
{
    S2App *a=context;
    r->revision=a->revision;
    if(q->operation==SC_UI_DATA_COUNT){r->total=a->count;
        return 0;
    }if(q->operation==SC_UI_DATA_CHILD_COUNT){r->total=0;
        return 0;
    }
    const ScSessionInfo *rows=s2_rows(a->rows),*row=q->operation==SC_UI_DATA_CHILD_AT && q->index<a->count?rows+q->index:s2_find(a->rows,a->count,q->item);
    if(!row)return SC_UI_END;
    r->item=row->id;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    r->total=(ScU32)(row-rows);
    if(q->operation!=SC_UI_DATA_CELL)return 0;
    const char *text=a->cell;
    if(q->field==0)text=row->name;
    else if(q->field==1)snprintf(a->cell,sizeof(a->cell),"%d",row->uid);
    else if(q->field==2)snprintf(a->cell,sizeof(a->cell),"%u",row->id);
    else if(q->field==3)snprintf(a->cell,sizeof(a->cell),"%s%s",row->closing?"Closing":row->visible?"Visible":"Hidden",row->kind==1?" / SYSTEM":row->kind==2?" / Locked":"");
    else if(q->field==4)snprintf(a->cell,sizeof(a->cell),"%u",row->members);
    else if(q->field==5)snprintf(a->cell,sizeof(a->cell),"%u",row->generation);
    else return SC_UI_E_INVALID;
    r->value.type=SC_VALUE_UTF8;
    r->value.data=text;
    r->value.bytes=(ScU32)strlen(text);
    return 0;
}
static ScStatus s2_menu_item(S2App *a,ScUiMenu *menu,ScU32 command,const char *label,ScU32 key,ScU32 modifiers)
{
    ScUiMenuItem item;
    s2_record(&item,sizeof(item));
    item.command=command;
    item.label=item.help=s2_text(label);
    s2_record(&item.shortcut,sizeof(item.shortcut));
    item.shortcut.key=key;
    item.shortcut.modifiers=modifiers;
    ScU32 id;
    ScStatus status=ScUiMenuInsert(menu,0,&item,&id);
    if(status<0)return status;
    ScUiCommandDesc d;
    s2_record(&d,sizeof(d));
    d.command=command;
    d.label=item.label;
    d.shortcut=item.shortcut;
    return ScUiCommandSet(a->ui,&d);
}
static ScStatus s2_menus(S2App *a,ScUiNode menubar)
{
    ScUiMenu **menus[]={&a->menu,&a->account_menu,&a->session_menu,&a->help_menu};
    for(ScU32 i=0;i<4;i++){ScStatus status=ScUiMenuCreate(menus[i]);
        if(status<0)return status;
    }
    struct {ScU32 group,id,key,modifiers;
        const char *label;
    } items[]={
        {0,S_REQUEST_SIGNIN,10,SC32_MOD_CONTROL,"&Sign in"},{0,S_CANCEL,27,0,"&Cancel verification"},{0,S_CLEAR,'l',SC32_MOD_CONTROL,"C&lear input"},{0,S_CLOSE,'q',SC32_MOD_CONTROL,"&Close"},
        {1,S_REFRESH,SC32_KEY_F1+4,0,"&Refresh sessions"},{1,S_SWITCH,0,0,"&Switch desktop"},{1,S_LOGOUT,SC32_KEY_DELETE,0,"&Log out..."},{1,S_LOCK,'l',SC32_MOD_CONTROL|SC32_MOD_SHIFT,"Loc&k desktop"},{1,S_PROPERTIES,0,0,"&Properties..."},{2,S_ABOUT,SC32_KEY_F1,0,"&About"}};
    ScUiMenu *groups[]={a->account_menu,a->session_menu,a->help_menu};
    for(ScU32 i=0;i<sizeof(items)/sizeof(items[0]);i++){if(a->locked && items[i].group==1)continue;
        ScStatus status=s2_menu_item(a,groups[items[i].group],items[i].id,items[i].label,items[i].key,items[i].modifiers);
        if(status<0)return status;
    }
    ScUiCommandDesc signin;
    s2_record(&signin,sizeof(signin));
    signin.command=S_SIGNIN;
    signin.label=s2_text("Sign in");
    ScStatus command_status=ScUiCommandSet(a->ui,&signin);
    if(command_status<0)return command_status;
    const char *names[]={"&Account","&Sessions","&Help"};
    for(ScU32 i=0;i<3;i++){if(a->locked && i==1)continue;
        ScUiMenuItem item;
        s2_record(&item,sizeof(item));
        item.label=s2_text(names[i]);
        item.submenu=groups[i];
        ScU32 id;
        ScStatus status=ScUiMenuInsert(a->menu,0,&item,&id);
        if(status<0)return status;
    }return ScUiMenuBind(a->ui,menubar,a->menu);
}
static ScStatus s2_build(S2App *a)
{
    s2_record(&a->self,sizeof(a->self));
    ScStatus status=ScIdentityQuery(&a->self);
    if(status<0)return status;
    a->locked=a->self.uid<0 && a->self.realm==SC32_BROKER_REALM_SERVICE && !(a->self.flags&SC32_IDENTITY_MODULES);
    a->revision=1;
    a->visible=1;
    a->auth_ui_mode=UINT_MAX;
    a->auth_ui_flags=UINT_MAX;
    ScUiContextDesc c;
    s2_record(&c,sizeof(c));
    c.flags=SC_UI_STOP_LAST_ROOT;
    c.cache_budget_bytes=262144;
    c.scheduling_quantum=32;
    status=ScUiContextCreate(&c,&a->ui);
    if(status<0)return status;
    ScUiWindowDesc window;
    s2_record(&window,sizeof(window));
    window.title=s2_text(a->locked?"SandCore - Sign in":"SandCore - Login and sessions");
    window.client_width=970;
    window.client_height=650;
    window.style=SC32_STYLE_STANDARD;
    status=ScUiWindowCreate(a->ui,&window,&a->root);
    if(status<0)return status;
#define S2_TRY(expression) if((status=(expression))<0)return status
    S2_TRY(s2_layout(a,a->root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0));
    ScUiNode menubar;
    S2_TRY(s2_widget(a,a->root,SC_UI_MENUBAR,"",0,&menubar));
    S2_TRY(s2_layout(a,menubar,SC_UI_ABSOLUTE,SC_UI_AUTO,25,0,0,22));
    S2_TRY(s2_menus(a,menubar));
    S2_TRY(s2_widget(a,a->root,SC_UI_LABEL,"",0,&a->identity));
    S2_TRY(s2_layout(a,a->identity,SC_UI_ABSOLUTE,SC_UI_AUTO,46,0,0,36));
    S2_TRY(ScUiNodeSetFont(a->ui,a->identity,SC_UI_FONT_HEADING));
    char identity[160];
    if(a->locked)snprintf(identity,sizeof(identity),"SIGN IN  |  A separate desktop for each login");
    else snprintf(identity,sizeof(identity),"%s  |  UID %d  |  Session %u",a->self.flags&SC32_IDENTITY_MODULES?"External hardware SYSTEM":a->self.uid==0?"root - normal account":"Normal account",a->self.uid,a->self.session);
    ScUiText text=s2_text(identity);
    S2_TRY(ScUiNodeSetText(a->ui,a->identity,&text));
    S2_TRY(s2_widget(a,a->root,SC_UI_TAB,"",0,&a->tabs));
    S2_TRY(s2_layout(a,a->tabs,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1,0,150));
    S2_TRY(s2_widget(a,a->tabs,SC_UI_SCROLL,"",0,&a->login_page));
    S2_TRY(s2_layout(a,a->login_page,SC_UI_SCROLL_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1,0,0));
    ScUiNode form;
    S2_TRY(s2_widget(a,a->login_page,SC_UI_PANEL,"",0,&form));
    S2_TRY(s2_layout(a,form,SC_UI_VBOX,580,SC_UI_AUTO,0,220,330));
    ScUiLayoutDesc layout;
    s2_record(&layout,sizeof(layout));
    S2_TRY(ScUiNodeGetLayout(a->ui,form,&layout));
    layout.max_width=640;
    layout.align_h=SC_UI_CENTER;
    layout.padding[0]=layout.padding[1]=layout.padding[2]=layout.padding[3]=18;
    layout.gap=8;
    S2_TRY(ScUiNodeSetLayout(a->ui,form,&layout));
    ScUiNode label;
    S2_TRY(s2_widget(a,form,SC_UI_LABEL,"Open a new account desktop. Background sessions keep running.",0,&label));
    S2_TRY(s2_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,42,0,0,32));
    S2_TRY(s2_widget(a,form,SC_UI_LABEL,"Username",0,&label));
    S2_TRY(s2_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,22,0,0,18));
    S2_TRY(s2_widget(a,form,SC_UI_TEXTBOX,"",0,&a->username));
    S2_TRY(s2_layout(a,a->username,SC_UI_ABSOLUTE,SC_UI_AUTO,34,0,120,30));
    S2_TRY(ScUiLabelAssociate(a->ui,label,a->username));
    S2_TRY(s2_widget(a,form,SC_UI_LABEL,"Password",0,&label));
    S2_TRY(s2_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,22,0,0,18));
    ScUiSecretDesc secret;
    s2_record(&secret,sizeof(secret));
    secret.parent=form;
    secret.max_bytes=128;
    S2_TRY(ScUiSecretCreate(a->ui,&secret,&a->password));
    S2_TRY(s2_layout(a,a->password,SC_UI_ABSOLUTE,SC_UI_AUTO,34,0,120,30));
    S2_TRY(ScUiLabelAssociate(a->ui,label,a->password));
    text=s2_text("Password");
    S2_TRY(ScUiAccessibleSetName(a->ui,a->password,&text));
    ScUiNode buttons;
    S2_TRY(s2_widget(a,form,SC_UI_PANEL,"",0,&buttons));
    S2_TRY(s2_layout(a,buttons,SC_UI_FLOW,SC_UI_AUTO,42,0,0,32));
    S2_TRY(s2_button(a,buttons,"Sign in",S_SIGNIN,&a->signin));
    S2_TRY(s2_button(a,buttons,"Cancel",S_CANCEL,&a->cancel));
    S2_TRY(s2_button(a,buttons,"Clear input",S_CLEAR,0));
    ScUiSecretSubmit submit;
    s2_record(&submit,sizeof(submit));
    submit.button=a->signin;
    submit.procedure=s2_submit;
    submit.context=a;
    S2_TRY(ScUiSecretBindSubmit(a->ui,a->password,&submit));
    S2_TRY(ScUiButtonSetDefault(a->ui,a->root,a->signin));
    S2_TRY(s2_widget(a,form,SC_UI_PROGRESS,"",0,&a->progress));
    S2_TRY(s2_layout(a,a->progress,SC_UI_ABSOLUTE,SC_UI_AUTO,18,0,0,16));
    S2_TRY(s2_widget(a,form,SC_UI_LABEL,"Passwords stay in the secret field until submitted, then are cleared.",0,&label));
    S2_TRY(s2_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,40,0,0,30));
    text=s2_text("Sign in");
    S2_TRY(ScUiTabAdd(a->ui,a->tabs,a->login_page,&text));
    S2_TRY(s2_widget(a,a->tabs,SC_UI_PANEL,"",0,&a->sessions_page));
    S2_TRY(s2_layout(a,a->sessions_page,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0));
    ScUiNode session_buttons;
    S2_TRY(s2_widget(a,a->sessions_page,SC_UI_PANEL,"",0,&session_buttons));
    S2_TRY(s2_layout(a,session_buttons,SC_UI_FLOW,SC_UI_AUTO,40,0,0,32));
    S2_TRY(s2_button(a,session_buttons,"Refresh",S_REFRESH,0));
    S2_TRY(s2_button(a,session_buttons,"Switch",S_SWITCH,0));
    S2_TRY(s2_button(a,session_buttons,"Log out...",S_LOGOUT,0));
    S2_TRY(s2_button(a,session_buttons,"Lock",S_LOCK,0));
    ScUiNode body;
    S2_TRY(s2_widget(a,a->sessions_page,SC_UI_PANEL,"",0,&body));
    S2_TRY(s2_layout(a,body,SC_UI_HBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0));
    S2_TRY(s2_widget(a,body,SC_UI_TABLE,"",S_PROPERTIES,&a->table));
    S2_TRY(s2_layout(a,a->table,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,80,80));
    S2_TRY(s2_widget(a,body,SC_UI_SPLITTER,"",0,&a->splitter));
    S2_TRY(s2_layout(a,a->splitter,SC_UI_ABSOLUTE,4,SC_UI_AUTO,0,4,0));
    S2_TRY(s2_widget(a,body,SC_UI_LABEL,"SESSION DETAILS",0,&a->details));
    S2_TRY(s2_layout(a,a->details,SC_UI_ABSOLUTE,244,SC_UI_AUTO,0,160,0));
    S2_TRY(ScUiNodeSetFont(a->ui,a->details,SC_UI_FONT_DENSE));
    ScUiProviderDesc provider;
    s2_record(&provider,sizeof(provider));
    provider.procedure=s2_provider;
    provider.context=a;
    S2_TRY(ScUiProviderCreate(&provider,&a->provider));
    S2_TRY(ScUiSelectionCreate(a->provider,SC_UI_SELECTION_ALLOW_EMPTY,&a->selection));
    S2_TRY(ScUiViewBind(a->ui,a->table,a->provider,a->selection));
    const char *names[]={"Account","UID","Session","State","Members","Generation"};
    ScI32 widths[]={190,78,90,145,80,100};
    ScUiColumnDesc columns[6];
    for(ScU32 i=0;i<6;i++){s2_record(columns+i,sizeof(columns[i]));
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].title=s2_text(names[i]);
        columns[i].width=widths[i];
        columns[i].min_width=48;
        columns[i].max_width=800;
    }S2_TRY(ScUiTableSetColumns(a->ui,a->table,columns,6));
    if(!a->locked){text=s2_text("My sessions");
        S2_TRY(ScUiTabAdd(a->ui,a->tabs,a->sessions_page,&text));
    }else S2_TRY(ScUiNodeSetState(a->ui,a->sessions_page,SC_UI_VISIBLE,0));
    S2_TRY(ScUiTabSelect(a->ui,a->tabs,a->login_page));
    S2_TRY(s2_widget(a,a->root,SC_UI_STATUS,"",0,&a->status));
    S2_TRY(s2_layout(a,a->status,SC_UI_ABSOLUTE,SC_UI_AUTO,26,0,0,22));
    ScUiSubscription listen;
    s2_record(&listen,sizeof(listen));
    listen.phases=SC_UI_CAPTURE_PHASE|SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    listen.procedure=s2_event;
    listen.context=a;
    ScU32 token;
    S2_TRY(ScUiListen(a->ui,0,&listen,&token));
    s2_update_auth_ui(a);
    S2_TRY(ScUiLayout(a->ui,a->root));
    S2_TRY(ScUiWindowShow(a->ui,a->root,SC32_WINDOW_SHOW));
    S2_TRY(ScUiFocus(a->ui,a->username));
    s2_status(a,"Ready to sign in");
    if(!a->locked)S2_TRY(s2_refresh(a));
    return 0;
#undef S2_TRY
}
