static ScUiText tool_text(const char *z)
{ScUiText t={sizeof(t),1,z,(ScU32)strlen(z)};
    return t;
}
static void tool_status(const char *z)
{
    if(!z || !strcmp(tool.status_text,z))return;
    snprintf(tool.status_text,sizeof(tool.status_text),"%s",z);
    if(!tool.status)return;
    ScUiStatusPart part;
    memset(&part,0,sizeof(part));
    part.size=sizeof(part);
    part.version=1;
    part.part=1;
    part.width=-1;
    part.text=tool_text(tool.status_text);
    ScUiStatusSetParts(tool.ui,tool.status,&part,1);
}
static ScUiNode tool_dialog_owner(void)
{
    ScUiNode owner=tool.root,after=0;
    while(tool.ui){ScUiNode root=0;
        if(ScUiContextRootNext(tool.ui,after,&root)!=0)break;
        after=root;
        ScWindowInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        if(ScUiWindowNative(tool.ui,root,&info)==0 && (info.state&SC32_WINDOW_VISIBLE) && (info.state&SC32_WINDOW_MODAL)){
            owner=root;
            if(info.state&SC32_WINDOW_FOCUSED)break;
    }}
    return owner;
}
static ScStatus tool_error(ScStatus s,const char *operation)
{
    if(s>=0 || s==SC_UI_E_CANCELLED)return s;
    ScShellErrorInfo error;
    memset(&error,0,sizeof(error));
    error.size=sizeof(error);
    error.version=1;
    error.code=error.cause=s;
    ScErrorInfo native;
    memset(&native,0,sizeof(native));
    native.size=sizeof(native);
    native.version=1;
    if(ScErrorQuery(&native)==0 && native.code==s){error.domain=native.domain;
        error.raw=native.raw;
    }
    ScUiDialog *dialog=0;
    if(tool.ui && tool.root && ScShellErrorDialog(tool.ui,tool_dialog_owner(),&error,operation,&dialog)==0)ScUiDialogRelease(dialog);
    fprintf(stderr,"system-tool error=%d operation=%s\n",s,operation?operation:"");
    return s;
}
static ScStatus tool_layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex)
{
    ScUiLayoutDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.mode=mode;
    d.width=width;
    d.height=height;
    d.max_width=d.max_height=SC_UI_AUTO;
    d.column_span=d.row_span=1;
    d.align_h=d.align_v=SC_UI_STRETCH;
    d.flex_weight=flex;
    d.gap=6;
    return ScUiNodeSetLayout(tool.ui,node,&d);
}
static ScStatus tool_widget(ScUiNode parent,ScU32 kind,const char *text,ScU32 command,ScI32 width,ScI32 height,ScU32 flex,ScUiNode *out)
{
    ScUiWidgetDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=kind;
    d.parent=parent;
    d.command=command;
    d.text=tool_text(text);
    if(kind==SC_UI_BUTTON || kind==SC_UI_TEXTBOX || kind==SC_UI_EDIT || kind==SC_UI_TABLE || kind==SC_UI_SPINBOX)d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    ScStatus s=ScUiWidgetCreate(tool.ui,&d,out);
    if(s<0)return s;
    return tool_layout(*out,kind==SC_UI_PANEL?SC_UI_HBOX:SC_UI_ABSOLUTE,width,height,flex);
}
static ScStatus tool_listen(ScUiNode node,ScU32 phases,ToolEventProc procedure,void *context)
{
    ScUiSubscription d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.phases=phases;
    d.procedure=procedure;
    d.context=context;
    ScU32 token;
    return ScUiListen(tool.ui,node,&d,&token);
}
static ScStatus tool_menu_item(ScUiMenu *menu,const ToolMenuEntry *entry,ScU32 flags)
{
    ScUiMenuItem item;
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.label=tool_text(entry->label);
    item.command=entry->command;
    item.flags=flags;
    item.shortcut.size=sizeof(item.shortcut);
    item.shortcut.version=1;
    item.shortcut.key=entry->key;
    item.shortcut.modifiers=entry->modifiers;
    if(item.command && item.shortcut.key){
        ScUiCommandDesc binding;memset(&binding,0,sizeof(binding));binding.size=sizeof(binding);binding.version=1;
        binding.command=item.command;binding.flags=flags&~SC_UI_MENU_SEPARATOR;binding.label=item.label;binding.shortcut=item.shortcut;
        ScStatus status=ScUiCommandSet(tool.ui,&binding);if(status<0)return status;
    }
    ScU32 token;
    return ScUiMenuInsert(menu,0,&item,&token);
}
static ScStatus tool_menu_part(const char *title,const ToolMenuEntry *items,ScU32 count)
{
    ScUiMenu *menu=0;
    ScStatus s=ScUiMenuCreate(&menu);
    for(ScU32 i=0;s==0 && i<count;i++)s=tool_menu_item(menu,items+i,items[i].command?0:SC_UI_MENU_SEPARATOR);
    ScUiMenuItem item;
    memset(&item,0,sizeof(item));
    item.size=sizeof(item);
    item.version=1;
    item.label=tool_text(title);
    item.submenu=menu;
    ScU32 token;
    if(s==0)s=ScUiMenuInsert(tool.menus,0,&item,&token);
    ScUiMenuRelease(menu);
    return s;
}
static ScStatus tool_copy(const char *text)
{
    ScClipboardInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScClipboardQuery(&info);
    ScUiText value=tool_text(text);
    if(s==0)s=ScShellClipboardText(&value,info.epoch,&info);
    if(s==0)tool_status("已复制到系统文本剪贴板");
    return s;
}
static ScStatus tool_message(const char *title,const char *message)
{
    ScUiMessageBoxDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=tool_dialog_owner();
    d.title=tool_text(title);
    d.message=tool_text(message);
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(tool.ui,&d,&dialog);
    ScUiDialogRelease(dialog);
    return s;
}
static ScStatus tool_string_node(ScUiNode node,ScString **out,const char **z)
{
    ScString *value=0;
    ScStatus s=ScUiNodeCopyText(tool.ui,node,&value);
    if(s<0)return s;
    s=ScStringCStr(value,z);
    if(s<0){ScStringRelease(value);
        return s;
    }*out=value;
    return 0;
}
static ScStatus tool_reserve(void **buffer,ScU32 *capacity,ScU32 count,ScU32 bytes)
{
    if(count<=*capacity)return 0;
    ScU32 target=*capacity?*capacity:32;
    while(target<count){if(target>UINT_MAX/2){target=count;
            break;
        }target*=2;
    }
    if(!bytes || target>UINT_MAX/bytes)return SC_UI_E_RANGE;
    void *candidate=realloc(*buffer,target*bytes);
    if(!candidate)return SC_UI_E_MEMORY;
    *buffer=candidate;
    *capacity=target;
    return 0;
}
static ScU32 tool_percent(ScU32 value,ScU32 total)
{
    if(!total)return 0;
    if(value>=total)return 100;
    /* 乘100前不能先溢出；二进制长除保持余数严格小于分母。 */
    ScU32 quotient=0,remainder=0;
    for(ScI32 bit=6;bit>=0;bit--){quotient*=2;
        if(remainder>=total-remainder){remainder-=total-remainder;
            quotient++;
        }else remainder*=2;
        if(100u&(1u<<bit)){if(remainder>=total-value){remainder-=total-value;
                quotient++;
            }else remainder+=value;
    }}
    return quotient;
}
static ScStatus tool_root_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_COMMAND){ScStatus s=tool.command?tool.command(event->command):SC_UI_E_NOT_SUPPORTED;
        if(s<0)tool_error(s,"命令");
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST){tool.quit=1;
        ScUiContextStop(ui,0);
        return SC_UI_VETO;
    }
    if(event->type==SC_UI_EVENT_VISIBILITY){ScWindowInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        if(ScUiWindowNative(ui,tool.root,&info)==0)tool.visible=(info.state&SC32_WINDOW_VISIBLE)!=0;
    }
    if(event->type==SC_UI_EVENT_LAYOUT && event->node==tool.root){tool.metrics.size=sizeof(tool.metrics);
        tool.metrics.version=1;
        ScUiMetricsQuery(ui,tool.root,&tool.metrics);
    }
    return tool.event && event->node==tool.root?tool.event(ui,event,0):0;
}
static void tool_visible(ScUiNode node,ScU32 visible)
{
    if(!node)return;
    ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(ScUiNodeQuery(tool.ui,node,&info)==0 && ((info.state&SC_UI_VISIBLE)!=0)!=visible)
    ScUiNodeSetState(tool.ui,node,SC_UI_VISIBLE,visible?SC_UI_VISIBLE:0);
}
static ScStatus tool_create(const char *title,ScU32 width,ScU32 height,ToolCommandProc command,ToolEventProc event)
{
    memset(&tool,0,sizeof(tool));
    tool.visible=1;
    tool.command=command;
    tool.event=event;
    ScUiContextDesc context;
    memset(&context,0,sizeof(context));
    context.size=sizeof(context);
    context.version=1;
    context.cache_budget_bytes=1048576;
    context.undo_budget_bytes=262144;
    context.scheduling_quantum=64;
    ScStatus s=ScUiContextCreate(&context,&tool.ui);
    if(s<0)return s;
    ScDisplayInfo display;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    s=ScDisplayQuery(&display);
    if(s<0)return s;
    ScUiWindowDesc window;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    window.title=tool_text(title);
    window.client_width=width<display.width?width:display.width;
    window.client_height=height+64<display.height?height:display.height>96?display.height-64:display.height;
    window.x=28;
    window.y=24;
    window.style=SC32_STYLE_STANDARD;
    s=ScUiWindowCreate(tool.ui,&window,&tool.root);
    if(s==0)s=tool_layout(tool.root,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,0);
    if(s==0)s=tool_widget(tool.root,SC_UI_MENUBAR,"",0,SC_UI_AUTO,26,0,&tool.menu);
    if(s==0)s=tool_widget(tool.root,SC_UI_TOOLBAR,"",0,SC_UI_AUTO,32,0,&tool.toolbar);
    if(s==0)s=tool_widget(tool.root,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&tool.content);
    if(s==0)s=tool_layout(tool.content,SC_UI_VBOX,SC_UI_AUTO,SC_UI_AUTO,1);
    if(s==0)s=tool_widget(tool.root,SC_UI_STATUS,"",0,SC_UI_AUTO,26,0,&tool.status);
    if(s==0)s=ScUiMenuCreate(&tool.menus);
    if(s==0)s=ScUiMenuBind(tool.ui,tool.menu,tool.menus);
    if(s==0)s=tool_listen(tool.root,SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE,tool_root_event,0);
    return s;
}
static void tool_close(void)
{
    if(tool.ui && tool.root)ScUiNodeDestroy(tool.ui,tool.root);
    ScUiMenuRelease(tool.menus);
    ScUiContextRelease(tool.ui);
    memset(&tool,0,sizeof(tool));
}
