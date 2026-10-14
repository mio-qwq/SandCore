#include "SC32PRIVATE.H"

ScStatus ScProcessSelf(ScProcessRef *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScProcessRef));if(checked<0)return sc32_invalid(checked,SC32_OP_SELF);
    ScProcessRef request={16,1,0,0};ScProcessInfo information;
    sc32_zero(&information,sizeof(information));information.size=sizeof(information);information.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROCESS,(ScI32)&request,(ScI32)&information,0,0,0);
    if(raw<0)return sc32_native(raw,SC32_OP_SELF);
    ScProcessRef value={16,1,information.pid,information.generation};sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScProcessQuery(const ScProcessRef *process,ScProcessInfo *out)
{
    ScProcessRef self={16,1,0,0};const ScProcessRef *request=process?process:&self;
    ScStatus checked=sc32_record_in(request,sizeof(ScProcessRef));if(checked<0)return sc32_invalid(checked,SC32_OP_PROCESS_QUERY);
    return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROCESS,(ScI32)request,(ScI32)out,0,0,0),SC32_OP_PROCESS_QUERY);
}
ScStatus ScProcessEnumerate(ScProcessPage *page,ScProcessInfo *rows,ScU32 capacity)
{
    ScStatus checked=sc32_record_out(page,sizeof(ScProcessPage));if(checked<0)return sc32_invalid(checked,SC32_OP_PROCESS_PAGE);
    if(!capacity || capacity>64)return sc32_invalid(SC32_E_RANGE,SC32_OP_PROCESS_PAGE);
    checked=sc32_out(rows,capacity*sizeof(ScProcessInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_PROCESS_PAGE);
    if(sc32_overlap(page,page->size,rows,capacity*sizeof(ScProcessInfo)))return sc32_invalid(SC32_E_INVALID,SC32_OP_PROCESS_PAGE);
    ScProcessInfo self;sc32_zero(&self,sizeof(self));self.size=sizeof(self);self.version=1;
    ScProcessRef reference={16,1,0,0};ScU32 identity[8],raw[1040];
    ScI32 status=sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROCESS,(ScI32)&reference,(ScI32)&self,0,0,0);
    if(status<0)return sc32_native(status,SC32_OP_PROCESS_PAGE);
    status=sc32_call(0x200,(ScI32)identity,0,0,0,0,0);if(status<0)return sc32_legacy(status,SC32_OP_PROCESS_PAGE);
    status=sc32_call(0x240,(ScI32)raw,(ScI32)(16+capacity*16),(ScI32)page->cursor,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_PROCESS_PAGE);
    ScU32 count=raw[3];if(count>capacity)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,status,SC32_OP_PROCESS_PAGE);
    for(ScU32 i=0;i<count;i++){
        const ScU32 *r=raw+16+i*16;ScProcessInfo value;sc32_zero(&value,sizeof(value));
        value.size=sizeof(value);value.version=1;value.pid=r[0];value.state=r[1];value.generation=r[2];
        value.uid=(ScI32)r[3];value.gid=(ScI32)r[4];value.realm=SC32_REALM_UNKNOWN;
        /* 旧枚举允许同UID跨会话明细；新API按已确认会话边界过滤副本。
         * 枚举保留同一次旧快照的代数/CPU，不逐行重新查询拼接两个时刻。 */
        int allowed=value.pid==self.pid || (self.uid>=0 && value.uid>=0 && value.uid==self.uid && self.session && r[7]==self.session)
            || (self.uid==0 && value.uid>=0) || identity[4]!=0;
        if(allowed && r[11]){
            value.flags=SC32_PROCESS_DETAILS;value.session=r[7];value.running_samples=r[5];value.dispatches=r[6];
            value.exit_code=(ScI32)r[12];value.waiting=r[13];value.wait_mask=r[14];value.pending_events=r[15];
            sc32_copy(value.name,r+8,12);
        }
        sc32_copy(rows+i,&value,sizeof(value));
    }
    ScProcessPage value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.cursor=raw[4];value.count=count;value.tick=raw[7];value.total_samples=raw[8];value.idle_samples=raw[9];
    value.kernel_samples=raw[10];value.user_samples=raw[11];value.graphics_samples=raw[12];value.hz=raw[13];
    value.examined=count;sc32_copy(page,&value,sizeof(value));return 0;
}
ScStatus ScProcessTerminate(const ScProcessRef *process)
{
    ScStatus checked=sc32_record_in(process,sizeof(ScProcessRef));if(checked<0)return sc32_invalid(checked,SC32_OP_TERMINATE);
    if(!process->pid || !process->generation)return sc32_invalid(SC32_E_INVALID,SC32_OP_TERMINATE);
    ScProcessInfo info;sc32_zero(&info,sizeof(info));info.size=sizeof(info);info.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROCESS,(ScI32)process,(ScI32)&info,0,0,0);
    if(raw<0)return sc32_native(raw,SC32_OP_TERMINATE);
    raw=sc32_call(0x22e,(ScI32)process->pid,(ScI32)process->generation,0,0,0,0);
    if(raw==-8)return sc32_fail(SC32_E_STALE,SC32_DOMAIN_LEGACY,raw,SC32_OP_TERMINATE);
    return sc32_legacy(raw,SC32_OP_TERMINATE);
}
ScStatus ScProcessSpawn(const char *command,const ScI32 streams[3],ScU32 flags,ScJob *out)
{
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,SC32_OP_SPAWN);
    checked=sc32_string(command,1087,0);if(checked<0)return sc32_invalid(checked,SC32_OP_SPAWN);
    if(flags&~1u)return sc32_invalid(SC32_E_INVALID,SC32_OP_SPAWN);
    ScI32 descriptors[3];
    if(streams){checked=sc32_probe(streams,12,SC32_WIRE_READ);if(checked<0)return sc32_invalid(checked,SC32_OP_SPAWN);
        for(ScU32 i=0;i<3;i++){descriptors[i]=streams[i];if(descriptors[i]<-1)return sc32_invalid(SC32_E_INVALID,SC32_OP_SPAWN);}}
    else for(ScU32 i=0;i<3;i++){ScU32 ready[8];ScI32 status=sc32_call(0x247,(ScI32)i,(ScI32)ready,0,0,0,0);descriptors[i]=status<0?-1:(ScI32)i;}
    ScI32 raw=sc32_call(0x218,(ScI32)command,(ScI32)descriptors,(ScI32)flags,0,0,0);
    if(raw<1){ScI32 code=raw==-2?SC32_E_NOT_FOUND:raw==-3?SC32_E_INVALID:raw==-4?SC32_E_MEMORY:raw==-5?SC32_E_ACCESS:SC32_E_FAILED;
        return sc32_fail(code,SC32_DOMAIN_LEGACY,raw,SC32_OP_SPAWN);}
    *out=(ScJob)raw;return 0;
}
static void sc32_job_value(ScJobInfo *value,const ScU32 raw[8])
{
    sc32_zero(value,sizeof(*value));value->size=sizeof(*value);value->version=1;
    value->state=raw[1]==2?SC32_JOB_PAUSED:raw[1]?SC32_JOB_RUNNING:SC32_JOB_FINISHED;
    value->exit_code=(ScI32)raw[2];value->child_pid=raw[3];value->child_generation=raw[4];value->original_pid=raw[5];value->owned=1;
}
ScStatus ScJobQuery(ScJob job,ScJobInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScJobInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_JOB_QUERY);
    ScU32 raw[8];ScI32 status=sc32_call(0x23a,(ScI32)job,(ScI32)raw,0,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_JOB_QUERY);
    ScJobInfo value;sc32_job_value(&value,raw);sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScJobWait(ScJob job,ScU32 timeout,ScJobInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScJobInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_JOB_WAIT);
    if(timeout>0x7FFFFFFFu && timeout!=SC32_INFINITE)return sc32_invalid(SC32_E_RANGE,SC32_OP_JOB_WAIT);
    ScU32 start=(ScU32)sc32_call(2,0,0,0,0,0,0);
    for(;;){
        ScU32 raw[8];ScI32 status=sc32_call(0x23a,(ScI32)job,(ScI32)raw,0,0,0,0);
        if(status<0)return sc32_legacy(status,SC32_OP_JOB_WAIT);
        ScJobInfo value;sc32_job_value(&value,raw);
        if(!status){
            checked=sc32_record_out(out,sizeof(ScJobInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_JOB_WAIT);
            /* WAIT2只有在完成时才领票；先验证最终out，坏缓冲不能丢退出结果。 */
            ScU32 final[8];status=sc32_call(0x219,(ScI32)job,(ScI32)final,0,0,0,0);
            if(status<0)return sc32_legacy(status,SC32_OP_JOB_WAIT);
            if(status)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,status,SC32_OP_JOB_WAIT);
            value.exit_code=(ScI32)final[2];sc32_copy(out,&value,sizeof(value));return 0;
        }
        if(!timeout){sc32_copy(out,&value,sizeof(value));return SC32_PENDING;}
        ScU32 remaining=SC32_INFINITE;
        if(timeout!=SC32_INFINITE){ScU32 elapsed=(ScU32)sc32_call(2,0,0,0,0,0,0)-start;
            if(elapsed>=timeout){sc32_copy(out,&value,sizeof(value));return SC32_TIMEOUT;}remaining=timeout-elapsed;}
        ScI32 events=sc32_call(0x241,8,(ScI32)remaining,0,0,0,0);
        if(events<0)return sc32_fail(events==-4?SC32_E_MEMORY:SC32_E_FAILED,SC32_DOMAIN_LEGACY,events,SC32_OP_JOB_WAIT);
    }
}
ScStatus ScJobClose(ScJob job)
{
    ScU32 raw[8];ScI32 status=sc32_call(0x23a,(ScI32)job,(ScI32)raw,0,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_JOB_CLOSE);
    if(status)return sc32_invalid(SC32_E_BUSY,SC32_OP_JOB_CLOSE);
    return sc32_legacy(sc32_call(0x219,(ScI32)job,(ScI32)raw,0,0,0,0),SC32_OP_JOB_CLOSE);
}
ScStatus ScJobCancel(ScJob job)
{
    ScU32 raw[8];ScI32 status=sc32_call(0x23a,(ScI32)job,(ScI32)raw,0,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_JOB_CANCEL);
    if(!status)return 0;
    ScProcessRef process={16,1,raw[3],raw[4]};ScStatus result=ScProcessTerminate(&process);
    if(result<0)sc32_last_error.operation=SC32_OP_JOB_CANCEL;
    return result;
}
void ScProcessExit(ScI32 code)
{sc32_call(0,(ScI32)code,0,0,0,0,0);for(;;)sc32_call(7,0,0,0,0,0,0);}
ScStatus ScProcessYield(void)
{return sc32_legacy(sc32_call(7,0,0,0,0,0,0),SC32_OP_YIELD);}
ScStatus ScProcessSleep(ScU32 ticks)
{
    if(ticks>0x7FFFFFFFu)return sc32_invalid(SC32_E_RANGE,SC32_OP_SLEEP);
    ScI32 raw=sc32_call(0x241,0,(ScI32)ticks,0,0,0,0);
    if(raw<0)return sc32_fail(raw==-4?SC32_E_MEMORY:SC32_E_FAILED,SC32_DOMAIN_LEGACY,raw,SC32_OP_SLEEP);
    return 0;
}
ScStatus ScProcessArguments(char *out,ScU32 capacity,ScU32 *length)
{
    ScStatus checked=sc32_text_buffers(out,capacity,length);if(checked<0)return sc32_invalid(checked,SC32_OP_ARGUMENTS);
    char value[1024];ScI32 raw=sc32_call(0x21c,(ScI32)value,1024,0,0,0,0);
    if(raw<0)return sc32_legacy(raw,SC32_OP_ARGUMENTS);
    return sc32_text_output(value,(ScU32)raw,out,capacity,length,SC32_OP_ARGUMENTS);
}
static ScStatus sc32_user_text(ScU32 kind,const char *name,char *out,ScU32 capacity,ScU32 *length,ScU32 operation)
{
    ScStatus checked=sc32_text_buffers(out,capacity,length);if(checked<0)return sc32_invalid(checked,operation);
    if(name){checked=sc32_string(name,31,0);if(checked<0)return sc32_invalid(checked,operation);}
    char value[256];ScI32 raw=sc32_call(0x70,(ScI32)kind,(ScI32)name,(ScI32)value,256,0,0);
    if(raw<0)return raw==-2?sc32_fail(SC32_E_NOT_FOUND,SC32_DOMAIN_LEGACY,raw,operation):sc32_legacy(raw,operation);
    return sc32_text_output(value,(ScU32)raw,out,capacity,length,operation);
}
ScStatus ScProcessEnvironment(const char *name,char *out,ScU32 capacity,ScU32 *length)
{if(!name)return sc32_invalid(SC32_E_INVALID,SC32_OP_ENV);return sc32_user_text(4,name,out,capacity,length,SC32_OP_ENV);}
ScStatus ScProcessSetEnvironment(const char *name,const char *value,ScU32 remove)
{
    ScStatus checked=sc32_string(name,31,0);if(checked<0)return sc32_invalid(checked,SC32_OP_ENV_SET);
    if(remove>1)return sc32_invalid(SC32_E_INVALID,SC32_OP_ENV_SET);
    if(!remove){checked=sc32_string(value,255,0);if(checked<0)return sc32_invalid(checked,SC32_OP_ENV_SET);}
    return sc32_legacy(sc32_call(0x224,(ScI32)name,(ScI32)value,(ScI32)remove,0,0,0),SC32_OP_ENV_SET);
}
ScStatus ScProcessWorkingDirectory(char *out,ScU32 capacity,ScU32 *length)
{return sc32_user_text(2,0,out,capacity,length,SC32_OP_CWD);}
ScStatus ScProcessSetWorkingDirectory(const char *path)
{
    char absolute[65];ScStatus checked=sc32_absolute(path,absolute,SC32_OP_CWD_SET);if(checked<0)return checked;
    ScI32 raw=sc32_call(0x72,(ScI32)absolute,0,0,0,0,0);
    return raw==-2?sc32_fail(SC32_E_NOT_FOUND,SC32_DOMAIN_LEGACY,raw,SC32_OP_CWD_SET):sc32_legacy(raw,SC32_OP_CWD_SET);
}
