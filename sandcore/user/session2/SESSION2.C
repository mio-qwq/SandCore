#include "model.c"
#include "ui.c"
#include "actions.c"
static ScU32 s2_auth_timeout(S2App *a,ScU32 timeout)
{
    if(!a->ticket.id)return timeout;
    ScMonotonicClock now;
    s2_record(&now,sizeof(now));
    if(ScSystemMonotonic(&now)<0)return 0;
    if(now.ticks.hi>a->auth_deadline.hi || (now.ticks.hi==a->auth_deadline.hi && now.ticks.lo>=a->auth_deadline.lo))return 0;
    ScU32 ticks=a->auth_deadline.lo-now.ticks.lo;
    return timeout==SC32_INFINITE || ticks<timeout?ticks:timeout;
}
int main(void)
{
    S2App *a=calloc(1,sizeof(*a));
    if(!a)return 1;
    ScStatus status=s2_build(a);
    while(status>=0 && !a->closed){status=ScUiContextStep(a->ui,0);
        if(status==SC_UI_END)break;
        s2_dialog_poll(a);
        while(a->commands){S2Command *command=a->commands;
            a->commands=command->next;
            if(!a->commands)a->last_command=0;
            ScU32 id=command->id;
            free(command);
            ScStatus result=s2_execute(a,id);
            if(result<0)s2_error(a,"Command",result);
            if(a->closed)break;
        }
        s2_refresh_step(a);
        s2_auth_poll(a);
        if(a->closed)break;
        if(a->refreshing || a->commands || a->dialog_done){ScProcessYield();
            continue;
        }
        ScU32 timeout=SC32_INFINITE;
        ScUiContextWaitTimeout(a->ui,SC32_INFINITE,&timeout);
        timeout=s2_auth_timeout(a,timeout);
        ScBrokerWaitDesc wait;
        s2_record(&wait,sizeof(wait));
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_STREAM|SC32_EVENT_VISIBILITY|(a->ticket.id?SC32_EVENT_AUTH:0);
        wait.timeout=timeout;
        ScU32 events=0;
        ScStatus waiting=ScBrokerWait(&wait,&events);
        if(waiting<0){status=waiting;
            break;
        }if(events&SC32_EVENT_AUTH)a->auth_poll=1;
    }
    /* 明文只在SCUI专页；正常退出、创建失败均清控件，再撤认证票据。
     * kernel强停沿已有页隔离/再次分配清零，不宣称会跑这些析构。 */
    s2_auth_cancel(a);
    ScUiDialogRelease(a->dialog);
    if(a->ui && a->password)ScUiSecretClear(a->ui,a->password);
    ScUiContextRelease(a->ui);
    ScUiSelectionRelease(a->selection);
    ScUiProviderRelease(a->provider);
    ScArrayRelease(a->candidate);
    ScArrayRelease(a->rows);
    ScUiMenuRelease(a->menu);
    ScUiMenuRelease(a->account_menu);
    ScUiMenuRelease(a->session_menu);
    ScUiMenuRelease(a->help_menu);
    while(a->commands){S2Command *c=a->commands;
        a->commands=c->next;
        free(c);
    }free(a);
    return status<0?1:0;
}
