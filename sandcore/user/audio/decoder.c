#include "../SCAUDIO.H"
#include "runtime.h"
#include "flac.h"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DRMP3_ASSERT(x) mp_assert((x)!=0)
#define DRMP3_COPY_MEMORY(d,s,n) mp_memory_copy(d,s,(u32)(n))
#define DRMP3_MOVE_MEMORY(d,s,n) mp_memory_move(d,s,(u32)(n))
#define DRMP3_ZERO_MEMORY(d,n) mp_memory_zero(d,(u32)(n))
#define DRMP3_MALLOC(n) mp_allocate((u32)(n))
#define DRMP3_REALLOC(p,n) mp_reallocate(p,(u32)(n))
#define DRMP3_FREE(p) mp_release(p)
#include "vendor/dr_mp3.h"
static u32 word(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static u16 half(const u8 *p){return (u16)(p[0]|((u16)p[1]<<8));}
static int bytes(sc_audio_file *f,void *out,u32 n)
{
    u32 used=0;while(used<n){int count=sc_stream_read(f->fd,(u8 *)out+used,n-used>65536?65536:n-used);
        if(count==-6){sc_yield();continue;}if(count<0){f->error=(u32)(0-count);return count;}if(!count)break;used+=(u32)count;}
    f->position+=used;return (int)used;
}
static size_t mp_read(void *user,void *out,size_t n)
{sc_audio_file *f=user;if(n>f->size-f->position)n=f->size-f->position;int count=bytes(f,out,(u32)n);return count<0?0:(size_t)count;}
static drmp3_bool32 mp_seek(void *user,int offset,drmp3_seek_origin origin)
{
    sc_audio_file *f=user;u32 base=origin==DRMP3_SEEK_SET?0:origin==DRMP3_SEEK_CUR?f->position:f->size;
    u32 distance=offset<0?0u-(u32)offset:(u32)offset;
    if((offset<0 && distance>base) || (offset>=0 && distance>f->size-base))return 0;
    u32 target=offset<0?base-distance:base+distance;int result=sc_stream_seek(f->fd,target);
    if(result<0)return 0;f->position=target;return 1;
}
static drmp3_bool32 mp_tell(void *user,drmp3_int64 *offset){*offset=((sc_audio_file *)user)->position;return 1;}
void sc_audio_file_close(sc_audio_file *f)
{
    if(f->decoder){if(f->kind==3)sc_flac_close(f);else {drmp3_uninit((drmp3 *)f->decoder);mp_release(f->decoder);f->decoder=0;}}
    if(f->fd>=0){sc_stream_close(f->fd,0);f->fd=-1;}
}
void sc_audio_file_move(sc_audio_file *target,sc_audio_file *source)
{
    if(target==source)return;sc_audio_file_close(target);*target=*source;
    /* MP3流回调保存file对象的地址，原始结构赋值会留下已退出栈帧
     * 的指针。所有权转移同时重绑回调，再清掉候选，不能二次释放。 */
    if(target->decoder){if(target->kind==3)sc_flac_rebind(target);else ((drmp3 *)target->decoder)->pUserData=target;}
    mp_memory_zero(source,sizeof(*source));source->fd=-1;
}
int sc_audio_file_open(sc_audio_file *f,const char *name)
{
    mp_memory_zero(f,sizeof(*f));f->fd=-1;char path[64];u32 info[2];
    if(sc_resolve(name,path,64)<0 || sc_stat(path,info)<0 || info[0]!=1)return -1;
    f->size=info[1];if(f->size<12)return -2;f->fd=sc_stream_open(name,1,0);if(f->fd<0)return f->fd;
    u8 header[40];if(bytes(f,header,12)!=12)goto bad;
    if(word(header)==0x43614C66u || word(header)==0x5367674Fu){
        if(sc_flac_open(f)<0)goto bad;return 0;
    }
    if(word(header)==0x46464952u && word(header+8)==0x45564157u){
        if(word(header+4)>f->size-8)goto bad;u32 riff_end=word(header+4)+8;int format_seen=0,data_seen=0;
        while(f->position<=riff_end && riff_end-f->position>=8){
            if(bytes(f,header,8)!=8)goto bad;u32 kind=word(header),n=word(header+4),at=f->position;
            if(n>riff_end-at || ((n&1u) && n==riff_end-at))goto bad;
            if(kind==0x20746D66u){
                u32 take=n<40?n:40;
                if(format_seen || n<16 || bytes(f,header,take)!=(int)take)goto bad;
                f->format=half(header);f->channels=half(header+2);f->rate=(int)word(header+4);f->block=half(header+12);f->bits=half(header+14);
                if(f->format==0xFFFE){
                    static const u8 guid_tail[14]={0,0,0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71};
                    if(n<40 || half(header+16)<22 || half(header+18)>f->bits)goto bad;
                    for(int g=0;g<14;g++)if(header[26+g]!=guid_tail[g])goto bad;
                    f->format=half(header+24);
                }
                if(f->channels<1 || f->channels>2 || f->rate<8000 || f->rate>192000
                    || (f->format!=1 && f->format!=3) || (f->format==3 && f->bits!=32)
                    || (f->bits!=8 && f->bits!=16 && f->bits!=24 && f->bits!=32) || f->block!=f->channels*(f->bits/8)
                    || word(header+8)!=(u32)f->rate*(u32)f->block)goto bad;
                if(f->format==3){u32 state[8];if(sc_simd_info(state)<0 || !state[1])goto bad;}
                format_seen=1;
            }else if(kind==0x61746164u){if(data_seen)goto bad;f->data_start=at;f->data_size=n;data_seen=1;}
            if(format_seen && data_seen)break;
            if(sc_stream_seek(f->fd,at+n+(n&1u))<0)goto bad;
            f->position=at+n+(n&1u);
        }
        if(!format_seen || !data_seen || f->data_size%(u32)f->block || sc_stream_seek(f->fd,f->data_start)<0)goto bad;
        f->position=f->data_start;f->kind=1;f->pcm_total=f->data_size/(u32)f->block;return 0;
    }
    if(sc_stream_seek(f->fd,0)<0)goto bad;f->position=0;
    u32 state[8];if(sc_simd_info(state)<0 || !state[1])goto bad;
    f->decoder=mp_allocate(sizeof(drmp3));if(!f->decoder)goto bad;
    if(!drmp3_init((drmp3 *)f->decoder,mp_read,mp_seek,mp_tell,0,f,0)){mp_release(f->decoder);f->decoder=0;goto bad;}
    drmp3 *mp=f->decoder;f->channels=(int)mp->channels;f->rate=(int)mp->sampleRate;
    if(f->channels<1 || f->channels>2 || f->rate<8000 || f->rate>192000)goto bad;
    f->kind=2;f->bits=16;return 0;
bad:
    sc_audio_file_close(f);return -2;
}
static short sample(const u8 *p,int bits,int format)
{
    if(format==3){union {u32 bits;float number;} v;v.bits=word(p);
        if((v.bits&0x7F800000u)==0x7F800000u)return 0;float x=v.number;if(x>=1.0f)return 32767;if(x<=-1.0f)return -32768;return (short)(x*32768.0f);}
    if(bits==8)return (short)(((int)p[0]-128)*256);if(bits==16)return (short)half(p);
    if(bits==24){u32 v=(u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16);if(v&0x800000u)v|=0xFF000000u;return (short)((int)v>>8);}
    return (short)((int)word(p)>>16);
}
int sc_audio_file_read(sc_audio_file *f,short *out,u32 frames)
{
    if(frames>2048 || f->fd<0)return -1;
    if(f->kind==3)return sc_flac_read(f,out,frames);
    if(f->kind==2){
        if(f->channels==2){int n=(int)drmp3_read_pcm_frames_s16(f->decoder,frames,out);f->pcm_position+=(u32)n;return f->error?-1:n;}
        short mono[2048];int n=(int)drmp3_read_pcm_frames_s16(f->decoder,frames,mono);
        for(int i=0;i<n;i++){out[i*2]=mono[i];out[i*2+1]=mono[i];}f->pcm_position+=(u32)n;return f->error?-1:n;
    }
    u32 remaining=f->data_start+f->data_size-f->position,count=remaining/(u32)f->block;if(count>frames)count=frames;
    u8 buffer[16384];u32 needed=count*(u32)f->block;if(bytes(f,buffer,needed)!=(int)needed)return -1;
    for(u32 i=0;i<count;i++){const u8 *p=buffer+i*(u32)f->block;out[i*2]=sample(p,f->bits,f->format);
        out[i*2+1]=f->channels==1?out[i*2]:sample(p+f->bits/8,f->bits,f->format);}
    f->pcm_position+=count;return (int)count;
}
int sc_audio_file_seek(sc_audio_file *f,u32 seconds)
{
    if(seconds>0xFFFFFFFFu/(u32)f->rate)return -1;u32 frame=seconds*(u32)f->rate;
    if(f->kind==3)return sc_flac_seek(f,frame);
    if(f->kind==2){if(!drmp3_seek_to_pcm_frame(f->decoder,frame))return -1;f->pcm_position=frame;return 0;}
    if(frame>f->data_size/(u32)f->block)return -1;u32 position=f->data_start+frame*(u32)f->block;
    if(sc_stream_seek(f->fd,position)<0)return -1;f->position=position;f->pcm_position=frame;return 0;
}
