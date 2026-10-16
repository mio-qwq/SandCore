#include "../sc32/SC32PRIVATE.H"
#include "SC32DEBUGWIRE.H"

typedef char d2_reference_is_24[(sizeof(ScDebugRef)==24)?1:-1];
typedef char d2_registers_are_96[(sizeof(ScDebugRegisters)==96)?1:-1];
typedef char d2_module_is_204[(sizeof(ScDebugModule)==204)?1:-1];
typedef char d2_request_is_92[(sizeof(Sc32DebugRequest)==92)?1:-1];

static ScStatus sc32_d2_invalid(ScStatus s,ScU32 selector)
{return sc32_invalid(s,SC32_D2_ERROR_BASE+selector-SC32_D2_FIRST);
}
static ScStatus sc32_d2_call(ScU32 selector,const void *request,void *out)
{return sc32_native(sc32_call(SC32_WIRE_GATE,(ScI32)selector,(ScI32)request,(ScI32)out,0,0,0),
    SC32_D2_ERROR_BASE+selector-SC32_D2_FIRST);
}
static ScStatus sc32_d2_request(const ScDebugRef *target,Sc32DebugRequest *r,ScU32 selector)
{
    ScStatus s=sc32_record_in(target,sizeof(*target));
    if(s<0)return sc32_d2_invalid(s,selector);
    if(!target->pid || !target->generation || !target->binding || target->reserved)
    return sc32_d2_invalid(SC32_E_INVALID,selector);
    sc32_zero(r,sizeof(*r));
    r->size=sizeof(*r);
    r->version=1;
    r->target=*target;
    return 0;
}
ScStatus ScDebugLaunch(const ScDebugLaunchDesc *desc,ScDebugLaunchInfo *out)
{
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_LAUNCH);
    s=sc32_record_in(desc,sizeof(*desc));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_LAUNCH);
    ScDebugLaunchDesc local=*desc;
    if(local.flags || local.reserved)return sc32_d2_invalid(SC32_E_INVALID,SC32_D2_LAUNCH);
    s=sc32_string(local.command,1087,0);
    if(s<0)return sc32_d2_invalid(s,SC32_D2_LAUNCH);
    for(ScU32 i=0;i<3;i++)if(local.streams[i]<-1)return sc32_d2_invalid(SC32_E_INVALID,SC32_D2_LAUNCH);
    return sc32_d2_call(SC32_D2_LAUNCH,&local,out);
}
ScStatus ScDebugQuery(const ScDebugRef *target,ScDebugSnapshot *out)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_QUERY);
    s=sc32_d2_request(target,&r,SC32_D2_QUERY);
    return s<0?s:sc32_d2_call(SC32_D2_QUERY,&r,out);
}
ScStatus ScDebugControl(const ScDebugRef *target,ScU32 stop,ScU32 action,ScDebugSnapshot *out)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_CONTROL);
    s=sc32_d2_request(target,&r,SC32_D2_CONTROL);
    if(s<0)return s;
    r.stop=stop;
    r.action=action;
    return sc32_d2_call(SC32_D2_CONTROL,&r,out);
}
ScStatus ScDebugRead(const ScDebugRef *target,ScU32 stop,ScU32 address,ScU32 bytes,
ScU32 flags,void *data,unsigned char *readable,ScDebugReadInfo *out)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_READ);
    s=sc32_d2_request(target,&r,SC32_D2_READ);
    if(s<0)return s;
    if(!bytes || bytes>SC32_DEBUG_READ_MAX || address>0xffffffffu-(bytes-1)
    || flags&~SC32_DEBUG_READ_ORIGINAL_CODE)return sc32_d2_invalid(SC32_E_RANGE,SC32_D2_READ);
    s=sc32_out(data,bytes);
    if(s==0)s=sc32_out(readable,bytes);
    if(s<0)return sc32_d2_invalid(s,SC32_D2_READ);
    if(sc32_overlap(data,bytes,readable,bytes) || sc32_overlap(data,bytes,out,out->size)
    || sc32_overlap(readable,bytes,out,out->size))return sc32_d2_invalid(SC32_E_INVALID,SC32_D2_READ);
    r.stop=stop;
    r.address=address;
    r.bytes=bytes;
    r.flags=flags;
    r.data=(ScU32)data;
    r.readable=(ScU32)readable;
    return sc32_d2_call(SC32_D2_READ,&r,out);
}
ScStatus ScDebugBreakpointAdd(const ScDebugRef *target,ScU32 stop,ScU32 address,ScDebugBreakpoint *out)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,SC32_D2_ADD);
    s=sc32_d2_request(target,&r,SC32_D2_ADD);
    if(s<0)return s;
    r.stop=stop;
    r.address=address;
    return sc32_d2_call(SC32_D2_ADD,&r,out);
}
ScStatus ScDebugBreakpointRemove(const ScDebugRef *target,ScU32 stop,ScU32 id,ScU32 generation)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_d2_request(target,&r,SC32_D2_REMOVE);
    if(s<0)return s;
    r.stop=stop;
    r.id=id;
    r.generation=generation;
    return sc32_d2_call(SC32_D2_REMOVE,&r,0);
}
static ScStatus sc32_d2_page(ScU32 selector,const ScDebugRef *target,ScU32 stop,ScU32 after,ScU32 epoch,
ScU32 module,ScU32 generation,void *rows,ScU32 capacity,ScU32 stride,ScDebugPage *out)
{
    Sc32DebugRequest r;
    ScStatus s=sc32_record_out(out,sizeof(*out));
    if(s<0)return sc32_d2_invalid(s,selector);
    s=sc32_d2_request(target,&r,selector);
    if(s<0)return s;
    if(!capacity || capacity>SC32_DEBUG_PAGE_MAX || !rows || after==0xffffffffu)
    return sc32_d2_invalid(SC32_E_INVALID,selector);
    s=sc32_out(rows,capacity*stride);
    if(s<0)return sc32_d2_invalid(s,selector);
    if(sc32_overlap(rows,capacity*stride,out,out->size))return sc32_d2_invalid(SC32_E_INVALID,selector);
    r.stop=stop;
    r.after=after;
    r.epoch=epoch;
    r.rows=(ScU32)rows;
    r.capacity=capacity;
    r.module=module;
    r.module_generation=generation;
    return sc32_d2_call(selector,&r,out);
}
ScStatus ScDebugBreakpoints(const ScDebugRef *target,ScU32 stop,ScU32 after,ScU32 epoch,
ScDebugBreakpoint *rows,ScU32 capacity,ScDebugPage *out)
{return sc32_d2_page(SC32_D2_BREAKS,target,stop,after,epoch,0,0,rows,capacity,sizeof(*rows),out);
}
ScStatus ScDebugModules(const ScDebugRef *target,ScU32 stop,ScU32 after,ScU32 epoch,
ScDebugModule *rows,ScU32 capacity,ScDebugPage *out)
{return sc32_d2_page(SC32_D2_MODULES,target,stop,after,epoch,0,0,rows,capacity,sizeof(*rows),out);
}
ScStatus ScDebugSegments(const ScDebugRef *target,ScU32 stop,ScU32 module,ScU32 generation,
ScU32 after,ScDebugSegment *rows,ScU32 capacity,ScDebugPage *out)
{return sc32_d2_page(SC32_D2_SEGMENTS,target,stop,after,0,module,generation,rows,capacity,sizeof(*rows),out);
}
ScStatus ScDebugSymbols(const ScDebugRef *target,ScU32 stop,ScU32 module,ScU32 generation,
ScU32 after,ScDebugSymbol *rows,ScU32 capacity,ScDebugPage *out)
{return sc32_d2_page(SC32_D2_SYMBOLS,target,stop,after,0,module,generation,rows,capacity,sizeof(*rows),out);
}
