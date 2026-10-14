#include "SCCLIBPRIVATE.H"
struct __scc_file {
    FILE *next,*previous,*line_next,*line_previous;
    ScFile handle;
    unsigned int flags,kind,capabilities,logical_position,backend_position;
    unsigned char *buffer;
    size_t capacity,read_position,read_end,write_used;
    unsigned int push_saved_position;
    int alive,is_standard,mode,owned_buffer,started,direction,barrier;
    int position_known,eof,error,push_valid,push_value,push_saved_known,line_linked;
    int operation_error,last_read_eof;
    unsigned char single;
};
static FILE *__scc_files,*__scc_lines;
static FILE __scc_standard_files[3];
static unsigned int __scc_standard_seen;
static void __scc_standard_open(unsigned int index);
static int __scc_stdio_failed(FILE *stream) {
    return !stream || stream->operation_error;
}
FILE *__scc_standard(unsigned int index)
{
    if(index>=3)return NULL;
    if(!(__scc_standard_seen&(1u<<index)))__scc_standard_open(index);
    return __scc_standard_files+index;
}
static int __scc_stream_error(FILE *stream,int error)
{
    if(stream){
        stream->error=1;
        stream->operation_error=1;
    }
    errno=error;
    return EOF;
}
static int __scc_stream_valid(FILE *stream)
{
    if(!stream || !stream->alive){
        errno=EBADF;
        return 0;
    }
    return 1;
}
static void __scc_stream_link(FILE *stream)
{
    stream->previous=NULL;
    stream->next=__scc_files;
    if(stream->next)stream->next->previous=stream;
    __scc_files=stream;
}
static void __scc_line_remove(FILE *stream)
{
    if(!stream->line_linked)return;
    if(stream->line_previous)stream->line_previous->line_next=stream->line_next;
    else __scc_lines=stream->line_next;
    if(stream->line_next)stream->line_next->line_previous=stream->line_previous;
    stream->line_next=stream->line_previous=NULL;
    stream->line_linked=0;
}
static void __scc_line_update(FILE *stream)
{
    if(stream->mode!=_IOLBF || !stream->write_used){
        __scc_line_remove(stream);
        return;
    }
    if(stream->line_linked)return;
    stream->line_next=__scc_lines;
    stream->line_previous=NULL;
    if(stream->line_next)stream->line_next->line_previous=stream;
    __scc_lines=stream;
    stream->line_linked=1;
}
static void __scc_stream_unlink(FILE *stream)
{
    __scc_line_remove(stream);
    if(stream->previous)stream->previous->next=stream->next;
    else __scc_files=stream->next;
    if(stream->next)stream->next->previous=stream->previous;
    stream->next=stream->previous=NULL;
}
static ScStatus __scc_file_info(FILE *stream,ScFileInfo *info)
{
    memset(info,0,sizeof(*info));
    info->size=sizeof(*info);
    info->version=1;
    return ScFileQuery(stream->handle,info);
}
static void __scc_io_init(ScIoResult *result)
{
    memset(result,0,sizeof(*result));
    result->size=sizeof(*result);
    result->version=1;
}
static int __scc_file_setup(FILE *stream,ScFile handle,int standard)
{
    memset(stream,0,sizeof(*stream));
    stream->handle=handle;
    stream->is_standard=standard;
    ScFileInfo info;
    ScStatus status=__scc_file_info(stream,&info);
    if(status){
        __scc_from_status(status);
        return -1;
    }
    stream->flags=info.flags;
    stream->kind=info.endpoint_kind;
    stream->capabilities=(info.flags&SC32_FILE_STATE_STANDARD)?info.endpoint_capabilities:
    SC32_ENDPOINT_SEEK|SC32_ENDPOINT_TELL|SC32_ENDPOINT_FLUSH;
    stream->backend_position=stream->logical_position=info.position;
    stream->position_known=(stream->capabilities&SC32_ENDPOINT_TELL)!=0;
    stream->mode=stream->kind==SC32_ENDPOINT_TTY?_IOLBF:_IOFBF;
    stream->capacity=BUFSIZ;
    stream->barrier=1;
    stream->alive=1;
    __scc_stream_link(stream);
    return 0;
}
static int __scc_stdio_init(void)
{
    __scc_files=__scc_lines=NULL;
    memset(__scc_standard_files,0,sizeof(__scc_standard_files));
    __scc_standard_seen=0;
    /* init没有take副作用，R1最后注册失败不会先关掉调用者旧0/1/2。 */
    return 0;
}
static void __scc_standard_open(unsigned int index)
{
    __scc_standard_seen|=1u<<index;
    ScFile handle=0;
    ScStatus status=ScFileGetStandard(index,&handle);
    if(status){
        __scc_from_status(status);
        return;
    }
    if(__scc_file_setup(__scc_standard_files+index,handle,1)){
        int error=errno;
        ScFileClose(handle);
        errno=error;
        return;
    }
    if(index==2)__scc_standard_files[index].mode=_IONBF;
}
static unsigned int __scc_open_mode(const char *mode)
{
    if(!mode || !*mode)return 0;
    unsigned int flags=0;
    char first=*mode++;
    if(first=='r')flags=SC32_FILE_READ;
    else if(first=='w')flags=SC32_FILE_WRITE|SC32_FILE_CREATE|SC32_FILE_TRUNCATE;
    else if(first=='a')flags=SC32_FILE_WRITE|SC32_FILE_CREATE|SC32_FILE_APPEND;
    else return 0;
    int binary=0,update=0;
    while(*mode){
        if(*mode=='b' && !binary)binary=1;
        else if(*mode=='+' && !update)update=1;
        else return 0;
        mode++;
    }
    if(update)flags|=SC32_FILE_READ|SC32_FILE_WRITE;
    return flags;
}
FILE *fopen(const char *path,const char *mode)
{
    unsigned int flags=__scc_open_mode(mode);
    if(!path || !flags){
        errno=EINVAL;
        return NULL;
    }
    /* 控制分配在truncate之前；可延迟的buffer失败走无缓冲，不回滚已建文件。 */
    FILE *stream=malloc(sizeof(*stream));
    if(!stream)return NULL;
    ScFileOpenDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.path=path;
    desc.flags=flags;
    desc.create_mode=23;
    ScFile handle=0;
    ScStatus status=ScFileOpen(&desc,&handle);
    if(status){
        __scc_from_status(status);
        free(stream);
        return NULL;
    }
    if(__scc_file_setup(stream,handle,0)){
        int error=errno;
        ScFileClose(handle);
        free(stream);
        errno=error;
        return NULL;
    }
    return stream;
}
static void __scc_buffer(FILE *stream)
{
    if(stream->mode==_IONBF || stream->buffer)return;
    stream->buffer=malloc(stream->capacity);
    if(stream->buffer)stream->owned_buffer=1;
    else stream->mode=_IONBF;
}
static int __scc_wait_again(FILE *stream,ScStatus status)
{
    if(status!=SC32_E_AGAIN)return 0;
    if(stream->kind!=SC32_ENDPOINT_PIPE && stream->kind!=SC32_ENDPOINT_TTY && stream->kind!=SC32_ENDPOINT_SERIAL)return 0;
    unsigned int events=0;
    /* STREAM通知在kernel按owner/代数锁存；醒来重试真实端点，绝不猜EOF。 */
    ScStatus waited=ScSystemWait(SC32_EVENT_STREAM,SC32_INFINITE,&events);
    if(waited<0){
        __scc_from_status(waited);
        stream->error=stream->operation_error=1;
        return -1;
    }
    return 1;
}
static int __scc_drain(FILE *stream)
{
    size_t accepted=0;
    int failed=0;
    while(accepted<stream->write_used){
        size_t count=stream->write_used-accepted;
        if(count>65536u)count=65536u;
        ScIoResult result;
        __scc_io_init(&result);
        ScStatus status=ScFileWrite(stream->handle,stream->buffer+accepted,count,&result);
        if(result.transferred>count){
            __scc_stream_error(stream,EIO);
            failed=1;
            break;
        }
        accepted+=result.transferred;
        if(result.transferred){
            stream->backend_position=result.position;
            /* 标准stdout/stderr可能共享真实游标；每次prefix按端点位置
             * 加待写后缀同步，不能只修append而遗漏普通可定位alias。 */
            if(stream->position_known){
                size_t pending=stream->write_used-accepted;
                if(pending>SIZE_MAX-result.position)stream->position_known=0;
                else stream->logical_position=result.position+pending;
            }
        }
        int retry=__scc_wait_again(stream,status);
        if(retry>0)continue;
        if(retry<0){
            failed=1;
            break;
        }
        if(status){
            __scc_from_status(status);
            stream->error=stream->operation_error=1;
            failed=1;
            break;
        }
        if(!result.transferred){
            __scc_stream_error(stream,EIO);
            failed=1;
            break;
        }
    }
    /* 只撤确定成功的prefix，保留余字节；下次flush绝不再次append前缀。 */
    if(accepted){
        stream->write_used-=accepted;
        memmove(stream->buffer,stream->buffer+accepted,stream->write_used);
    }
    __scc_line_update(stream);
    return failed?EOF:0;
}
static int __scc_flush_output(FILE *stream)
{
    if(__scc_drain(stream))return EOF;
    /* 即使buffer为空也刷新真实后端；不拿close/reopen冒充持久flush。 */
    ScStatus status=ScFileFlush(stream->handle);
    if(status){
        __scc_from_status(status);
        stream->error=stream->operation_error=1;
        return EOF;
    }
    return 0;
}
int fflush(FILE *stream)
{
    if(!stream){
        int error=0;
        for(FILE *item=__scc_files;item;item=item->next){
            if(!(item->flags&SC32_FILE_WRITE) || item->direction==1)continue;
            if(__scc_flush_output(item)){
                if(!error)error=errno;
            }
            else item->barrier=1;
        }
        if(error){
            errno=error;
            return EOF;
        }
        return 0;
    }
    if(!__scc_stream_valid(stream))return EOF;
    if(!(stream->flags&SC32_FILE_WRITE) || stream->direction==1)return __scc_stream_error(stream,EINVAL);
    stream->started=1;
    if(__scc_flush_output(stream))return EOF;
    stream->barrier=1;
    return 0;
}
static int __scc_begin(FILE *stream,int write)
{
    if(!__scc_stream_valid(stream))return -1;
    stream->operation_error=0;
    if(!(stream->flags&(write?SC32_FILE_WRITE:SC32_FILE_READ)))return __scc_stream_error(stream,EBADF);
    if(stream->direction && stream->direction!=(write?2:1) && !stream->barrier && !(write && stream->last_read_eof))
    return __scc_stream_error(stream,EINVAL);
    stream->started=1;
    stream->direction=write?2:1;
    stream->barrier=0;
    return 0;
}
static void __scc_interactive_input(FILE *stream)
{
    if(stream->kind!=SC32_ENDPOINT_TTY && stream->kind!=SC32_ENDPOINT_SERIAL)return;
    int saved=errno;
    FILE *item=__scc_lines;
    /* 仅请求新交互输入时扫待写行缓冲链；缓冲内fgetc不扫所有FILE。 */
    while(item){
        FILE *next=item->line_next;
        __scc_drain(item);
        item=next;
    }
    errno=saved;
}
static int __scc_read_once(FILE *stream,void *data,size_t count,ScIoResult *result,ScStatus *status)
{
    __scc_interactive_input(stream);
    for(;;){
        __scc_io_init(result);
        *status=ScFileRead(stream->handle,data,count,result);
        if(result->transferred)return 0;
        int retry=__scc_wait_again(stream,*status);
        if(retry>0)continue;
        return retry<0?-1:0;
    }
}
static size_t __scc_fill(FILE *stream)
{
    if(stream->eof){
        stream->last_read_eof=1;
        return 0;
    }
    __scc_buffer(stream);
    unsigned char *buffer=stream->buffer?stream->buffer:&stream->single;
    size_t count=stream->buffer?stream->capacity:1u;
    if(count>65536u)count=65536u;
    ScIoResult result;
    ScStatus status;
    if(__scc_read_once(stream,buffer,count,&result,&status))return 0;
    if(result.transferred>count){
        __scc_stream_error(stream,EIO);
        return 0;
    }
    stream->read_position=0;
    stream->read_end=result.transferred;
    if(result.transferred){
        stream->backend_position=result.position;
        if(stream->position_known)stream->logical_position=result.position-result.transferred;
    }
    stream->last_read_eof=0;
    if(status){
        __scc_from_status(status);
        stream->error=stream->operation_error=1;
    }
    else if(!result.transferred)stream->eof=stream->last_read_eof=1;
    return result.transferred;
}
int fgetc(FILE *stream)
{
    if(__scc_begin(stream,0))return EOF;
    if(stream->push_valid){
        int value=stream->push_value;
        stream->push_valid=0;
        stream->logical_position=stream->push_saved_position;
        stream->position_known=stream->push_saved_known;
        stream->last_read_eof=0;
        return value;
    }
    if(stream->read_position==stream->read_end && !__scc_fill(stream))return EOF;
    unsigned char *buffer=stream->buffer?stream->buffer:&stream->single;
    int value=buffer[stream->read_position++];
    stream->last_read_eof=0;
    if(stream->position_known)stream->logical_position++;
    return value;
}
int getc(FILE *stream) {
    return fgetc(stream);
}
int getchar(void) {
    return fgetc(stdin);
}
size_t fread(void *data,size_t width,size_t count,FILE *stream)
{
    if(!width || !count)return 0;
    if(count>SIZE_MAX/width){
        __scc_stream_error(stream,EOVERFLOW);
        return 0;
    }
    if(__scc_begin(stream,0))return 0;
    unsigned char *out=data;
    size_t total=width*count,accepted=0;
    if(stream->push_valid){
        int value=fgetc(stream);
        if(value!=EOF)out[accepted++]=(unsigned char)value;
    }
    while(accepted<total){
        if(stream->read_position==stream->read_end && stream->mode==_IOFBF && total-accepted>=stream->capacity){
            if(stream->operation_error)break;
            if(stream->eof){
                stream->last_read_eof=1;
                break;
            }
            size_t portion=total-accepted;
            if(portion>65536u)portion=65536u;
            ScIoResult result;
            ScStatus status;
            /* 空缓冲的大块直达caller，避免多次4KiB门和第二份整块复制。 */
            if(__scc_read_once(stream,out+accepted,portion,&result,&status))break;
            if(result.transferred>portion){
                __scc_stream_error(stream,EIO);
                break;
            }
            accepted+=result.transferred;
            stream->last_read_eof=0;
            if(result.transferred){
                stream->backend_position=result.position;
                if(stream->position_known)stream->logical_position=result.position;
            }
            if(status){
                __scc_from_status(status);
                stream->error=stream->operation_error=1;
                break;
            }
            if(!result.transferred){
                stream->eof=stream->last_read_eof=1;
                break;
            }
            continue;
        }
        if(stream->read_position==stream->read_end){
            if(stream->operation_error || !__scc_fill(stream))break;
        }
        size_t portion=stream->read_end-stream->read_position;
        if(portion>total-accepted)portion=total-accepted;
        unsigned char *buffer=stream->buffer?stream->buffer:&stream->single;
        memcpy(out+accepted,buffer+stream->read_position,portion);
        stream->read_position+=portion;
        accepted+=portion;
        stream->last_read_eof=0;
        if(stream->position_known)stream->logical_position+=portion;
    }
    return accepted/width;
}
size_t fwrite(const void *data,size_t width,size_t count,FILE *stream)
{
    if(!width || !count)return 0;
    if(count>SIZE_MAX/width){
        __scc_stream_error(stream,EOVERFLOW);
        return 0;
    }
    if(__scc_begin(stream,1))return 0;
    const unsigned char *in=data;
    size_t total=width*count,accepted=0;
    while(accepted<total){
        size_t remaining=total-accepted;
        if(stream->mode==_IONBF || (!stream->write_used && stream->mode==_IOFBF && remaining>=stream->capacity)){
            size_t portion=remaining>65536u?65536u:remaining;
            ScIoResult result;
            __scc_io_init(&result);
            ScStatus status=ScFileWrite(stream->handle,in+accepted,portion,&result);
            if(result.transferred>portion){
                __scc_stream_error(stream,EIO);
                break;
            }
            accepted+=result.transferred;
            if(result.transferred){
                stream->backend_position=result.position;
                if(stream->position_known)stream->logical_position=result.position;
            }
            int retry=__scc_wait_again(stream,status);
            if(retry>0)continue;
            if(retry<0)break;
            if(status){
                __scc_from_status(status);
                stream->error=stream->operation_error=1;
                break;
            }
            if(!result.transferred){
                __scc_stream_error(stream,EIO);
                break;
            }
            continue;
        }
        __scc_buffer(stream);
        if(stream->mode==_IONBF)continue;
        if(stream->write_used==stream->capacity && __scc_drain(stream))break;
        size_t portion=stream->capacity-stream->write_used;
        if(portion>remaining)portion=remaining;
        int newline=0;
        if(stream->mode==_IOLBF){
            const unsigned char *line=memchr(in+accepted,'\n',portion);
            if(line){
                portion=(size_t)(line-(in+accepted))+1u;
                newline=1;
            }
        }
        memcpy(stream->buffer+stream->write_used,in+accepted,portion);
        stream->write_used+=portion;
        accepted+=portion;
        if(stream->position_known){
            if(portion>SIZE_MAX-stream->logical_position)stream->position_known=0;
            else stream->logical_position+=portion;
        }
        __scc_line_update(stream);
        if((newline || stream->write_used==stream->capacity) && __scc_drain(stream))break;
    }
    return accepted/width;
}
int fputc(int value,FILE *stream)
{
    unsigned char byte=(unsigned char)value;
    return fwrite(&byte,1,1,stream)==1 && !stream->operation_error?(int)byte:EOF;
}
int putc(int value,FILE *stream) {
    return fputc(value,stream);
}
int putchar(int value) {
    return fputc(value,stdout);
}
char *fgets(char *data,int capacity,FILE *stream)
{
    if(capacity<=0 || !data){
        errno=EINVAL;
        return NULL;
    }
    if(!__scc_stream_valid(stream))return NULL;
    if(capacity==1){
        if(!__scc_stream_valid(stream))return NULL;
        data[0]=0;
        return data;
    }
    int used=0,failed=0;
    while(used<capacity-1){
        int value=fgetc(stream);
        if(stream->operation_error)failed=1;
        if(value==EOF)break;
        data[used++]=(char)value;
        if(failed || value=='\n')break;
    }
    if(failed)return NULL;
    if(!used)return NULL;
    data[used]=0;
    return data;
}
int fputs(const char *text,FILE *stream)
{
    size_t bytes=strlen(text);
    if(!bytes)return __scc_stream_valid(stream)?0:EOF;
    return fwrite(text,1,bytes,stream)==bytes && !stream->operation_error?0:EOF;
}
int puts(const char *text)
{
    return fputs(text,stdout)==EOF || fputc('\n',stdout)==EOF?EOF:0;
}
int ungetc(int value,FILE *stream)
{
    if(value==EOF)return EOF;
    if(__scc_begin(stream,0))return EOF;
    if(stream->push_valid)return EOF;
    stream->push_saved_position=stream->logical_position;
    stream->push_saved_known=stream->position_known;
    if(stream->position_known){
        if(stream->logical_position)stream->logical_position--;
        else stream->position_known=0;
    }
    stream->push_value=(unsigned char)value;
    stream->push_valid=1;
    stream->eof=stream->last_read_eof=0;
    return stream->push_value;
}
int feof(FILE *stream) {
    return __scc_stream_valid(stream)?stream->eof:0;
}
int ferror(FILE *stream) {
    return __scc_stream_valid(stream)?stream->error:0;
}
void clearerr(FILE *stream) {
    if(__scc_stream_valid(stream))stream->eof=stream->error=stream->operation_error=0;
}
static int __scc_position(FILE *stream,unsigned int *position)
{
    if(!__scc_stream_valid(stream))return -1;
    if(!(stream->capabilities&SC32_ENDPOINT_TELL)){
        errno=ESPIPE;
        return -1;
    }
    if((stream->flags&SC32_FILE_APPEND) && stream->write_used && __scc_drain(stream))return -1;
    if(!stream->position_known){
        errno=EINVAL;
        return -1;
    }
    *position=stream->logical_position;
    return 0;
}
long ftell(FILE *stream)
{
    unsigned int position;
    if(__scc_position(stream,&position))return -1;
    if(position>LONG_MAX){
        errno=EOVERFLOW;
        return -1;
    }
    return (long)position;
}
static int __scc_set_position(FILE *stream,unsigned int position)
{
    ScFileInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    unsigned int first=position>(unsigned int)INT_MAX?(unsigned int)INT_MAX:position;
    ScStatus status=ScFileSeek(stream->handle,(int)first,SC32_SEEK_SET,&info);
    unsigned int remaining=position-first;
    while(!status && remaining){
        unsigned int part=remaining>(unsigned int)INT_MAX?(unsigned int)INT_MAX:remaining;
        status=ScFileSeek(stream->handle,(int)part,SC32_SEEK_CUR,&info);
        remaining-=part;
    }
    if(status){
        __scc_from_status(status);
        int error=errno;
        stream->read_position=stream->read_end=0;
        stream->push_valid=0;
        if(!__scc_file_info(stream,&info)){
            stream->backend_position=stream->logical_position=info.position;
            stream->position_known=1;
        }
        else stream->position_known=0;
        errno=error;
        return -1;
    }
    stream->logical_position=stream->backend_position=info.position;
    stream->position_known=1;
    stream->read_position=stream->read_end=0;
    stream->push_valid=0;
    stream->eof=stream->last_read_eof=0;
    stream->barrier=1;
    stream->direction=0;
    return 0;
}
int fseek(FILE *stream,long offset,int origin)
{
    if(!__scc_stream_valid(stream))return -1;
    if(origin!=SEEK_SET && origin!=SEEK_CUR && origin!=SEEK_END){
        errno=EINVAL;
        return -1;
    }
    if(!(stream->capabilities&SC32_ENDPOINT_SEEK)){
        errno=ESPIPE;
        return -1;
    }
    if((stream->flags&SC32_FILE_WRITE) && __scc_flush_output(stream))return -1;
    unsigned int base=0;
    if(origin==SEEK_CUR){
        if(__scc_position(stream,&base))return -1;
    }
    else if(origin==SEEK_END){
        ScFileInfo info;
        ScStatus status=__scc_file_info(stream,&info);
        if(status){
            __scc_from_status(status);
            return -1;
        }
        base=info.length;
    }
    unsigned int position;
    if(offset<0){
        unsigned int amount=0u-(unsigned int)offset;
        if(amount>base){
            errno=EINVAL;
            return -1;
        }
        position=base-amount;
    }
    else{
        if((unsigned long)offset>SIZE_MAX-base){
            errno=EOVERFLOW;
            return -1;
        }
        position=base+(unsigned int)offset;
    }
    stream->started=1;
    return __scc_set_position(stream,position);
}
void rewind(FILE *stream)
{
    if(!stream)return;
    (void)fseek(stream,0,SEEK_SET);
    stream->error=0;
}
int fgetpos(FILE *stream,fpos_t *position)
{
    unsigned int value;
    if(!position){
        errno=EINVAL;
        return -1;
    }
    if(__scc_position(stream,&value))return -1;
    position->position=value;
    position->valid=0x53434650u;
    return 0;
}
int fsetpos(FILE *stream,const fpos_t *position)
{
    if(!position || position->valid!=0x53434650u){
        errno=EINVAL;
        return -1;
    }
    if(!__scc_stream_valid(stream))return -1;
    if(!(stream->capabilities&SC32_ENDPOINT_SEEK)){
        errno=ESPIPE;
        return -1;
    }
    if((stream->flags&SC32_FILE_WRITE) && __scc_flush_output(stream))return -1;
    stream->started=1;
    return __scc_set_position(stream,position->position);
}
int setvbuf(FILE *stream,char *buffer,int mode,size_t capacity)
{
    if(!__scc_stream_valid(stream))return -1;
    if(stream->started || (mode!=_IOFBF && mode!=_IOLBF && mode!=_IONBF) || (mode!=_IONBF && !capacity)){
        errno=EINVAL;
        return -1;
    }
    unsigned char *replacement=(unsigned char *)buffer;
    int owned=0;
    if(mode!=_IONBF && !replacement){
        replacement=malloc(capacity);
        if(!replacement)return -1;
        owned=1;
    }
    if(stream->owned_buffer)free(stream->buffer);
    stream->buffer=mode==_IONBF?NULL:replacement;
    stream->capacity=mode==_IONBF?1u:capacity;
    stream->owned_buffer=owned;
    stream->mode=mode;
    return 0;
}
void setbuf(FILE *stream,char *buffer)
{
    (void)setvbuf(stream,buffer,buffer?_IOFBF:_IONBF,buffer?BUFSIZ:0);
}
int fclose(FILE *stream)
{
    if(!__scc_stream_valid(stream))return EOF;
    int error=0;
    if((stream->flags&SC32_FILE_WRITE) && __scc_flush_output(stream))error=errno;
    ScStatus status=ScFileClose(stream->handle);
    if(status && !error)error=__scc_from_status(status);
    /* 后端CLOSE最终失败也销毁alias；控制对象不能留下可复用的旧handle。 */
    __scc_stream_unlink(stream);
    stream->alive=0;
    stream->handle=0;
    if(stream->owned_buffer)free(stream->buffer);
    stream->buffer=NULL;
    stream->owned_buffer=0;
    if(!stream->is_standard)free(stream);
    if(error){
        errno=error;
        return EOF;
    }
    return 0;
}
void perror(const char *prefix)
{
    int saved=errno;
    if(prefix && *prefix){
        fputs(prefix,stderr);
        fputs(": ",stderr);
    }
    fputs(strerror(saved),stderr);
    fputc('\n',stderr);
    errno=saved;
}
int remove(const char *path)
{
    ScStatus status=ScPathRemove(path);
    if(status){
        __scc_from_status(status);
        return -1;
    }
    return 0;
}
int rename(const char *before,const char *after)
{
    ScStatus status=ScPathRename(before,after);
    if(status){
        __scc_from_status(status);
        return -1;
    }
    return 0;
}
static void __scc_early_write(const char *text)
{
    FILE *stream=__scc_standard(2);
    if(!stream->alive)return;
    size_t count=strlen(text);
    ScIoResult result;
    __scc_io_init(&result);
    (void)ScFileWrite(stream->handle,text,count>65536u?65536u:count,&result);
}
static void __scc_stdio_finish(void)
{
    int first=0;
    FILE *diagnostic=__scc_standard_files+2;
    /* stderr最后关，别的流失败仍可给一次固定诊断；不反复分配/格式化。 */
    while(__scc_files){
        FILE *stream=__scc_files;
        if(stream==diagnostic){
            stream=stream->next;
            if(!stream)break;
        }
        if(fclose(stream) && !first)first=errno;
    }
    if(first)__scc_early_write("SCCLIB: final I/O error\n");
    if(diagnostic->alive && fclose(diagnostic) && !first)first=errno;
    if(first)errno=first;
}
