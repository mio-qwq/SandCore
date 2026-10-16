#include "SOUNDWIRE.H"
#include "SOUNDCOVER.H"
#include <stdlib.h>
#include <string.h>
#include "sound_cover.c"

/* 只有当前/候选两个业务角色，不是系统任务上限。每个解码器和声音
 * 属于自己的普通worker；候选慢打开/seek不会让旧worker停止喂音频。 */
static ScIpcChannel channel;
static ScBrokerIdentity peer;
static ScIdentityInfo identity;
static ScSoundFile *source;
static ScAudioVoice voice;
static ScU32 paused=1,eof,finished,opened,quit,base_frame,sequence;
static ScU32 volume=224,peak_cursor,peaks[SND_PEAK_COUNT];
static ScU32 telemetry_dirty;
static short pcm[4096];
static ScU32 pcm_at,pcm_frames;
static SndMessage pending;
static ScU32 pending_valid,pending_important;
static ScMonotonicClock last_status;
static SndCover album;
static ScU32 cover_at;

static void record(void *p,ScU32 bytes)
{memset(p,0,bytes);((ScU32 *)p)[0]=bytes;((ScU32 *)p)[1]=1;}
static int sender(const ScBrokerIdentity *actual)
{
    return actual->pid==peer.pid && actual->generation==peer.generation
        && actual->uid==peer.uid && actual->gid==peer.gid && actual->session==peer.session
        && actual->realm==SC32_BROKER_REALM_NORMAL && !actual->flags
        && actual->subject_uid==peer.subject_uid && actual->subject_gid==peer.subject_gid;
}
static ScStatus number(const char **cursor,ScU32 *out,ScU32 last)
{
    const char *p=*cursor;ScU32 n=0;
    if(*p<'0' || *p>'9' || (*p=='0' && p[1]>='0' && p[1]<='9'))return SC32_E_INVALID;
    while(*p>='0' && *p<='9'){
        ScU32 digit=(ScU32)(*p++-'0');if(n>(0xffffffffu-digit)/10u)return SC32_E_RANGE;n=n*10u+digit;
    }
    if(last?*p!=0:*p!='.')return SC32_E_INVALID;
    *cursor=last?p:p+1;*out=n;return 0;
}
static ScStatus bootstrap(void)
{
    record(&identity,sizeof(identity));ScStatus status=ScIdentityQuery(&identity);if(status<0)return status;
    if(identity.realm!=SC32_BROKER_REALM_NORMAL || identity.uid<0 || identity.gid<0
        || (identity.flags&~SC32_IDENTITY_MANAGE))return SC32_E_ACCESS;
    char name[96];ScU32 bytes=0;status=ScProcessArguments(name,sizeof(name),&bytes);if(status<0)return status;
    if(bytes>=sizeof(name) || strlen(name)!=bytes || strncmp(name,"SND2.",5))return SC32_E_INVALID;
    const char *cursor=name+5;ScU32 parent_pid=0,parent_generation=0,session=0,nonce=0;
    status=number(&cursor,&parent_pid,0);if(!status)status=number(&cursor,&parent_generation,0);
    if(!status)status=number(&cursor,&session,0);
    if(!status)status=number(&cursor,&nonce,1);
    if(status<0)return status;
    if(!parent_pid || !parent_generation || !nonce || session!=identity.session)return SC32_E_ACCESS;
    ScIpcConnectDesc connect;record(&connect,sizeof(connect));connect.name=name;connect.session=session;
    status=ScIpcConnect(&connect,&channel);if(status<0)return status;
    for(;;){
        record(&peer,sizeof(peer));status=ScIpcPeer(channel,&peer);if(status!=SC32_E_AGAIN)break;
        ScBrokerWaitDesc wait;record(&wait,sizeof(wait));wait.handle=channel;
        wait.ready_flags=SC32_BROKER_READY_WRITE|SC32_BROKER_READY_CLOSED;
        wait.minimum_write_bytes=sizeof(SndMessage);wait.timeout=SC32_INFINITE;
        ScU32 events=0;status=ScBrokerWait(&wait,&events);if(status<0)return status;
    }
    if(status<0)return status;
    if(peer.pid!=parent_pid || peer.generation!=parent_generation || peer.session!=session
        || peer.realm!=SC32_BROKER_REALM_NORMAL || peer.flags
        || peer.uid!=identity.uid || peer.gid!=identity.gid
        || peer.subject_uid!=identity.uid || peer.subject_gid!=identity.gid)return SC32_E_ACCESS;
    return 0;
}
static void reply(ScU32 opcode,ScStatus status,ScU32 important)
{
    record(&pending,sizeof(pending));pending.opcode=opcode;pending.sequence=sequence;pending.status=status;
    pending.value=volume;pending.base_frame=base_frame;
    pending.flags=(finished?SND_FLAG_FINISHED:0)|(paused?SND_FLAG_PAUSED:0)|(opened?SND_FLAG_OPENED:0);
    if(source){record(&pending.file,sizeof(pending.file));ScSoundQuery(source,&pending.file);}
    if(pending.file.rate)pending.seconds=base_frame/pending.file.rate;
    if(voice){record(&pending.voice,sizeof(pending.voice));ScAudioQuery(voice,&pending.voice);}
    memcpy(pending.peaks,peaks,sizeof(peaks));
    memcpy(pending.title,album.title,sizeof(album.title));memcpy(pending.artist,album.artist,sizeof(album.artist));
    pending.cover_bytes=album.bytes;pending.cover_kind=album.source_kind;
    pending_valid=1;pending_important=important;telemetry_dirty=0;
}
static ScStatus flush(void)
{
    if(!pending_valid)return 0;
    ScStatus status=ScIpcSend(channel,SC32_IPC_FRAME_DATA,&pending,sizeof(pending));
    if(!status){pending_valid=pending_important=0;record(&last_status,sizeof(last_status));
        status=ScSystemMonotonic(&last_status);}
    return status;
}
static ScStatus fill(void)
{
    if(!voice || !source || finished)return 0;
    ScAudioVoiceInfo state;record(&state,sizeof(state));ScStatus status=ScAudioQuery(voice,&state);if(status<0)return status;
    if(eof){if(!state.queued_frames && !finished){finished=1;telemetry_dirty=1;}return 0;}
    if(!pcm_frames){
        if(state.free_frames<2048)return 0;
        status=ScSoundRead(source,pcm,2048,&pcm_frames);if(status<0)return status;pcm_at=0;
        if(!pcm_frames){eof=1;return ScAudioControl(voice,SC32_AUDIO_DRAIN,0);}
        ScU32 peak=0;
        for(ScU32 i=0;i<pcm_frames*2;i++){ScI32 value=pcm[i];if(value<0)value=-value;if((ScU32)value>peak)peak=(ScU32)value;}
        peaks[peak_cursor++&63u]=peak;
    }
    ScU32 consumed=0;status=ScAudioSubmit(voice,pcm+pcm_at*2,pcm_frames,&consumed);
    if(status==SC32_E_AGAIN)return 0;
    if(status<0)return status;
    pcm_at+=consumed;pcm_frames-=consumed;return 0;
}
static ScStatus open_source(const SndMessage *message)
{
    if(opened || source || voice || message->value>256 || (message->flags&~SND_SKIP_METADATA))return SC32_E_INVALID;
    if(!memchr(message->path,0,sizeof(message->path)) || message->path[0]!='/')return SC32_E_INVALID;
    ScSoundOpenDesc desc;record(&desc,sizeof(desc));desc.path=message->path;desc.initial_seconds=message->seconds;
    ScStatus status=ScSoundOpen(&desc,&source);if(status<0)return status;
    ScSoundFileInfo file;record(&file,sizeof(file));status=ScSoundQuery(source,&file);if(status<0)return status;
    if(message->file.file_revision && message->file.file_revision!=file.file_revision)return SC32_E_STALE;
    ScAudioOpenDesc device;record(&device,sizeof(device));device.rate=file.rate;device.channels=2;
    status=ScAudioOpen(&device,&voice);if(status<0)return status;
    status=ScAudioControl(voice,SC32_AUDIO_PAUSE,1);if(status<0)return status;
    volume=message->value;status=ScAudioControl(voice,SC32_AUDIO_VOLUME,volume);if(status<0)return status;
    base_frame=file.frame_position;opened=1;
    /* 候选先预填一个真实块且声音保持暂停；父收到READY后才切换。
     * 大文件打开失败或设备配额不足，不会停止另一个原播放worker。 */
    status=fill();
    if(!status && !(message->flags&SND_SKIP_METADATA))snd_cover_open(file.path,file.file_revision,&album);
    return status;
}
static ScStatus send_cover(void)
{
    if(!opened || !album.data || cover_at==album.bytes || pending_valid)return 0;
    SndCoverChunk message;memset(&message,0,20);message.version=1;message.opcode=SND_COVER;
    message.offset=cover_at;message.bytes=album.bytes-cover_at;
    if(message.bytes>SND_COVER_CHUNK)message.bytes=SND_COVER_CHUNK;
    message.size=20+message.bytes;memcpy(message.data,album.data+cover_at,message.bytes);
    ScStatus status=ScIpcSend(channel,SC32_IPC_FRAME_DATA,&message,message.size);
    if(!status){cover_at+=message.bytes;
        if(cover_at==album.bytes){memset(album.data,0,album.bytes);free(album.data);album.data=0;}}
    return status;
}
static ScStatus receive(void)
{
    SndMessage message;ScIpcPacket packet;record(&packet,sizeof(packet));
    ScStatus status=ScIpcReceive(channel,&message,sizeof(message),&packet);
    if(status==SC32_E_AGAIN)return 0;
    if(status<0)return status;
    if(packet.flags&SC32_IPC_FRAME_EOF)return SC32_E_BROKEN_PIPE;
    if(packet.type!=SC32_IPC_FRAME_DATA || packet.bytes!=sizeof(message) || !sender(&packet.sender)
        || message.size!=sizeof(message) || message.version!=1 || !message.sequence
        || message.sequence<=sequence)return SC32_E_INVALID;
    sequence=message.sequence;
    if(message.opcode==SND_OPEN){status=open_source(&message);reply(status<0?SND_ERROR:SND_READY,status,1);return 0;}
    if(message.opcode==SND_QUIT){
        /* ACK必须发生在设备已停止之后。否则父可能先唤醒新声音，
         * 再轮到旧worker退出，造成切歌瞬间两首同时播放。 */
        if(voice){status=ScAudioControl(voice,SC32_AUDIO_STOP,0);if(status<0)return status;voice=0;}
        opened=0;quit=1;reply(SND_ACK,0,1);return 0;
    }
    if(!opened || !voice)return SC32_E_INVALID;
    if(message.opcode==SND_SIDECAR){
        ScSoundFileInfo file;record(&file,sizeof(file));status=ScSoundQuery(source,&file);
        if(!status){snd_cover_close(&album);cover_at=0;
            /* 可选图片的缺失或损坏不是音频错误。只在已确认坏内嵌图时
             * 触发一次旁置回退，不在每次播放刷新重复扫描目录。 */
            snd_cover_sidecar(file.path,file.file_revision,&album);}
    }
    else if(message.opcode==SND_PAUSE && message.value<=1){status=ScAudioControl(voice,SC32_AUDIO_PAUSE,message.value);
        if(!status)paused=message.value;}
    else if(message.opcode==SND_VOLUME && message.value<=256){status=ScAudioControl(voice,SC32_AUDIO_VOLUME,message.value);
        if(!status)volume=message.value;}
    else status=SC32_E_INVALID;
    reply(status<0?SND_ERROR:SND_ACK,status,1);return 0;
}
int main(void)
{
    ScStatus status=bootstrap();if(status<0)return 10;
    reply(SND_HELLO,0,1);
    for(;;){
        status=flush();if(status<0 && status!=SC32_E_AGAIN)break;
        if(quit && !pending_valid)break;
        if(!pending_important){status=receive();if(status<0)break;}
        if(opened && (!paused || !last_status.hz)){
            status=fill();if(status<0){opened=0;
                if(voice){ScAudioControl(voice,SC32_AUDIO_STOP,0);voice=0;}
                reply(SND_ERROR,status,1);}
        }
        status=send_cover();if(status<0 && status!=SC32_E_AGAIN)break;
        if(!pending_important && opened && (telemetry_dirty || (!paused && !finished))){ScMonotonicClock now;record(&now,sizeof(now));
            status=ScSystemMonotonic(&now);if(status<0)break;
            if(telemetry_dirty || !last_status.hz || now.ticks.hi!=last_status.ticks.hi || now.ticks.lo-last_status.ticks.lo>=now.hz/4u)
                reply(SND_STATUS,0,0);
        }
        ScBrokerWaitDesc wait;record(&wait,sizeof(wait));wait.handle=channel;
        wait.ready_flags=SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED;
        if(pending_valid || (opened && album.data)){
            wait.ready_flags|=SC32_BROKER_READY_WRITE;
            wait.minimum_write_bytes=pending_valid?sizeof(pending):20u+(album.bytes-cover_at>SND_COVER_CHUNK?SND_COVER_CHUNK:album.bytes-cover_at);
        }
        wait.event_mask=SC32_EVENT_AUDIO|SC32_EVENT_STREAM;
        wait.timeout=opened && !paused && !finished?25:SC32_INFINITE;
        ScU32 events=0;status=ScBrokerWait(&wait,&events);if(status<0)break;
    }
    if(voice)ScAudioControl(voice,SC32_AUDIO_STOP,0);
    if(source)ScSoundClose(source);
    snd_cover_close(&album);
    if(channel)ScIpcClose(channel);
    return status==SC32_E_BROKEN_PIPE || status>=0?0:11;
}
