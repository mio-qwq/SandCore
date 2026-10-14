#include "SCLIBPRIVATE.H"

static void sl_string_dispose(SlBase *base,int quiet)
{ScString *string=(ScString *)base;
    (void)quiet;
    if(!string->borrowed)free(string->data);
    string->data=0;
    string->bytes=string->capacity=0;}
static ScStatus sl_string_new(const void *bytes,ScU32 count,int borrowed,ScString **out)
{
    if(!out || (count && !bytes))return SC_UTIL_E_INVALID;
    if(count==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    ScString *string=sl_object(SL_STRING,sizeof(*string),sl_string_dispose);
    if(!string)return SC_UTIL_E_MEMORY;
    string->generation=1;
    if(borrowed){string->data=(unsigned char *)bytes;
        string->borrowed=1;}
    else{
        string->data=malloc(count+1u);
        if(!string->data){ScStringRelease(string);
            return SC_UTIL_E_MEMORY;}
        if(count)memcpy(string->data,bytes,count);
        string->data[count]=0;
        string->capacity=count+1u;
    }
    string->bytes=count;
    *out=string;
    return 0;
}
ScStatus ScStringCreate(const void *bytes,ScU32 count,ScString **out){return sl_string_new(bytes,count,0,out);}
ScStatus ScStringBorrow(const void *bytes,ScU32 count,ScString **out){return sl_string_new(bytes,count,1,out);}
ScStatus ScStringRetain(ScString *string){return sl_retain(string,SL_STRING);}
void ScStringRelease(ScString *string){sl_release(string,SL_STRING);}
ScStatus ScStringQuery(const ScString *string,ScContainerInfo *out)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,string->bytes,string->borrowed?string->bytes:string->capacity-1u,1,string->generation,string->borrowed?SC_STRING_BORROWED:0);
    return 0;
}
ScStatus ScStringView(const ScString *string,ScView *out)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_view(out,string->data,string->bytes,string->generation);
    return 0;
}
ScStatus ScStringCStr(ScString *string,const char **out)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(string->borrowed){
        status=sl_mutation(&string->base,string->generation);
        if(status)return status;
        unsigned char *fresh=malloc(string->bytes+1u);
        if(!fresh)return SC_UTIL_E_MEMORY;
        if(string->bytes)memcpy(fresh,string->data,string->bytes);
        fresh[string->bytes]=0;
        string->data=fresh;
        string->capacity=string->bytes+1u;
        string->borrowed=0;
        string->generation++;
    }
    *out=(const char *)string->data;
    return 0;
}
static ScStatus sl_string_replace(ScString *string,ScU32 position,ScU32 removed,const void *bytes,ScU32 count)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(status)return status;
    status=sl_mutation(&string->base,string->generation);
    if(status)return status;
    if(position>string->bytes || removed>string->bytes-position || (count && !bytes))return SC_UTIL_E_RANGE;
    if(!removed && !count)return 0;
    if(count>0xFFFFFFFEu-(string->bytes-removed))return SC_UTIL_E_RANGE;
    ScU32 wanted=string->bytes-removed+count;
    if(string->borrowed || string->capacity<wanted+1u){
        ScU32 capacity=sl_capacity(string->borrowed?0:string->capacity,wanted+1u,1);
        unsigned char *fresh=malloc(capacity);
        if(!fresh)return SC_UTIL_E_MEMORY;
        if(position)memcpy(fresh,string->data,position);
        if(count)memcpy(fresh+position,bytes,count);
        if(string->bytes-position-removed)memcpy(fresh+position+count,string->data+position+removed,string->bytes-position-removed);
        fresh[wanted]=0;
        if(!string->borrowed)free(string->data);
        string->data=fresh;
        string->capacity=capacity;
        string->borrowed=0;
    }else{
        void *saved=0;
        const void *source=bytes;
        if(sl_alias(bytes,count,string->data,string->capacity)){
            saved=malloc(count);
            if(!saved)return SC_UTIL_E_MEMORY;
            memcpy(saved,bytes,count);
            source=saved;
        }
        memmove(string->data+position+count,string->data+position+removed,string->bytes-position-removed);
        if(count)memcpy(string->data+position,source,count);
        string->data[wanted]=0;
        free(saved);
    }
    string->bytes=wanted;
    string->generation++;
    return 0;
}
ScStatus ScStringAssign(ScString *string,const void *bytes,ScU32 count)
{ScStatus status=sl_valid(string,SL_STRING);
    return status?status:sl_string_replace(string,0,string->bytes,bytes,count);}
ScStatus ScStringInsert(ScString *string,ScU32 position,const void *bytes,ScU32 count)
{return sl_string_replace(string,position,0,bytes,count);}
ScStatus ScStringErase(ScString *string,ScU32 position,ScU32 count)
{return sl_string_replace(string,position,count,0,0);}
ScStatus ScStringAppend(ScString *string,const void *bytes,ScU32 count)
{ScStatus status=sl_valid(string,SL_STRING);
    return status?status:sl_string_replace(string,string->bytes,0,bytes,count);}
