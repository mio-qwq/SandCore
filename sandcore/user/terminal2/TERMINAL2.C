#include "TERMINAL2.H"
#include "terminal_jobs.c"
#include "terminal_ui.c"
int main(void)
{
    strcpy(terminal_app.command,"/BIN/SH.SCX -i");
    char arguments[1088];
    ScU32 bytes=0;
    ScStatus status=ScProcessArguments(arguments,sizeof(arguments),&bytes);
    if(!status && bytes){
        if(strncmp(arguments,"--command ",10) || !arguments[10]){
            fprintf(stderr,"Terminal2 [--command /absolute/PROGRAM.SCX arguments]\n");
            return 2;
        }
        if(strlen(arguments+10)>=sizeof(terminal_app.command))return 2;
        strcpy(terminal_app.command,arguments+10);
    }
    status=a_ui();
    if(status<0){
        fprintf(stderr,"Terminal2 UI: %d\n",status);
        if(terminal_app.ui)ScUiContextRelease(terminal_app.ui);
        return 1;
    }
    status=a_session_new();
    if(status<0)a_error(status,"无法启动交互 Shell");
    while(!terminal_app.stopped){
        ScUiContextStep(terminal_app.ui,0);
        int more=a_jobs_step();
        if(terminal_app.quitting && !terminal_app.sessions){
            ScUiNodeDestroy(terminal_app.ui,terminal_app.root);
            terminal_app.stopped=1;
            break;
        }
        /* GUI与native流使用同一个真实任务事件等待。GUI-only wait会漏掉
         * STREAM/JOB而睡死；定时预算由SCUI提供，空闲不按100Hz轮询。 */
        ScU32 timeout=more?1u:SC32_INFINITE;
        ScUiContextWaitTimeout(terminal_app.ui,timeout,&timeout);
        ScU32 events=0;
        ScSystemWait(SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM|SC32_EVENT_JOB,timeout,&events);
    }
    for(ScU32 i=0;i<5;i++)ScUiMenuRelease(terminal_app.menus[i]);
    ScUiContextRelease(terminal_app.ui);
    return 0;
}
