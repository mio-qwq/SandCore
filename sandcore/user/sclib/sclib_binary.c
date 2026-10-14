#include "SCLIBPRIVATE.H"

typedef char sl_reader_32[(sizeof(ScBinaryReader)==32)?1:-1];
static ScStatus sl_reader_range(const ScBinaryReader *reader,ScU32 bytes)
{
    ScStatus status=sl_input(reader,sizeof(*reader));
    if(status)return status;
    if(reader->reserved[0] || reader->reserved[1] || reader->reserved[2] || (reader->bytes && !reader->data) || reader->position>reader->bytes)return SC_UTIL_E_INVALID;
    return bytes>reader->bytes-reader->position?SC_UTIL_E_RANGE:0;
}
ScStatus ScBinaryReaderInit(ScBinaryReader *reader,const void *bytes,ScU32 count)
{
    if(!reader || (count && !bytes))return SC_UTIL_E_INVALID;
    ScBinaryReader value={32,1,bytes,count,0,{0,0,0}};
    memcpy(reader,&value,sizeof(value));
    return 0;
}
ScStatus ScBinaryRead(ScBinaryReader *reader,void *out,ScU32 bytes)
{
    ScStatus status=sl_reader_range(reader,bytes);
    if(status || (bytes && !out))return status?status:SC_UTIL_E_INVALID;
    if(bytes)memmove(out,(const unsigned char *)reader->data+reader->position,bytes);
    reader->position+=bytes;
    return 0;
}
static ScStatus sl_binary_integer(ScBinaryReader *reader,ScU32 bytes,ScU32 *out)
{
    ScStatus status=sl_reader_range(reader,bytes);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    const unsigned char *p=(const unsigned char *)reader->data+reader->position;
    ScU32 value=0;
    for(ScU32 i=0;i<bytes;i++)value|=(ScU32)p[i]<<(i*8u);
    *out=value;
    reader->position+=bytes;
    return 0;
}
ScStatus ScBinaryReadU8(ScBinaryReader *reader,ScU32 *out){return sl_binary_integer(reader,1,out);}
ScStatus ScBinaryReadU16LE(ScBinaryReader *reader,ScU32 *out){return sl_binary_integer(reader,2,out);}
ScStatus ScBinaryReadU32LE(ScBinaryReader *reader,ScU32 *out){return sl_binary_integer(reader,4,out);}
ScStatus ScBinaryReadCounterLE(ScBinaryReader *reader,ScWideCounter *out)
{
    ScStatus status=sl_reader_range(reader,8);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    const unsigned char *p=(const unsigned char *)reader->data+reader->position;
    ScWideCounter value={0,0};
    for(ScU32 i=0;i<4u;i++){value.lo|=(ScU32)p[i]<<(i*8u);
        value.hi|=(ScU32)p[i+4u]<<(i*8u);}
    memcpy(out,&value,sizeof(value));
    reader->position+=8u;
    return 0;
}
static ScStatus sl_var_read(const ScBinaryReader *reader,ScU32 *value,ScU32 *length)
{
    ScStatus status=sl_reader_range(reader,0);
    if(status)return status;
    const unsigned char *p=reader->data;
    ScU32 result=0;
    for(ScU32 i=0;i<5u;i++){
        if(i>=reader->bytes-reader->position)return SC_UTIL_E_RANGE;
        ScU32 byte=p[reader->position+i];
        if(i==4u && (byte&0xF0u))return SC_UTIL_E_SYNTAX;
        result|=(byte&127u)<<(i*7u);
        if(!(byte&128u)){if(i && !(byte&127u))return SC_UTIL_E_SYNTAX;
            *value=result;
            *length=i+1u;
            return 0;}
    }
    return SC_UTIL_E_SYNTAX;
}
ScStatus ScBinaryReadVarU32(ScBinaryReader *reader,ScU32 *out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScU32 value,length;
    ScStatus status=sl_var_read(reader,&value,&length);
    if(status)return status;
    *out=value;
    reader->position+=length;
    return 0;
}
ScStatus ScBinaryReadUtf8(ScBinaryReader *reader,ScView *out)
{
    ScStatus status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScU32 bytes,length;
    status=sl_var_read(reader,&bytes,&length);
    if(status)return status;
    if(bytes>reader->bytes-reader->position-length)return SC_UTIL_E_RANGE;
    const unsigned char *p=(const unsigned char *)reader->data+reader->position+length;
    status=ScUtf8Validate(p,bytes,0);
    if(status)return status;
    sl_view(out,p,bytes,0);
    reader->position+=length+bytes;
    return 0;
}
ScStatus ScBinarySkip(ScBinaryReader *reader,ScU32 bytes)
{ScStatus status=sl_reader_range(reader,bytes);
    if(status)return status;
    reader->position+=bytes;
    return 0;}
static ScStatus sl_binary_write(ScBuffer *buffer,ScU32 value,ScU32 bytes)
{
    unsigned char output[4];
    for(ScU32 i=0;i<bytes;i++)output[i]=(unsigned char)(value>>(i*8u));
    return ScBufferAppend(buffer,output,bytes);
}
ScStatus ScBinaryWriteU8(ScBuffer *buffer,ScU32 value)
{return value>255u?SC_UTIL_E_RANGE:sl_binary_write(buffer,value,1);}
ScStatus ScBinaryWriteU16LE(ScBuffer *buffer,ScU32 value)
{return value>65535u?SC_UTIL_E_RANGE:sl_binary_write(buffer,value,2);}
ScStatus ScBinaryWriteU32LE(ScBuffer *buffer,ScU32 value){return sl_binary_write(buffer,value,4);}
ScStatus ScBinaryWriteCounterLE(ScBuffer *buffer,const ScWideCounter *value)
{
    if(!value)return SC_UTIL_E_INVALID;
    unsigned char output[8];
    for(ScU32 i=0;i<4u;i++){output[i]=(unsigned char)(value->lo>>(i*8u));
        output[i+4u]=(unsigned char)(value->hi>>(i*8u));}
    return ScBufferAppend(buffer,output,8);
}
static ScU32 sl_var_bytes(ScU32 value,unsigned char output[5])
{
    ScU32 count=0;
    do{ScU32 byte=value&127u;
        value>>=7;
        output[count++]=(unsigned char)(byte|(value?128u:0u));}while(value);
    return count;
}
ScStatus ScBinaryWriteVarU32(ScBuffer *buffer,ScU32 value)
{unsigned char output[5];
    ScU32 bytes=sl_var_bytes(value,output);
    return ScBufferAppend(buffer,output,bytes);}
ScStatus ScBinaryWriteUtf8(ScBuffer *buffer,const void *utf8,ScU32 bytes)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(status)return status;
    status=ScUtf8Validate(utf8,bytes,0);
    if(status)return status;
    unsigned char prefix[5];
    ScU32 length=sl_var_bytes(bytes,prefix);
    if(bytes>0xFFFFFFFFu-length || length+bytes>0xFFFFFFFFu-buffer->bytes)return SC_UTIL_E_RANGE;
    /* 一个完整候选记录再append，不能先发布长度后因为正文OOM留下
     * 一个Reader永远读不完的半条记录；alias输入也仍由旧buffer持有。 */
    unsigned char *record=malloc(length+bytes);
    if(!record)return SC_UTIL_E_MEMORY;
    memcpy(record,prefix,length);
    if(bytes)memcpy(record+length,utf8,bytes);
    status=ScBufferAppend(buffer,record,length+bytes);
    free(record);
    return status;
}
