#include "SC32PRIVATE.H"
#include "SC32CAPTUREWIRE.H"
ScStatus ScProcessSpawnCapture(const char *command,ScCaptureInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(*out));
    if(checked<0)return sc32_invalid(checked,0x210u);
    checked=sc32_string(command,1087,0);
    if(checked<0)return sc32_invalid(checked,0x210u);
    Sc32CaptureRequest request;
    sc32_zero(&request,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.command=(ScU32)command;
    ScCaptureInfo result;
    sc32_zero(&result,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    ScI32 status=sc32_call(SC32_WIRE_GATE,SC32_F_CAPTURE,(ScI32)&request,(ScI32)&result,0,0,0);
    if(status<0)return sc32_native(status,0x210u);
    if(result.size!=sizeof(result)||result.version!=1||!result.job||!result.output||!result.error)
    return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,status,0x210u);
    sc32_copy(out,&result,sizeof(result));
    return 0;
}
