#include "flac.h"
#include "runtime.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DRFLAC_ASSERT(x) mp_assert((x)!=0)
#define DRFLAC_COPY_MEMORY(d,s,n) mp_memory_copy(d,s,(u32)(n))
#define DRFLAC_MOVE_MEMORY(d,s,n) mp_memory_move(d,s,(u32)(n))
#define DRFLAC_ZERO_MEMORY(d,n) mp_memory_zero(d,(u32)(n))
#define DRFLAC_MALLOC(n) mp_allocate((u32)(n))
#define DRFLAC_REALLOC(p,n) mp_reallocate(p,(u32)(n))
#define DRFLAC_FREE(p) mp_release(p)
#include "flacvendor/dr_flac.h"
/* 保留CRC和整数标量核心。回调只经当前任务的SandFS流式票据读取，
 * 不读整曲、不提供stdio/公共libc，不因解码器成熟省略适配边界。 */
static size_t read_bytes(void *user,void *out,size_t size)
{
    sc_audio_file *f=user;u32 used=0;if(size>f->size-f->position)size=f->size-f->position;
    while(used<size){u32 chunk=(u32)size-used;if(chunk>65536)chunk=65536;
        int n=sc_stream_read(f->fd,(u8 *)out+used,chunk);
        if(n==-6){sc_yield();continue;}if(n<0){f->error=(u32)(0-n);break;}if(!n)break;used+=(u32)n;}
    f->position+=used;return used;
}
static drflac_bool32 seek_bytes(void *user,int offset,drflac_seek_origin origin)
{
    sc_audio_file *f=user;u32 base=origin==DRFLAC_SEEK_SET?0:origin==DRFLAC_SEEK_CUR?f->position:f->size;
    u32 distance=offset<0?0u-(u32)offset:(u32)offset;
    if((offset<0 && distance>base) || (offset>=0 && distance>f->size-base))return 0;
    u32 target=offset<0?base-distance:base+distance;if(sc_stream_seek(f->fd,target)<0)return 0;
    f->position=target;return 1;
}
static drflac_bool32 tell_bytes(void *user,drflac_int64 *out){*out=((sc_audio_file *)user)->position;return 1;}
int sc_flac_open(sc_audio_file *f)
{
    if(sc_stream_seek(f->fd,0)<0)return -2;f->position=0;
    drflac *decoder=drflac_open(read_bytes,seek_bytes,tell_bytes,f,0);if(!decoder)return -2;
    f->decoder=decoder;f->kind=3;
    if(decoder->channels<1 || decoder->channels>2 || decoder->sampleRate<8000 || decoder->sampleRate>192000
        || decoder->totalPCMFrameCount>0xFFFFFFFFull){sc_flac_close(f);return -2;}
    f->rate=(int)decoder->sampleRate;f->channels=(int)decoder->channels;f->bits=(int)decoder->bitsPerSample;
    f->pcm_total=(u32)decoder->totalPCMFrameCount;return 0;
}
int sc_flac_read(sc_audio_file *f,short *stereo,u32 frames)
{
    if(!frames)return 0;
    short mono[2048];int n=(int)drflac_read_pcm_frames_s16(f->decoder,frames,f->channels==2?stereo:mono);
    if(f->channels==1)for(int i=0;i<n;i++)stereo[i*2]=stereo[i*2+1]=mono[i];
    f->pcm_position+=(u32)n;
    /* 已知总帧数时截断/坏CRC不能被当作正常曲末；无总数时依上游结果。 */
    if(f->error || (!n && f->pcm_total && f->pcm_position<f->pcm_total))return -1;return n;
}
int sc_flac_seek(sc_audio_file *f,u32 frame)
{
    drflac *decoder=f->decoder;if(f->pcm_total && frame>f->pcm_total)return -1;
    if(!decoder->totalPCMFrameCount){
        /* 流式Ogg编码器允许STREAMINFO总帧数为0。上游seek将非零目标
         * 裁到total，误报成功却回到开头；未知长度时按实际PCM前进，
         * 向后先复位，每2048帧让出。不推测时长，不把0当实际曲长。 */
        if(frame<f->pcm_position){if(!drflac_seek_to_pcm_frame(decoder,0))return -1;f->pcm_position=0;}
        while(f->pcm_position<frame){u32 count=frame-f->pcm_position;if(count>2048)count=2048;
            u32 n=(u32)drflac_read_pcm_frames_s16(decoder,count,0);f->pcm_position+=n;
            if(n!=count || f->error)return -1;sc_yield();}return 0;
    }
    if(!drflac_seek_to_pcm_frame(decoder,frame))return -1;f->pcm_position=frame;return 0;
}
void sc_flac_close(sc_audio_file *f){if(f->decoder){drflac_close(f->decoder);f->decoder=0;}}
void sc_flac_rebind(sc_audio_file *f)
{
    if(!f->decoder)return;drflac *decoder=f->decoder;
    /* Ogg的bitstream回调仍需指向容器；只把容器内的真正文件回调
     * 重绑到新主人，不能用file覆盖上游内部oggbs对象地址。 */
    if(decoder->_oggbs)((drflac_oggbs *)decoder->_oggbs)->pUserData=f;
    else decoder->bs.pUserData=f;
}
