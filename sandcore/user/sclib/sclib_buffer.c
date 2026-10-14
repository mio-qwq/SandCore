#include "SCLIBPRIVATE.H"

static void sl_buffer_dispose(SlBase *base,int quiet)
{ScBuffer *buffer=(ScBuffer *)base;
    (void)quiet;
    if(!buffer->borrowed)free(buffer->data);
    buffer->data=0;
    buffer->bytes=buffer->capacity=0;}
ScStatus ScBufferCreate(ScU32 initial_capacity,ScBuffer **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScBuffer *buffer=sl_object(SL_BUFFER,sizeof(*buffer),sl_buffer_dispose);
    if(!buffer)return SC_UTIL_E_MEMORY;
    if(initial_capacity){buffer->data=malloc(initial_capacity);
        if(!buffer->data){ScBufferRelease(buffer);
            return SC_UTIL_E_MEMORY;}}
    buffer->capacity=initial_capacity;
    buffer->generation=1;
    *out=buffer;
    return 0;
}
ScStatus ScBufferBorrow(const void *bytes,ScU32 count,ScBuffer **out)
{
    if(!out || (count && !bytes))return SC_UTIL_E_INVALID;
    ScBuffer *buffer=sl_object(SL_BUFFER,sizeof(*buffer),sl_buffer_dispose);
    if(!buffer)return SC_UTIL_E_MEMORY;
    buffer->data=(unsigned char *)bytes;
    buffer->bytes=buffer->capacity=count;
    buffer->borrowed=1;
    buffer->generation=1;
    *out=buffer;
    return 0;
}
ScStatus ScBufferRetain(ScBuffer *buffer){return sl_retain(buffer,SL_BUFFER);}
void ScBufferRelease(ScBuffer *buffer){sl_release(buffer,SL_BUFFER);}
ScStatus ScBufferQuery(const ScBuffer *buffer,ScContainerInfo *out)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,buffer->bytes,buffer->capacity,1,buffer->generation,buffer->borrowed?1u:0u);
    return 0;
}
ScStatus ScBufferView(const ScBuffer *buffer,ScView *out)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_view(out,buffer->data,buffer->bytes,buffer->generation);
    return 0;
}
ScStatus ScBufferReserve(ScBuffer *buffer,ScU32 capacity)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(status)return status;
    status=sl_mutation(&buffer->base,buffer->generation);
    if(status)return status;
    if(capacity<=buffer->capacity)return 0;
    unsigned char *fresh=buffer->borrowed?malloc(capacity):realloc(buffer->data,capacity);
    if(!fresh)return SC_UTIL_E_MEMORY;
    if(buffer->borrowed && buffer->bytes)memcpy(fresh,buffer->data,buffer->bytes);
    buffer->data=fresh;
    buffer->capacity=capacity;
    buffer->borrowed=0;
    buffer->generation++;
    return 0;
}
static ScStatus sl_buffer_change(ScBuffer *buffer,ScU32 offset,const void *bytes,ScU32 count,int resize)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(status)return status;
    status=sl_mutation(&buffer->base,buffer->generation);
    if(status)return status;
    if(!resize && count && !bytes)return SC_UTIL_E_INVALID;
    if(count>0xFFFFFFFFu-offset)return SC_UTIL_E_RANGE;
    ScU32 wanted=resize?offset:offset+count;
    if(!resize && !count)return 0;
    if(!resize && wanted<buffer->bytes)wanted=buffer->bytes;
    if(resize && wanted==buffer->bytes)return 0;
    if(buffer->borrowed || wanted>buffer->capacity){
        ScU32 capacity=sl_capacity(buffer->borrowed?0:buffer->capacity,wanted,1);
        unsigned char *fresh=capacity?malloc(capacity):0;
        if(capacity && !fresh)return SC_UTIL_E_MEMORY;
        ScU32 kept=buffer->bytes<wanted?buffer->bytes:wanted;
        if(kept)memcpy(fresh,buffer->data,kept);
        if(wanted>kept)memset(fresh+kept,0,wanted-kept);
        if(!resize && count)memcpy(fresh+offset,bytes,count);
        if(!buffer->borrowed)free(buffer->data);
        buffer->data=fresh;
        buffer->capacity=capacity;
        buffer->borrowed=0;
    }else{
        void *saved=0;
        const void *source=bytes;
        if(!resize && sl_alias(bytes,count,buffer->data,buffer->capacity)){
            saved=malloc(count);
            if(!saved)return SC_UTIL_E_MEMORY;
            memcpy(saved,bytes,count);
            source=saved;
        }
        if(wanted>buffer->bytes)memset(buffer->data+buffer->bytes,0,wanted-buffer->bytes);
        if(!resize && count)memmove(buffer->data+offset,source,count);
        free(saved);
    }
    buffer->bytes=wanted;
    buffer->generation++;
    return 0;
}
ScStatus ScBufferResize(ScBuffer *buffer,ScU32 bytes){return sl_buffer_change(buffer,bytes,0,0,1);}
ScStatus ScBufferWrite(ScBuffer *buffer,ScU32 offset,const void *bytes,ScU32 count){return sl_buffer_change(buffer,offset,bytes,count,0);}
ScStatus ScBufferAppend(ScBuffer *buffer,const void *bytes,ScU32 count)
{ScStatus status=sl_valid(buffer,SL_BUFFER);
    return status?status:ScBufferWrite(buffer,buffer->bytes,bytes,count);}
ScStatus ScBufferConsume(ScBuffer *buffer,ScU32 count)
{
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(status)return status;
    status=sl_mutation(&buffer->base,buffer->generation);
    if(status)return status;
    if(count>buffer->bytes)return SC_UTIL_E_RANGE;
    if(!count)return 0;
    if(buffer->borrowed){
        ScU32 kept=buffer->bytes-count;
        unsigned char *fresh=kept?malloc(kept):0;
        if(kept && !fresh)return SC_UTIL_E_MEMORY;
        if(kept)memcpy(fresh,buffer->data+count,kept);
        buffer->data=fresh;
        buffer->capacity=kept;
        buffer->borrowed=0;
    }else memmove(buffer->data,buffer->data+count,buffer->bytes-count);
    buffer->bytes-=count;
    buffer->generation++;
    return 0;
}
ScStatus ScBufferClear(ScBuffer *buffer){return ScBufferResize(buffer,0);}
