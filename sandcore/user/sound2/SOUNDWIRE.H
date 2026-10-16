#ifndef SANDCORE_SOUND2_WIRE_H
#define SANDCORE_SOUND2_WIRE_H
#include "SC32BROKER.H"
#include "SC32AUDIO.H"
#include "SCSOUND.H"

/* 应用自己的值协议。既不把指针传给另一进程，也不把这份私有头
 * 安装进普通SDK。真实sender/代数由Broker提供，不能用正文伪造。 */
enum {SND_HELLO=1,SND_OPEN=2,SND_READY=3,SND_PAUSE=4,SND_VOLUME=5,
    SND_STATUS=6,SND_QUIT=7,SND_ACK=8,SND_ERROR=9,SND_COVER=10,SND_SIDECAR=11,
    SND_FLAG_FINISHED=1,SND_FLAG_PAUSED=2,SND_FLAG_OPENED=4,
    SND_FRAME_BUDGET=8192,SND_PEAK_COUNT=64,SND_SKIP_METADATA=1,
    SND_COVER_CHUNK=4096,SND_COVER_LIMIT=16777216};
typedef struct {
    ScU32 size,version,opcode,sequence;ScStatus status;
    ScU32 value,seconds,base_frame,flags;
    ScSoundFileInfo file;
    ScAudioVoiceInfo voice;
    ScU32 peaks[SND_PEAK_COUNT];
    char path[68];
    char title[128],artist[128];ScU32 cover_bytes,cover_kind;
} SndMessage;
typedef struct {
    ScU32 size,version,opcode,offset,bytes;unsigned char data[SND_COVER_CHUNK];
} SndCoverChunk;
typedef char snd_wire_keeps_820[(sizeof(SndMessage)==820)?1:-1];
#endif
