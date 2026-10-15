#include "../sc32/SC32PRIVATE.H"
#include "../SC32PATH.H"
#include "SC32PATHWIRE.H"

ScStatus ScPathQuery(const char *path,ScPathInfo *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+0x23u;
    ScStatus status=sc32_record_out(out,sizeof(*out));
    if(status<0)return sc32_invalid(status,operation);
    status=sc32_string(path,127,0);
    if(status<0)return sc32_invalid(status,operation);
    Sc32PathQueryRequest query;
    sc32_zero(&query,sizeof(query));
    query.size=sizeof(query);query.version=1;query.path=(ScU32)path;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_P_QUERY,(ScI32)&query,(ScI32)out,0,0,0);
    return sc32_native(raw,operation);
}
