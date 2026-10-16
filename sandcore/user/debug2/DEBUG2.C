#include "D2APP.H"
#include "decode.c"
#include "model.c"
#include "sidecar.c"
#include "ui.c"

int main(void)
{
    D2App *a=calloc(1,sizeof(*a));
    if(!a)return 1;
    ScStatus s=D2BuildUi(a);
    if(s==0){char arguments[1088];
        ScU32 length=0;
        if(ScProcessArguments(arguments,sizeof(arguments),&length)==0 && length){const char *target=arguments;
            if(!strncmp(target,"--launch ",9))target+=9;
            ScUiText text=D2Text(target);
            ScUiNodeSetText(a->ui,a->commandbox,&text);
            s=D2Launch(a,target);
            if(s<0)D2Error(a,"Launch",s);
        }
        ScI32 result=0;
        s=ScUiContextRun(a->ui,&result);
    }
    /* 任何构造失败同样走真实owner回收。正常关闭先领作业再停止
     * context；异常退出的独占调试子进程由内核owner清理链终止。 */
    if(a->active){ScDebugSnapshot snap;
        D2Record(&snap,sizeof(snap));
        ScDebugControl(&a->launch.target,0,SC32_DEBUG_TERMINATE,&snap);
        ScJobInfo result;
        D2Record(&result,sizeof(result));
        ScJobWait(a->launch.job,SC32_INFINITE,&result);
    }
    D2FrameDestroy(a->candidate);
    D2FrameDestroy(a->frame);
    ScUiDialogRelease(a->dialog);
    a->dialog=0;
    if(a->ui){ScUiContextRelease(a->ui);
        for(ScU32 i=0;i<D2_PANES;i++){ScUiSelectionRelease(a->panes[i].selection);
            ScUiProviderRelease(a->panes[i].provider);
    }}
    ScUiMenuRelease(a->menu);
    ScUiMenuRelease(a->file_menu);
    ScUiMenuRelease(a->debug_menu);
    ScUiMenuRelease(a->view_menu);
    ScUiMenuRelease(a->tools);
    ScConfigRelease(a->saved_layout);
    free(a);
    return s<0?1:0;
}
