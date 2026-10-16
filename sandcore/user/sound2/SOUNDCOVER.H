#ifndef SANDCORE_SOUND2_COVER_PRIVATE_H
#define SANDCORE_SOUND2_COVER_PRIVATE_H
#include "SC32API.H"
typedef struct {
    char title[128],artist[128];unsigned char *data;ScU32 bytes,source_kind;
} SndCover;
/* 只在受管音频worker的候选准备阶段运行；标题/图片错误不停止音频。
 * 返回的压缩图片独立拥有，IPC发送完或取消以后由Close擦除归还。 */
ScStatus snd_cover_open(const char *absolute_path,ScU32 exact_revision,SndCover *out);
ScStatus snd_cover_sidecar(const char *absolute_path,ScU32 exact_revision,SndCover *out);
void snd_cover_close(SndCover *);
#endif
