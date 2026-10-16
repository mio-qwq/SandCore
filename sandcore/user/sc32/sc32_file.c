#include "SC32PRIVATE.H"
#include "SC32FILEWIRE.H"
#include "SC32CAPTUREWIRE.H"

#define SC32_REPLY_UNWRITTEN ((ScI32)0x80000000u)
static void sc32_file_reply(Sc32FileReply *reply)
{sc32_zero(reply,sizeof(*reply));reply->size=sizeof(*reply);reply->version=1;reply->raw=SC32_REPLY_UNWRITTEN;}
static int sc32_file_reply_valid(const Sc32FileReply *reply)
{return reply->size==sizeof(*reply) && reply->version==1 && reply->raw!=SC32_REPLY_UNWRITTEN;}
static ScI32 sc32_file_call(ScU32 selector,const void *request,Sc32FileReply *reply)
{
    for(;;){
        sc32_file_reply(reply);
        ScI32 status=sc32_call(SC32_WIRE_GATE,(ScI32)selector,(ScI32)request,(ScI32)reply,0,0,0);
        /* -10是FILE私有COW续进，并非公开非阻塞状态。每步先让出，
         * kernel不跨gate保存用户buffer；零已接受字节才可重放同请求。
         * STD管道-6保持非阻塞AGAIN，非零前缀从不重复写入。 */
        if(status==SC32_E_AGAIN && sc32_file_reply_valid(reply) && reply->raw==-10 && !reply->transferred){
            sc32_call(7,0,0,0,0,0,0);continue;
        }
        return status;
    }
}
static ScStatus sc32_file_status(ScI32 status,const Sc32FileReply *reply,ScU32 selector)
{
    ScU32 operation=SC32_OP_FILE_BASE+selector-SC32_F_OPEN;
    if(!status){if(sc32_file_reply_valid(reply))return 0;return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,status,operation);}
    ScI32 code=status<=SC32_E_INVALID && status>=SC32_E_FAILED?status:SC32_E_FAILED;
    ScU32 domain=sc32_file_reply_valid(reply) && (reply->flags&(SC32_FILE_STANDARD|SC32_FILE_NATIVE_ENDPOINT))?SC32_DOMAIN_LEGACY:SC32_DOMAIN_KERNEL;
    return sc32_fail(code,domain,sc32_file_reply_valid(reply)?reply->raw:status,operation);
}
static void sc32_file_information(ScFileInfo *out,const Sc32FileReply *reply)
{
    ScFileInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.handle=reply->handle;value.position=reply->position;value.length=reply->length;
    value.object_id=reply->object;value.revision=reply->revision;value.flags=reply->flags;
    value.endpoint_kind=reply->endpoint_kind;value.endpoint_capabilities=reply->endpoint_capabilities;sc32_copy(out,&value,sizeof(value));
}
static ScI32 sc32_file_handle(ScFile file,ScU32 value,ScU32 selector,Sc32FileReply *reply)
{
    Sc32FileHandleRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.handle=file;request.value=value;
    return sc32_file_call(selector,&request,reply);
}
ScStatus ScFileOpen(const ScFileOpenDesc *desc,ScFile *out)
{
    ScStatus checked=sc32_record_in(desc,sizeof(ScFileOpenDesc));if(checked<0)return sc32_invalid(checked,SC32_OP_FILE_BASE);
    checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,SC32_OP_FILE_BASE);
    if(desc->reserved[0] || desc->reserved[1] || desc->reserved[2])return sc32_invalid(SC32_E_INVALID,SC32_OP_FILE_BASE);
    checked=sc32_string(desc->path,127,0);if(checked<0)return sc32_invalid(checked,SC32_OP_FILE_BASE);
    Sc32FileOpenRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.path=(ScU32)desc->path;request.flags=desc->flags;request.create_mode=desc->create_mode;
    Sc32FileReply reply;sc32_file_reply(&reply);
    ScI32 raw=sc32_file_call(SC32_F_OPEN,&request,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_OPEN);if(status)return status;
    *out=reply.handle;return 0;
}
static ScStatus sc32_file_io(ScFile file,void *buffer,ScU32 bytes,ScIoResult *out,ScU32 selector)
{
    ScU32 operation=SC32_OP_FILE_BASE+selector-SC32_F_OPEN;
    ScStatus checked=sc32_record_out(out,sizeof(ScIoResult));if(checked<0)return sc32_invalid(checked,operation);
    if(bytes>SC32_FILE_IO_MAX)return sc32_invalid(SC32_E_RANGE,operation);
    checked=sc32_probe(buffer,bytes,selector==SC32_F_READ?SC32_WIRE_WRITE:SC32_WIRE_READ);
    if(checked<0)return sc32_invalid(checked,operation);
    if(sc32_overlap(out,out->size,buffer,bytes))return sc32_invalid(SC32_E_INVALID,operation);
    Sc32FileIoRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.handle=file;request.buffer=(ScU32)buffer;request.bytes=bytes;Sc32FileReply reply;sc32_file_reply(&reply);
    ScI32 raw=sc32_file_call(selector,&request,&reply);
    /* typed错误是否带实际前缀由kernel写回记录决定。前置拒绝不写记录，
     * raw哨兵保持；进入IO/增长AGAIN才可发表零或非零传输结果。 */
    if(sc32_file_reply_valid(&reply)){
        if(reply.transferred>bytes)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,raw,operation);
        ScIoResult value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
        value.transferred=reply.transferred;value.position=reply.position;value.length=reply.length;
        value.object_id=reply.object;value.revision=reply.revision;value.flags=reply.flags;sc32_copy(out,&value,sizeof(value));
    }
    return sc32_file_status(raw,&reply,selector);
}
ScStatus ScFileRead(ScFile file,void *out,ScU32 bytes,ScIoResult *result)
{return sc32_file_io(file,out,bytes,result,SC32_F_READ);}
ScStatus ScFileWrite(ScFile file,const void *data,ScU32 bytes,ScIoResult *result)
{return sc32_file_io(file,(void *)data,bytes,result,SC32_F_WRITE);}
ScStatus ScFileSeek(ScFile file,ScI32 offset,ScU32 whence,ScFileInfo *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_F_SEEK-SC32_F_OPEN;
    ScStatus checked=sc32_record_out(out,sizeof(ScFileInfo));if(checked<0)return sc32_invalid(checked,operation);
    if(whence>SC32_SEEK_END)return sc32_invalid(SC32_E_INVALID,operation);
    Sc32FileSeekRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.handle=file;request.whence=whence;request.offset=offset;Sc32FileReply reply;sc32_file_reply(&reply);
    ScI32 raw=sc32_file_call(SC32_F_SEEK,&request,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_SEEK);if(status)return status;
    sc32_file_information(out,&reply);return 0;
}
ScStatus ScFileTell(ScFile file,ScU32 *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_F_TELL-SC32_F_OPEN;
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,operation);
    Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,0,SC32_F_TELL,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_TELL);if(status)return status;*out=reply.position;return 0;
}
ScStatus ScFileFlush(ScFile file)
{Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,0,SC32_F_FLUSH,&reply);return sc32_file_status(raw,&reply,SC32_F_FLUSH);}
ScStatus ScFileClose(ScFile file)
{Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,0,SC32_F_CLOSE,&reply);return sc32_file_status(raw,&reply,SC32_F_CLOSE);}
ScStatus ScFileQuery(ScFile file,ScFileInfo *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_F_QUERY-SC32_F_OPEN;
    ScStatus checked=sc32_record_out(out,sizeof(ScFileInfo));if(checked<0)return sc32_invalid(checked,operation);
    Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,0,SC32_F_QUERY,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_QUERY);if(status)return status;sc32_file_information(out,&reply);return 0;
}
ScStatus ScFileTruncate(ScFile file,ScU32 bytes)
{Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,bytes,SC32_F_TRUNCATE,&reply);return sc32_file_status(raw,&reply,SC32_F_TRUNCATE);}
ScStatus ScFileDuplicate(ScFile file,ScFile *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_F_DUPLICATE-SC32_F_OPEN;
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,operation);
    Sc32FileReply reply;ScI32 raw=sc32_file_handle(file,0,SC32_F_DUPLICATE,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_DUPLICATE);if(status)return status;*out=reply.handle;return 0;
}
ScStatus ScFileGetStandard(ScU32 index,ScFile *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_F_STANDARD-SC32_F_OPEN;
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,operation);
    if(index>2)return sc32_invalid(SC32_E_INVALID,operation);
    Sc32FileReply reply;ScI32 raw=sc32_file_handle(0,index,SC32_F_STANDARD,&reply);
    ScStatus status=sc32_file_status(raw,&reply,SC32_F_STANDARD);if(status)return status;*out=reply.handle;return 0;
}
ScStatus ScDirectoryOpen(const char *path,ScDirectory *out)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_D_OPEN-SC32_F_OPEN;
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,operation);
    checked=sc32_string(path,127,0);if(checked<0)return sc32_invalid(checked,operation);
    Sc32FileOpenRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;request.path=(ScU32)path;
    Sc32DirectoryReply reply;sc32_zero(&reply,sizeof(reply));reply.size=sizeof(reply);reply.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_D_OPEN,(ScI32)&request,(ScI32)&reply,0,0,0);
    if(raw)return sc32_native(raw,operation);
    if(!reply.handle)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,raw,operation);
    *out=reply.handle;return 0;
}
ScStatus ScDirectoryNext(ScDirectory directory,ScDirectoryEntry *rows,ScU32 capacity,ScDirectoryPage *page)
{
    ScU32 operation=SC32_OP_FILE_BASE+SC32_D_NEXT-SC32_F_OPEN;
    ScStatus checked=sc32_record_out(page,sizeof(ScDirectoryPage));if(checked<0)return sc32_invalid(checked,operation);
    if(!capacity || capacity>SC32_DIRECTORY_BATCH_MAX)return sc32_invalid(SC32_E_RANGE,operation);
    checked=sc32_out(rows,capacity*sizeof(ScDirectoryEntry));if(checked<0)return sc32_invalid(checked,operation);
    if(sc32_overlap(page,page->size,rows,capacity*sizeof(ScDirectoryEntry)))return sc32_invalid(SC32_E_INVALID,operation);
    Sc32DirectoryNextRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;
    request.handle=directory;request.rows=(ScU32)rows;request.capacity=capacity;
    Sc32DirectoryReply reply;sc32_zero(&reply,sizeof(reply));reply.size=sizeof(reply);reply.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_D_NEXT,(ScI32)&request,(ScI32)&reply,0,0,0);
    if(raw)return sc32_native(raw,operation);
    if(reply.count>capacity)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,raw,operation);
    ScDirectoryPage value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.handle=reply.handle;value.count=reply.count;value.cursor=reply.cursor;value.generation=reply.epoch;value.flags=reply.flags;
    sc32_copy(page,&value,sizeof(value));return 0;
}
ScStatus ScDirectoryClose(ScDirectory directory)
{
    Sc32FileHandleRequest request;sc32_zero(&request,sizeof(request));request.size=sizeof(request);request.version=1;request.handle=directory;
    Sc32DirectoryReply reply;sc32_zero(&reply,sizeof(reply));reply.size=sizeof(reply);reply.version=1;
    ScI32 raw=sc32_call(SC32_WIRE_GATE,SC32_D_CLOSE,(ScI32)&request,(ScI32)&reply,0,0,0);
    return sc32_native(raw,SC32_OP_FILE_BASE+SC32_D_CLOSE-SC32_F_OPEN);
}
