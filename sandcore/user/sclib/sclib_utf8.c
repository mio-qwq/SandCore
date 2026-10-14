#include "SCLIBPRIVATE.H"

ScU32 ScUnicodeIsScalar(ScU32 scalar)
{return scalar<=0x10FFFFu && !(scalar>=0xD800u && scalar<=0xDFFFu);}
ScU32 ScUnicodeIsNoncharacter(ScU32 scalar)
{return ScUnicodeIsScalar(scalar) && ((scalar>=0xFDD0u && scalar<=0xFDEFu) || (scalar&0xFFFFu)>=0xFFFEu);}
ScU32 ScUnicodeIsControl(ScU32 scalar)
{return scalar<32u || (scalar>=127u && scalar<=159u);}
ScU32 ScUnicodeIsWhitespace(ScU32 scalar)
{
    /* 固定Unicode15.1 White_Space集合，不把locale的字节ctype误作
     * Unicode分类，也不把零宽格式符/不换行连字符偷偷当空白。 */
    return (scalar>=9u && scalar<=13u) || scalar==32u || scalar==0x85u || scalar==0xA0u
        || scalar==0x1680u || (scalar>=0x2000u && scalar<=0x200Au) || scalar==0x2028u
        || scalar==0x2029u || scalar==0x202Fu || scalar==0x205Fu || scalar==0x3000u;
}
ScStatus ScUtf8Decode(const void *bytes,ScU32 count,ScU32 *offset,ScU32 *scalar)
{
    if(!offset || !scalar || offset==scalar || (count && !bytes) || *offset>count)return SC_UTIL_E_INVALID;
    if(*offset==count)return SC_UTIL_END;
    const unsigned char *p=bytes;
    ScU32 at=*offset,first=p[at],length,value,minimum;
    if(first<0x80u){length=1;
        value=first;
        minimum=0;}
    else if(first>=0xC2u && first<=0xDFu){length=2;
        value=first&31u;
        minimum=0x80u;}
    else if(first>=0xE0u && first<=0xEFu){length=3;
        value=first&15u;
        minimum=0x800u;}
    else if(first>=0xF0u && first<=0xF4u){length=4;
        value=first&7u;
        minimum=0x10000u;}
    else return SC_UTIL_E_UTF8;
    if(length>count-at)return SC_UTIL_E_UTF8;
    for(ScU32 i=1;i<length;i++){
        ScU32 next=p[at+i];
        if((next&0xC0u)!=0x80u)return SC_UTIL_E_UTF8;
        value=(value<<6)|(next&63u);
    }
    if(value<minimum || !ScUnicodeIsScalar(value))return SC_UTIL_E_UTF8;
    *offset=at+length;
    *scalar=value;
    return 0;
}
ScStatus ScUtf8Previous(const void *bytes,ScU32 count,ScU32 *offset,ScU32 *scalar)
{
    if(!offset || !scalar || offset==scalar || (count && !bytes) || *offset>count)return SC_UTIL_E_INVALID;
    if(!*offset)return SC_UTIL_END;
    const unsigned char *p=bytes;
    ScU32 end=*offset,at=end-1u,steps=0;
    while(at && (p[at]&0xC0u)==0x80u && steps<3u){at--;
        steps++;}
    ScU32 next=at,value;
    ScStatus status=ScUtf8Decode(bytes,end,&next,&value);
    if(status || next!=end)return SC_UTIL_E_UTF8;
    *offset=at;
    *scalar=value;
    return 0;
}
ScStatus ScUtf8Encode(ScU32 scalar,void *out,ScU32 capacity,ScU32 *written)
{
    if(!written || !ScUnicodeIsScalar(scalar))return SC_UTIL_E_INVALID;
    ScU32 length=scalar<0x80u?1u:scalar<0x800u?2u:scalar<0x10000u?3u:4u;
    if(!out || capacity<length)return SC_UTIL_E_RANGE;
    unsigned char value[4];
    if(length==1)value[0]=(unsigned char)scalar;
    else{
        ScU32 remaining=scalar;
        for(ScU32 i=length-1u;i;i--){value[i]=(unsigned char)(0x80u|(remaining&63u));
            remaining>>=6;}
        value[0]=(unsigned char)((length==2?0xC0u:length==3?0xE0u:0xF0u)|remaining);
    }
    memcpy(out,value,length);
    *written=length;
    return 0;
}
ScStatus ScUtf8Validate(const void *bytes,ScU32 count,ScU32 *bad_offset)
{
    if(count && !bytes)return SC_UTIL_E_INVALID;
    ScU32 at=0,scalar;
    while(at<count){ScU32 start=at;
        ScStatus status=ScUtf8Decode(bytes,count,&at,&scalar);
        if(status){if(bad_offset)*bad_offset=start;
            return status;}}
    if(bad_offset)*bad_offset=count;
    return 0;
}
ScStatus ScUtf8Count(const void *bytes,ScU32 count,ScU32 *scalars)
{
    if(!scalars || (count && !bytes))return SC_UTIL_E_INVALID;
    ScU32 at=0,total=0,scalar;
    while(at<count){ScStatus status=ScUtf8Decode(bytes,count,&at,&scalar);
        if(status)return status;
        total++;}
    *scalars=total;
    return 0;
}
ScStatus ScUtf8Offset(const void *bytes,ScU32 count,ScU32 scalar_index,ScU32 *byte_offset)
{
    if(!byte_offset)return SC_UTIL_E_INVALID;
    ScStatus status=ScUtf8Validate(bytes,count,0);
    if(status)return status;
    ScU32 at=0,scalar;
    for(ScU32 i=0;i<scalar_index;i++){status=ScUtf8Decode(bytes,count,&at,&scalar);
        if(status)return SC_UTIL_E_RANGE;}
    *byte_offset=at;
    return 0;
}
ScStatus ScUtf8Trim(ScString *string)
{
    ScStatus status=sl_valid(string,SL_STRING);
    if(status)return status;
    status=ScUtf8Validate(string->data,string->bytes,0);
    if(status)return status;
    ScU32 first=0,last=string->bytes,scalar;
    while(first<last){ScU32 at=first;
        ScUtf8Decode(string->data,last,&at,&scalar);
        if(!ScUnicodeIsWhitespace(scalar))break;
        first=at;}
    while(last>first){ScU32 at=last;
        ScUtf8Previous(string->data,string->bytes,&at,&scalar);
        if(!ScUnicodeIsWhitespace(scalar))break;
        last=at;}
    if(!first && last==string->bytes)return 0;
    return ScStringAssign(string,string->data+first,last-first);
}
