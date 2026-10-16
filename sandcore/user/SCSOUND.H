#ifndef SANDCORE_SCSOUND_H
#define SANDCORE_SCSOUND_H
#include "SC32API.H"

/* 独立Ring3流式解码库。解码不创建声音设备；调用者可在自己的
 * 受管worker中使用，再经SC32AUDIO提交，GUI不必阻塞等待seek。 */
typedef struct ScSoundFile ScSoundFile;
enum {SC_SOUND_ABI_MAJOR=1,SC_SOUND_ABI_MINOR=0,SC_SOUND_VERSION=1,
    SC_SOUND_WAV=1,SC_SOUND_MP3=2,SC_SOUND_FLAC=3,
    SC_SOUND_LENGTH_KNOWN=1,SC_SOUND_EOF=2,SC_SOUND_FAILED=4,SC_SOUND_CLOSING=8,
    SC_SOUND_E_INVALID=-701,SC_SOUND_E_VERSION=-702,SC_SOUND_E_RANGE=-703,
    SC_SOUND_E_MEMORY=-704,SC_SOUND_E_IO=-705,SC_SOUND_E_FORMAT=-706,
    SC_SOUND_E_NOT_SUPPORTED=-707,SC_SOUND_E_STALE=-708,SC_SOUND_E_ACCESS=-709,
    SC_SOUND_E_BUSY=-710};
typedef struct {
    ScU32 size,version;const char *path;ScU32 flags,initial_seconds,reserved[3];
} ScSoundOpenDesc;
typedef struct {
    ScU32 size,version,kind,rate,channels,bits,file_bytes,file_revision;
    ScU32 frame_position,total_frames,flags;ScStatus status,cause;ScU32 reserved[3];
    char path[68];
} ScSoundFileInfo;

ScStatus ScSoundOpen(const ScSoundOpenDesc *,ScSoundFile **out);
/* 总是输出双声道有符号16位PCM；每次1..2048帧，需要frames*4B。
 * 只有成功才写out_frames；0是明确EOF，错误绝不伪装成曲末。 */
ScStatus ScSoundRead(ScSoundFile *,void *stereo,ScU32 frames,ScU32 *out_frames);
ScStatus ScSoundSeek(ScSoundFile *,ScU32 seconds);
ScStatus ScSoundQuery(const ScSoundFile *,ScSoundFileInfo *out);
/* 0消耗对象；PENDING保持对象供下一步Close，绝不重放解码或发布。 */
ScStatus ScSoundClose(ScSoundFile *);
#endif
