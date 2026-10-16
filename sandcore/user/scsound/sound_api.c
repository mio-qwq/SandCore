#include "SOUNDPRIVATE.H"

static ScSoundFile *snd_files;
static ScSoundFile *snd_garbage;
static void snd_collect(void)
{
    /* 每次公开操作至多推进一个内部失败候选的关闭，不把Pending
     * 对象遗失，也不在GUI调用者里轮询直到磁盘/owner资源释放。 */
    if(snd_garbage)ScSoundClose(snd_garbage);
}
static void snd_close_internal(ScSoundFile *file)
{
    ScStatus status=ScSoundClose(file);
    if(status && !file->garbage){file->garbage=1;file->garbage_next=snd_garbage;snd_garbage=file;}
}
static int snd_valid(const ScSoundFile *file)
{for(ScSoundFile *p=snd_files;p;p=p->next)if(p==file)return 1;return 0;}
static void snd_init(void *p,ScU32 bytes)
{memset(p,0,bytes);((ScU32 *)p)[0]=bytes;((ScU32 *)p)[1]=1;}
static ScStatus snd_error(ScStatus cause,ScStatus fallback)
{
    if(cause==SC32_E_MEMORY)return SC_SOUND_E_MEMORY;
    if(cause==SC32_E_ACCESS)return SC_SOUND_E_ACCESS;
    if(cause==SC32_E_STALE)return SC_SOUND_E_STALE;
    if(cause==SC32_E_NOT_SUPPORTED)return SC_SOUND_E_NOT_SUPPORTED;
    if(cause==SC32_E_BUSY || cause==SC32_E_AGAIN)return SC_SOUND_E_BUSY;
    if(cause==SC32_E_RANGE)return SC_SOUND_E_RANGE;
    return fallback;
}
static ScStatus snd_source_close(ScSoundFile *file)
{
    if(file->file){ScStatus status=ScFileClose(file->file);
        if(status==SC32_E_AGAIN || status==SC32_E_BUSY || status==SC32_PENDING)return SC32_PENDING;
        if(status<0 && status!=SC32_E_STALE)return status;
        file->file=0;}
    if(file->lease.lease){
        ScLeaseControl control;ScLeaseInfo result;snd_init(&control,sizeof(control));snd_init(&result,sizeof(result));
        control.lease=file->lease.lease;control.generation=file->lease.generation;
        control.operation_sequence=file->lease.operation_sequence;
        ScStatus status=ScFileLeaseRelease(&control,&result);
        if(result.lease==file->lease.lease)file->lease=result;
        if(status==SC32_PENDING || status==SC32_E_BUSY || status==SC32_E_AGAIN)return SC32_PENDING;
        if(status<0 && status!=SC32_E_STALE)return status;
        file->lease.lease=0;
    }
    return 0;
}
static void snd_unlink(ScSoundFile *file)
{
    ScSoundFile **slot=&snd_files;while(*slot && *slot!=file)slot=&(*slot)->next;
    if(*slot)*slot=file->next;
    slot=&snd_garbage;while(*slot && *slot!=file)slot=&(*slot)->garbage_next;
    if(*slot)*slot=file->garbage_next;
    free(file->decoded.scratch);free(file);
}
static void snd_abandon(ScSoundFile *file)
{
    /* 失败对象没有交给调用者。先挂入本库链以便fini/退出仍有明确
     * 资源归属，正常READ pin没有发布事务；不在析构中无界等待。 */
    file->next=snd_files;snd_files=file;
    snd_close_internal(file);
}
ScStatus ScSoundOpen(const ScSoundOpenDesc *desc,ScSoundFile **out)
{
    snd_collect();
    if(!desc || !out || desc->size!=sizeof(*desc))return SC_SOUND_E_INVALID;
    if(desc->version!=1)return SC_SOUND_E_VERSION;
    if(!desc->path || desc->flags || desc->reserved[0] || desc->reserved[1]
        || desc->reserved[2])return SC_SOUND_E_INVALID;
    ScSoundFile *file=calloc(1,sizeof(*file));if(!file)return SC_SOUND_E_MEMORY;
    ScU32 length=0;ScStatus status=ScPathResolve(desc->path,file->path,sizeof(file->path),&length);
    if(status<0){free(file);return snd_error(status,SC_SOUND_E_INVALID);}
    ScPathInfo metadata;snd_init(&metadata,sizeof(metadata));status=ScPathQuery(file->path,&metadata);
    if(status<0){free(file);return snd_error(status,SC_SOUND_E_IO);}
    if(metadata.kind!=SC32_PATH_FILE){free(file);return SC_SOUND_E_FORMAT;}
    ScLeaseOpenDesc pin;snd_init(&pin,sizeof(pin));snd_init(&file->lease,sizeof(file->lease));
    pin.path=file->path;pin.object_kind=SC32_PATH_FILE;pin.lease_kind=SC32_LEASE_READ_STABLE;
    pin.expected_revision=metadata.generation;pin.open_flags=SC32_FILE_READ;
    status=ScFileLeaseOpenPinned(&pin,&file->lease);
    if(status<0){free(file);return snd_error(status,SC_SOUND_E_IO);}
    file->file=file->lease.file;file->source_revision=file->lease.revision;
    ScFileInfo info;snd_init(&info,sizeof(info));status=ScFileQuery(file->file,&info);
    if(status<0){snd_abandon(file);return snd_error(status,SC_SOUND_E_IO);}
    file->file_bytes=info.length;file->decoded.fd=file->file;file->decoded.size=info.length;
    file->decoded.scratch=malloc(16384);
    if(!file->decoded.scratch){snd_abandon(file);return SC_SOUND_E_MEMORY;}
    /* 唯一源先固定再解析，header与解码回调共用该真实FILE/版本，
     * 没有stat后重新打开的换源窗口；候选失败不碰旧播放对象。 */
    snd_backend_cause=0;
    int parsed=sc_audio_file_open(&file->decoded,file->path);
    if(parsed<0){ScStatus cause=snd_backend_cause;snd_abandon(file);return snd_error(cause,SC_SOUND_E_FORMAT);}
    if(desc->initial_seconds){
        if(!file->decoded.rate || desc->initial_seconds>0xffffffffu/(ScU32)file->decoded.rate){snd_abandon(file);return SC_SOUND_E_RANGE;}
        snd_backend_cause=0;
        int moved=sc_audio_file_seek(&file->decoded,desc->initial_seconds);
        if(moved<0 || snd_backend_cause<0){ScStatus cause=snd_backend_cause;snd_abandon(file);return snd_error(cause,SC_SOUND_E_RANGE);}
    }
    file->next=snd_files;snd_files=file;*out=file;return 0;
}
ScStatus ScSoundRead(ScSoundFile *file,void *stereo,ScU32 frames,ScU32 *out_frames)
{
    snd_collect();
    if(!snd_valid(file) || !stereo || !out_frames)return SC_SOUND_E_INVALID;
    if(!frames || frames>2048)return SC_SOUND_E_RANGE;
    if(file->busy || file->closing)return SC_SOUND_E_BUSY;
    if(file->status<0)return file->status;
    file->busy=1;snd_backend_cause=0;
    int count=sc_audio_file_read(&file->decoded,stereo,frames);file->busy=0;
    if(count<0 || snd_backend_cause<0 || (ScU32)count>frames){
        file->cause=snd_backend_cause;file->status=snd_error(file->cause,SC_SOUND_E_IO);return file->status;}
    file->eof=count==0;*out_frames=(ScU32)count;return 0;
}
ScStatus ScSoundQuery(const ScSoundFile *file,ScSoundFileInfo *out)
{
    snd_collect();
    if(!snd_valid(file) || !out || out->size<sizeof(*out) || out->size>4096)return SC_SOUND_E_INVALID;
    if(out->version!=1)return SC_SOUND_E_VERSION;
    ScSoundFileInfo value;snd_init(&value,sizeof(value));const sc_audio_file *d=&file->decoded;
    value.kind=(ScU32)d->kind;value.rate=(ScU32)d->rate;value.channels=(ScU32)d->channels;value.bits=(ScU32)d->bits;
    value.file_bytes=file->file_bytes;value.file_revision=file->source_revision;
    value.frame_position=d->pcm_position;value.total_frames=d->pcm_total;
    if(d->kind==SC_SOUND_WAV || (d->kind==SC_SOUND_FLAC && d->pcm_total))value.flags|=SC_SOUND_LENGTH_KNOWN;
    if(file->eof)value.flags|=SC_SOUND_EOF;
    if(file->status<0)value.flags|=SC_SOUND_FAILED;
    if(file->closing)value.flags|=SC_SOUND_CLOSING;
    value.status=file->status;value.cause=file->cause;memcpy(value.path,file->path,sizeof(value.path));
    memcpy(out,&value,sizeof(value));return 0;
}
ScStatus ScSoundClose(ScSoundFile *file)
{
    if(!snd_valid(file))return SC_SOUND_E_STALE;
    if(file->busy)return SC_SOUND_E_BUSY;
    file->closing=1;
    if(!file->decoder_closed){sc_audio_file_close(&file->decoded);file->decoder_closed=1;}
    ScStatus status=snd_source_close(file);
    if(status==SC32_PENDING)return SC32_PENDING;
    if(status<0){file->cause=status;file->status=snd_error(status,SC_SOUND_E_IO);return file->status;}
    snd_unlink(file);return 0;
}
ScStatus ScSoundSeek(ScSoundFile *file,ScU32 seconds)
{
    snd_collect();
    if(!snd_valid(file))return SC_SOUND_E_STALE;
    if(file->busy || file->closing)return SC_SOUND_E_BUSY;
    if(!file->decoded.rate || seconds>0xffffffffu/(ScU32)file->decoded.rate)return SC_SOUND_E_RANGE;
    ScU32 frame=seconds*(ScU32)file->decoded.rate;
    if(frame==file->decoded.pcm_position && !file->status){file->eof=0;return 0;}
    /* seek不是每帧工作。新解码候选在受管worker里流式定位，失败时
     * 老解码位置/曲目完整保留；不能让上游的半次seek破坏当前播放。
     * 空间仅另一个解码器和16KiB scratch，不复制整曲；长seek由
     * worker隔离，调用者可取消其真实job，不能在GUI回调里同步调用。 */
    file->busy=1;ScSoundOpenDesc open;snd_init(&open,sizeof(open));open.path=file->path;open.initial_seconds=seconds;
    ScSoundFile *candidate=0;ScStatus status=ScSoundOpen(&open,&candidate);
    if(status<0){file->busy=0;return status;}
    if(candidate->source_revision!=file->source_revision || candidate->file_bytes!=file->file_bytes){
        snd_close_internal(candidate);file->busy=0;return SC_SOUND_E_STALE;}
    unsigned char *previous_scratch=file->decoded.scratch;
    sc_audio_file_move(&file->decoded,&candidate->decoded);free(previous_scratch);
    ScFile previous_file=file->file;ScLeaseInfo previous_lease=file->lease;
    file->file=candidate->file;file->lease=candidate->lease;
    candidate->file=previous_file;candidate->lease=previous_lease;
    file->eof=0;file->status=file->cause=0;file->busy=0;
    snd_close_internal(candidate);return 0;
}
void ScSoundFinalize(void)
{
    /* 断言/算术损坏后不再调用可能不一致的解码器析构。ROOT已经
     * 转到独立退出栈，原现代FILE/lease和私有页由内核owner链回收。 */
    if(snd_poisoned){snd_files=snd_garbage=0;return;}
    while(snd_files){ScSoundFile *file=snd_files;
        if(!file->decoder_closed)sc_audio_file_close(&file->decoded);
        snd_source_close(file);snd_unlink(file);}
}
