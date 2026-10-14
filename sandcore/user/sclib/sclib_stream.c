#include "SCLIBPRIVATE.H"

struct ScStream {
    SlBase base;
    ScU32 kind,flags,position;
    void *context;
    ScBuffer *buffer;
    FILE *file;
    ScStreamReadProc read;
    ScStreamWriteProc write;
    ScStreamSeekProc seek;
    ScStreamFlushProc flush;
    ScStreamCloseProc close;
    ScWideCounter bytes_read,bytes_written;
};
static void sl_stream_dispose(SlBase *base,int quiet)
{
    ScStream *stream=(ScStream *)base;
    if(!quiet){
        if(stream->kind==0 && (stream->flags&SC_STREAM_OWN_CONTEXT) && stream->close)stream->close(stream->context);
        if(stream->kind==2 && (stream->flags&SC_STREAM_OWN_FILE))fclose(stream->file);
    }
    if(stream->buffer)ScBufferRelease(stream->buffer);
    stream->buffer=0;
    stream->file=0;
    stream->context=0;
}
ScStatus ScStreamCreate(const ScStreamDesc *desc,ScStream **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if((desc->flags&~15u) || ((desc->flags&SC_STREAM_READ) && !desc->read)
        || ((desc->flags&SC_STREAM_WRITE) && !desc->write) || ((desc->flags&SC_STREAM_SEEK) && !desc->seek)
        || ((desc->flags&SC_STREAM_OWN_CONTEXT) && !desc->close) || desc->reserved[0] || desc->reserved[1] || desc->reserved[2])return SC_UTIL_E_INVALID;
    ScStream *stream=sl_object(SL_STREAM,sizeof(*stream),sl_stream_dispose);
    if(!stream)return SC_UTIL_E_MEMORY;
    status=sl_pin((const void *)desc->read);
    if(!status)status=sl_pin((const void *)desc->write);
    if(!status)status=sl_pin((const void *)desc->seek);
    if(!status)status=sl_pin((const void *)desc->flush);
    if(!status)status=sl_pin((const void *)desc->close);
    if(status){ScStreamRelease(stream);
        return status;}
    stream->flags=desc->flags;
    stream->context=desc->context;
    stream->read=desc->read;
    stream->write=desc->write;
    stream->seek=desc->seek;
    stream->flush=desc->flush;
    stream->close=desc->close;
    *out=stream;
    return 0;
}
ScStatus ScStreamFromBuffer(ScBuffer *buffer,ScU32 flags,ScStream **out)
{
    if(!out || (flags&~7u))return SC_UTIL_E_INVALID;
    ScStatus status=sl_valid(buffer,SL_BUFFER);
    if(status)return status;
    ScStream *stream=sl_object(SL_STREAM,sizeof(*stream),sl_stream_dispose);
    if(!stream)return SC_UTIL_E_MEMORY;
    status=ScBufferRetain(buffer);
    if(status){ScStreamRelease(stream);
        return status;}
    stream->kind=1;
    stream->flags=flags;
    stream->buffer=buffer;
    *out=stream;
    return 0;
}
ScStatus ScStreamFromFile(FILE *file,ScU32 flags,ScStream **out)
{
    if(!file || !out || (flags&~23u))return SC_UTIL_E_INVALID;
    ScU32 position=0;
    if(flags&SC_STREAM_SEEK){
        long actual=ftell(file);
        if(actual<0)return errno==ESPIPE?SC_UTIL_E_NOT_SUPPORTED:SC_UTIL_E_IO;
        position=(ScU32)actual;
    }
    ScStream *stream=sl_object(SL_STREAM,sizeof(*stream),sl_stream_dispose);
    if(!stream)return SC_UTIL_E_MEMORY;
    stream->kind=2;
    stream->flags=flags;
    stream->file=file;
    stream->position=position;
    *out=stream;
    return 0;
}
ScStatus ScStreamRetain(ScStream *stream){return sl_retain(stream,SL_STREAM);}
void ScStreamRelease(ScStream *stream){sl_release(stream,SL_STREAM);}
static ScStatus sl_stream_io(ScStream *stream,void *data,ScU32 bytes,ScU32 *transferred,int write)
{
    ScStatus status=sl_enter(stream,SL_STREAM);
    if(status)return status;
    if(!transferred || (bytes && !data)){sl_leave(stream);
        return SC_UTIL_E_INVALID;}
    if(stream->base.mutation){sl_leave(stream);
        return SC_UTIL_E_BUSY;}
    if(!(stream->flags&(write?SC_STREAM_WRITE:SC_STREAM_READ))){sl_leave(stream);
        return SC_UTIL_E_NOT_SUPPORTED;}
    if(bytes>0xFFFFFFFFu-stream->position){sl_leave(stream);
        return SC_UTIL_E_RANGE;}
    if(!bytes){*transferred=0;
        sl_leave(stream);
        return 0;}
    ScU32 done=0;
    stream->base.mutation=1;
    if(stream->kind==0){
        status=write?stream->write(stream->context,data,bytes,&done):stream->read(stream->context,data,bytes,&done);
        if(done>bytes){done=0;
            status=SC_UTIL_E_IO;}
        if(!status && !done)status=SC_UTIL_E_AGAIN;
    }else if(stream->kind==1){
        if(write){status=ScBufferWrite(stream->buffer,stream->position,data,bytes);
            if(!status)done=bytes;}
        else if(stream->position>=stream->buffer->bytes)status=SC_UTIL_END;
        else{
            done=stream->buffer->bytes-stream->position;
            if(done>bytes)done=bytes;
            memmove(data,stream->buffer->data+stream->position,done);
        }
    }else{
        errno=0;
        done=write?fwrite(data,1,bytes,stream->file):fread(data,1,bytes,stream->file);
        if(ferror(stream->file))status=errno==EAGAIN?SC_UTIL_E_AGAIN:SC_UTIL_E_IO;
        else if(!write && !done && feof(stream->file))status=SC_UTIL_END;
        else if(done<bytes && write)status=SC_UTIL_E_IO;
    }
    if(stream->kind==2 && (stream->flags&SC_STREAM_SEEK)){
        long actual=ftell(stream->file);
        if(actual<0){if(!status)status=SC_UTIL_E_IO;}else stream->position=(ScU32)actual;
    }else stream->position+=done;
    sl_counter_add(write?&stream->bytes_written:&stream->bytes_read,done);
    stream->base.mutation=0;
    *transferred=done;
    sl_leave(stream);
    return status;
}
ScStatus ScStreamRead(ScStream *stream,void *out,ScU32 bytes,ScU32 *transferred){return sl_stream_io(stream,out,bytes,transferred,0);}
ScStatus ScStreamWrite(ScStream *stream,const void *bytes,ScU32 count,ScU32 *transferred){return sl_stream_io(stream,(void *)bytes,count,transferred,1);}
ScStatus ScStreamSeek(ScStream *stream,ScI32 offset,ScU32 whence,ScU32 *position)
{
    ScStatus status=sl_enter(stream,SL_STREAM);
    if(status)return status;
    if(!position || whence>2u){sl_leave(stream);
        return SC_UTIL_E_INVALID;}
    if(stream->base.mutation){sl_leave(stream);
        return SC_UTIL_E_BUSY;}
    if(!(stream->flags&SC_STREAM_SEEK)){sl_leave(stream);
        return SC_UTIL_E_NOT_SUPPORTED;}
    ScU32 value=0;
    stream->base.mutation=1;
    if(stream->kind==0)status=stream->seek(stream->context,offset,whence,&value);
    else if(stream->kind==1){
        ScU32 base=whence==0?0:whence==1?stream->position:stream->buffer->bytes;
        ScU32 magnitude=offset<0?0u-(ScU32)offset:(ScU32)offset;
        if((offset<0 && magnitude>base) || (offset>=0 && magnitude>0xFFFFFFFFu-base))status=SC_UTIL_E_RANGE;
        else value=offset<0?base-magnitude:base+magnitude;
    }else{
        if(fseek(stream->file,(long)offset,(int)whence))status=SC_UTIL_E_IO;
        else{long actual=ftell(stream->file);
            if(actual<0)status=SC_UTIL_E_IO;
            else value=(ScU32)actual;}
    }
    stream->base.mutation=0;
    if(!status){stream->position=value;
        *position=value;}
    sl_leave(stream);
    return status;
}
ScStatus ScStreamFlush(ScStream *stream)
{
    ScStatus status=sl_enter(stream,SL_STREAM);
    if(status)return status;
    if(stream->base.mutation){sl_leave(stream);
        return SC_UTIL_E_BUSY;}
    stream->base.mutation=1;
    if(stream->kind==0 && stream->flush)status=stream->flush(stream->context);
    else if(stream->kind==2 && fflush(stream->file))status=SC_UTIL_E_IO;
    stream->base.mutation=0;
    sl_leave(stream);
    return status;
}
ScStatus ScStreamQuery(const ScStream *stream,ScStreamInfo *out)
{
    ScStatus status=sl_valid(stream,SL_STREAM);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScStreamInfo value={32,1,stream->flags,stream->position,stream->bytes_read.lo,stream->bytes_read.hi,stream->bytes_written.lo,stream->bytes_written.hi};
    memcpy(out,&value,sizeof(value));
    return 0;
}
