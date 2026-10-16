#include "INSTALLER2.H"
#include "app_ui.inc"
#include "app_io.inc"
#include "app_provider.inc"
#include "install_model.inc"
#include "install_job.inc"
#include "install_view.inc"
int main(void)
{
    ScStatus s=installer_create();
    while(s==0 && !app.quit){
        ScStatus ui=app_step();
        if(ui<0){s=ui;
            break;
        }
        ScStatus work=installer_step();
        if(work<0)error(work,"文件安装；来源保留");
        if(app.relayout || installer.phase || installer.reporting){ScProcessYield();
            continue;
        }
        ScU32 timeout=SC32_INFINITE;
        ScUiContextWaitTimeout(app.ui,timeout,&timeout);
        ScBrokerWaitDesc wait;
        INIT(wait);
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM;
        wait.timeout=timeout;
        ScU32 events;
        ScStatus asleep=ScBrokerWait(&wait,&events);
        if(asleep<0){s=asleep;
            break;
        }
    }
    installer_close();
    return s<0?-s:0;
}
