#include "../sc32/SC32PRIVATE.H"
#include "../SC32AUDIO.H"
#include "../SC32SIMD.H"

enum {SC32_AUDIO_OP_DEVICE=0x1500,SC32_AUDIO_OP_OPEN,SC32_AUDIO_OP_SUBMIT,
    SC32_AUDIO_OP_CONTROL,SC32_AUDIO_OP_QUERY,SC32_AUDIO_OP_EFFECT,
    SC32_AUDIO_OP_MASTER,SC32_AUDIO_OP_SIMD};
typedef char audio_device_is_64[(sizeof(ScAudioDeviceInfo)==64)?1:-1];
typedef char audio_voice_is_64[(sizeof(ScAudioVoiceInfo)==64)?1:-1];
typedef char audio_open_is_32[(sizeof(ScAudioOpenDesc)==32)?1:-1];
typedef char simd_snapshot_is_48[(sizeof(ScSimdInfo)==48)?1:-1];

static ScStatus sc32_audio_result(ScI32 raw,ScU32 operation)
{
    if(raw>=0)return 0;
    /* 老设备的-4表示UID配额/混音声道忙，不伪称malloc失败。
     * 不改变旧调用错误；现代接口单独保留原raw用于诊断。 */
    ScStatus code=raw==-1?SC32_E_INVALID:raw==-2?SC32_E_NOT_SUPPORTED:
        raw==-4?SC32_E_BUSY:raw==-5?SC32_E_ACCESS:raw==-6?SC32_E_AGAIN:SC32_E_FAILED;
    return sc32_fail(code,SC32_DOMAIN_LEGACY,raw,operation);
}
ScStatus ScAudioDeviceQuery(ScAudioDeviceInfo *out)
{
    ScStatus status=sc32_record_out(out,sizeof(*out));
    if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_DEVICE);
    ScU32 raw[16];ScI32 result=sc32_call(0x230,(ScI32)raw,0,0,0,0,0);
    if(result<0)return sc32_audio_result(result,SC32_AUDIO_OP_DEVICE);
    ScAudioDeviceInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.available=raw[1];value.rate=raw[2];value.channels=raw[3];value.bits=raw[4];value.master_volume=raw[5];
    value.submitted_frames=raw[6];value.completed_frames=raw[7];value.underruns=raw[8];
    value.clipped_samples=raw[9];value.interrupts=raw[10];value.dma_queued=raw[11];
    sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScAudioOpen(const ScAudioOpenDesc *desc,ScAudioVoice *out)
{
    ScStatus status=sc32_record_in(desc,sizeof(*desc));
    if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_OPEN);
    status=sc32_out(out,sizeof(*out));if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_OPEN);
    if(desc->rate<8000 || desc->rate>192000 || desc->channels<1 || desc->channels>2
        || desc->flags || desc->reserved[0] || desc->reserved[1] || desc->reserved[2])
        return sc32_invalid(SC32_E_RANGE,SC32_AUDIO_OP_OPEN);
    ScI32 result=sc32_call(0x231,(ScI32)desc->rate,(ScI32)desc->channels,0,0,0,0);
    if(result<0)return sc32_audio_result(result,SC32_AUDIO_OP_OPEN);
    if(!result)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,result,SC32_AUDIO_OP_OPEN);
    *out=(ScU32)result;return 0;
}
ScStatus ScAudioSubmit(ScAudioVoice voice,const void *samples,ScU32 frames,ScU32 *out_frames)
{
    ScStatus status=sc32_out(out_frames,sizeof(*out_frames));
    if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_SUBMIT);
    if(!voice || !samples || !frames || frames>4096)return sc32_invalid(SC32_E_RANGE,SC32_AUDIO_OP_SUBMIT);
    /* 内核仍在同一陷入内核验真实voice/owner/声道和完整只读PCM区间，
     * 不能用本库先查状态后的陈旧声道决定安全长度或保存用户指针。 */
    ScI32 result=sc32_call(0x232,(ScI32)voice,(ScI32)samples,(ScI32)frames,0,0,0);
    if(result<0)return sc32_audio_result(result,SC32_AUDIO_OP_SUBMIT);
    if((ScU32)result>frames)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,result,SC32_AUDIO_OP_SUBMIT);
    *out_frames=(ScU32)result;return 0;
}
ScStatus ScAudioControl(ScAudioVoice voice,ScU32 command,ScU32 value)
{
    if(!voice || command>SC32_AUDIO_DRAIN || (command==SC32_AUDIO_PAUSE && value>1)
        || (command==SC32_AUDIO_VOLUME && value>256)
        || ((command==SC32_AUDIO_STOP || command==SC32_AUDIO_CLEAR || command==SC32_AUDIO_DRAIN) && value))
        return sc32_invalid(SC32_E_RANGE,SC32_AUDIO_OP_CONTROL);
    return sc32_audio_result(sc32_call(0x233,(ScI32)voice,(ScI32)command,(ScI32)value,0,0,0),SC32_AUDIO_OP_CONTROL);
}
ScStatus ScAudioQuery(ScAudioVoice voice,ScAudioVoiceInfo *out)
{
    ScStatus status=sc32_record_out(out,sizeof(*out));
    if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_QUERY);
    if(!voice)return sc32_invalid(SC32_E_INVALID,SC32_AUDIO_OP_QUERY);
    ScU32 raw[16];ScI32 result=sc32_call(0x234,(ScI32)voice,(ScI32)raw,0,0,0,0);
    if(result<0)return sc32_audio_result(result,SC32_AUDIO_OP_QUERY);
    ScAudioVoiceInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;value.voice=voice;
    value.rate=raw[1];value.channels=raw[2];value.queued_frames=raw[3];value.free_frames=raw[4];
    value.paused=raw[5];value.finished=raw[6];value.consumed_frames=raw[7];value.starved_frames=raw[8];value.volume=raw[9];
    sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScAudioEffect(ScU32 kind)
{
    if(kind>SC32_AUDIO_QUESTION)return sc32_invalid(SC32_E_RANGE,SC32_AUDIO_OP_EFFECT);
    return sc32_audio_result(sc32_call(0x235,(ScI32)kind,0,0,0,0,0),SC32_AUDIO_OP_EFFECT);
}
ScStatus ScAudioMasterVolume(ScU32 volume)
{
    if(volume>256)return sc32_invalid(SC32_E_RANGE,SC32_AUDIO_OP_MASTER);
    return sc32_audio_result(sc32_call(0x233,0,5,(ScI32)volume,0,0,0),SC32_AUDIO_OP_MASTER);
}
ScStatus ScSimdQuery(ScSimdInfo *out)
{
    ScStatus status=sc32_record_out(out,sizeof(*out));
    if(status<0)return sc32_invalid(status,SC32_AUDIO_OP_SIMD);
    ScU32 raw[8];ScI32 result=sc32_call(0x21d,(ScI32)raw,0,0,0,0,0);
    if(result<0)return sc32_legacy(result,SC32_AUDIO_OP_SIMD);
    ScSimdInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.ready=raw[1];value.fxsr=raw[2];value.sse2=raw[3];value.cpuid_features=raw[4];value.saved_bytes=raw[5];value.switches=raw[6];
    sc32_copy(out,&value,sizeof(value));return 0;
}
