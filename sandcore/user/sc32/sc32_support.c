#include "SC32PRIVATE.H"

static ScErrorInfo sc32_last_error;
static void sc32_zero(void *out,ScU32 bytes)
{for(ScU32 i=0;i<bytes;i++)((unsigned char *)out)[i]=0;}
static void sc32_copy(void *out,const void *input,ScU32 bytes)
{for(ScU32 i=0;i<bytes;i++)((unsigned char *)out)[i]=((const unsigned char *)input)[i];}
static ScStatus sc32_probe(const void *address,ScU32 bytes,ScU32 access)
{
    Sc32ProbeRequest request={20,1,(ScU32)address,bytes,access};
    return sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROBE,(ScI32)&request,0,0,0,0);
}
static ScStatus sc32_out(void *out,ScU32 bytes)
{return sc32_probe(out,bytes,SC32_WIRE_WRITE);}
static ScStatus sc32_record_out(void *out,ScU32 bytes)
{
    ScStatus checked=sc32_probe(out,8,SC32_WIRE_READ);if(checked<0)return checked;
    const ScU32 *header=out;
    if(header[1]!=1)return SC32_E_VERSION;
    if(header[0]<bytes || header[0]>SC32_WIRE_MAX_RECORD)return SC32_E_INVALID;
    return sc32_out(out,header[0]);
}
static ScStatus sc32_record_in(const void *input,ScU32 bytes)
{
    ScStatus checked=sc32_probe(input,8,SC32_WIRE_READ);if(checked<0)return checked;
    const ScU32 *header=input;
    if(header[1]!=1)return SC32_E_VERSION;
    if(header[0]!=bytes)return SC32_E_INVALID;
    return sc32_probe(input,bytes,SC32_WIRE_READ);
}
static ScStatus sc32_string(const char *text,ScU32 maximum,ScU32 *length)
{
    if(!text || maximum==0xFFFFFFFFu)return SC32_E_INVALID;
    ScU32 used=0;
    /* 只探查即将读取的页内区间。先读完一页再探下一页，避免每字节陷入；
     * 若NUL恰在页末，则不会因为后页缺失而拒绝完整字符串。 */
    while(used<=maximum){
        ScU32 address=(ScU32)text+used,portion=4096u-(address&4095u);
        if(portion>maximum-used+1)portion=maximum-used+1;
        ScStatus checked=sc32_probe((const void *)address,portion,SC32_WIRE_READ);
        if(checked<0)return checked;
        for(ScU32 i=0;i<portion;i++)if(!text[used+i]){if(length)*length=used+i;return 0;}
        used+=portion;
    }
    return SC32_E_RANGE;
}
static ScStatus sc32_fail(ScI32 code,ScU32 domain,ScI32 raw,ScU32 operation)
{
    sc32_zero(&sc32_last_error,sizeof(sc32_last_error));
    sc32_last_error.size=sizeof(sc32_last_error);sc32_last_error.version=1;
    sc32_last_error.code=code;sc32_last_error.domain=domain;
    sc32_last_error.raw=raw;sc32_last_error.operation=operation;return code;
}
static ScStatus sc32_invalid(ScI32 code,ScU32 operation)
{
    ScI32 stable=code<=SC32_E_INVALID && code>=SC32_E_FAILED?code:SC32_E_FAILED;
    return sc32_fail(stable,SC32_DOMAIN_LIBRARY,code,operation);
}
static ScStatus sc32_native(ScI32 raw,ScU32 operation)
{
    if(raw>=0 && raw<=SC32_RESIDENT)return raw;
    ScI32 code=raw<=SC32_E_INVALID && raw>=SC32_E_FAILED?raw:SC32_E_FAILED;
    return sc32_fail(code,SC32_DOMAIN_KERNEL,raw,operation);
}
static ScStatus sc32_legacy(ScI32 raw,ScU32 operation)
{
    if(raw>=0)return 0;
    /* 旧-1包含路径/IO/映射等多种来源，只保留粗错误。-5是既有权限合同。 */
    return sc32_fail(raw==-5?SC32_E_ACCESS:SC32_E_FAILED,SC32_DOMAIN_LEGACY,raw,operation);
}
static ScStatus sc32_runtime_error(ScI32 raw,ScU32 operation)
{
    if(raw==SC_SCL_OK)return 0;
    if(raw==SC_SCL_RESIDENT)return SC32_RESIDENT;
    ScI32 code=SC32_E_FAILED;
    if(raw==SC_SCL_FORMAT || raw==SC_SCL_HANDLE || raw==SC_SCL_CYCLE)code=SC32_E_INVALID;
    else if(raw==SC_SCL_RANGE)code=SC32_E_RANGE;
    else if(raw==SC_SCL_SYMBOL || raw==SC_SCL_NOT_FOUND)code=SC32_E_NOT_FOUND;
    else if(raw==SC_SCL_VERSION)code=SC32_E_VERSION;
    else if(raw==SC_SCL_MEMORY)code=SC32_E_MEMORY;
    else if(raw==SC_SCL_BUSY)code=SC32_E_BUSY;
    else if(raw==SC_SCL_IO)code=SC32_E_IO;
    return sc32_fail(code,SC32_DOMAIN_SCLDR,raw,operation);
}
static int sc32_overlap(const void *a,ScU32 a_bytes,const void *b,ScU32 b_bytes)
{
    if(!a_bytes || !b_bytes)return 0;
    ScU32 x=(ScU32)a,y=(ScU32)b;
    return x<=y?y-x<a_bytes:x-y<b_bytes;
}
static ScStatus sc32_absolute(const char *path,char out[65],ScU32 operation)
{
    ScStatus checked=sc32_string(path,127,0);if(checked<0)return sc32_invalid(checked,operation);
    char relative[64];ScI32 result=sc32_call(0x73,(ScI32)path,(ScI32)relative,64,0,0,0);
    if(result<0)return sc32_legacy(result,operation);
    if(result>63)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,result,operation);
    out[0]='/';for(ScI32 i=0;i<=result;i++)out[i+1]=relative[i];return 0;
}
static ScStatus sc32_text_buffers(char *out,ScU32 capacity,ScU32 *length)
{
    ScStatus checked=sc32_out(out,capacity);if(checked<0)return checked;
    if(length){checked=sc32_out(length,4);if(checked<0)return checked;
        if(sc32_overlap(out,capacity,length,4))return SC32_E_INVALID;}
    return 0;
}
static ScStatus sc32_text_output(const char *text,ScU32 bytes,char *out,ScU32 capacity,ScU32 *length,ScU32 operation)
{
    if(length)*length=bytes;
    if(capacity<=bytes)return sc32_invalid(SC32_E_RANGE,operation);
    sc32_copy(out,text,bytes);out[bytes]=0;return 0;
}
static const char *sc32_error_name(ScI32 code)
{
    switch(code){
    case SC32_OK:return "OK";
    case SC32_PENDING:return "PENDING";
    case SC32_TIMEOUT:return "TIMEOUT";
    case SC32_RESIDENT:return "RESIDENT";
    case SC32_E_INVALID:return "INVALID";
    case SC32_E_VERSION:return "VERSION";
    case SC32_E_ACCESS:return "ACCESS";
    case SC32_E_NOT_FOUND:return "NOT_FOUND";
    case SC32_E_EXISTS:return "EXISTS";
    case SC32_E_MEMORY:return "MEMORY";
    case SC32_E_NO_SPACE:return "NO_SPACE";
    case SC32_E_STALE:return "STALE";
    case SC32_E_AGAIN:return "AGAIN";
    case SC32_E_BROKEN_PIPE:return "BROKEN_PIPE";
    case SC32_E_IO:return "IO";
    case SC32_E_NOT_SUPPORTED:return "NOT_SUPPORTED";
    case SC32_E_BUSY:return "BUSY";
    case SC32_E_RANGE:return "RANGE";
    case SC32_E_CANCELLED:return "CANCELLED";
    case SC32_E_FAILED:return "FAILED";
    default:return "UNKNOWN";
    }
}
static ScU32 sc32_append(char *out,ScU32 at,const char *text)
{while(*text)out[at++]=*text++;return at;}
static ScU32 sc32_number(char *out,ScU32 at,ScI32 value)
{
    ScU32 number=value<0?0u-(ScU32)value:(ScU32)value;char digits[10];ScU32 used=0;
    if(value<0)out[at++]='-';
    do{digits[used++]=(char)('0'+number%10);number/=10;}while(number);
    while(used)out[at++]=digits[--used];
    return at;
}
static ScStatus sc32_error_text(const char *text,ScU32 bytes,char *out,ScU32 capacity,ScU32 *required)
{
    ScStatus checked=required?sc32_out(required,4):0;if(checked<0)return checked;
    checked=sc32_out(out,capacity);if(checked<0)return checked;
    if(required && sc32_overlap(out,capacity,required,4))return SC32_E_INVALID;
    if(required)*required=bytes+1;
    if(capacity<=bytes)return SC32_E_RANGE;
    sc32_copy(out,text,bytes);out[bytes]=0;return 0;
}
ScStatus ScErrorQuery(ScErrorInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScErrorInfo));if(checked<0)return checked;
    ScErrorInfo copy=sc32_last_error;copy.size=sizeof(copy);copy.version=1;
    sc32_copy(out,&copy,sizeof(copy));return 0;
}
ScStatus ScErrorName(ScI32 code,char *out,ScU32 capacity,ScU32 *required)
{
    const char *text=sc32_error_name(code);ScU32 bytes=0;while(text[bytes])bytes++;
    return sc32_error_text(text,bytes,out,capacity,required);
}
ScStatus ScErrorFormat(const ScErrorInfo *error,char *out,ScU32 capacity,ScU32 *required)
{
    ScStatus checked=sc32_record_in(error,sizeof(ScErrorInfo));if(checked<0)return checked;
    ScErrorInfo copy=*error;if(copy.reserved[0] || copy.reserved[1])return SC32_E_INVALID;
    char text[128];ScU32 at=0;
    at=sc32_append(text,at,sc32_error_name(copy.code));at=sc32_append(text,at," [domain=");
    at=sc32_number(text,at,(ScI32)copy.domain);at=sc32_append(text,at," raw=");
    at=sc32_number(text,at,copy.raw);at=sc32_append(text,at," operation=");
    at=sc32_number(text,at,(ScI32)copy.operation);text[at++]=']';
    return sc32_error_text(text,at,out,capacity,required);
}
