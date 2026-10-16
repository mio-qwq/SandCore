#include "MONITOR2.H"
#include "tools_ui.inc"
#include "monitor_model.inc"
#include "monitor_view.inc"
int main(void)
{
    ScStatus s=mon_create();
    if(s<0)goto done;
    while(!tool.quit){s=ScUiContextStep(tool.ui,0);
        if(s<0 || tool.quit)break;
        if(mon.reorder_pending && !mon.order.active && !mon.scan){mon.reorder_pending=0;
            s=mon_index_begin(&mon.current,0,mon.field,mon.descending);
            if(s<0)tool_error(s,"排序/过滤");
        }
        if(mon.sample_pending && !mon.order.active && !mon.scan && (!mon.frozen || mon.refresh_once))mon_sample_begin();
        if(mon.scan==1){s=mon_scan_step();
            if(s<0)tool_error(s,"进程快照失败，保留旧列表");
        }
        if(mon.order.active){s=mon_index_step();
            if(s<0)tool_error(s,"进程索引");
        }
        if(mon.scan || mon.order.active){ScProcessYield();
            continue;
        }
        ScBrokerWaitDesc wait;
        memset(&wait,0,sizeof(wait));
        wait.size=sizeof(wait);
        wait.version=1;
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_JOB;
        wait.timeout=SC32_INFINITE;
        s=ScUiContextWaitTimeout(tool.ui,SC32_INFINITE,&wait.timeout);
        if(s<0)break;
        ScU32 events=0;
        s=ScBrokerWait(&wait,&events);
        if(s<0)break;
    }
    done:
    if(s<0)fprintf(stderr,"Monitor2 error=%d\n",s);
    if(mon.timer && tool.ui)ScUiTimerCancel(tool.ui,mon.timer);
    ScUiDialogRelease(mon.confirm);
    tool_close();
    ScUiSelectionRelease(mon.selection);
    ScUiProviderRelease(mon.provider);
    mon_snapshot_free(&mon.current);
    mon_snapshot_free(&mon.staging);
    free(mon.work_indices);
    return s<0?-s:0;
}
