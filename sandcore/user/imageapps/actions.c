#include "IMAGEAPP.H"

static void image_drop_candidate(ImageApp *a)
{ScGfxSurfaceRelease(a->candidate);
    a->candidate=0;
    a->candidate_path[0]=0;
    a->candidate_native=a->candidate_revision=0;
}
static void image_error(ImageApp *a,const char *action,ScStatus status)
{
    char text[240];
    snprintf(text,sizeof(text),"%s failed (%d). Current image and saved identity are retained.",action,status);
    image_status(a,text);
    if(a->dialog || a->closing)return;
    ScUiMessageBoxDesc d;
    image_record(&d,sizeof(d));
    d.owner=a->root;
    d.title=image_text("Image operation");
    d.message=image_text(text);
    d.completion=image_dialog_complete;
    d.context=a;
    a->dialog_kind=I_DIALOG_INFO;
    ScStatus s=ScUiMessageBox(a->ui,&d,&a->dialog);
    if(s<0)a->dialog_kind=0;
}
static int image_view_busy(ImageApp *a)
{ScGfxCanvasInfo v;
    image_record(&v,sizeof(v));
    return a->view && ScGfxCanvasQuery(a->view,&v)==0 && v.busy;
}
static int image_dirty(ImageApp *a)
{ScGfxSurfaceInfo s;
    image_record(&s,sizeof(s));
    return a->surface && ScGfxSurfaceQuery(a->surface,&s)==0 && (s.flags&SC_GFX_DIRTY);
}
static ScStatus image_file_dialog(ImageApp *a,ScU32 kind)
{
    if(a->dialog)return SC_UI_E_BUSY;
    ScUiFileDialogDesc d;
    image_record(&d,sizeof(d));
    d.owner=a->root;
    d.flags=kind==I_DIALOG_OPEN?SC_UI_FILE_OPEN:SC_UI_FILE_SAVE|SC_UI_FILE_OVERWRITE;
    d.title=image_text(kind==I_DIALOG_OPEN?"Open image":kind==I_DIALOG_SAVE?"Save native SCB2 image":"Export opaque BMP");
    d.directory="/SYS/PICTURES";
    d.completion=image_dialog_complete;
    d.context=a;
    a->dialog_kind=kind;
    ScStatus s=ScUiFileDialog(a->ui,&d,&a->dialog);
    if(s<0)a->dialog_kind=0;
    return s;
}
static ScStatus image_save(ImageApp *a,const char *path,ScU32 format,ScU32 revision)
{
    if(a->operation || image_view_busy(a) || !a->surface)return SC_GFX_E_BUSY;
    ScStatus s=ScGfxSaveBegin(a->surface,path,format,revision,&a->operation);
    if(s==0){a->operation_kind=I_SAVE;
        image_status(a,format==SC_GFX_FILE_SCB2?"Saving native image...":"Exporting opaque BMP...");
    }return s;
}
static void image_commit_candidate(ImageApp *a)
{
    if(!a->candidate)return;
    ScStatus s=ScGfxCanvasSetSurface(a->view,a->candidate);
    if(s<0){image_error(a,"Replace image",s);
        return;
    }
    ScGfxSurfaceRelease(a->surface);
    a->surface=a->candidate;
    a->candidate=0;
    snprintf(a->path,sizeof(a->path),"%s",a->candidate_path);
    a->file_revision=a->candidate_revision;
    a->native_path=a->candidate_native;
    a->candidate_path[0]=0;
    a->candidate_revision=a->candidate_native=0;
    a->intent=0;
    ScGfxCanvasSetMode(a->view,SC_GFX_VIEW_FIT,100);
    image_refresh(a);
    image_status(a,"Image ready");
}
static ScStatus image_discard_dialog(ImageApp *a)
{
    ScUiMenuItem buttons[3];
    for(ScU32 i=0;i<3;i++)image_record(buttons+i,sizeof(buttons[i]));
    buttons[0].command=I_SAVE_CHANGES;
    buttons[0].label=image_text("Save");
    buttons[1].command=I_DISCARD;
    buttons[1].label=image_text("Discard");
    buttons[2].command=I_KEEP;
    buttons[2].label=image_text("Cancel");
    ScUiMessageBoxDesc d;
    image_record(&d,sizeof(d));
    d.owner=a->root;
    d.title=image_text("Unsaved image");
    d.message=image_text("Save changes before continuing?");
    d.buttons=buttons;
    d.button_count=3;
    d.default_command=I_SAVE_CHANGES;
    d.cancel_command=I_KEEP;
    d.completion=image_dialog_complete;
    d.context=a;
    a->dialog_kind=I_DIALOG_DISCARD;
    ScStatus s=ScUiMessageBox(a->ui,&d,&a->dialog);
    if(s<0)a->dialog_kind=0;
    return s;
}
static void image_consider_intent(ImageApp *a)
{
    if(!a->intent || a->dialog || a->operation || image_view_busy(a))return;
    if(image_dirty(a)){ScStatus s=image_discard_dialog(a);
        if(s<0){a->closing=0;
            image_error(a,"Confirm changes",s);
        }return;
    }
    if(a->intent==I_INTENT_REPLACE)image_commit_candidate(a);
    else{a->closed=1;
        ScUiContextStop(a->ui,0);
    }
}
static ScStatus image_config_number(ScConfig *config,const char *key,ScU32 value)
{ScValue v;
    image_record(&v,sizeof(v));
    v.type=SC_VALUE_U32;
    v.lo=value;
    return ScConfigSet(config,"Image",key,&v);
}
static ScStatus image_config_text(ScConfig *config,const char *key,const char *text)
{ScValue v;
    image_record(&v,sizeof(v));
    v.type=SC_VALUE_UTF8;
    v.data=text;
    v.bytes=(ScU32)strlen(text);
    return ScConfigSet(config,"Image",key,&v);
}
static ScStatus image_brush_apply(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    ImageApp *a=context;
    if(e->type!=SC_UI_EVENT_APPLY || !e->data)return 0;
    const char *keys[]={"Red","Green","Blue","Alpha","Radius","Opacity"};
    ScU32 values[6];
    for(ScU32 i=0;i<6;i++){ScValue v;
        image_record(&v,sizeof(v));
        ScStatus s=ScConfigGet((const ScConfig *)e->data,"Image",keys[i],&v);
        if(s<0 || (v.type!=SC_VALUE_U32 && v.type!=SC_VALUE_I32) || v.lo>(i==4?64u:255u) || (i==4 && !v.lo))return SC_UI_E_RANGE;
        values[i]=v.lo;
    }
    ScGfxBrush brush=a->brush;
    brush.argb=(values[3]<<24)|(values[0]<<16)|(values[1]<<8)|values[2];
    brush.radius=values[4];
    brush.opacity=values[5];
    ScStatus s=ScGfxCanvasSetBrush(a->view,&brush);
    if(s==0){a->brush=brush;
        image_status(a,"Brush updated");
    }return s;
}
static ScStatus image_properties(ImageApp *a,ScU32 brush)
{
    if(a->dialog || !a->surface || image_view_busy(a))return SC_UI_E_BUSY;
    ScConfig *config=0;
    ScStatus s=ScConfigCreate(&config);
    if(s<0)return s;
    ScGfxSurfaceInfo info;
    image_record(&info,sizeof(info));
    s=ScGfxSurfaceQuery(a->surface,&info);
    const char *keys_brush[]={"Red","Green","Blue","Alpha","Radius","Opacity"};
    const char *keys_image[]={"Path","Width","Height","Bytes","Revision","History","Alpha","FileRevision"};
    ScU32 count=brush?6u:8u;
    ScUiFieldDesc fields[8];
    memset(fields,0,sizeof(fields));
    for(ScU32 i=0;i<count && s==0;i++){image_record(fields+i,sizeof(fields[i]));
        fields[i].field=i+1;
        fields[i].kind=brush?SC_UI_SPINBOX:SC_UI_TEXTBOX;
        fields[i].section="Image";
        fields[i].key=brush?keys_brush[i]:keys_image[i];
        fields[i].label=image_text(fields[i].key);
        fields[i].flags=brush?0:SC_UI_FIELD_READONLY;
        fields[i].minimum=brush && i==4?1:0;
        fields[i].maximum=brush && i==4?64:255;
        if(brush){ScU32 values[]={a->brush.argb>>16&255u,a->brush.argb>>8&255u,a->brush.argb&255u,a->brush.argb>>24,a->brush.radius,a->brush.opacity};
            s=image_config_number(config,keys_brush[i],values[i]);
        }
        else{char value[96];
            if(i==0)snprintf(value,sizeof(value),"%s",a->path[0]?a->path:"Untitled");
            else if(i==6)snprintf(value,sizeof(value),"Straight ARGB32, preserved");
            else{ScU32 values[]={0,info.width,info.height,info.bytes,info.revision,info.history_bytes,0,a->file_revision};
                snprintf(value,sizeof(value),"%u",values[i]);
            }s=image_config_text(config,keys_image[i],value);
    }}
    ScUiPropertySheetDesc d;
    image_record(&d,sizeof(d));
    d.owner=a->root;
    d.title=image_text(brush?"Brush and RGBA":"Original image properties");
    d.initial=config;
    d.context=a;
    d.completion=image_dialog_complete;
    if(brush)d.apply=image_brush_apply;
    if(s==0){a->dialog_kind=brush?I_DIALOG_BRUSH:I_DIALOG_INFO;
        s=ScUiPropertySheet(a->ui,&d,fields,count,&a->dialog);
        if(s<0)a->dialog_kind=0;
    }ScConfigRelease(config);
    return s;
}
static ScStatus image_start_load(ImageApp *a,const char *path)
{
    if(a->operation || a->candidate)return SC_GFX_E_BUSY;
    ScStatus s=ScGfxLoadBegin(path,&a->operation);
    if(s==0){a->operation_kind=I_OPEN;
        image_status(a,"Loading candidate image...");
    }return s;
}
static ScGfxRect image_selection(ImageApp *a)
{ScGfxCanvasInfo v;
    image_record(&v,sizeof(v));
    ScGfxCanvasQuery(a->view,&v);
    return v.selection;
}
static ScStatus image_execute(ImageApp *a,ScU32 command)
{
    if(command==I_CANCEL){if(a->operation)ScGfxOperationCancel(a->operation);
        ScGfxCanvasCancel(a->view);
        image_status(a,"Cancelling operation...");
        return 0;
    }
    if(command==I_CLOSE){a->closing=1;
        a->intent=I_INTENT_CLOSE;
        image_drop_candidate(a);
        if(a->operation)ScGfxOperationCancel(a->operation);
        ScGfxCanvasCancel(a->view);
        image_consider_intent(a);
        return 0;
    }
    if(a->closing || a->dialog)return SC_GFX_E_BUSY;
    if(command==I_OPEN)return image_file_dialog(a,I_DIALOG_OPEN);
    if(command==I_ABOUT){ScUiMessageBoxDesc d;
        image_record(&d,sizeof(d));
        d.owner=a->root;
        d.title=image_text(IMAGE_APP_CANVAS?"Canvas 2":"Lens 2");
        d.message=image_text("Native SCUI and SCGFX. Full source pixels, straight alpha, owned asynchronous operations and reversible edits.\nPNG / JPEG / WebP decode in the isolated image service.");
        d.completion=image_dialog_complete;
        d.context=a;
        a->dialog_kind=I_DIALOG_INFO;
        return ScUiMessageBox(a->ui,&d,&a->dialog);
    }
    if(command==I_FIT || command==I_NATIVE)return ScGfxCanvasSetMode(a->view,command==I_FIT?SC_GFX_VIEW_FIT:SC_GFX_VIEW_PERCENT,100);
    if(command==I_CENTER)return ScGfxCanvasCenter(a->view);
    if(command==I_ZOOM_IN || command==I_ZOOM_OUT){ScGfxCanvasInfo v;
        image_record(&v,sizeof(v));
        ScGfxCanvasQuery(a->view,&v);
        ScU32 n=v.mode==SC_GFX_VIEW_PERCENT?v.percent:100;
        n=command==I_ZOOM_IN?n+25:n>25?n-25:10;
        return ScGfxCanvasSetMode(a->view,SC_GFX_VIEW_PERCENT,n>800?800:n);
    }
    if(command==I_IMAGE_PROPERTIES || command==I_BRUSH_PROPERTIES)return image_properties(a,command==I_BRUSH_PROPERTIES);
    if(command==I_FLOAT){if(a->float_root)return 0;
        ScUiWindowDesc d;
        image_record(&d,sizeof(d));
        d.owner=a->root;
        d.title=image_text("Image properties and history");
        d.client_width=330;
        d.client_height=590;
        d.style=SC32_STYLE_STANDARD;
        ScStatus s=ScUiDockFloat(a->ui,a->dock,&d,&a->float_root);
        if(s==0){image_layout(a,a->float_root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1,0,0);
            ScUiNodeSetState(a->ui,a->dock,SC_UI_VISIBLE,SC_UI_VISIBLE);
            ScUiLayout(a->ui,a->float_root);
        }return s;
    }
    if(command==I_DOCK){if(!a->float_root)return 0;
        ScStatus s=ScUiDockMove(a->ui,a->dock,a->body,SC_UI_DOCK_RIGHT);
        if(s==0){ScUiNodeDestroy(a->ui,a->float_root);
            a->float_root=0;
            image_layout(a,a->dock,SC_UI_VBOX,230,SC_UI_AUTO,0,160,0);
            ScUiNodeSetState(a->ui,a->dock,SC_UI_VISIBLE,a->compact?0:SC_UI_VISIBLE);
            ScUiLayout(a->ui,a->root);
        }return s;
    }
    if(a->operation || image_view_busy(a))return SC_GFX_E_BUSY;
    if(command==I_SAVE)return a->native_path && a->path[0]?image_save(a,a->path,SC_GFX_FILE_SCB2,a->file_revision):image_file_dialog(a,I_DIALOG_SAVE);
    if(command==I_SAVE_AS || command==I_EXPORT)return image_file_dialog(a,command==I_SAVE_AS?I_DIALOG_SAVE:I_DIALOG_EXPORT);
    if(command==I_NEW && IMAGE_APP_CANVAS){ScGfxSurfaceDesc d;
        image_record(&d,sizeof(d));
        d.width=512;
        d.height=320;
        d.fill_argb=a->colors[0];
        ScStatus s=ScGfxSurfaceCreate(&d,&a->candidate);
        if(s<0)return s;
        a->candidate_path[0]=0;
        a->candidate_revision=a->candidate_native=0;
        a->intent=I_INTENT_REPLACE;
        image_consider_intent(a);
        return 0;
    }
    if(!a->surface)return SC_GFX_E_NOT_FOUND;
    ScStatus s=SC_GFX_E_NOT_SUPPORTED;
    if(command>=I_TOOL_FIRST && command<I_TOOL_FIRST+8)return ScGfxCanvasSetTool(a->view,command-I_TOOL_FIRST+1);
    if(command>=I_COLOR_FIRST && command<I_COLOR_FIRST+8){a->brush.argb=a->colors[command-I_COLOR_FIRST];
        return ScGfxCanvasSetBrush(a->view,&a->brush);
    }
    if(command==I_RADIUS_DOWN || command==I_RADIUS_UP){ScGfxBrush b=a->brush;
        if(command==I_RADIUS_UP){if(b.radius<64)b.radius++;
        }else if(b.radius>1)b.radius--;
        s=ScGfxCanvasSetBrush(a->view,&b);
        if(s==0)a->brush=b;
        return s;
    }
    if(command==I_UNDO || command==I_REDO)s=ScGfxHistoryBegin(a->surface,command==I_UNDO?1:2,&a->operation);
    else if(command==I_CW || command==I_CCW || command==I_FLIP_X || command==I_FLIP_Y)s=ScGfxTransformBegin(a->surface,command-I_CW+1,&a->operation);
    else if(command==I_ALL){ScGfxSurfaceInfo info;
        image_record(&info,sizeof(info));
        ScGfxSurfaceQuery(a->surface,&info);
        ScGfxRect r={0,0,(ScI32)info.width,(ScI32)info.height};
        return ScGfxCanvasSetSelection(a->view,&r);
    }
    else if(command==I_COPY || (command==I_CUT && IMAGE_APP_CANVAS)){ScGfxRect r=image_selection(a);
        s=ScGfxClipboardCopyBegin(a->surface,r.width && r.height?&r:0,0,&a->operation);
        if(s==0 && command==I_CUT){if(!r.width || !r.height){ScGfxSurfaceInfo info;
                image_record(&info,sizeof(info));
                ScGfxSurfaceQuery(a->surface,&info);
                r.x=0;r.y=0;
                r.width=(ScI32)info.width;r.height=(ScI32)info.height;
            }a->cut_pending=1;
            a->cut_rect=r;
    }}
    else if(command==I_PASTE && IMAGE_APP_CANVAS){ScGfxRect r=image_selection(a);
        s=ScGfxClipboardPasteBegin(a->surface,r.x,r.y,&a->operation);
    }
    else if(command==I_DELETE && IMAGE_APP_CANVAS){ScGfxRect r=image_selection(a);
        if(!r.width || !r.height)return 0;
        s=ScGfxClearRectBegin(a->surface,&r,0,&a->operation);
    }
    else if(command==I_CLEAR && IMAGE_APP_CANVAS)s=ScGfxClearBegin(a->surface,a->colors[0],&a->operation);
    if(s==SC_GFX_END){image_status(a,"No history in this direction");
        return 0;
    }if(s==0){a->operation_kind=command;
        image_status(a,"Working...");
    }return s;
}
static ScStatus image_event(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    ImageApp *a=context;
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST && e->phase==SC_UI_CAPTURE_PHASE){if(e->node==a->float_root)image_queue(a,I_DOCK);
        else if(e->node==a->root)image_queue(a,I_CLOSE);
        else return 0;
        return SC_UI_VETO;
    }
    if(e->type==SC_UI_EVENT_COMMAND && e->command>=I_NEW && e->command<=I_KEEP && (!a->dialog || e->root==a->root || e->root==a->float_root)
    && (e->phase==SC_UI_BUBBLE_PHASE || (!e->node && e->phase==SC_UI_TARGET_PHASE))){ScStatus s=image_queue(a,e->command);
        if(s<0)a->failure=s;
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_COMMAND_QUERY && e->phase==SC_UI_TARGET_PHASE && e->bytes==sizeof(ScUiCommandDesc)){ScUiCommandDesc *d=(ScUiCommandDesc *)e->data;
        int enabled=!a->closing && !a->dialog;
        if(d->command==I_CANCEL)enabled=a->operation || image_view_busy(a);
        else if(d->command!=I_CLOSE && d->command!=I_FIT && d->command!=I_NATIVE && d->command!=I_CENTER && d->command!=I_ZOOM_IN && d->command!=I_ZOOM_OUT && d->command!=I_FLOAT && d->command!=I_DOCK)enabled=enabled && !a->operation && !image_view_busy(a);
        if(!a->surface && d->command!=I_OPEN && d->command!=I_CLOSE && d->command!=I_ABOUT)enabled=0;
        if(enabled)d->flags&=~SC_UI_MENU_DISABLED;
        else d->flags|=SC_UI_MENU_DISABLED;
        return 0;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN && !a->dialog && (e->root==a->root || e->root==a->float_root) && e->phase==SC_UI_CAPTURE_PHASE && !(e->modifiers&(SC32_MOD_ALT|SC32_MOD_CONTROL))){ScU32 command=0;
        if(e->key==SC32_KEY_F1)command=I_OPEN;
        else if(IMAGE_APP_CANVAS){if(e->key=='b')command=I_TOOL_FIRST;
            else if(e->key=='e')command=I_TOOL_FIRST+1;
            else if(e->key=='l')command=I_TOOL_FIRST+2;
            else if(e->key=='r')command=I_TOOL_FIRST+3;
            else if(e->key=='g')command=I_TOOL_FIRST+4;
            else if(e->key=='v')command=I_TOOL_FIRST+5;
            else if(e->key=='i')command=I_TOOL_FIRST+6;
            else if(e->key=='h')command=I_TOOL_FIRST+7;
        }
        if(e->key==27){if(a->operation || image_view_busy(a))command=I_CANCEL;
            else if(!IMAGE_APP_CANVAS && !a->dialog)command=I_CLOSE;
        }
        if(command){image_queue(a,command);
            return SC_UI_HANDLED;
    }}
    if(e->type==SC_UI_EVENT_LAYOUT && e->node==a->root && e->phase==SC_UI_TARGET_PHASE){ScWindowInfo w;
        image_record(&w,sizeof(w));
        ScUiMetrics metrics;
        image_record(&metrics,sizeof(metrics));
        if(ScUiWindowNative(a->ui,a->root,&w)==0 && ScUiMetricsQuery(a->ui,a->root,&metrics)==0){ScU32 compact=w.client_width*100u<760u*metrics.scale_percent;
            if(compact!=a->compact){a->compact=compact;
                ScU32 visible=compact?0:SC_UI_VISIBLE;
                ScUiBatchBegin(a->ui);
                if(a->tools)ScUiNodeSetState(a->ui,a->tools,SC_UI_VISIBLE,visible);
                if(a->tools_split)ScUiNodeSetState(a->ui,a->tools_split,SC_UI_VISIBLE,visible);
                ScUiNodeSetState(a->ui,a->right_split,SC_UI_VISIBLE,visible);
                if(!a->float_root)ScUiNodeSetState(a->ui,a->dock,SC_UI_VISIBLE,visible);
                ScUiBatchEnd(a->ui);
    }}}
    return 0;
}