static ScStatus sl_string_format(ScString *string,const char *format,va_list arguments,int append)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(status || !format)return status?status:SC_UTIL_E_INVALID;
    va_list count_args;
    va_copy(count_args,arguments);
    int needed=vsnprintf(0,0,format,count_args);
    va_end(count_args);
    if(needed<0)return SC_UTIL_E_IO;
    char *fresh=malloc((ScU32)needed+1u);
    if(!fresh)return SC_UTIL_E_MEMORY;
    va_list write_args;
    va_copy(write_args,arguments);
    int actual=vsnprintf(fresh,(ScU32)needed+1u,format,write_args);
    va_end(write_args);
    if(actual!=needed){free(fresh);
        return SC_UTIL_E_IO;}
    status=append?ScStringAppend(string,fresh,(ScU32)needed):ScStringAssign(string,fresh,(ScU32)needed);
    free(fresh);
    return status;
}
ScStatus ScStringVFormat(ScString *string,const char *format,va_list arguments){return sl_string_format(string,format,arguments,0);}
ScStatus ScStringAppendVFormat(ScString *string,const char *format,va_list arguments){return sl_string_format(string,format,arguments,1);}
ScStatus ScStringFormat(ScString *string,const char *format,...)
{va_list arguments;
    va_start(arguments,format);
    ScStatus status=ScStringVFormat(string,format,arguments);
    va_end(arguments);
    return status;}
ScStatus ScStringAppendFormat(ScString *string,const char *format,...)
{va_list arguments;
    va_start(arguments,format);
    ScStatus status=ScStringAppendVFormat(string,format,arguments);
    va_end(arguments);
    return status;}
ScStatus ScFormatHex(const void *bytes,ScU32 count,ScU32 uppercase,ScString **out)
{
    if(!out || (count && !bytes) || uppercase>1u)return SC_UTIL_E_INVALID;
    if(count>0x7FFFFFFEu)return SC_UTIL_E_RANGE;
    ScString *string;
    ScStatus status=ScStringCreate(0,0,&string);
    if(status)return status;
    ScU32 length=count*2u;
    unsigned char *fresh=malloc(length+1u);
    if(!fresh){ScStringRelease(string);
        return SC_UTIL_E_MEMORY;}
    const char *digits=uppercase?"0123456789ABCDEF":"0123456789abcdef";
    const unsigned char *p=bytes;
    for(ScU32 i=0;i<count;i++){fresh[i*2u]=(unsigned char)digits[p[i]>>4];
        fresh[i*2u+1u]=(unsigned char)digits[p[i]&15u];}
    fresh[length]=0;
    free(string->data);
    string->data=fresh;
    string->bytes=length;
    string->capacity=length+1u;
    *out=string;
    return 0;
}
ScStatus ScFormatEscape(const void *utf8,ScU32 count,ScString **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScStatus status=ScUtf8Validate(utf8,count,0);
    if(status)return status;
    ScString *string;
    status=ScStringCreate(0,0,&string);
    if(status)return status;
    const unsigned char *p=utf8;
    for(ScU32 i=0;i<count && !status;i++){
        unsigned char ch=p[i];
        char escaped[7];
        ScU32 length=1;
        escaped[0]=(char)ch;
        if(ch=='"' || ch=='\\'){escaped[0]='\\';
            escaped[1]=(char)ch;
            length=2;}
        else if(ch=='\n' || ch=='\r' || ch=='\t'){escaped[0]='\\';
            escaped[1]=ch=='\n'?'n':ch=='\r'?'r':'t';
            length=2;}
        else if(ch<32u || ch==127u){int n=snprintf(escaped,sizeof(escaped),"\\u%04x",(unsigned int)ch);
            if(n!=6){status=SC_UTIL_E_IO;
                break;}length=6;}
        status=ScStringAppend(string,escaped,length);
    }
    if(status){ScStringRelease(string);
        return status;}
    *out=string;
    return 0;
}
ScStatus ScFormatBytes(ScU32 bytes,ScString **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    const char *unit="B";
    ScU32 divisor=1;
    if(bytes>=1073741824u){divisor=1073741824u;
        unit="GiB";}
    else if(bytes>=1048576u){divisor=1048576u;
        unit="MiB";}
    else if(bytes>=1024u){divisor=1024u;
        unit="KiB";}
    char text[48];
    int length;
    if(divisor==1)length=snprintf(text,sizeof(text),"%u B",bytes);
    else{
        ScU32 remainder=bytes%divisor,fraction=0;
        /* ceil(divisor*i/10)拆成商余，避免GiB余数乘十的32位溢出，
         * 也不能用divisor/10的截断值制造1.10这种错误小数。 */
        for(ScU32 i=1;i<10u;i++){
            ScU32 threshold=(divisor/10u)*i+((divisor%10u)*i+9u)/10u;
            if(remainder<threshold)break;
            fraction=i;
        }
        length=snprintf(text,sizeof(text),"%u.%u %s",bytes/divisor,fraction,unit);
    }
    if(length<0 || (ScU32)length>=sizeof(text))return SC_UTIL_E_IO;
    return ScStringCreate(text,(ScU32)length,out);
}
