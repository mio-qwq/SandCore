#include "IMAGEAPP.H"

static ScStatus image_layout(ImageApp *a,ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex,ScI32 minimum_width,ScI32 minimum_height)
{
    ScUiLayoutDesc d;
    image_record(&d,sizeof(d));
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.flex_weight=flex;
    d.min_width=minimum_width;
    d.min_height=minimum_height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.align_h=d.align_v=SC_UI_STRETCH;
    d.column_span=d.row_span=1;
    return ScUiNodeSetLayout(a->ui,node,&d);
}
static ScStatus image_widget(ImageApp *a,ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScUiNode *out)
{
    ScUiWidgetDesc d;
    image_record(&d,sizeof(d));
    d.kind=kind;
    d.parent=parent;
    d.command=command;
    d.text=image_text(label);
    if(kind==SC_UI_TABLE)d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScStatus s=ScUiWidgetCreate(a->ui,&d,out);
    return s<0?s:image_layout(a,*out,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,0,0,0);
}
static ScStatus image_button(ImageApp *a,ScUiNode parent,const char *label,ScU32 command)
{ScUiNode node;
    ScStatus s=image_widget(a,parent,SC_UI_BUTTON,label,command,&node);
    return s<0?s:image_layout(a,node,SC_UI_ABSOLUTE,SC_UI_AUTO,28,0,40,24);
}
static void image_status(ImageApp *a,const char *message)
{
    snprintf(a->message,sizeof(a->message),"%s",message);
    if(!a->status)return;
    ScUiStatusPart parts[2];
    image_record(parts,sizeof(parts[0]));
    image_record(parts+1,sizeof(parts[1]));
    parts[0].part=1;
    parts[0].width=-1;
    parts[0].text=image_text(a->message);
    const char *hint=IMAGE_APP_CANVAS?"Ctrl+S Save | B Brush | Esc Cancel":"F Fit | 1 Native | C Center";
    parts[1].part=2;
    parts[1].width=260;
    parts[1].text=image_text(hint);
    ScUiStatusSetParts(a->ui,a->status,parts,2);
}
static ScStatus image_queue(ImageApp *a,ScU32 command)
{ImageCommand *c=malloc(sizeof(*c));
    if(!c)return SC_UI_E_MEMORY;
    c->command=command;
    c->next=0;
    if(a->last_command)a->last_command->next=c;
    else a->commands=c;
    a->last_command=c;
    return 0;
}
static ScStatus image_dialog_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{(void)ui;
    ImageApp *a=context;
    if(event->type==SC_UI_EVENT_DIALOG_COMPLETE){a->dialog_done=1;
        a->dialog_command=event->command;
    }return 0;
}
static ScStatus image_history_data(void *context,const ScUiDataRequest *q,ScUiDataReply *r)
{
    ImageApp *a=context;
    ScGfxSurfaceInfo info;
    image_record(&info,sizeof(info));
    if(!a->surface || ScGfxSurfaceQuery(a->surface,&info)<0)return SC_UI_E_STALE;
    ScU32 count=info.undo_records+info.redo_records;
    r->revision=a->provider_revision;
    if(q->operation==SC_UI_DATA_COUNT){r->total=count;
        return 0;
    }if(q->operation==SC_UI_DATA_CHILD_COUNT){r->total=0;
        return 0;
    }
    ScU32 index=q->operation==SC_UI_DATA_CHILD_AT?q->index:q->item?q->item-1:UINT_MAX;
    if(index>=count)return SC_UI_END;
    r->item=index+1;
    r->total=index;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(q->operation!=SC_UI_DATA_CELL)return 0;
    char label[48];
    ScU32 undo=index<info.undo_records;
    ScStatus s=ScGfxHistoryLabel(a->surface,undo?1:2,undo?index:index-info.undo_records,label,sizeof(label));
    if(s)return SC_UI_E_STALE;
    snprintf(a->history_text,sizeof(a->history_text),"%s  %s",undo?"Undo":"Redo",label);
    r->value.type=SC_VALUE_UTF8;
    r->value.data=a->history_text;
    r->value.bytes=(ScU32)strlen(a->history_text);
    return 0;
}
static void image_refresh(ImageApp *a)
{
    ScGfxSurfaceInfo s;
    image_record(&s,sizeof(s));
    ScGfxCanvasInfo v;
    image_record(&v,sizeof(v));
    if(a->surface && ScGfxSurfaceQuery(a->surface,&s)==0 && ScGfxCanvasQuery(a->view,&v)==0){
        char title[128];
        const char *name=a->path[0]?a->path:"Untitled";
        snprintf(title,sizeof(title),"%s%s - %s",s.flags&SC_GFX_DIRTY?"* ":"",name,IMAGE_APP_CANVAS?"Canvas 2":"Lens 2");
        ScUiText t=image_text(title);
        ScUiWindowSetTitle(a->ui,a->root,&t);
        char details[512];
        snprintf(details,sizeof(details),"%s\n\n%u x %u px\nARGB32  |  %u bytes\n\n%s  %u%%\nPan  %d, %d\n\nRevision  %u\nSaved  %u\n\nUndo  %u  |  Redo  %u\nHistory  %u / %u bytes\n\n%s",name,s.width,s.height,s.bytes,v.mode==SC_GFX_VIEW_FIT?"Fit":"Native scale",v.percent,v.pan_x,v.pan_y,s.revision,s.saved_revision,s.undo_records,s.redo_records,s.history_bytes,s.history_budget,s.flags&SC_GFX_DIRTY?"Unsaved changes":"Saved image");
        t=image_text(details);
        ScUiNodeSetText(a->ui,a->properties,&t);
        if(a->history_surface!=a->surface || a->last_history_revision!=s.revision || a->last_undo!=s.undo_records || a->last_redo!=s.redo_records){
            a->history_surface=a->surface;
            a->last_history_revision=s.revision;
            a->last_undo=s.undo_records;
            a->last_redo=s.redo_records;
            if(++a->provider_revision==0)a->provider_revision=1;
            ScUiDataChange change;
            image_record(&change,sizeof(change));
            change.revision=a->provider_revision;
            change.kind=SC_UI_DATA_RESET;
            ScUiProviderNotify(a->history_provider,&change);
        }
    }
}
static ScStatus image_canvas_event(ScGfxCanvas *view,const ScGfxCanvasEvent *e,void *context)
{
    (void)view;
    ImageApp *a=context;
    if(e->type==SC_GFX_CANVAS_CHANGED || e->type==SC_GFX_CANVAS_VIEW || e->type==SC_GFX_CANVAS_SELECTION)image_refresh(a);
    else if(e->type==SC_GFX_CANVAS_COLOR){a->brush.argb=e->argb;
        ScGfxCanvasSetBrush(a->view,&a->brush);
        char message[64];
        snprintf(message,sizeof(message),"Picked #%08X",e->argb);
        image_status(a,message);
    }
    else if(e->type==SC_GFX_CANVAS_ERROR){char message[96];
        snprintf(message,sizeof(message),"Edit failed (%d); previous history retained",e->status);
        image_status(a,message);
    }
    else if(e->type==SC_GFX_CANVAS_CONTEXT){ScUiNodeInfo n;
        image_record(&n,sizeof(n));
        if(ScUiNodeQuery(a->ui,e->node,&n)==0){ScU32 popup=0;
            ScUiMenuOpen(a->ui,n.root,a->edit_menu,n.rectangle.x+e->x,n.rectangle.y+e->y,0,&popup);
    }}
    return 0;
}
static ScStatus image_menu_item(ImageApp *a,ScUiMenu *m,ScU32 command,const char *label,ScU32 key,ScU32 modifiers)
{
    ScUiMenuItem i;
    image_record(&i,sizeof(i));
    i.command=command;
    i.label=i.help=image_text(label);
    image_record(&i.shortcut,sizeof(i.shortcut));
    i.shortcut.key=key;
    i.shortcut.modifiers=modifiers;
    ScU32 id;
    ScStatus s=ScUiMenuInsert(m,0,&i,&id);
    if(s<0)return s;
    ScUiCommandDesc c;
    image_record(&c,sizeof(c));
    c.command=command;
    c.label=i.label;
    c.shortcut=i.shortcut;
    return ScUiCommandSet(a->ui,&c);
}
static ScStatus image_menus(ImageApp *a)
{
    ScUiMenu **all[]={&a->menu,&a->file_menu,&a->edit_menu,&a->view_menu,&a->image_menu,&a->help_menu,&a->tool_menu,&a->drawing_menu};
    for(ScU32 j=0;j<8;j++){ScStatus s=ScUiMenuCreate(all[j]);
        if(s<0)return s;
    }
    struct {ScU32 group,command,key,mods;
        const char *label;
    } items[]={
        {0,I_NEW,'n',SC32_MOD_CONTROL,"&New 512 x 320"},{0,I_OPEN,'o',SC32_MOD_CONTROL,"&Open image..."},{0,I_SAVE,'s',SC32_MOD_CONTROL,"&Save SCB2"},
        {0,I_SAVE_AS,'s',SC32_MOD_CONTROL|SC32_MOD_SHIFT,"Save &as SCB2..."},{0,I_EXPORT,'e',SC32_MOD_CONTROL,"&Export BMP..."},{0,I_CLOSE,'q',SC32_MOD_CONTROL,"E&xit"},
        {1,I_UNDO,'z',SC32_MOD_CONTROL,"&Undo"},{1,I_REDO,'y',SC32_MOD_CONTROL,"&Redo"},{1,I_CUT,'x',SC32_MOD_CONTROL,"Cu&t"},{1,I_COPY,'c',SC32_MOD_CONTROL,"&Copy pixels"},
        {1,I_PASTE,'v',SC32_MOD_CONTROL,"&Paste pixels"},{1,I_DELETE,SC32_KEY_DELETE,0,"&Delete selection"},{1,I_ALL,'a',SC32_MOD_CONTROL,"Select &all"},{1,I_CLEAR,0,0,"C&lear paper"},
        {2,I_FIT,'f',0,"&Fit image"},{2,I_NATIVE,'1',0,"&100% physical pixels"},{2,I_ZOOM_IN,'+',SC32_MOD_CONTROL,"Zoom &in"},{2,I_ZOOM_OUT,'-',SC32_MOD_CONTROL,"Zoom &out"},{2,I_CENTER,'c',0,"&Center"},
        {2,I_FLOAT,0,0,"&Float properties / history"},{2,I_DOCK,0,0,"&Dock properties / history"},
        {3,I_CW,0,0,"Rotate 90 clockwise"},{3,I_CCW,0,0,"Rotate 90 counterclockwise"},{3,I_FLIP_X,0,0,"Flip horizontal"},{3,I_FLIP_Y,0,0,"Flip vertical"},
        {3,I_BRUSH_PROPERTIES,0,0,"Brush and RGBA..."},{3,I_IMAGE_PROPERTIES,0,0,"Image properties..."},{4,I_CANCEL,27,0,"&Cancel current operation"},{4,I_ABOUT,SC32_KEY_F1,SC32_MOD_SHIFT,"&About"}};
    ScUiMenu *groups[]={a->file_menu,a->edit_menu,a->view_menu,a->image_menu,a->help_menu};
    for(ScU32 j=0;j<sizeof(items)/sizeof(items[0]);j++){if(!IMAGE_APP_CANVAS && (items[j].command==I_NEW || items[j].command==I_BRUSH_PROPERTIES || items[j].command==I_CLEAR || items[j].command==I_CUT || items[j].command==I_PASTE || items[j].command==I_DELETE))continue;
        ScStatus s=image_menu_item(a,groups[items[j].group],items[j].command,items[j].label,items[j].key,items[j].mods);
        if(s<0)return s;
    }
    const char *names[]={"&File","&Edit","&View","&Image","&Help"};
    for(ScU32 j=0;j<5;j++){ScUiMenuItem i;
        image_record(&i,sizeof(i));
        i.label=image_text(names[j]);
        i.submenu=groups[j];
        ScU32 id;
        ScStatus s=ScUiMenuInsert(a->menu,0,&i,&id);
        if(s<0)return s;
    }
    if(IMAGE_APP_CANVAS){
        const char *labels[]={"Brush","Eraser","Line","Rectangle","Fill","Select","Pick color","Pan"};
        const ScU32 keys[]={'b','e','l','r','g','v','i','h'};
        for(ScU32 j=0;j<8;j++){ScStatus s=image_menu_item(a,a->drawing_menu,I_TOOL_FIRST+j,labels[j],keys[j],0);
            if(s<0)return s;
        }
        for(ScU32 j=0;j<8;j++){char color[20];
            snprintf(color,sizeof(color),"Color #%06X",a->colors[j]&0xffffffu);
            ScStatus s=image_menu_item(a,a->drawing_menu,I_COLOR_FIRST+j,color,0,0);
            if(s<0)return s;
        }
        ScUiMenuItem i;
        image_record(&i,sizeof(i));
        i.label=image_text("&Tools");
        i.submenu=a->drawing_menu;
        ScU32 id;
        ScStatus s=ScUiMenuInsert(a->menu,0,&i,&id);
        if(s<0)return s;
    }
    ScU32 commands[]={I_OPEN,I_SAVE,I_UNDO,I_REDO,I_FIT,I_NATIVE,I_ZOOM_IN,I_ZOOM_OUT,I_CENTER,I_CANCEL};
    const char *labels[]={"Open","Save","Undo","Redo","Fit","100%","+","-","Center","Cancel"};
    for(ScU32 j=0;j<10;j++){ScUiMenuItem i;
        image_record(&i,sizeof(i));
        i.command=commands[j];
        i.label=image_text(labels[j]);
        ScU32 id;
        ScStatus s=ScUiMenuInsert(a->tool_menu,0,&i,&id);
        if(s<0)return s;
    }
    ScStatus s=ScUiMenuBind(a->ui,a->menubar,a->menu);
    return s<0?s:ScUiToolbarBind(a->ui,a->toolbar,a->tool_menu);
}
static ScStatus image_build(ImageApp *a)
{
    ScUiContextDesc c;
    image_record(&c,sizeof(c));
    c.flags=SC_UI_STOP_LAST_ROOT;
    c.cache_budget_bytes=512u*1024u;
    c.scheduling_quantum=32;
    ScStatus s=ScUiContextCreate(&c,&a->ui);
    if(s<0)return s;
    ScUiWindowDesc w;
    image_record(&w,sizeof(w));
    w.title=image_text(IMAGE_APP_CANVAS?"Canvas 2":"Lens 2");
    w.client_width=1100;
    w.client_height=750;
    w.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(a->ui,&w,&a->root);
    if(s<0)return s;
#define IMAGE_TRY(expression) if((s=(expression))<0)return s
    IMAGE_TRY(image_layout(a,a->root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0));
    IMAGE_TRY(image_widget(a,a->root,SC_UI_MENUBAR,"",0,&a->menubar));
    IMAGE_TRY(image_layout(a,a->menubar,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,0,22));
    IMAGE_TRY(image_widget(a,a->root,SC_UI_TOOLBAR,"",0,&a->toolbar));
    IMAGE_TRY(image_layout(a,a->toolbar,SC_UI_ABSOLUTE,SC_UI_AUTO,34,0,0,28));
    IMAGE_TRY(image_widget(a,a->root,SC_UI_PANEL,"",0,&a->body));
    IMAGE_TRY(image_layout(a,a->body,SC_UI_HBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,80));
    if(IMAGE_APP_CANVAS){IMAGE_TRY(image_widget(a,a->body,SC_UI_PANEL,"",0,&a->tools));
        IMAGE_TRY(image_layout(a,a->tools,SC_UI_VBOX,148,SC_UI_AUTO,0,112,0));
        ScUiNode label;
        IMAGE_TRY(image_widget(a,a->tools,SC_UI_LABEL,"TOOLS",0,&label));
        IMAGE_TRY(image_layout(a,label,SC_UI_ABSOLUTE,SC_UI_AUTO,24,0,0,22));
        const char *tools[]={"Brush","Eraser","Line","Rectangle","Fill","Select","Pick color","Pan"};
        for(ScU32 j=0;j<8;j++)IMAGE_TRY(image_button(a,a->tools,tools[j],I_TOOL_FIRST+j));
        IMAGE_TRY(image_button(a,a->tools,"RGBA / Brush...",I_BRUSH_PROPERTIES));
        ScUiNode row;
        IMAGE_TRY(image_widget(a,a->tools,SC_UI_PANEL,"",0,&row));
        IMAGE_TRY(image_layout(a,row,SC_UI_HBOX,SC_UI_AUTO,28,0,0,24));
        IMAGE_TRY(image_button(a,row,"Radius -",I_RADIUS_DOWN));
        IMAGE_TRY(image_button(a,row,"+",I_RADIUS_UP));
        IMAGE_TRY(image_widget(a,a->tools,SC_UI_PANEL,"",0,&row));
        IMAGE_TRY(image_layout(a,row,SC_UI_GRID_LAYOUT,SC_UI_AUTO,112,0,0,80));
        ScUiLayoutDesc grid;
        image_record(&grid,sizeof(grid));
        ScUiNodeGetLayout(a->ui,row,&grid);
        grid.columns=2;
        grid.gap=2;
        IMAGE_TRY(ScUiNodeSetLayout(a->ui,row,&grid));
        for(ScU32 j=0;j<8;j++){char label_text[16];
            snprintf(label_text,sizeof(label_text),"#%06X",a->colors[j]&0xffffffu);
            ScUiNode button;
            IMAGE_TRY(image_widget(a,row,SC_UI_BUTTON,label_text,I_COLOR_FIRST+j,&button));
            ScUiLayoutDesc ld;
            image_record(&ld,sizeof(ld));
            ScUiNodeGetLayout(a->ui,button,&ld);
            ld.column=j%2;
            ld.row=j/2;
            ld.height=26;
            IMAGE_TRY(ScUiNodeSetLayout(a->ui,button,&ld));
            ScUiIcon *icon=0;
            IMAGE_TRY(ScUiIconCreateArgb(1,1,4,a->colors+j,&icon));
            s=ScUiNodeSetIcon(a->ui,button,icon);
            ScUiIconRelease(icon);
            if(s<0)return s;
        }
        IMAGE_TRY(image_widget(a,a->body,SC_UI_SPLITTER,"",0,&a->tools_split));
        IMAGE_TRY(image_layout(a,a->tools_split,SC_UI_ABSOLUTE,4,SC_UI_AUTO,0,4,0));
    }
    IMAGE_TRY(image_widget(a,a->body,SC_UI_PANEL,"",0,&a->view_parent));
    IMAGE_TRY(image_layout(a,a->view_parent,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,96,64));
    ScGfxCanvasDesc vd;
    image_record(&vd,sizeof(vd));
    vd.ui=a->ui;
    vd.parent=a->view_parent;
    vd.surface=a->surface;
    vd.flags=SC_GFX_CANVAS_CHECKER|(IMAGE_APP_CANVAS?0:SC_GFX_CANVAS_READONLY);
    vd.procedure=image_canvas_event;
    vd.context=a;
    IMAGE_TRY(ScGfxCanvasCreate(&vd,&a->view));
    ScGfxCanvasInfo vi;
    image_record(&vi,sizeof(vi));
    IMAGE_TRY(ScGfxCanvasQuery(a->view,&vi));
    IMAGE_TRY(image_layout(a,vi.node,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,64,64));
    IMAGE_TRY(image_widget(a,a->body,SC_UI_SPLITTER,"",0,&a->right_split));
    IMAGE_TRY(image_layout(a,a->right_split,SC_UI_ABSOLUTE,4,SC_UI_AUTO,0,4,0));
    IMAGE_TRY(image_widget(a,a->body,SC_UI_DOCK,"",0,&a->dock));
    IMAGE_TRY(image_layout(a,a->dock,SC_UI_VBOX,230,SC_UI_AUTO,0,160,0));
    IMAGE_TRY(image_widget(a,a->dock,SC_UI_LABEL,"IMAGE",0,&a->properties));
    IMAGE_TRY(image_layout(a,a->properties,SC_UI_ABSOLUTE,SC_UI_AUTO,SC_UI_AUTO,1,0,160));
    IMAGE_TRY(ScUiNodeSetFont(a->ui,a->properties,SC_UI_FONT_DENSE));
    IMAGE_TRY(image_button(a,a->dock,"Image properties...",I_IMAGE_PROPERTIES));
    IMAGE_TRY(image_widget(a,a->dock,SC_UI_TABLE,"",0,&a->history));
    IMAGE_TRY(image_layout(a,a->history,SC_UI_ABSOLUTE,SC_UI_AUTO,170,0,0,60));
    ScUiProviderDesc pd;
    image_record(&pd,sizeof(pd));
    pd.procedure=image_history_data;
    pd.context=a;
    a->provider_revision=1;
    IMAGE_TRY(ScUiProviderCreate(&pd,&a->history_provider));
    IMAGE_TRY(ScUiSelectionCreate(a->history_provider,SC_UI_SELECTION_ALLOW_EMPTY,&a->history_selection));
    IMAGE_TRY(ScUiViewBind(a->ui,a->history,a->history_provider,a->history_selection));
    ScUiColumnDesc column;
    image_record(&column,sizeof(column));
    column.column=1;
    column.title=image_text("History");
    column.width=218;
    column.min_width=80;
    column.max_width=900;
    IMAGE_TRY(ScUiTableSetColumns(a->ui,a->history,&column,1));
    IMAGE_TRY(image_widget(a,a->root,SC_UI_STATUS,"",0,&a->status));
    IMAGE_TRY(image_layout(a,a->status,SC_UI_ABSOLUTE,SC_UI_AUTO,25,0,0,22));
    IMAGE_TRY(image_menus(a));
    ScUiSubscription listen;
    image_record(&listen,sizeof(listen));
    listen.phases=SC_UI_CAPTURE_PHASE|SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    listen.procedure=image_event;
    listen.context=a;
    ScU32 token;
    IMAGE_TRY(ScUiListen(a->ui,0,&listen,&token));
    IMAGE_TRY(ScGfxCanvasSetBrush(a->view,&a->brush));
    IMAGE_TRY(ScUiLayout(a->ui,a->root));
    IMAGE_TRY(ScUiWindowShow(a->ui,a->root,SC32_WINDOW_SHOW));
    image_status(a,"Ready");
    image_refresh(a);
    return 0;
#undef IMAGE_TRY
}
