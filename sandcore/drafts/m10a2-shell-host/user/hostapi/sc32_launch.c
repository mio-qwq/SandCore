#include "../sc32/SC32PRIVATE.H"
#include "SC32HOSTWIRE.H"

/* Error.operation与selector分域，避免碰已有GUI的800诊断编号。 */
static ScStatus sc32_host_invalid(ScI32 code,ScU32 selector)
{return sc32_invalid(code,SC32_H_OPERATION+selector-SC32_H_LAUNCH);}
static ScStatus sc32_host_native(ScI32 raw,ScU32 selector)
{return sc32_native(raw,SC32_H_OPERATION+selector-SC32_H_LAUNCH);}

ScStatus ScProcessLaunch(const ScLaunchDesc *desc,ScLaunchInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScLaunchInfo));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_LAUNCH);
    checked=sc32_record_in(desc,sizeof(ScLaunchDesc));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_LAUNCH);
    ScLaunchDesc value=*desc;
    if(value.flags || value.reserved[0] || value.reserved[1] || value.reserved[2])
        return sc32_host_invalid(SC32_E_INVALID,SC32_H_LAUNCH);
    ScU32 path_bytes,argument_bytes;
    checked=sc32_string(value.path,127,&path_bytes);
    if(checked<0)return sc32_host_invalid(checked,SC32_H_LAUNCH);
    const char *arguments=value.arguments?value.arguments:"";
    checked=sc32_string(arguments,1023,&argument_bytes);
    if(checked<0)return sc32_host_invalid(checked,SC32_H_LAUNCH);
    Sc32HostLaunch request={32,1,(ScU32)value.path,path_bytes+1,
        (ScU32)arguments,argument_bytes+1,0,0};
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,SC32_H_LAUNCH,
        (ScI32)&request,(ScI32)out,0,0,0),SC32_H_LAUNCH);
}

static ScStatus sc32_host_query(ScU32 operation,ScShellHostInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScShellHostInfo));
    if(checked<0)return sc32_host_invalid(checked,operation);
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,(ScI32)operation,0,
        (ScI32)out,0,0,0),operation);
}
ScStatus ScShellHostQuery(ScShellHostInfo *out)
{return sc32_host_query(SC32_H_QUERY,out);}
ScStatus ScShellHostRestart(ScShellHostInfo *out)
{return sc32_host_query(SC32_H_RESTART,out);}
ScStatus ScShellHostPrepare(const ScShellHostDesc *desc,ScShellHostInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScShellHostInfo));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_PREPARE);
    checked=sc32_record_in(desc,sizeof(ScShellHostDesc));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_PREPARE);
    ScShellHostDesc value=*desc;
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,SC32_H_PREPARE,
        (ScI32)&value,(ScI32)out,0,0,0),SC32_H_PREPARE);
}
static ScStatus sc32_host_control(ScU32 operation,ScU32 epoch,ScU32 value,
    void *out,ScU32 bytes)
{
    ScStatus checked=sc32_record_out(out,bytes);
    if(checked<0)return sc32_host_invalid(checked,operation);
    Sc32HostControl input={32,1,epoch,value,0,{0,0,0}};
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,(ScI32)operation,
        (ScI32)&input,(ScI32)out,0,0,0),operation);
}
ScStatus ScShellHostActivate(ScU32 epoch,ScShellHostInfo *out)
{return sc32_host_control(SC32_H_ACTIVATE,epoch,0,out,sizeof(*out));}
ScStatus ScShellHostConnect(ScU32 epoch,ScShellConnectionInfo *out)
{return sc32_host_control(SC32_H_CONNECT,epoch,0,out,sizeof(*out));}
ScStatus ScShellHostWorkspace(ScU32 epoch,ScU32 height,ScShellHostInfo *out)
{return sc32_host_control(SC32_H_WORKSPACE,epoch,height,out,sizeof(*out));}
ScStatus ScShellHostDeactivate(ScU32 epoch,ScShellHostInfo *out)
{return sc32_host_control(SC32_H_DEACTIVATE,epoch,0,out,sizeof(*out));}
ScStatus ScShellHostWindows(ScU32 host_epoch,ScU32 after,ScU32 expected_epoch,
    ScShellWindowInfo *rows,ScU32 capacity,ScShellWindowPage *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScShellWindowPage));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOWS);
    if(!capacity || capacity>64u || !rows || after==0xFFFFFFFFu)
        return sc32_host_invalid(SC32_E_INVALID,SC32_H_WINDOWS);
    checked=sc32_out(rows,capacity*sizeof(*rows));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOWS);
    if(sc32_overlap(rows,capacity*sizeof(*rows),out,out->size))
        return sc32_host_invalid(SC32_E_INVALID,SC32_H_WINDOWS);
    Sc32HostWindows input={32,1,host_epoch,after,expected_epoch,(ScU32)rows,capacity,0};
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,SC32_H_WINDOWS,
        (ScI32)&input,(ScI32)out,0,0,0),SC32_H_WINDOWS);
}
ScStatus ScShellHostWindowAction(const ScShellWindowAction *action,ScWindowInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScWindowInfo));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOW_ACTION);
    checked=sc32_record_in(action,sizeof(ScShellWindowAction));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOW_ACTION);
    ScShellWindowAction input=*action;
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,SC32_H_WINDOW_ACTION,
        (ScI32)&input,(ScI32)out,0,0,0),SC32_H_WINDOW_ACTION);
}
ScStatus ScShellHostWindowAt(const ScShellHostPoint *point,ScWindowInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScWindowInfo));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOW_AT);
    checked=sc32_record_in(point,sizeof(ScShellHostPoint));
    if(checked<0)return sc32_host_invalid(checked,SC32_H_WINDOW_AT);
    ScShellHostPoint input=*point;
    if(input.flags || input.reserved[0] || input.reserved[1])
        return sc32_host_invalid(SC32_E_INVALID,SC32_H_WINDOW_AT);
    return sc32_host_native(sc32_call(SC32_WIRE_GATE,SC32_H_WINDOW_AT,
        (ScI32)&input,(ScI32)out,0,0,0),SC32_H_WINDOW_AT);
}
