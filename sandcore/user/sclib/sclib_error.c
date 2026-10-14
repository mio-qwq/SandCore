#include "SCLIBPRIVATE.H"

struct ScErrorObject {SlBase base;
    ScUtilErrorInfo info;
    ScString *message;};
static void sl_error_dispose(SlBase *base,int quiet)
{ScErrorObject *error=(ScErrorObject *)base;
    (void)quiet;
    if(error->message)ScStringRelease(error->message);
    error->message=0;}
ScStatus ScErrorObjectCreate(const ScErrorDesc *desc,ScErrorObject **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(desc->reserved || !desc->message)return SC_UTIL_E_INVALID;
    ScU32 bytes;
    status=sl_string_bytes(desc->message,&bytes);
    if(status)return status;
    status=ScUtf8Validate(desc->message,bytes,0);
    if(status)return status;
    ScErrorObject *error=sl_object(SL_ERROR,sizeof(*error),sl_error_dispose);
    if(!error)return SC_UTIL_E_MEMORY;
    status=ScStringCreate(desc->message,bytes,&error->message);
    if(status){ScErrorObjectRelease(error);
        return status;}
    error->info.size=sizeof(error->info);
    error->info.version=1;
    error->info.code=desc->code;
    error->info.domain=desc->domain;
    error->info.raw=desc->raw;
    error->info.operation=desc->operation;
    *out=error;
    return 0;
}
ScStatus ScErrorObjectRetain(ScErrorObject *error){return sl_retain(error,SL_ERROR);}
void ScErrorObjectRelease(ScErrorObject *error){sl_release(error,SL_ERROR);}
ScStatus ScErrorObjectQuery(const ScErrorObject *error,ScUtilErrorInfo *out)
{
    ScStatus status=sl_valid(error,SL_ERROR);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    memcpy(out,&error->info,sizeof(error->info));
    return 0;
}
ScStatus ScErrorObjectMessage(const ScErrorObject *error,ScView *out)
{ScStatus status=sl_valid(error,SL_ERROR);
    return status?status:ScStringView(error->message,out);}
ScStatus ScErrorObjectFormat(const ScErrorObject *error,ScString **out)
{
    ScStatus status=sl_valid(error,SL_ERROR);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    ScString *text;
    status=ScStringCreate(0,0,&text);
    if(status)return status;
    status=ScStringAppendFormat(text,"code=%d domain=%u raw=%d op=%u: ",error->info.code,error->info.domain,error->info.raw,error->info.operation);
    if(!status)status=ScStringAppend(text,error->message->data,error->message->bytes);
    if(status){ScStringRelease(text);
        return status;}
    *out=text;
    return 0;
}
