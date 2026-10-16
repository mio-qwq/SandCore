#define main file_job_previous_main
#include "FILEJOBPROBE.C"
#undef main

/* 发布者是另一个真实受管进程；发布后先正常退出，再由父进程粘贴。
 * 这样跨进程及publisher退出生命周期都不能靠一份本地缓存冒充。 */
static ScJob publisher_job;
static ScStatus publish(ScU32 cut)
{
    ScShellFileItem item;record(&item,sizeof(item));item.item=1;item.kind=SC32_PATH_FILE;
    item.source=cut?MOVED:SOURCE;
    ScClipboardInfo clip;record(&clip,sizeof(clip));
    ScStatus status=ScClipboardQuery(&clip);if(status<0)return status;
    ScU32 epoch=clip.epoch;record(&clip,sizeof(clip));
    return cut?ScShellClipboardCutFiles(&item,1,epoch,&clip):ScShellClipboardCopyFiles(&item,1,epoch,&clip);
}
static ScStatus pasted(ScU32 kind)
{
    ScMonotonicClock begin;record(&begin,sizeof(begin));
    ScStatus status=ScSystemMonotonic(&begin);if(status<0)return status;
    for(;;){
        ScShellFileProgress progress;record(&progress,sizeof(progress));
        status=ScShellFileOperationQuery(job,&progress);if(status<0)return status;
        if(progress.state==SC_SHELL_JOB_FINISHED){
            if(progress.status || progress.items_done!=1 || progress.items_failed || progress.items_skipped)return SC32_E_FAILED;
            break;
        }
        if(progress.state==SC_SHELL_JOB_CONFLICT || progress.state>SC_SHELL_JOB_FINISHED){
            printf("FILECLIP unexpected state=%u status=%d cause=%d raw=%d\n",
                progress.state,progress.status,progress.cause,progress.raw);return SC32_E_FAILED;
        }
        ScMonotonicClock now;record(&now,sizeof(now));status=ScSystemMonotonic(&now);if(status<0)return status;
        if(now.ticks.hi!=begin.ticks.hi || now.ticks.lo-begin.ticks.lo>60u*begin.hz)return SC32_E_FAILED;
        status=ScShellFileOperationStep(job,2);if(status<0)return status;
    }
    ScShellFileResult result;record(&result,sizeof(result));
    status=ScShellFileOperationResult(job,0,&result);if(status)return status<0?status:SC32_E_FAILED;
    if(result.operation!=kind || result.state!=SC_SHELL_RESULT_DONE || result.status
       || !(result.flags&SC_SHELL_DURABLE) || (result.flags&SC_SHELL_SOURCE_RETAINED))return SC32_E_FAILED;
    printf("FILECLIP ACK operation=%u stage=%u flags=%u worker=%u/%u\n",
        kind,result.stage,result.flags,result.worker_pid,result.worker_generation);
    return 0;
}
static ScStatus transfer(ScU32 cut,ScU32 session)
{
    ScStatus status=ScProcessSpawn(cut?
        "/HOME/M10SHELL/GENERATED/FILECLIPPROBE.SCX cut":
        "/HOME/M10SHELL/GENERATED/FILECLIPPROBE.SCX copy",0,SC32_SPAWN_NO_STREAM_INHERIT,&publisher_job);
    if(status<0)return status;
    ScJobInfo child;record(&child,sizeof(child));
    status=ScJobWait(publisher_job,SC32_INFINITE,&child);if(status<0)return status;
    /* Wait完成即领票，存活child_pid清零，历史身份由original_pid保留。
     * 不能再Close已消费票据，也不能把退出后的0当作发布者PID。 */
    publisher_job=0;
    if(child.state!=SC32_JOB_FINISHED || child.exit_code || !child.original_pid || !child.child_generation){
        printf("FILECLIP child state=%u exit=%d original=%u generation=%u\n",
            child.state,child.exit_code,child.original_pid,child.child_generation);return SC32_E_FAILED;
    }
    ScClipboardInfo clip;record(&clip,sizeof(clip));status=ScClipboardQuery(&clip);if(status<0)return status;
    if(clip.formats!=1 || !clip.bytes || clip.publisher.pid!=child.original_pid
       || clip.publisher.generation!=child.child_generation || clip.publisher.uid!=1000
       || clip.publisher.session!=session)return SC32_E_FAILED;
    printf("FILECLIP PUBLISHER_EXITED operation=%u pid=%u/%u epoch=%u\n",
        cut?2:1,clip.publisher.pid,clip.publisher.generation,clip.epoch);fflush(0);
    status=ScShellClipboardPaste("/HOME/M10SHELL/CLIPDEST",cut?"/HOME/M10SHELL/MV.LOG":"/HOME/M10SHELL/CP.LOG",0,0,&job);
    if(status<0)return status;
    status=pasted(cut?SC_SHELL_MOVE:SC_SHELL_COPY);if(status<0)return status;
    status=contents(cut?"/HOME/M10SHELL/CLIPDEST/OP-MOVE.BIN":"/HOME/M10SHELL/CLIPDEST/OP-SOURCE.BIN");if(status<0)return status;
    if(cut){
        status=missing(MOVED);if(status<0)return status;
        record(&clip,sizeof(clip));status=ScShellClipboardReconcile(job,&clip);if(status<0)return status;
        if(clip.formats || clip.bytes || !(clip.flags&SC32_CLIPBOARD_EMPTY))return SC32_E_FAILED;
        printf("FILECLIP CUT_RECONCILED_EMPTY epoch=%u\n",clip.epoch);
    }
    ScShellFileOperationClose(job);job=0;return 0;
}
int main(void)
{
    char arguments[32];ScU32 bytes=0;
    ScStatus status=ScProcessArguments(arguments,sizeof(arguments),&bytes);
    if(status<0)return 2;
    if(!strcmp(arguments,"copy"))return publish(0)<0?3:0;
    if(!strcmp(arguments,"cut"))return publish(1)<0?4:0;
    ScIdentityInfo identity;record(&identity,sizeof(identity));status=ScIdentityQuery(&identity);
    if(status<0 || identity.uid!=1000 || identity.flags || identity.realm)return 5;
    printf("FILEJOB ID uid=1000 sid=%u generation=%u\n",identity.session,identity.task_generation);
    checkpoint=10;status=transfer(0,identity.session);
    if(status>=0){checkpoint=11;status=transfer(1,identity.session);}
    if(status>=0){checkpoint=12;status=contents(SOURCE);}
    if(opened)ScFileClose(opened);
    ScShellFileOperationClose(job);
    if(publisher_job){ScJobCancel(publisher_job);ScJobClose(publisher_job);}
    if(status<0){printf("FILEJOB FAIL clipboard_checkpoint=%u status=%d\n",checkpoint,status);return 1;}
    printf("FILECLIP PASS cross_process_copy_cut_paste_and_publisher_exit\n");return 0;
}
