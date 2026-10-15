#include "../SC32DROP.H"
#include "../sc32/SC32PRIVATE.H"
#include "SC32DROPWIRE.H"

/* 仅组合到独立SC32API.SCL；不含SCAPI或新的asm/syscall桥。
 * 先验全部caller out再effects；未接kernel gate时保真实错误。 */
typedef char drop_desc_is_48[(sizeof(ScDropTargetDesc)==48)?1:-1];
typedef char drop_point_is_32[(sizeof(ScDropPoint)==32)?1:-1];
typedef char drop_info_is_144[(sizeof(ScDropInfo)==144)?1:-1];
typedef char drop_validate_is_32[(sizeof(ScDropValidateRequest)==32)?1:-1];
typedef char drop_envelope_is_64[(sizeof(ScDragEnvelope)==64)?1:-1];
static ScStatus drop_error(ScStatus error,ScU32 operation)
{return sc32_invalid(error,SC32_D_OPERATION+operation-SC32_D_REGISTER);}
static ScStatus drop_result(ScI32 raw,ScU32 operation)
{return sc32_native(raw,SC32_D_OPERATION+operation-SC32_D_REGISTER);}
static ScI32 drop_gate(ScU32 operation,const void *input,void *out)
{return sc32_call(SC32_WIRE_GATE,(ScI32)operation,(ScI32)input,(ScI32)out,0,0,0);}
ScStatus ScDropRegister(const ScDropTargetDesc *desc,ScDropTargetRef *out)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_record_out(out,sizeof(*out));
    if(error)return drop_error(error,SC32_D_REGISTER);
    if(!desc->format_count || desc->format_count>SC32_DROP_FORMAT_LIMIT)return drop_error(SC32_E_RANGE,SC32_D_REGISTER);
    error=sc32_probe(desc->formats,desc->format_count*sizeof(ScU32),SC32_WIRE_READ);
    if(error)return drop_error(error,SC32_D_REGISTER);
    Sc32DropRegister input;
    sc32_copy(&input,desc,sizeof(input));input.formats=(ScU32)desc->formats;
    return drop_result(drop_gate(SC32_D_REGISTER,&input,out),SC32_D_REGISTER);
}
ScStatus ScDropUnregister(ScWindow window,ScU32 generation)
{
    Sc32DropWindow input={16,1,window,generation};
    return drop_result(drop_gate(SC32_D_UNREGISTER,&input,0),SC32_D_UNREGISTER);
}
ScStatus ScDropAt(const ScDropPoint *point,ScDropInfo *out)
{
    ScStatus error=sc32_record_in(point,sizeof(*point));
    if(!error)error=sc32_record_out(out,sizeof(*out));
    if(error)return drop_error(error,SC32_D_AT);
    return drop_result(drop_gate(SC32_D_AT,point,out),SC32_D_AT);
}
ScStatus ScDropValidate(const ScDropValidateRequest *ticket,ScDropInfo *out)
{
    ScStatus error=sc32_record_in(ticket,sizeof(*ticket));
    if(!error)error=sc32_record_out(out,sizeof(*out));
    if(error)return drop_error(error,SC32_D_VALIDATE);
    return drop_result(drop_gate(SC32_D_VALIDATE,ticket,out),SC32_D_VALIDATE);
}
ScStatus ScDropConnect(const ScDropValidateRequest *ticket,ScIpcChannel *out)
{
    ScStatus error=sc32_record_in(ticket,sizeof(*ticket));
    if(!error)error=sc32_out(out,4);
    if(error)return drop_error(error,SC32_D_CONNECT);
    Sc32BrokerToken reply={16,1,0,0};
    ScI32 raw=drop_gate(SC32_D_CONNECT,ticket,&reply);
    if(raw)return drop_result(raw,SC32_D_CONNECT);
    *out=reply.handle;return 0;
}
ScStatus ScDropComplete(const ScDropToken *ticket,ScU32 action)
{
    ScStatus error=sc32_record_in(ticket,sizeof(*ticket));
    if(error)return drop_error(error,SC32_D_COMPLETE);
    if(action!=SC32_DROP_COPY && action!=SC32_DROP_MOVE)return drop_error(SC32_E_INVALID,SC32_D_COMPLETE);
    Sc32DropComplete input={24,1,ticket->ticket_lo,ticket->ticket_hi,action,0};
    return drop_result(drop_gate(SC32_D_COMPLETE,&input,0),SC32_D_COMPLETE);
}
ScStatus ScDropCancel(const ScDropToken *ticket)
{
    ScStatus error=sc32_record_in(ticket,sizeof(*ticket));
    if(error)return drop_error(error,SC32_D_CANCEL);
    return drop_result(drop_gate(SC32_D_CANCEL,ticket,0),SC32_D_CANCEL);
}
