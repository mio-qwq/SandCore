#include "SC32PRIVATE.H"

ScStatus ScSystemCapabilities(ScCapabilities *out)
{
    ScStatus status=sc32_native(sc32_call(SC32_WIRE_GATE,SC32_WIRE_CAPABILITIES,0,(ScI32)out,0,0,0),SC32_OP_CAPS);
    /* 这个能力在本R3库/ROOT，而非新增内核gate；导入版本已保证实现存在。 */
    if(!status)out->capabilities_lo|=SC32_CAP_CALLBACK_LIFETIME;
    return status;
}
ScStatus ScSystemMemory(ScMemoryInfo *out)
{return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_WIRE_MEMORY,0,(ScI32)out,0,0,0),SC32_OP_MEMORY);}
ScStatus ScSystemMonotonic(ScMonotonicClock *out)
{return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_WIRE_MONOTONIC,0,(ScI32)out,0,0,0),SC32_OP_MONOTONIC);}
ScStatus ScSystemClock(ScUtcClock *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScUtcClock));if(checked<0)return sc32_invalid(checked,SC32_OP_CLOCK);
    ScU32 raw[8];ScI32 result=sc32_call(0x22f,(ScI32)raw,0,0,0,0,0);
    if(result<0)return sc32_fail(result==-6?SC32_E_AGAIN:result==-2?SC32_E_INVALID:SC32_E_FAILED,SC32_DOMAIN_LEGACY,result,SC32_OP_CLOCK);
    ScUtcClock value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.year=raw[1];value.month=raw[2];value.day=raw[3];value.hour=raw[4];value.minute=raw[5];value.second=raw[6];
    sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScSystemCpu(ScCpuInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScCpuInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_CPU);
    ScU32 raw[64];ScI32 result=sc32_call(0x79,(ScI32)raw,0,0,0,0,0);if(result<0)return sc32_legacy(result,SC32_OP_CPU);
    ScCpuInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.hz=raw[1];value.tick=raw[2];value.total=raw[3];value.idle=raw[4];value.kernel=raw[5];
    value.user=raw[6];value.graphics=raw[7];value.method=raw[9];sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScSystemStorage(ScStorageInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScStorageInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_STORAGE);
    ScU32 raw[16];ScI32 result=sc32_call(0x227,(ScI32)raw,0,0,0,0,0);if(result<0)return sc32_legacy(result,SC32_OP_STORAGE);
    ScStorageInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.mounted=raw[1];value.format=raw[2];value.disk_sectors=raw[3];value.data_start=raw[4];value.data_end=raw[5];
    value.directory_capacity=raw[6];value.record_count=raw[7];value.live_sectors=raw[8];value.reserved_sectors=raw[9];
    value.free_sectors=raw[10];value.largest_free_run=raw[11];value.object_sequence=raw[12];value.active_bank=raw[13];
    value.metadata_generation=raw[14];value.faulted=raw[15];sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScSystemWait(ScU32 mask,ScU32 timeout,ScU32 *events)
{
    ScStatus checked=sc32_out(events,4);if(checked<0)return sc32_invalid(checked,SC32_OP_WAIT);
    if((mask&~255u) || (timeout>0x7FFFFFFFu && timeout!=SC32_INFINITE) || (!mask && timeout==SC32_INFINITE))
        return sc32_invalid(SC32_E_INVALID,SC32_OP_WAIT);
    ScI32 result=sc32_call(0x241,(ScI32)mask,(ScI32)timeout,0,0,0,0);
    if(result<0)return sc32_fail(result==-4?SC32_E_MEMORY:SC32_E_FAILED,SC32_DOMAIN_LEGACY,result,SC32_OP_WAIT);
    *events=(ScU32)result;return ((ScU32)result&0x40000000u)?SC32_TIMEOUT:0;
}
ScStatus ScMemoryAllocatePages(ScU32 bytes,void **out)
{
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,SC32_OP_ALLOC);
    if(!bytes || bytes>0x04000000u)return sc32_invalid(SC32_E_RANGE,SC32_OP_ALLOC);
    ScU32 base=(ScU32)sc32_call(0x100,(ScI32)bytes,0,0,0,0,0);
    if(!base)return sc32_fail(SC32_E_MEMORY,SC32_DOMAIN_LEGACY,0,SC32_OP_ALLOC);
    if(base<0x40000000u || base>=0x44000000u)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,(ScI32)base,SC32_OP_ALLOC);
    *out=(void *)base;return 0;
}
ScStatus ScMemoryFreePages(void *base)
{
    if((ScU32)base<0x40000000u || (ScU32)base>=0x44000000u || ((ScU32)base&4095u))return sc32_invalid(SC32_E_INVALID,SC32_OP_FREE);
    ScI32 raw=sc32_call(0x101,(ScI32)base,0,0,0,0,0);
    return raw<0?sc32_fail(SC32_E_INVALID,SC32_DOMAIN_LEGACY,raw,SC32_OP_FREE):0;
}
