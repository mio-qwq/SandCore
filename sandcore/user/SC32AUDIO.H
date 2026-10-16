#ifndef SANDCORE_SC32AUDIO_H
#define SANDCORE_SC32AUDIO_H
#include "SC32API.H"

/* PCM流仍属于真实调用任务；库不取得额外权限。所有计数为既有
 * 设备的32位累计值，调用者用无符号差分，不能伪造64位精度。 */
#define SC32_AUDIO_MIN_ABI_MINOR 4u
#define SC32_AUDIO_MIN_LIBRARY_VERSION 6u
typedef ScU32 ScAudioVoice;
enum {SC32_AUDIO_STOP=0,SC32_AUDIO_PAUSE=1,SC32_AUDIO_VOLUME=2,
    SC32_AUDIO_CLEAR=3,SC32_AUDIO_DRAIN=4};
enum {SC32_AUDIO_START_SOUND=0,SC32_AUDIO_STOP_SOUND=1,SC32_AUDIO_NOTICE=2,
    SC32_AUDIO_ERROR=3,SC32_AUDIO_COMPLETE=4,SC32_AUDIO_QUESTION=5};
typedef struct {
    ScU32 size,version,rate,channels,flags,reserved[3];
} ScAudioOpenDesc;
typedef struct {
    ScU32 size,version,available,rate,channels,bits,master_volume;
    ScU32 submitted_frames,completed_frames,underruns,clipped_samples,interrupts,dma_queued,reserved[3];
} ScAudioDeviceInfo;
typedef struct {
    ScU32 size,version,voice,rate,channels,queued_frames,free_frames,paused,finished;
    ScU32 consumed_frames,starved_frames,volume,reserved[4];
} ScAudioVoiceInfo;

ScStatus ScAudioDeviceQuery(ScAudioDeviceInfo *out);
/* 8000..192000Hz，1或2声道，flags/reserved均0。失败不改out。 */
ScStatus ScAudioOpen(const ScAudioOpenDesc *desc,ScAudioVoice *out);
/* 1..4096帧，每声道有符号16位交错PCM。成功可以部分提交，
 * 只消费out_frames个输入帧；AGAIN及其它失败不改out_frames。 */
ScStatus ScAudioSubmit(ScAudioVoice voice,const void *samples,ScU32 frames,ScU32 *out_frames);
/* STOP/CLEAR/DRAIN的value为0，PAUSE为0/1，VOLUME为0..256。 */
ScStatus ScAudioControl(ScAudioVoice voice,ScU32 command,ScU32 value);
ScStatus ScAudioQuery(ScAudioVoice voice,ScAudioVoiceInfo *out);
ScStatus ScAudioEffect(ScU32 kind);
/* 独立管理操作沿既有auth_can_manage检查，普通调用者不会因此提权。 */
ScStatus ScAudioMasterVolume(ScU32 volume);
#endif
