#include "S2APP.H"
static ScStatus s2_message(S2App *a,const char *title,const char *message)
{
    if(a->dialog)return SC_UI_E_BUSY;
    ScUiMessageBoxDesc d;
    s2_record(&d,sizeof(d));
    d.owner=a->root;
    d.title=s2_text(title);
    d.message=s2_text(message);
    d.completion=s2_dialog_complete;
    d.context=a;
    a->dialog_kind=S_DIALOG_INFO;
    ScStatus status=ScUiMessageBox(a->ui,&d,&a->dialog);
    if(status<0)a->dialog_kind=0;
    return status;
}
static ScStatus s2_execute(S2App *a,ScU32 command)
{
    if(command==S_SIGNIN)return 0;
    if(command==S_CLOSE){s2_auth_cancel(a);
        a->closed=1;
        ScUiContextStop(a->ui,0);
        return 0;
    }
    if(command==S_CANCEL){s2_auth_cancel(a);
        s2_update_auth_ui(a);
        s2_status(a,"Sign-in cancelled; password cleared");
        return 0;
    }
    if(a->dialog)return SC_UI_E_BUSY;
    if(command==S_CLEAR){s2_auth_cancel(a);
        ScUiText empty=s2_text("");
        ScUiNodeSetText(a->ui,a->username,&empty);
        s2_update_auth_ui(a);
        ScUiFocus(a->ui,a->username);
        s2_status(a,"Input cleared");
        return 0;
    }
    if(command==S_REQUEST_SIGNIN)return ScUiCommand(a->ui,a->signin,S_SIGNIN);
    if(command==S_LOGIN_PAGE)return ScUiTabSelect(a->ui,a->tabs,a->login_page);
    if(command==S_ABOUT)return s2_message(a,"Login and sessions","Each successful sign-in creates a separate normal desktop. Switching keeps background work; logging out stops that session's processes.\nOrdinary root does not obtain external hardware SYSTEM authority.\nThe password field uses dedicated erasable memory and no clipboard or undo history.");
    if(a->locked)return SC32_E_ACCESS;
    if(command==S_SESSIONS_PAGE)return ScUiTabSelect(a->ui,a->tabs,a->sessions_page);
    if(command==S_REFRESH)return s2_refresh(a);
    if(command==S_LOCK){ScUiSecretClear(a->ui,a->password);
        return ScSessionControl(a->self.session,SC32_SESSION_LOGIN_PANEL);
    }
    if(command==S_PROPERTIES){ScSessionInfo row;
        ScStatus status=s2_selected(a,&row);
        if(status<0)return status;
        char message[320];
        snprintf(message,sizeof(message),"%s\nSession %u, UID %d, GID %d\n%s, %s\n%u members, generation %u",row.name,row.id,row.uid,row.gid,row.kind==1?"External SYSTEM":row.kind==2?"Locked":"Normal",row.visible?"visible":"hidden",row.members,row.generation);
        return s2_message(a,"Session properties",message);
    }
    if(command==S_SWITCH || command==S_LOGOUT){if(a->control_pending || a->refreshing)return SC32_E_BUSY;
        ScStatus status=s2_selected(a,&a->control);
        if(status<0)return status;
        if(a->control.closing)return SC32_E_STALE;
        a->control_action=command==S_SWITCH?SC32_SESSION_SHOW:SC32_SESSION_LOGOUT;
        if(command==S_SWITCH){a->control_pending=1;
            status=s2_refresh(a);
            if(status<0)a->control_pending=0;
            return status;
        }
        if(a->control.kind!=0)return SC32_E_ACCESS;
        char message[240];
        snprintf(message,sizeof(message),"Log out %s (session %u)?\nIts processes and unsaved windows will close.",a->control.name,a->control.id);
        ScUiMenuItem buttons[2];
        s2_record(buttons,sizeof(buttons[0]));
        s2_record(buttons+1,sizeof(buttons[1]));
        buttons[0].command=SC_UI_DIALOG_OK;
        buttons[0].label=s2_text("Log out");
        buttons[1].command=SC_UI_DIALOG_CANCEL;
        buttons[1].label=s2_text("Cancel");
        ScUiMessageBoxDesc d;
        s2_record(&d,sizeof(d));
        d.owner=a->root;
        d.title=s2_text("Log out session");
        d.message=s2_text(message);
        d.buttons=buttons;
        d.button_count=2;
        d.default_command=SC_UI_DIALOG_CANCEL;
        d.cancel_command=SC_UI_DIALOG_CANCEL;
        d.completion=s2_dialog_complete;
        d.context=a;
        a->dialog_kind=S_DIALOG_LOGOUT;
        status=ScUiMessageBox(a->ui,&d,&a->dialog);
        if(status<0)a->dialog_kind=0;
        return status;
    }
    return SC_UI_E_NOT_SUPPORTED;
}
static void s2_dialog_poll(S2App *a)
{
    if(!a->dialog || !a->dialog_done)return;
    ScUiDialog *dialog=a->dialog;
    ScU32 kind=a->dialog_kind,command=a->dialog_command;
    a->dialog=0;
    a->dialog_done=a->dialog_kind=a->dialog_command=0;
    ScUiDialogRelease(dialog);
    if(kind==S_DIALOG_LOGOUT && command==SC_UI_DIALOG_OK){a->control_pending=1;
        ScStatus status=s2_refresh(a);
        if(status<0){a->control_pending=0;
            s2_error(a,"Validate selected session",status);
    }}
}
static ScStatus s2_validate_name(S2App *a,const ScUiEvent *e)
{
    if(!e->data || e->bytes!=sizeof(ScUiText))return 0;
    const ScUiText *text=e->data;
    if(text->bytes>31)return SC_UI_VETO;
    for(ScU32 i=0;i<text->bytes;i++){unsigned char c=(unsigned char)text->data[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return SC_UI_VETO;
    }
    if(e->flags&SC_UI_EVENT_DEVICE){ScUiTextSelection selected;
        s2_record(&selected,sizeof(selected));
        ScString *whole=0;
        const char *name=0;
        ScStatus status=ScUiEditGetSelection(a->ui,a->username,&selected);
        if(status==0)status=ScUiNodeCopyText(a->ui,a->username,&whole);
        if(status==0)status=ScStringCStr(whole,&name);
        if(status==0){ScU32 removed=selected.anchor>selected.caret?selected.anchor-selected.caret:selected.caret-selected.anchor;
            if((ScU32)strlen(name)-removed+text->bytes>31)status=SC_UI_E_RANGE;
        }ScStringRelease(whole);
        if(status<0)return SC_UI_VETO;
    }
    return 0;
}
static ScStatus s2_event(ScUiContext *ui,const ScUiEvent *e,void *context)
{
    (void)ui;
    S2App *a=context;
    if(e->type==SC_UI_EVENT_CLOSE_REQUEST && e->node==a->root && e->phase==SC_UI_CAPTURE_PHASE){s2_queue(a,S_CLOSE);
        return SC_UI_VETO;
    }
    if(e->type==SC_UI_EVENT_COMMAND && e->command>=S_SIGNIN && e->command<=S_SESSIONS_PAGE && (!a->dialog || e->root==a->root)
    && (e->phase==SC_UI_BUBBLE_PHASE || (!e->node && e->phase==SC_UI_TARGET_PHASE))){if(e->command!=S_SIGNIN){ScStatus status=s2_queue(a,e->command);
            if(status<0)a->failure=status;
        }return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_COMMAND_QUERY && e->phase==SC_UI_TARGET_PHASE && e->bytes==sizeof(ScUiCommandDesc)){ScUiCommandDesc *d=(ScUiCommandDesc *)e->data;
        int enabled=!a->closed && !a->dialog;
        if(d->command==S_SIGNIN || d->command==S_REQUEST_SIGNIN)enabled=enabled && (!a->ticket.id || a->auth.state==SC32_AUTH_READY);
        if(d->command==S_CANCEL)enabled=enabled && a->ticket.id;
        if(d->command==S_REFRESH)enabled=enabled && !a->refreshing && !a->locked;
        if(d->command==S_SWITCH || d->command==S_LOGOUT || d->command==S_PROPERTIES){ScSessionInfo row;
            enabled=enabled && !a->locked && !a->refreshing && !a->control_pending && s2_selected(a,&row)==0;
            if(enabled && d->command==S_LOGOUT && row.kind!=0)enabled=0;
        }
        if(d->command==S_LOCK)enabled=enabled && !a->locked;
        if(enabled)d->flags&=~SC_UI_MENU_DISABLED;
        else d->flags|=SC_UI_MENU_DISABLED;
        return 0;
    }
    if(e->type==SC_UI_EVENT_VALUE_CHANGING && e->node==a->username && e->phase==SC_UI_CAPTURE_PHASE)return s2_validate_name(a,e);
    if(e->type==SC_UI_EVENT_SELECTION_CHANGED && e->node==a->table && e->phase==SC_UI_BUBBLE_PHASE){s2_properties_update(a);
        return 0;
    }
    if(e->type==SC_UI_EVENT_TIMER && e->command==a->auth_timer && e->phase==SC_UI_CAPTURE_PHASE){a->auth_poll=1;
        return 0;
    }
    if(e->type==SC_UI_EVENT_VISIBILITY && e->node==a->root && e->phase==SC_UI_CAPTURE_PHASE){ScWindowInfo info;
        s2_record(&info,sizeof(info));
        if(ScUiWindowNative(a->ui,a->root,&info)==0){a->visible=(info.state&SC32_WINDOW_VISIBLE)!=0 && !(info.state&SC32_WINDOW_MINIMIZED);
            if(a->visible && !a->locked)s2_refresh(a);
        }return 0;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN && !a->dialog && e->root==a->root && e->phase==SC_UI_CAPTURE_PHASE && e->key==27){s2_queue(a,a->ticket.id?S_CANCEL:S_CLOSE);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_KEY_DOWN && !a->dialog && e->node==a->table && e->phase==SC_UI_CAPTURE_PHASE && e->key==10 && !(e->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))){s2_queue(a,S_SWITCH);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_LAYOUT && e->node==a->root && e->phase==SC_UI_TARGET_PHASE){ScWindowInfo info;
        s2_record(&info,sizeof(info));
        ScUiMetrics metrics;
        s2_record(&metrics,sizeof(metrics));
        if(ScUiWindowNative(a->ui,a->root,&info)==0 && ScUiMetricsQuery(a->ui,a->root,&metrics)==0){ScU32 compact=info.client_width*100u<760u*metrics.scale_percent;
            if(compact!=a->compact){a->compact=compact;
                ScUiBatchBegin(a->ui);
                ScUiNodeSetState(a->ui,a->details,SC_UI_VISIBLE,compact?0:SC_UI_VISIBLE);
                ScUiNodeSetState(a->ui,a->splitter,SC_UI_VISIBLE,compact?0:SC_UI_VISIBLE);
                ScUiBatchEnd(a->ui);
    }}}
    return 0;
}
