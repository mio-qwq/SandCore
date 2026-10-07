/* libex 1.1：非阻塞 S16 双声道；尾巴按帧移动两份样本。
 * 内核 submit 复制已接收样本，因此只有未提交尾巴需要保持有效。
 * 音效共用一份可增长缓冲；忙时保留旧尾巴，不覆盖或释放悬空指针。 */
#include "exaudio.h"
static int exaudio_ready;
static u32 exaudio_token,exaudio_rate=48000;
static const s16 *exaudio_pending;
static u32 exaudio_pending_frames;
static s16 *exaudio_tone_buf;
static u32 exaudio_tone_capacity;
int exaudio_init(void)
{
    if(exaudio_ready)return 0;
    u32 info[16];if(sc_audio_info(info)||!info[1])return -1;
    exaudio_rate=info[2]?info[2]:48000;
    int token=sc_audio_open(exaudio_rate,2);if(token<0)return -1;
    exaudio_token=(u32)token;exaudio_ready=1;exaudio_pending=0;exaudio_pending_frames=0;return 0;
}
void exaudio_shutdown(void)
{
    if(exaudio_ready)sc_audio_control(exaudio_token,0,0);
    exaudio_ready=0;exaudio_pending=0;exaudio_pending_frames=0;
    if(exaudio_tone_buf)sc_free(exaudio_tone_buf);
    exaudio_tone_buf=0;exaudio_tone_capacity=0;
}
void exaudio_pump(void)
{
    if(!exaudio_ready)return;
    while(exaudio_pending_frames){
        int n=sc_audio_submit(exaudio_token,exaudio_pending,exaudio_pending_frames);
        if(n<=0||(u32)n>exaudio_pending_frames)break;
        exaudio_pending+=n*2;exaudio_pending_frames-=(u32)n;
    }
    if(!exaudio_pending_frames)exaudio_pending=0;
}
u32 exaudio_stream(const s16 *samples,u32 frames)
{
    if(!exaudio_ready||!samples||!frames)return 0;
    exaudio_pump();if(exaudio_pending_frames)return 0;
    u32 done=0;
    while(done<frames){
        int n=sc_audio_submit(exaudio_token,samples+done*2,frames-done);
        if(n<=0||(u32)n>frames-done)break;done+=(u32)n;
    }
    if(done<frames){exaudio_pending=samples+done*2;exaudio_pending_frames=frames-done;}
    return done;
}
int exaudio_sysfx(int kind){return kind<0||kind>5?-1:sc_audio_effect((u32)kind);}
int exaudio_tone(int freq,int ms,int wave,int volume)
{
    if(freq<20||freq>12000||ms<10||ms>2000||wave<0||wave>2)return -1;
    if(!exaudio_ready)return 0;
    exaudio_pump();if(exaudio_pending_frames)return -2;
    if(volume<0)volume=0;if(volume>256)volume=256;
    u32 frames=exaudio_rate*(u32)ms/1000;
    if(frames>exaudio_tone_capacity){
        s16 *next=sc_alloc(frames*4);if(!next)return -1;
        if(exaudio_tone_buf)sc_free(exaudio_tone_buf);exaudio_tone_buf=next;exaudio_tone_capacity=frames;
    }
    u32 period=exaudio_rate/(u32)freq;if(period<2)period=2;
    u32 rise=exaudio_rate/100,tail=exaudio_rate/500,noise=0x1234567u,age=0;
    int envelope=volume;
    for(u32 i=0;i<frames;i++){
        u32 now=i*1000/exaudio_rate;
        while(age<now){envelope=envelope*255/256;age++;}
        int gain=envelope;
        /* 起音作用于当前样本，不能把第零样本的零增益反馈给整段包络。 */
        if(i<rise)gain=(int)((u32)gain*i/rise);
        if(frames-i<tail)gain=(int)((u32)gain*(frames-i)/tail);
        int sample;
        if(wave==EXWAVE_NOISE){noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;sample=(s16)(noise&65535);}
        else if(wave==EXWAVE_TRIANGLE){u32 pos=i%period,half=period/2;sample=(int)(pos<half?pos*65536/half:(period-pos)*65536/(period-half))-32768;}
        else sample=i%period<period/2?11000:-11000;
        sample=sample*gain/256;if(sample>32767)sample=32767;if(sample<-32768)sample=-32768;
        exaudio_tone_buf[i*2]=exaudio_tone_buf[i*2+1]=(s16)sample;
    }
    exaudio_stream(exaudio_tone_buf,frames);return 0;
}
