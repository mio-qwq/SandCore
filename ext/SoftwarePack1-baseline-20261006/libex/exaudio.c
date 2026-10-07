/* =====================================================================
 * exaudio.c —— 音频包装与程序化音效实现。
 *
 * 内核合同（SYSCALL.md 0x230..0x236）：AC97 固定 48000Hz/S16/双声道，
 * submit 非阻塞返回实际接收帧数；无声卡时 open/info 全部失败，
 * 本库所有入口退化为安全空转，游戏/工具无需特判。
 *
 * 尾巴续喂模型：submit 可能只接收部分帧（队列 8192 帧上限），
 * 剩余部分记在 pending；游戏循环每帧 exaudio_pump() 续喂。
 * samples 归调用方所有，播完之前必须保持有效——音效缓冲由
 * exaudio_tone 在堆上生成并常驻到播完，满足该约定。
 * ===================================================================== */
#include "exaudio.h"

static int exaudio_ready;          /* 设备存在且 voice 已打开 */
static u32 exaudio_token;
static u32 exaudio_rate=48000;
static const s16 *exaudio_pending; /* 未喂完的尾巴 */
static u32 exaudio_pending_frames;

int exaudio_init(void)
{
    u32 info[16];
    if(sc_audio_info(info))return -1;          /* 无设备/快照失败 */
    if(!info[1])return -1;                     /* ready=0 */
    exaudio_rate=info[2]?info[2]:48000;
    int token=sc_audio_open(exaudio_rate,2);
    if(token<0)return -1;
    exaudio_token=(u32)token;
    exaudio_ready=1;
    exaudio_pending=0;
    exaudio_pending_frames=0;
    return 0;
}

void exaudio_shutdown(void)
{
    if(!exaudio_ready)return;
    sc_audio_control(exaudio_token,0,0);       /* command 0 = close */
    exaudio_ready=0;
    exaudio_pending=0;
    exaudio_pending_frames=0;
}

u32 exaudio_stream(const s16 *samples,u32 frames)
{
    if(!exaudio_ready||!samples||!frames)return 0;
    if(exaudio_pending_frames){
        /* 上次还有尾巴：先续喂，避免后到的音效插队把前者截断 */
        u32 done=0;
        while(done<exaudio_pending_frames){
            int n=sc_audio_submit(exaudio_token,exaudio_pending+done*
                                  2 /* 双声道交错 */,exaudio_pending_frames-done);
            if(n<=0)break;
            done+=(u32)n;
        }
        exaudio_pending+=done;
        exaudio_pending_frames-=done;
    }
    u32 done=0;
    while(done<frames){
        int n=sc_audio_submit(exaudio_token,samples+done*2,frames-done);
        if(n<=0)break;
        done+=(u32)n;
    }
    if(done<frames){
        exaudio_pending=samples+done;
        exaudio_pending_frames=frames-done;
    }else{
        exaudio_pending=0;
        exaudio_pending_frames=0;
    }
    return done;
}

void exaudio_pump(void)
{
    if(!exaudio_ready||!exaudio_pending_frames)return;
    u32 done=0;
    while(done<exaudio_pending_frames){
        int n=sc_audio_submit(exaudio_token,exaudio_pending+done*2,
                              exaudio_pending_frames-done);
        if(n<=0)break;
        done+=(u32)n;
    }
    exaudio_pending+=done;
    exaudio_pending_frames-=done;
}

int exaudio_sysfx(int kind)
{
    if(kind<0||kind>5)return -1;
    return sc_audio_effect((u32)kind);
}

/* ---------------- 程序化音效 ----------------
 * 单声道生成后左右复制成双声道交错。包络：前 10ms 线性起音
 * 防爆音，之后指数衰减到 2ms 线性收尾；噪声用 xorshift 白噪声，
 * 三角波相位累加。全部整数运算，任何帧率下行为一致。 */

/* 音效缓冲常驻到播完：登记基址，播完后在下次 tone 时回收，
 * 同时进程退出由内核按代数统一回收，双保险不泄漏。 */
#define EXTONE_MAX 16
static u8 *extone_live[EXTONE_MAX];

int exaudio_tone(int freq_hz,int ms,int wave,int volume)
{
    if(freq_hz<20||freq_hz>12000||ms<10||ms>2000)return -1;
    if(volume<0)volume=0;
    if(volume>256)volume=256;
    if(!exaudio_ready)return 0;                /* 无声卡：什么都不生成 */
    u32 frames=(u32)exaudio_rate*ms/1000;
    u32 bytes=frames*4u;                       /* 双声道 × S16 */
    u8 *raw=sc_alloc(bytes);
    if(!raw)return -1;
    /* 回收槽：找到空位或最旧的一块（简单 FIFO，音效短，16 足够） */
    int slot=-1;
    for(int i=0;i<EXTONE_MAX;i++)if(!extone_live[i]){slot=i;break;}
    if(slot<0){
        sc_free(extone_live[0]);
        for(int i=1;i<EXTONE_MAX;i++)extone_live[i-1]=extone_live[i];
        extone_live[EXTONE_MAX-1]=0;
        slot=EXTONE_MAX-1;
    }
    extone_live[slot]=raw;
    s16 *out=(s16 *)raw;
    u32 phase=0;
    u32 noise=0x1234567u;
    u32 period=(u32)exaudio_rate/(u32)freq_hz;
    if(!period)period=1;
    u32 half=period/2;
    if(!half)half=1;
    u32 rise=exaudio_rate/100;                 /* 10ms 起音段 */
    u32 tail_len=exaudio_rate/500;             /* 2ms 收尾段 */
    u32 cur_ms=0;                              /* 包络按 1ms 步进衰减 */
    int env=volume;
    for(u32 i=0;i<frames;i++){
        u32 age_ms=i*1000u/exaudio_rate;       /* i<96000，乘积不超 32 位 */
        while(cur_ms<age_ms){env=env*27/32;cur_ms++;}   /* ≈-0.65dB/ms */
        if(i<rise)env=(int)((u32)env*i/rise);  /* 起音段线性压低 */
        if(tail_len&&frames-i<tail_len)
            env=(int)((u32)env*(frames-i)/tail_len);
        int sample;
        if(wave==EXWAVE_NOISE){
            noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;
            sample=(int)((s16)(noise&0xFFFF)); /* 白噪声：全带宽能量 */
        }else if(wave==EXWAVE_TRIANGLE){
            u32 pos=phase%period;
            u32 v=pos<half?pos*65536u/half:(period-pos)*65536u/half;
            sample=(int)v-32768;
        }else{                                  /* 方波：复古芯片音主波形 */
            sample=(phase%period<half)?11000:-11000;
        }
        sample=sample*env/256;
        if(sample>32767)sample=32767;
        if(sample<-32768)sample=-32768;
        out[i*2]=(s16)sample;
        out[i*2+1]=(s16)sample;                 /* 左右同相 */
        phase++;
    }
    exaudio_stream((const s16 *)raw,frames);
    return 0;
}
