#include <errno.h>
static void sh_ui_text(const char *text,ScUiText *out) {
    out->size=sizeof(*out);
    out->version=1;
    out->data=text;
    out->bytes=text?(ScU32)strlen(text):0;
}
static ScStatus sh_widget(ScUiContext *ui,ScUiNode parent,ScU32 kind,const char *text,ScU32 command,ScI32 height,ScUiNode *out) {
    ScUiWidgetDesc d;
    ScUiLayoutDesc l;
    ScStatus s;
    ScUiNode node;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.kind=kind;
    if(kind==SC_UI_LABEL)d.flags=SC_UI_WRAP;
    d.parent=parent;
    d.command=command;
    sh_ui_text(text,&d.text);
    if(kind==SC_UI_BUTTON||kind==SC_UI_TEXTBOX||kind==SC_UI_SPINBOX||kind==SC_UI_EDIT)d.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    s=ScUiWidgetCreate(ui,&d,&node);
    if(s<0)return sh_cause(s,90,0);
    memset(&l,0,sizeof(l));
    l.size=sizeof(l);
    l.version=1;
    /* span即使在非Grid布局也须显式有效，不把零初始化当作默认1。 */
    l.column_span=l.row_span=1;
    l.width=SC_UI_AUTO;
    l.height=height;
    l.min_width=0;
    l.min_height=0;
    l.max_width=SC_UI_AUTO;
    l.max_height=SC_UI_AUTO;
    l.mode=kind==SC_UI_PANEL?SC_UI_HBOX:SC_UI_ABSOLUTE;
    l.gap=8;
    l.align_h=SC_UI_STRETCH;
    l.align_v=SC_UI_STRETCH;
    s=ScUiNodeSetLayout(ui,node,&l);
    if(s<0) {
        ScUiNodeDestroy(ui,node);
        return sh_cause(s,90,0);
    }
    *out=node;
    return 0;
}
static ScStatus sh_dialog_label(ScShellDialog *d,const char *text) {
    ScUiText t;
    sh_ui_text(text,&t);
    return sh_cause(ScUiNodeSetText(d->ui,d->label,&t),90,0);
}
ScStatus ScShellFileDialog(ScUiContext *ui,const ScUiFileDialogDesc *description,ScUiDialog **out) {
    ScUiFileDialogDesc d;
    ScStatus s=sh_record(description,sizeof(*description));
    char *directory=0;
    if(s<0)return s;
    d=*description;
    if(d.directory) {
        s=sh_absolute(d.directory,&directory);
        if(s<0)return s;
        d.directory=directory;
    }
    if(!d.title.bytes)sh_ui_text(d.flags&SC_UI_FILE_SAVE?"Save file":"Open file",&d.title);
    s=ScUiFileDialog(ui,&d,out);
    free(directory);
    return sh_cause(s,91,0);
}
ScStatus ScShellConfirm(ScUiContext *ui,ScUiNode owner,const ScUiText *title,const ScUiText *message,ScUiEventProc completion,void *context,ScUiDialog **out) {
    ScUiMessageBoxDesc d;
    ScUiMenuItem buttons[2];
    memset(&d,0,sizeof(d));
    memset(buttons,0,sizeof(buttons));
    if(sh_record(title,sizeof(*title))<0||sh_record(message,sizeof(*message))<0)return SC_SHELL_E_INVALID;
    buttons[0].size=buttons[1].size=sizeof(*buttons);
    buttons[0].version=buttons[1].version=1;
    buttons[0].command=SC_UI_DIALOG_OK;
    sh_ui_text("Continue",&buttons[0].label);
    buttons[1].command=SC_UI_DIALOG_CANCEL;
    sh_ui_text("Cancel",&buttons[1].label);
    d.size=sizeof(d);
    d.version=1;
    d.owner=owner;
    d.title=*title;
    d.message=*message;
    d.buttons=buttons;
    d.button_count=2;
    d.default_command=SC_UI_DIALOG_CANCEL;
    d.cancel_command=SC_UI_DIALOG_CANCEL;
    d.completion=completion;
    d.context=context;
    return sh_cause(ScUiMessageBox(ui,&d,out),92,0);
}
static const char *sh_error_description(ScStatus code) {
    if(code==SC_SHELL_E_MEMORY)return "Not enough memory";
    if(code==SC_SHELL_E_ACCESS)return "Permission denied";
    if(code==SC_SHELL_E_NOT_FOUND)return "The item was not found";
    if(code==SC_SHELL_E_EXISTS)return "An item already exists";
    if(code==SC_SHELL_E_STALE)return "The item changed. Refresh before trying again";
    if(code==SC_SHELL_E_BUSY)return "The item is in use by another operation";
    if(code==SC_SHELL_E_UNKNOWN)return "Result unknown. Check the files before retrying";
    if(code==SC_SHELL_E_CANCELLED)return "Operation cancelled";
    if(code==SC_SHELL_E_UTF8)return "The text has an invalid encoding";
    if(code==SC_SHELL_E_RANGE)return "The value is outside the supported range";
    if(code==SC_SHELL_E_VERSION)return "An application or library update is required";
    if(code==SC_SHELL_E_PROTOCOL)return "The service returned an invalid message";
    if(code==SC_SHELL_E_NOT_SUPPORTED)return "This operation is unavailable for the selected item";
    if(code==SC_SHELL_E_INVALID)return "The request is invalid";
    return "The operation could not be completed";
}
ScStatus ScShellErrorDialog(ScUiContext *ui,ScUiNode owner,const ScShellErrorInfo *error,const char *path,ScUiDialog **out) {
    ScUiMessageBoxDesc d;
    ScUiMenuItem button;
    ScString *message=0;
    const char *text=0;
    ScStatus s=sh_record(error,sizeof(*error));
    if(s<0)return s;
    s=ScStringCreate(0,0,&message);
    if(s>=0)s=ScStringFormat(message,"%s\n%s\nDetails: error %d, cause %d, domain %u, raw %d",sh_error_description(error->code),path?path:"",error->code,error->cause,error->domain,error->raw);
    if(s>=0)s=ScStringCStr(message,&text);
    if(s<0) {
        ScStringRelease(message);
        return sh_cause(s,93,0);
    }
    memset(&d,0,sizeof(d));
    memset(&button,0,sizeof(button));
    button.size=sizeof(button);
    button.version=1;
    button.command=SC_UI_DIALOG_OK;
    sh_ui_text("Close",&button.label);
    d.size=sizeof(d);
    d.version=1;
    d.owner=owner;
    sh_ui_text("Shell error",&d.title);
    sh_ui_text(text,&d.message);
    d.buttons=&button;
    d.button_count=1;
    d.default_command=d.cancel_command=SC_UI_DIALOG_OK;
    s=ScUiMessageBox(ui,&d,out);
    ScStringRelease(message);
    return sh_cause(s,93,0);
}
static void sh_dialog_dispose(ShObject *base) {
    ScShellDialog *d=(ScShellDialog *)base;
    if(d->listener)ScUiDisconnect(d->ui,d->listener);
    if(!sh_quiet) {
        if(d->dialog)ScUiDialogRelease(d->dialog);
        if(d->root)ScUiNodeDestroy(d->ui,d->root);
        ScUiDocumentRelease(d->document);
        if(d->ui)ScUiContextRelease(d->ui);
    }
    /* latequiet把UI引用交SCUI随后自己的quiet fini收束；此处若调用
     * 普通NodeDestroy/最后ContextRelease会重新进入已结束应用回调。 */
    if(d->lease.lease) {
        ScFile file=d->lease.file;
        ScDirectory directory=d->lease.directory;
        sh_lease_drop(&d->lease,d->pending);
        if(file)ScFileClose(file);
        if(directory)ScDirectoryClose(directory);
    }
    if(d->job)sh_release(&d->job->base);
    ScConfigRelease(d->values);
    free(d->path);
}
static ScStatus sh_properties_begin(ScShellDialog *d) {
    ScString *text=0;
    const char *z=0;
    char *end;
    ScU32 value;
    ScLeaseOpenDesc open;
    ScLeaseModeDesc mode;
    ScStatus s;
    if(d->pending)return SC_SHELL_E_BUSY;
    s=ScUiNodeCopyText(d->ui,d->mode,&text);
    if(s>=0)s=ScStringCStr(text,&z);
    if(s<0)goto done;
    errno=0;
    value=(ScU32)strtoul(z,&end,10);
    if(errno==ERANGE||end==z||*end||*z=='-'||*z=='+'||value>63) {
        s=SC_SHELL_E_RANGE;
        goto done;
    }
    if(d->lease.lease) {
        ScFile file=d->lease.file;
        ScDirectory directory=d->lease.directory;
        sh_lease_drop(&d->lease,0);
        if(file)ScFileClose(file);
        if(directory)ScDirectoryClose(directory);
        memset(&d->lease,0,sizeof(d->lease));
    }
    memset(&open,0,sizeof(open));
    open.size=sizeof(open);
    open.version=1;
    open.path=d->path;
    open.object_kind=d->metadata.kind;
    open.lease_kind=d->metadata.kind==SC32_PATH_DIRECTORY?SC32_LEASE_NAMESPACE:SC32_LEASE_READ_STABLE;
    open.expected_revision=d->metadata.generation;
    open.flags=SC32_LEASE_METADATA_ONLY;
    d->lease.size=sizeof(d->lease);
    d->lease.version=1;
    s=ScFileLeaseOpenPinned(&open,&d->lease);
    if(s<0)goto done;
    memset(&mode,0,sizeof(mode));
    mode.size=sizeof(mode);
    mode.version=1;
    mode.lease=d->lease.lease;
    mode.generation=d->lease.generation;
    mode.expected_object=d->lease.object_id;
    mode.expected_revision=d->lease.revision;
    mode.expected_snapshot_epoch=d->lease.snapshot_epoch;
    mode.final_mode=value;
    s=ScFileLeaseSetMode(&mode,&d->lease);
    if(s==SC32_PENDING) {
        d->pending=1;
        d->requested_mode=value;
        ScUiNodeSetState(d->ui,d->apply,SC_UI_ENABLED,0);
        ScUiNodeSetState(d->ui,d->mode,SC_UI_ENABLED,0);
        sh_dialog_label(d,"Saving permissions...");
        s=0;
    } else if(s>=0) {
        d->metadata.rw=value;
        d->metadata.generation=d->lease.revision;
        sh_dialog_label(d,"Permissions saved");
    }
    done:ScStringRelease(text);
    if(s<0) {
        sh_cause(s,94,0);
        sh_dialog_label(d,"Permissions unchanged; check the reported error");
    }
    return s;
}
static ScStatus sh_dialog_event(ScUiContext *ui,const ScUiEvent *event,void *context) {
    ScShellDialog *d=(ScShellDialog *)context;
    ScStatus s=0;
    ScU32 handled=0;
    if(!sh_valid(d,SH_DIALOG)||sh_quiet)return 0;
    sh_enter(&d->base);
    if(event->type==SC_UI_EVENT_DESTROY&&event->node==d->root) {
        d->root=0;
        handled=1;
    } else if(event->type==SC_UI_EVENT_COMMAND&&event->command==SC_UI_DIALOG_OK&&d->kind==1) {
        ScUiNode root=d->root;
        d->root=0;
        d->command=SC_UI_DIALOG_OK;
        s=ScUiNodeDestroy(ui,root);
        handled=1;
    } else if(event->type==SC_UI_EVENT_COMMAND&&event->command==SC_UI_DIALOG_APPLY&&d->mode) {
        s=sh_properties_begin(d);
        handled=1;
    } else if((event->type==SC_UI_EVENT_COMMAND&&event->command==SC_UI_DIALOG_CANCEL)||event->type==SC_UI_EVENT_CLOSE_REQUEST) {
        handled=1;
        d->command=SC_UI_DIALOG_CANCEL;
        if(d->job&&!d->job->closed) {
            s=ScShellFileOperationCancel(d->job);
            sh_dialog_label(d,"Cancelling; completed files are kept");
            s=SC_UI_VETO;
        } else if(d->pending) {
            d->close_requested=1;
            sh_dialog_label(d,"Finishing the current metadata operation");
            s=SC_UI_VETO;
        } else {
            ScUiNode root=d->root;
            d->root=0;
            if(root)s=ScUiNodeDestroy(ui,root);
        }
    }
    sh_leave(&d->base);
    return s<0?s:(event->type==SC_UI_EVENT_CLOSE_REQUEST?s:(handled?SC_UI_HANDLED:0));
}
static ScStatus sh_dialog_window(ScUiContext *ui,ScUiNode owner,const char *title,ScU32 logical_height,ScShellDialog **out) {
    ScShellDialog *d=0;
    ScUiWindowDesc window;
    ScUiLayoutDesc layout;
    ScUiSubscription listener;
    ScDisplayInfo display;
    ScU32 scale,w,h;
    ScStatus s;
    if(!out||!ui)return SC_SHELL_E_INVALID;
    d=(ScShellDialog *)sh_new(sizeof(*d),SH_DIALOG,sh_dialog_dispose);
    if(!d)return SC_SHELL_E_MEMORY;
    s=ScUiContextRetain(ui);
    if(s<0)goto fail;
    d->ui=ui;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    s=ScDisplayQuery(&display);
    if(s<0)goto fail;
    scale=display.scale_percent?display.scale_percent:100;
    w=480*scale/100;
    h=logical_height*scale/100;
    if(w+32>display.width)w=display.width>32?display.width-32:display.width;
    if(h+64>display.height)h=display.height>64?display.height-64:display.height;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    window.owner=owner;
    window.style=SC32_STYLE_CLOSE|SC32_STYLE_MODAL;
    window.client_width=w;
    window.client_height=h;
    window.x=(ScI32)(display.width-w)/2;
    window.y=(ScI32)(display.height-h)/2;
    sh_ui_text(title,&window.title);
    s=ScUiWindowCreate(ui,&window,&d->root);
    if(s<0)goto fail;
    memset(&layout,0,sizeof(layout));
    layout.size=sizeof(layout);
    layout.version=1;
    layout.column_span=layout.row_span=1;
    layout.mode=SC_UI_VBOX;
    layout.width=SC_UI_AUTO;
    layout.height=SC_UI_AUTO;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.padding[0]=layout.padding[1]=layout.padding[2]=layout.padding[3]=12;
    layout.gap=8;
    layout.align_h=SC_UI_STRETCH;
    s=ScUiNodeSetLayout(ui,d->root,&layout);
    if(s<0)goto fail;
    memset(&listener,0,sizeof(listener));
    listener.size=sizeof(listener);
    listener.version=1;
    listener.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;
    listener.procedure=sh_dialog_event;
    listener.context=d;
    s=ScUiListen(ui,d->root,&listener,&d->listener);
    if(s<0)goto fail;
    *out=d;
    return 0;
    fail:if(d)sh_release(&d->base);
    return sh_cause(s,95,0);
}
ScStatus ScShellProperties(ScUiContext *ui,ScUiNode owner,const char *path,ScShellDialog **out) {
    ScShellDialog *d=0;
    ScString *summary=0;
    const char *text;
    ScUiNode n,row,path_node;
    ScValue value;
    ScUiText title;
    ScStatus s;
    if(!out)return SC_SHELL_E_INVALID;
    s=sh_dialog_window(ui,owner,"Properties",280,&d);
    if(s<0)return s;
    s=sh_absolute(path,&d->path);
    if(s<0)goto fail;
    memset(&d->metadata,0,sizeof(d->metadata));
    d->metadata.size=sizeof(d->metadata);
    d->metadata.version=1;
    s=ScPathMetadata(d->path,&d->metadata);
    if(s<0)goto fail;
    s=sh_widget(ui,d->root,SC_UI_TEXTBOX,d->path,0,30,&path_node);
    if(s<0)goto fail;
    s=ScUiNodeSetState(ui,path_node,SC_UI_READONLY,SC_UI_READONLY);
    if(s<0)goto fail;
    s=ScStringCreate(0,0,&summary);
    if(s>=0)s=ScStringFormat(summary,"%s  |  %u bytes\nOwner %d   Group %d   Revision %u",d->metadata.kind==2?"Directory":"File",d->metadata.bytes,d->metadata.uid,d->metadata.gid,d->metadata.generation);
    if(s>=0)s=ScStringCStr(summary,&text);
    if(s>=0)s=sh_widget(ui,d->root,SC_UI_LABEL,text,0,48,&n);
    if(s<0)goto fail;
    s=sh_widget(ui,d->root,SC_UI_LABEL,"Permissions (owner/group/other RW bits, 0..63)",0,22,&n);
    if(s<0)goto fail;
    s=sh_widget(ui,d->root,SC_UI_SPINBOX,"",0,28,&d->mode);
    if(s>=0)s=ScUiSpinRange(ui,d->mode,0,63,1);
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.type=SC_VALUE_I32;
    value.lo=d->metadata.rw;
    if(s>=0)s=ScUiNodeSetValue(ui,d->mode,&value);
    if(s<0)goto fail;
    ScUiLabelAssociate(ui,n,d->mode);
    s=sh_widget(ui,d->root,SC_UI_LABEL,"Changes use your current filesystem permissions",0,28,&d->label);
    if(s<0)goto fail;
    s=sh_widget(ui,d->root,SC_UI_PANEL,"",0,30,&row);
    if(s<0)goto fail;
    s=sh_widget(ui,row,SC_UI_BUTTON,"Apply",SC_UI_DIALOG_APPLY,28,&d->apply);
    if(s>=0)s=sh_widget(ui,row,SC_UI_BUTTON,"Close",SC_UI_DIALOG_CANCEL,28,&d->cancel);
    if(s<0)goto fail;
    sh_ui_text("Full path",&title);
    ScUiAccessibleSetName(ui,path_node,&title);
    sh_ui_text("Permissions",&title);
    ScUiAccessibleSetName(ui,d->mode,&title);
    ScStringRelease(summary);
    *out=d;
    return 0;
    fail:ScStringRelease(summary);
    sh_release(&d->base);
    return sh_cause(s,96,0);
}
ScStatus ScShellProgress(ScUiContext *ui,ScUiNode owner,ScShellFileJob *job,ScShellDialog **out) {
    ScShellDialog *d=0;
    ScStatus s;
    ScUiDocumentDesc document;
    ScUiLayoutDesc layout;
    if(!out||!sh_valid(job,SH_JOB))return SC_SHELL_E_INVALID;
    s=sh_dialog_window(ui,owner,"File operation",320,&d);
    if(s<0)return s;
    s=sh_job_hold(job);
    if(s<0)goto fail;
    d->job=job;
    d->kind=2;
    s=sh_widget(ui,d->root,SC_UI_LABEL,"Preparing the operation",0,62,&d->label);
    if(s>=0)s=sh_widget(ui,d->root,SC_UI_PROGRESS,"",0,24,&d->progress);
    if(s>=0)s=sh_widget(ui,d->root,SC_UI_EDIT,"",0,SC_UI_AUTO,&d->details);
    memset(&document,0,sizeof(document));
    document.size=sizeof(document);
    document.version=1;
    sh_ui_text("",&document.text);
    if(s>=0)s=ScUiDocumentCreate(&document,&d->document);
    if(s>=0)s=ScUiEditBind(ui,d->details,d->document);
    if(s>=0)s=ScUiNodeSetState(ui,d->details,SC_UI_READONLY,SC_UI_READONLY);
    memset(&layout,0,sizeof(layout));
    layout.size=sizeof(layout);
    layout.version=1;
    layout.column_span=layout.row_span=1;
    layout.width=layout.height=SC_UI_AUTO;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.min_height=60;
    layout.flex_weight=1;
    if(s>=0)s=ScUiNodeSetLayout(ui,d->details,&layout);
    if(s>=0)s=sh_widget(ui,d->root,SC_UI_BUTTON,"Cancel",SC_UI_DIALOG_CANCEL,30,&d->cancel);
    if(s<0)goto fail;
    *out=d;
    return 0;
    fail:sh_release(&d->base);
    return sh_cause(s,97,0);
}
static const char *sh_progress_state(const ScShellFileProgress *p) {
    if(p->state==SC_SHELL_JOB_CONNECTING)return "Starting worker";
    if(p->state==SC_SHELL_JOB_PLANNING)return "Planning";
    if(p->state==SC_SHELL_JOB_CONFLICT)return "Waiting for a decision";
    if(p->state==SC_SHELL_JOB_CANCELLING)return "Cancelling";
    if(p->state==SC_SHELL_JOB_CANCELLED)return "Cancelled; completed items are kept";
    if(p->state==SC_SHELL_JOB_UNKNOWN)return "Result unknown; check the files";
    if(p->state==SC_SHELL_JOB_FAILED||p->items_failed||p->status<0)return p->items_done?"Partially completed":"Failed";
    if(p->state==SC_SHELL_JOB_FINISHED)return "Completed";
    return p->flags&SC_SHELL_PROGRESS_PRIVATE_PREFIX?"Copying, not committed":"Working";
}
static ScStatus sh_progress_bytes(const ScWideCounter *bytes,ScString **out) {
    ScWideCounter value=*bytes;
    char reversed[20],text[21];
    ScU32 used=0,i;
    if(!value.hi)return sh_cause(ScFormatBytes(value.lo,out),98,0);
    /* 标准库只提供u32 byte formatter；逐位长除法给真正u64十进制，
     * 不用浮点/long long，也不把hi:lo实现细节当用户可读字节数。 */
    do {
        ScWideCounter quotient;
        ScU32 remainder=0;
        ScI32 bit;
        quotient.lo=quotient.hi=0;
        for(bit=63;bit>=0;--bit) {
            ScU32 digit=bit>=32?(value.hi>>(bit-32))&1u:(value.lo>>bit)&1u;
            remainder=remainder*2+digit;
            if(remainder>=10) {
                remainder-=10;
                if(bit>=32)quotient.hi|=1u<<(bit-32);
                else quotient.lo|=1u<<bit;
            }
        }
        reversed[used++]=(char)('0'+remainder);
        value=quotient;
    }while(value.lo||value.hi);
    for(i=0;i<used;++i)text[i]=reversed[used-1-i];
    text[used]=0;
    {
        ScString *string=0;
        ScStatus s=ScStringCreate(0,0,&string);
        if(s>=0)s=ScStringFormat(string,"%s bytes",text);
        if(s<0)ScStringRelease(string);
        else *out=string;
        return sh_cause(s,98,0);
    }
}
static const char *sh_item_result_name(ScU32 state) {
    if(state==SC_SHELL_RESULT_DONE)return "Completed";
    if(state==SC_SHELL_RESULT_SKIPPED)return "Skipped";
    if(state==SC_SHELL_RESULT_SOURCE_RETAINED)return "Copied; source kept";
    if(state==SC_SHELL_RESULT_CANCELLED)return "Cancelled";
    if(state==SC_SHELL_RESULT_UNCERTAIN)return "Result unknown";
    return "Failed";
}
static ScStatus sh_progress_details(ScShellDialog *d,ScU32 *ready) {
    ScContainerInfo info;
    ScWindowInfo window;
    ScU32 budget=16,added=0;
    ScStatus s;
    ScString *line=0;
    memset(&window,0,sizeof(window));
    window.size=sizeof(window);
    window.version=1;
    s=ScUiWindowNative(d->ui,d->root,&window);
    if(s<0)return s;
    if(!(window.state&SC32_WINDOW_VISIBLE))return 0;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScArrayQuery(d->job->results,&info);
    if(s<0)return s;
    if(d->log_index==info.length)return 0;
    s=ScStringCreate(0,0,&line);
    if(s<0)return s;
    while(budget--&&d->log_index+added<info.length) {
        ScShellFileResult result;
        s=ScArrayGet(d->job->results,d->log_index+added,&result);
        if(s<0)break;
        s=ScStringAppendFormat(line,"%s\n%s%s%s\n",sh_item_result_name(result.state),result.source,*result.destination?" -> ":"",result.destination);
        if(s>=0&&result.status<0)s=ScStringAppendFormat(line,"  Details: error %d, cause %d, domain %u, raw %d\n",result.status,result.cause,result.domain,result.raw);
        if(s<0)break;
        ++added;
    }
    if(s>=0) {
        ScView view;
        ScUiDocumentInfo document;
        ScUiText text;
        memset(&view,0,sizeof(view));
        view.size=sizeof(view);
        view.version=1;
        s=ScStringView(line,&view);
        memset(&document,0,sizeof(document));
        document.size=sizeof(document);
        document.version=1;
        if(s>=0)s=ScUiDocumentQuery(d->document,&document);
        text.size=sizeof(text);
        text.version=1;
        text.data=(const char *)view.data;
        text.bytes=view.bytes;
        if(s>=0)s=ScUiDocumentReplace(d->document,document.bytes,0,&text,SC_UI_REPLACE_NO_UNDO);
        if(s>=0)d->log_index+=added;
    }
    ScStringRelease(line);
    if(s>=0)s=ScArrayQuery(d->job->results,&info);
    if(s>=0&&d->log_index<info.length)*ready=1;
    return s;
}
ScStatus ScShellDialogStep(ScShellDialog *d) {
    ScStatus s=0;
    ScU32 ready=0;
    if(!sh_valid(d,SH_DIALOG))return SC_SHELL_E_STALE;
    if(!d->root)return SC_SHELL_END;
    sh_enter(&d->base);
    if(d->pending) {
        ScLeaseControl c;
        memset(&c,0,sizeof(c));
        c.size=sizeof(c);
        c.version=1;
        c.lease=d->lease.lease;
        c.generation=d->lease.generation;
        c.operation_sequence=d->lease.operation_sequence;
        s=ScFileLeaseAdvance(&c,&d->lease);
        if(s!=SC32_PENDING) {
            d->pending=0;
            ScUiNodeSetState(d->ui,d->apply,SC_UI_ENABLED,SC_UI_ENABLED);
            ScUiNodeSetState(d->ui,d->mode,SC_UI_ENABLED,SC_UI_ENABLED);
            if(s>=0) {
                d->metadata.rw=d->requested_mode;
                d->metadata.generation=d->lease.revision;
                sh_dialog_label(d,"Permissions saved");
            } else {
                sh_cause(s,98,0);
                sh_dialog_label(d,s==SC32_E_STALE?"The file changed; reopen Properties":"Permissions were not saved");
            }
            if(d->close_requested) {
                ScUiNode root=d->root;
                d->root=0;
                ScUiNodeDestroy(d->ui,root);
            }
        } else s=0;
    }
    if(d->job&&d->root&&(!d->have_progress||memcmp(&d->last_progress,&d->job->progress,sizeof(d->last_progress)))) {
        ScShellFileProgress p;
        ScString *line=0,*amount=0;
        const char *text=0,*quantity=0;
        ScUiText t;
        p=d->job->progress;
        s=sh_progress_bytes(&p.bytes,&amount);
        if(s>=0)s=ScStringCStr(amount,&quantity);
        if(s>=0)s=ScStringCreate(0,0,&line);
        if(s>=0)s=ScStringFormat(line,"%s\n%u completed, %u failed, %u skipped\n%s",sh_progress_state(&p),p.items_done,p.items_failed,p.items_skipped,quantity);
        if(s>=0)s=ScStringCStr(line,&text);
        if(s>=0)s=sh_dialog_label(d,text);
        ScStringRelease(line);
        ScStringRelease(amount);
        if(s>=0)s=ScUiProgressSet(d->ui,d->progress,p.items_done,p.items_total?p.items_total:1,(p.total_known||d->job->closed)?SC_UI_PROGRESS_DETERMINATE:SC_UI_PROGRESS_INDETERMINATE);
        if(d->job->closed) {
            sh_ui_text("Close",&t);
            ScUiNodeSetText(d->ui,d->cancel,&t);
        }
        if(s>=0) {
            d->last_progress=p;
            d->have_progress=1;
        }
    }
    if(s>=0&&d->job&&d->root)s=sh_progress_details(d,&ready);
    if(s>=0)s=d->root?(d->pending||ready?SC_SHELL_READY:SC_SHELL_PENDING):SC_SHELL_END;
    sh_leave(&d->base);
    return sh_cause(s,98,0);
}
void ScShellDialogClose(ScShellDialog *d) {
    if(sh_valid(d,SH_DIALOG))sh_release(&d->base);
}
static ScStatus sh_menu_confirm_delete(ScShellMenu *m) {
    ScShellDialog *d=0;
    ScString *paths=0;
    const char *text;
    ScUiNode list,row,yes;
    ScUiLayoutDesc layout;
    ScU32 i;
    ScStatus s;
    s=sh_dialog_window(m->desc.ui,m->desc.owner,"Delete selected items",340,&d);
    if(s<0)return s;
    d->kind=1;
    s=sh_widget(d->ui,d->root,SC_UI_LABEL,"Delete these items? Completed deletions cannot be undone.",0,40,&d->label);
    if(s>=0)s=ScStringCreate(0,0,&paths);
    for(i=0;s>=0&&i<m->desc.count;++i) {
        s=ScStringAppendFormat(paths,"%s\n",m->items[i].source);
    }
    if(s>=0)s=ScStringCStr(paths,&text);
    if(s>=0)s=sh_widget(d->ui,d->root,SC_UI_EDIT,text,0,SC_UI_AUTO,&list);
    if(s>=0) {
        memset(&layout,0,sizeof(layout));
        layout.size=sizeof(layout);
        layout.version=1;
    layout.column_span=layout.row_span=1;
        layout.width=layout.height=SC_UI_AUTO;
        layout.max_width=layout.max_height=SC_UI_AUTO;
        layout.min_height=80;
        layout.flex_weight=1;
        s=ScUiNodeSetLayout(d->ui,list,&layout);
    }
    if(s>=0)s=ScUiNodeSetState(d->ui,list,SC_UI_READONLY,SC_UI_READONLY);
    if(s>=0)s=sh_widget(d->ui,d->root,SC_UI_PANEL,"",0,30,&row);
    if(s>=0)s=sh_widget(d->ui,row,SC_UI_BUTTON,"Delete",SC_UI_DIALOG_OK,28,&yes);
    if(s>=0)s=sh_widget(d->ui,row,SC_UI_BUTTON,"Cancel",SC_UI_DIALOG_CANCEL,28,&d->cancel);
    ScStringRelease(paths);
    if(s<0) {
        ScShellDialogClose(d);
        return sh_cause(s,99,0);
    }
    ScUiButtonSetDefault(d->ui,d->root,d->cancel);
    m->dialog=d;
    return 0;
}
