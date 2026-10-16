#ifndef SANDCORE_SOUND_PRIVATE_H
#define SANDCORE_SOUND_PRIVATE_H
#include "../SCSOUND.H"
#include "../SC32LEASE.H"
#include "../SC32PATH.H"
#include "../SC32SIMD.H"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* 保留旧解码核心的私有整数/函数名，但只适配正式SC32/标准库，
 * 不包含SCAPI或复制寄存器桥。scratch归该文件，消除16KiB栈数组。 */
typedef ScU32 u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef struct {
    ScFile fd;int kind,channels,rate,bits,format,block;
    u32 size,position,data_start,data_size,pcm_position,error;
    void *decoder;u32 pcm_total;unsigned char *scratch;
} sc_audio_file;
struct ScSoundFile {
    ScSoundFile *next;
    ScSoundFile *garbage_next;
    sc_audio_file decoded;
    ScLeaseInfo lease;
    ScFile file;
    ScU32 file_bytes,source_revision,eof,busy,closing,decoder_closed,garbage;
    ScStatus status,cause;
    char path[68];
};
extern ScStatus snd_backend_cause;
extern ScU32 snd_poisoned;
void *mp_memory_copy(void *,const void *,u32);
void *mp_memory_move(void *,const void *,u32);
void *mp_memory_zero(void *,u32);
void *mp_allocate(u32);
void *mp_reallocate(void *,u32);
void mp_release(void *);
void mp_assert(int);
int sc_stream_read(ScFile,void *,u32);
int sc_stream_seek(ScFile,u32);
int sc_simd_info(u32 out[8]);
void sc_yield(void);
int sc_audio_file_open(sc_audio_file *,const char *);
int sc_audio_file_read(sc_audio_file *,short *,u32);
int sc_audio_file_seek(sc_audio_file *,u32);
void sc_audio_file_close(sc_audio_file *);
void sc_audio_file_move(sc_audio_file *,sc_audio_file *);
int sc_flac_open(sc_audio_file *);
int sc_flac_read(sc_audio_file *,short *,u32);
int sc_flac_seek(sc_audio_file *,u32);
void sc_flac_close(sc_audio_file *);
void sc_flac_rebind(sc_audio_file *);
void ScSoundFinalize(void);
#endif
