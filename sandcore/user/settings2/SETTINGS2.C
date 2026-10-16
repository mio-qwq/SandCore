#include "SETTINGS2.H"
#include "app_ui.inc"
#include "app_io.inc"
#include "settings_model.inc"
#include "settings_programs.inc"
#include "settings_save.inc"
#include "settings_document.inc"
#include "settings_view.inc"

static ScStatus settings_step(void)
{
    ScStatus s=settings_document_step();
    if(s<0)error(s,"读入配置；原草稿保留");
    ScStatus p=program_step();
    if(p<0)error(p,"桌面程序目录");
    ScStatus w=settings_save_step();
    if(w<0)error(w,"配置保存/激活");
    if(!settings.save.phase && !settings.image)pin_release(&settings.image_pin,0);
    return app.relayout || settings.loading || settings.program_loading || settings.save.phase || settings.image_more?SC32_PENDING:0;
}
static void settings_close(void)
{
    if(settings.timer)ScUiTimerCancel(app.ui,settings.timer);
    if(settings.owns_preview)ScDisplayApply(0,0,0,SC32_DISPLAY_CANCEL);
    settings_image_cancel();
    if(settings.dialog){ScUiDialogClose(settings.dialog,SC_UI_DIALOG_CANCEL);
        ScUiDialogRelease(settings.dialog);
        settings.dialog=0;
    }
    read_dispose(&settings.reader);
    io_dispose(&settings.save.write);
    pin_release(&settings.save.temporary,0);
    ScStringRelease(settings.save.body);
    ScShellRequestClose(settings.request);
    ScShellCatalogRelease(settings.catalog);
    ScShellClose(settings.shell);
    app_close();
    for(ScU32 i=0;i<settings.row_count;i++)program_drop(settings.rows+i);
    free(settings.rows);
    ScUiProviderRelease(settings.env_provider);
    ScUiProviderRelease(settings.program_provider);
    ScUiSelectionRelease(settings.env_selection);
    ScUiSelectionRelease(settings.program_selection);
    ScUiDocumentRelease(settings.document);
    ScUiMenuRelease(settings.context_menu);
}
int main(void)
{
    ScStatus s=settings_create();
    ScU32 events=SC32_EVENT_IMAGE;
    while(s==0 && !app.quit && !settings.closing){
        ScStatus ui=app_step();
        if(ui<0){s=ui;
            break;
        }
        if(settings.image && (settings.image_more || (events&settings.image_wait))){ScStatus image=settings_image_step();
            if(image<0)error(image,"图片验证；原背景保留");
        }
        ScStatus work=settings_step();
        events=0;
        if(work==SC32_PENDING){ScProcessYield();
            continue;
        }
        ScU32 timeout=SC32_INFINITE;
        ScUiContextWaitTimeout(app.ui,timeout,&timeout);
        ScBrokerWaitDesc wait;
        INIT(wait);
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM|settings.image_wait;
        wait.timeout=timeout;
        ScStatus asleep=ScBrokerWait(&wait,&events);
        if(asleep<0){s=asleep;
            break;
        }
    }
    settings_close();
    return s<0?-s:0;
}
