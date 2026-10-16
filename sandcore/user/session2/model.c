#include "S2APP.H"
static const ScSessionInfo *s2_rows(const ScArray *array)
{ScView view;
    s2_record(&view,sizeof(view));
    return array && ScArrayView(array,&view)==0?view.data:0;
}
static const ScSessionInfo *s2_find(const ScArray *array,ScU32 count,ScU32 id)
{const ScSessionInfo *rows=s2_rows(array);
    if(!rows)return 0;
    ScU32 low=0,high=count;
    while(low<high){ScU32 middle=low+(high-low)/2;
        if(rows[middle].id<id)low=middle+1;
        else high=middle;
    }return low<count && rows[low].id==id?rows+low:0;
}
static ScStatus s2_selected(S2App *a,ScSessionInfo *out)
{ScUiSelectionInfo info;
    s2_record(&info,sizeof(info));
    ScStatus status=ScUiSelectionQuery(a->selection,&info);
    if(status<0)return status;
    const ScSessionInfo *row=s2_find(a->rows,a->count,info.current);
    if(!row)return SC32_E_NOT_FOUND;
     *out=*row;
    return 0;
}
static ScStatus s2_refresh(S2App *a)
{
    if(a->locked)return SC32_E_ACCESS;
    if(a->refreshing)return 0;
    ScArrayDesc desc;
    s2_record(&desc,sizeof(desc));
    desc.item_bytes=sizeof(ScSessionInfo);
    ScStatus status=ScArrayCreate(&desc,&a->candidate);
    if(status<0)return status;
    ScUiSelectionInfo selected;
    s2_record(&selected,sizeof(selected));
    if(ScUiSelectionQuery(a->selection,&selected)==0)a->selection_restore=selected.current;
    else a->selection_restore=0;
    a->candidate_count=0;
    a->cursor=SC32_ENUM_BEGIN;
    a->refreshing=1;
    s2_status(a,"Reading accessible sessions...");
    return 0;
}
static void s2_refresh_step(S2App *a)
{
    if(!a->refreshing)return;
    ScSessionPage page;
    s2_record(&page,sizeof(page));
    page.cursor=a->cursor;
    ScSessionInfo rows[64];
    ScStatus status=ScSessionEnumerate(&page,rows,64);
    if(status==0 && page.count){if(page.count>UINT_MAX-a->candidate_count)status=SC32_E_RANGE;
        else status=ScArrayInsert(a->candidate,a->candidate_count,rows,page.count);
    }
    if(status<0){ScArrayRelease(a->candidate);
        a->candidate=0;
        a->refreshing=0;
        a->control_pending=0;
        s2_error(a,"Session refresh",status);
        return;
    }
    a->candidate_count+=page.count;
    if(page.cursor!=SC32_ENUM_BEGIN){if(page.cursor==a->cursor){ScArrayRelease(a->candidate);
            a->candidate=0;
            a->refreshing=0;
            a->control_pending=0;
            s2_error(a,"Session cursor",SC32_E_STALE);
            return;
        }a->cursor=page.cursor;
        return;
    }
    ScArray *old=a->rows;
    a->rows=a->candidate;
    a->candidate=0;
    a->count=a->candidate_count;
    a->refreshing=0;
    if(++a->revision==0)a->revision=1;
    ScUiDataChange change;
    s2_record(&change,sizeof(change));
    change.revision=a->revision;
    change.kind=SC_UI_DATA_RESET;
    ScUiProviderNotify(a->provider,&change);
    if(a->selection_restore && s2_find(a->rows,a->count,a->selection_restore))ScUiSelectionSet(a->selection,a->selection_restore,SC_UI_REPLACE);
    ScArrayRelease(old);
    s2_properties_update(a);
    char text[96];
    snprintf(text,sizeof(text),"%u accessible sessions",a->count);
    s2_status(a,text);
    if(a->control_pending){ScSessionInfo expected=a->control;
        const ScSessionInfo *now=s2_find(a->rows,a->count,expected.id);
        ScU32 action=a->control_action;
        a->control_pending=0;
        if(!now || now->generation!=expected.generation || now->uid!=expected.uid || now->gid!=expected.gid || now->kind!=expected.kind || now->closing){s2_error(a,"Session changed; refresh and choose again",SC32_E_STALE);
            return;
        }
        status=ScSessionControl(now->id,action);
        if(status<0)s2_error(a,action==SC32_SESSION_SHOW?"Switch desktop":"Log out",status);
        else{s2_status(a,action==SC32_SESSION_SHOW?"Desktop switched":"Session logged out");
            if(action==SC32_SESSION_LOGOUT)s2_refresh(a);
    }}
}
static ScStatus s2_submit(ScUiContext *ui,ScUiNode secret,const char *password,ScU32 bytes,void *context)
{
    (void)ui;
    (void)secret;
    S2App *a=context;
    if(a->closed)return SC_UI_E_CANCELLED;
    if(a->ticket.id){if(a->auth.state==SC32_AUTH_READY){a->auth_activate=1;
            return 0;
        }return SC_UI_E_BUSY;
    }
    ScString *name=0;
    const char *username=0;
    ScStatus status=ScUiNodeCopyText(a->ui,a->username,&name);
    if(status==0)status=ScStringCStr(name,&username);
    if(status==0){ScAuthLoginDesc desc;
        s2_record(&desc,sizeof(desc));
        desc.username=username;
        desc.username_bytes=(ScU32)strlen(username);
        desc.password=password;
        desc.password_bytes=bytes;
        s2_record(&a->ticket,sizeof(a->ticket));
        status=ScAuthLoginBegin(&desc,&a->ticket);
    }
    ScStringRelease(name);
    if(status<0){a->ticket.id=0;
        a->failure=status;
        s2_status(a,status==SC32_E_AGAIN?"Please wait before another sign-in attempt":"Sign-in could not start; password cleared");
        return 0;
    }
    ScMonotonicClock clock;
    s2_record(&clock,sizeof(clock));
    if(ScSystemMonotonic(&clock)<0){s2_auth_cancel(a);
        s2_status(a,"Clock unavailable; sign-in cancelled");
        return 0;
    }
    a->auth_deadline=clock.ticks;
    ScU32 old=a->auth_deadline.lo;
    a->auth_deadline.lo+=60000u;
    if(a->auth_deadline.lo<old)a->auth_deadline.hi++;
    a->auth_poll=1;
    s2_record(&a->auth,sizeof(a->auth));
    a->auth.state=SC32_AUTH_PENDING;
    s2_status(a,"Verifying password...");
    return 0;
}
static void s2_auth_cancel(S2App *a)
{
    if(a->ticket.id)ScAuthLoginCancel(&a->ticket);
    a->ticket.id=0;
    a->auth_poll=a->auth_activate=0;
    memset(&a->auth,0,sizeof(a->auth));
    if(a->auth_timer){ScUiTimerCancel(a->ui,a->auth_timer);
        a->auth_timer=0;
    }if(a->password)ScUiSecretClear(a->ui,a->password);
}
static void s2_update_auth_ui(S2App *a)
{
    ScU32 busy=a->ticket.id!=0,mode=!busy?0:a->auth.state==SC32_AUTH_READY?2:1;
    if(a->auth_ui_mode!=mode){a->auth_ui_mode=mode;
        ScUiNodeSetState(a->ui,a->username,SC_UI_ENABLED,busy?0:SC_UI_ENABLED);
        ScUiNodeSetState(a->ui,a->password,SC_UI_ENABLED,busy?0:SC_UI_ENABLED);
        ScUiText label=s2_text(mode==2?"Open desktop":busy?"Verifying...":"Sign in");
        ScUiNodeSetText(a->ui,a->signin,&label);
        ScUiNodeSetState(a->ui,a->signin,SC_UI_ENABLED,mode!=1?SC_UI_ENABLED:0);
        ScUiNodeSetState(a->ui,a->cancel,SC_UI_ENABLED,busy?SC_UI_ENABLED:0);
        ScUiNodeSetState(a->ui,a->progress,SC_UI_VISIBLE,mode==1?SC_UI_VISIBLE:0);
    }
    if(mode==1){if(a->auth_ui_done!=a->auth.iterations_done || a->auth_ui_total!=a->auth.iterations_total || a->auth_ui_flags!=a->auth.flags){
            ScUiProgressSet(a->ui,a->progress,a->auth.iterations_done,a->auth.iterations_total,a->auth.flags&SC32_AUTH_PROGRESS?SC_UI_PROGRESS_DETERMINATE:SC_UI_PROGRESS_INDETERMINATE);
            a->auth_ui_done=a->auth.iterations_done;
            a->auth_ui_total=a->auth.iterations_total;
            a->auth_ui_flags=a->auth.flags;
        }
        if(!a->auth_timer){ScUiTimerDesc d;
            s2_record(&d,sizeof(d));
            d.node=a->root;
            d.flags=SC_UI_TIMER_DRAW_ONLY;
            d.period_ticks=10;
            ScMonotonicClock clock;
            s2_record(&clock,sizeof(clock));
            if(ScSystemMonotonic(&clock)==0){d.deadline=clock.ticks;
                if((d.deadline.lo+=10u)<10u)d.deadline.hi++;
                ScUiTimerSet(a->ui,&d,&a->auth_timer);
    }}}
    else if(a->auth_timer){ScUiTimerCancel(a->ui,a->auth_timer);
        a->auth_timer=0;
    }
}
static void s2_auth_poll(S2App *a)
{
    if(!a->ticket.id)return;
    ScMonotonicClock clock;
    s2_record(&clock,sizeof(clock));
    ScStatus status=ScSystemMonotonic(&clock);
    if(status<0 || clock.ticks.hi>a->auth_deadline.hi || (clock.ticks.hi==a->auth_deadline.hi && clock.ticks.lo>=a->auth_deadline.lo)){s2_auth_cancel(a);
        s2_status(a,"Sign-in expired; password cleared");
        s2_update_auth_ui(a);
        return;
    }
    if(a->auth_poll){a->auth_poll=0;
        s2_record(&a->auth,sizeof(a->auth));
        status=ScAuthLoginQuery(&a->ticket,&a->auth);
        if(status<0 || a->auth.state==SC32_AUTH_DENIED){s2_auth_cancel(a);
            s2_status(a,"Sign-in failed; password cleared");
            s2_update_auth_ui(a);
            return;
        }
        if(a->auth.state==SC32_AUTH_READY)a->auth_activate=1;
    }
    if(a->auth_activate){a->auth_activate=0;
        status=ScAuthLoginActivate(&a->ticket);
        if(status==0){a->ticket.id=0;
            if(a->auth_timer){ScUiTimerCancel(a->ui,a->auth_timer);
                a->auth_timer=0;
            }ScUiSecretClear(a->ui,a->password);
            s2_status(a,"Signed in to a separate desktop");
            if(a->locked){a->closed=1;
                ScUiContextStop(a->ui,0);
            }else{s2_update_auth_ui(a);
                s2_refresh(a);
            }return;
        }
        s2_status(a,"Desktop could not open; retry or cancel the verified sign-in");
    }
    s2_update_auth_ui(a);
}
