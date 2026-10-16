#include <SCSHELL.H>
#include <stdio.h>
#include <string.h>

/* 此用例只使用公开动态库接口；二进制跨越两次64KiB worker块，
 * 由客体逐字节独立验算，不把文件长度或成功提示当作内容证据。 */
#define SOURCE "/HOME/M10SHELL/OP-SOURCE.BIN"
#define COPIED "/HOME/M10SHELL/OP-COPY.BIN"
#define MOVED "/HOME/M10SHELL/OP-MOVE.BIN"
#define DATA_BYTES 131079u
static ScShellFileJob *job;
static ScFile opened;
static ScU32 checkpoint;
static unsigned char buffer[4096];

static void record(void *value,ScU32 bytes)
{
    memset(value,0,bytes);((ScU32 *)value)[0]=bytes;((ScU32 *)value)[1]=1;
}
static ScStatus check_path(const char *name)
{
    ScPathInfo info;record(&info,sizeof(info));
    ScStatus status=ScPathQuery(name,&info);if(status<0)return status;
    if(info.kind!=SC32_PATH_FILE || info.bytes!=DATA_BYTES || info.uid!=1000
       || info.rw!=23 || !info.generation)return SC32_E_FAILED;
    return 0;
}
static ScStatus missing(const char *name)
{
    ScPathInfo info,before;memset(&info,0x5a,sizeof(info));
    info.size=sizeof(info);info.version=1;before=info;
    ScStatus status=ScPathQuery(name,&info);
    if(status!=SC32_E_NOT_FOUND || memcmp(&info,&before,sizeof(info)))return SC32_E_FAILED;
    return 0;
}
static ScStatus contents(const char *name)
{
    ScFileOpenDesc desc;record(&desc,sizeof(desc));desc.path=name;desc.flags=SC32_FILE_READ;
    ScStatus status=ScFileOpen(&desc,&opened);if(status<0)return status;
    ScU32 offset=0;
    while(offset<DATA_BYTES){
        ScIoResult io;record(&io,sizeof(io));
        ScU32 count=DATA_BYTES-offset;if(count>sizeof(buffer))count=sizeof(buffer);
        status=ScFileRead(opened,buffer,count,&io);if(status<0)break;
        if(io.transferred!=count){status=SC32_E_FAILED;break;}
        for(ScU32 i=0;i<count;i++)if(buffer[i]!=(unsigned char)((offset+i)*37u+19u)){
            status=SC32_E_FAILED;break;
        }
        if(status<0)break;offset+=count;
    }
    if(status>=0){ScIoResult end;record(&end,sizeof(end));
        status=ScFileRead(opened,buffer,1,&end);
        if(status>=0 && end.transferred)status=SC32_E_FAILED;
    }
    ScStatus closed=ScFileClose(opened);opened=0;
    if(status<0)return status;if(closed<0)return closed;
    printf("FILEJOB CONTENT bytes=%u path=%s\n",offset,name);return 0;
}
static ScStatus operation(ScU32 kind,const char *source,const char *destination,const char *recovery)
{
    ScShellFileItem item;record(&item,sizeof(item));item.item=1;item.kind=SC32_PATH_FILE;
    item.source=source;item.destination=destination;
    ScShellFileJobDesc desc;record(&desc,sizeof(desc));desc.operation=kind;
    desc.flags=SC_SHELL_PRESERVE_MODE;desc.items=&item;desc.count=1;
    desc.recovery_path=recovery;desc.final_mode=23;
    ScMonotonicClock begin;record(&begin,sizeof(begin));
    ScStatus status=ScSystemMonotonic(&begin);if(status<0)return status;
    status=ScShellFileOperationBegin(&desc,&job);if(status<0)return status;
    ScU32 previous=0;
    for(;;){
        ScShellFileProgress progress;record(&progress,sizeof(progress));
        status=ScShellFileOperationQuery(job,&progress);if(status<0)return status;
        if(progress.state!=previous){
            printf("FILEJOB STATE operation=%u state=%u stage=%u item=%u status=%d cause=%d domain=%u raw=%d\n",
                kind,progress.state,progress.stage,progress.current_item,progress.status,
                progress.cause,progress.domain,progress.raw);fflush(0);previous=progress.state;
        }
        if(progress.state==SC_SHELL_JOB_FINISHED){
            if(progress.status || progress.items_done!=1 || progress.items_failed || progress.items_skipped)return SC32_E_FAILED;
            break;
        }
        if(progress.state==SC_SHELL_JOB_CONFLICT || progress.state>SC_SHELL_JOB_FINISHED)return SC32_E_FAILED;
        ScMonotonicClock now;record(&now,sizeof(now));status=ScSystemMonotonic(&now);if(status<0)return status;
        if(now.ticks.hi!=begin.ticks.hi || now.ticks.lo-begin.ticks.lo>60u*begin.hz)return SC32_E_FAILED;
        status=ScShellFileOperationStep(job,2);if(status<0)return status;
    }
    ScShellFileResult result;record(&result,sizeof(result));
    status=ScShellFileOperationResult(job,0,&result);if(status)return status<0?status:SC32_E_FAILED;
    printf("FILEJOB ACK operation=%u state=%u stage=%u flags=%u bytes=%u/%u worker=%u/%u status=%d\n",
        kind,result.state,result.stage,result.flags,result.bytes.hi,result.bytes.lo,
        result.worker_pid,result.worker_generation,result.status);fflush(0);
    if(result.state!=SC_SHELL_RESULT_DONE || result.status || !(result.flags&SC_SHELL_DURABLE)
       || (result.flags&SC_SHELL_SOURCE_RETAINED) || result.stage<SC_SHELL_STAGE_DESTINATION
       || !result.worker_pid || !result.worker_generation)return SC32_E_FAILED;
    ScShellFileOperationClose(job);job=0;
    return 0;
}
static ScStatus probe(void)
{
    ScIdentityInfo identity;record(&identity,sizeof(identity));
    ScStatus status=ScIdentityQuery(&identity);if(status<0)return status;
    if(identity.uid!=1000 || identity.realm!=SC32_BROKER_REALM_NORMAL || identity.flags)return SC32_E_ACCESS;
    status=ScProcessSetEnvironment("SCSHELL_TRACE","/HOME/M10SHELL/WORK.TRACE",0);if(status<0)return status;
    printf("FILEJOB ID uid=%d sid=%u generation=%u\n",identity.uid,identity.session,identity.task_generation);
    checkpoint=1;status=check_path(SOURCE);if(status<0)return status;
    checkpoint=2;status=missing(COPIED);if(status<0)return status;
    status=missing(MOVED);if(status<0)return status;
    ScPathInfo denied,before;memset(&denied,0x6b,sizeof(denied));
    denied.size=sizeof(denied);denied.version=1;before=denied;
    checkpoint=3;status=ScPathQuery("/SYS/AUTH/NOFILE",&denied);
    if(status!=SC32_E_ACCESS || memcmp(&denied,&before,sizeof(denied)))return SC32_E_FAILED;
    printf("FILEJOB PATH_QUERY metadata_missing_access_and_untouched_output_PASS\n");
    checkpoint=4;status=operation(SC_SHELL_COPY,SOURCE,COPIED,"/HOME/M10SHELL/CP.LOG");if(status<0)return status;
    checkpoint=5;status=check_path(COPIED);if(status<0)return status;
    status=contents(COPIED);if(status<0)return status;
    checkpoint=6;status=operation(SC_SHELL_MOVE,COPIED,MOVED,"/HOME/M10SHELL/MV.LOG");if(status<0)return status;
    checkpoint=7;status=check_path(MOVED);if(status<0)return status;
    status=missing(COPIED);if(status<0)return status;
    status=contents(MOVED);if(status<0)return status;
    checkpoint=8;status=contents(SOURCE);if(status<0)return status;
    return 0;
}
int main(void)
{
    ScStatus status=probe();
    if(opened)ScFileClose(opened);
    ScShellFileOperationClose(job);
    if(status<0){
        ScShellErrorInfo error;record(&error,sizeof(error));ScShellErrorQuery(&error);
        printf("FILEJOB FAIL checkpoint=%u status=%d shell=%d cause=%d domain=%u raw=%d\n",
            checkpoint,status,error.code,error.cause,error.domain,error.raw);return 1;
    }
    printf("FILEJOB PASS typed_query_copy_move_and_actual_binary_content\n");return 0;
}
