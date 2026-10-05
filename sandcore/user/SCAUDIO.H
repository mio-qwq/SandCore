#ifndef SCAUDIO_H
#define SCAUDIO_H
#include "SCAPI.H"
/* PCM提交是专用SandCore接口，不依赖标准C运行库。MP3解码组件由
 * 宿主引导编译，WAV解析/提交和播放控制仍运行在客体三环。 */
typedef struct {
    int fd,kind,channels,rate,bits,format,block;
    u32 size,position,data_start,data_size,pcm_position,error;
    void *decoder;
    u32 pcm_total;
} sc_audio_file;
int sc_audio_file_open(sc_audio_file *file,const char *path);
int sc_audio_file_read(sc_audio_file *file,short *stereo,u32 frames);
int sc_audio_file_seek(sc_audio_file *file,u32 seconds);
void sc_audio_file_close(sc_audio_file *file);
void sc_audio_file_move(sc_audio_file *target,sc_audio_file *source);
#endif
