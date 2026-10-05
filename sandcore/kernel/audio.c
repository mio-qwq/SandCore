#include "audio.h"
#include "task.h"
#include "auth.h"
#include "memory.h"
#include "timer.h"
#include "fs.h"
#define VOICES 8
#define QUEUE 8192u
#define DMA_FRAMES 256u
#define DMA_TARGET 8u
typedef signed short s16;
typedef struct {
    u32 token,generation,rate,channels,head,tail,phase,step,volume,paused,finished,consumed,starved;
    int owner;s16 samples[QUEUE*2];
} voice_t;
typedef struct {u32 address,length;} descriptor_t;
static voice_t *voices;
static descriptor_t *descriptors;
static s16 *dma;
static u32 ready,mixer,master,irq_line,pci_address,next_token,submitted,completed,refills,underruns,last_civ,engine_running;
static u32 master_volume=256,dma_queued,produced,total_clipped,irq_events;
static u32 power_pending,power_ticks;
typedef struct {char path[64];u32 offset,end,token,rate,channels,block;} effect_t;
static effect_t effects[4];
static u32 last_effect[6],effect_seen;
static u32 pci_read(u32 address,u32 offset){outl(0xCF8,address|(offset&0xFC));return inl(0xCFC);}
static void pci_write(u32 address,u32 offset,u32 value){outl(0xCF8,address|(offset&0xFC));outl(0xCFC,value);}
static voice_t *voice(int pid,u32 token)
{
    for(int i=0;i<VOICES;i++)if(voices[i].token==token && voices[i].owner==pid
        && (!pid || voices[i].generation==task_generation(pid)))return &voices[i];return 0;
}
void audio_init(void)
{
    ready=0;pci_address=0;engine_running=0;
    /* 初期仅适配QEMU模拟的ICH 82801AA AC97，不把其它音频PCI设备
     * 猜成相同寄存器布局。未找到/初始化失败时GUI与串口继续工作。 */
    for(u32 bus=0;bus<256 && !pci_address;bus++)for(u32 dev=0;dev<32 && !pci_address;dev++){
        u32 first=0x80000000u|(bus<<16)|(dev<<11);if((pci_read(first,0)&0xFFFF)==0xFFFF)continue;
        u32 functions=pci_read(first,0xC)&0x00800000u?8:1;
        for(u32 fn=0;fn<functions;fn++){
            u32 address=first|(fn<<8);if(pci_read(address,0)==0x24158086u){pci_address=address;break;}
        }
    }
    if(!pci_address)return;
    u32 a=pci_read(pci_address,0x10),b=pci_read(pci_address,0x14);
    if(!(a&1) || !(b&1) || (a&~0xFFFFu) || (b&~0xFFFFu))return;
    mixer=a&~3u;master=b&~3u;irq_line=pci_read(pci_address,0x3C)&255;
    if(!mixer || !master || irq_line>=16 || irq_line==0 || irq_line==1 || irq_line==3 || irq_line==4 || irq_line==12)return;
    u32 pages=(sizeof(voice_t)*VOICES+4095)/4096;
    voices=(voice_t *)pframe_alloc_run(pages);descriptors=(descriptor_t *)pframe_alloc();dma=(s16 *)pframe_alloc_run(8);
    if(!voices || !descriptors || !dma){if(voices)pframe_free_run((u32)voices,pages);if(descriptors)pframe_free((u32)descriptors);
        if(dma)pframe_free_run((u32)dma,8);voices=0;descriptors=0;dma=0;return;}
    for(u32 i=0;i<pages*4096;i++)((u8 *)voices)[i]=0;
    for(u32 i=0;i<32768;i++)((u8 *)dma)[i]=0;
    pci_write(pci_address,4,(pci_read(pci_address,4)&0xFFFFu)|5u); /* IO+bus master，status不写回 */
    outl((u16)(master+0x2C),2);outw((u16)mixer,0);
    u32 budget=100000;while(budget-- && !(inl((u16)(master+0x30))&0x100u))io_wait();
    if(!(inl((u16)(master+0x30))&0x100u))goto failed;
    outw((u16)(mixer+2),0);outw((u16)(mixer+0x18),0); /* 主音量/PCM不静音 */
    outw((u16)(mixer+0x2A),0); /* 固定48kHz，由整数软件重采样处理其它速率 */
    outb((u16)(master+0x1B),2);budget=100000;
    while(budget-- && (inb((u16)(master+0x1B))&2))io_wait();if(inb((u16)(master+0x1B))&2)goto failed;
    for(u32 i=0;i<32;i++){descriptors[i].address=(u32)(dma+i*DMA_FRAMES*2);descriptors[i].length=DMA_FRAMES*2|0x80000000u;}
    outl((u16)(master+0x10),(u32)descriptors);outw((u16)(master+0x16),0x1C);
    if(irq_line<8)outb(0x21,(u8)(inb(0x21)&~(1u<<irq_line)));
    else {outb(0xA1,(u8)(inb(0xA1)&~(1u<<(irq_line-8))));outb(0x21,(u8)(inb(0x21)&~4u));}
    last_civ=0;dma_queued=produced=next_token=0;ready=1;return;
failed:
    outb((u16)(master+0x1B),0);pframe_free_run((u32)voices,pages);pframe_free((u32)descriptors);pframe_free_run((u32)dma,8);
    voices=0;descriptors=0;dma=0;
}
void audio_irq(u32 irq)
{
    if(!ready || irq!=irq_line)return;u16 status=inw((u16)(master+0x16));if(status&0x1C){outw((u16)(master+0x16),status&0x1C);irq_events++;}
}
static s16 clip(int sample)
{if(sample>32767){total_clipped++;return 32767;}if(sample<-32768){total_clipped++;return -32768;}return (s16)sample;}
static void mix(u32 index)
{
    s16 *target=dma+index*DMA_FRAMES*2;
    for(u32 f=0;f<DMA_FRAMES;f++){
        int left=0,right=0;
        for(int i=0;i<VOICES;i++){
            voice_t *v=&voices[i];if(!v->token || v->paused)continue;
            u32 available=v->head-v->tail;if(!available){if(!v->finished)v->starved++;continue;}
            u32 at=(v->tail&(QUEUE-1))*2,next=available>1?((v->tail+1)&(QUEUE-1))*2:at;
            int fraction=(int)(v->phase>>8),l=v->samples[at],r=v->samples[at+1];
            l+=((int)v->samples[next]-l)*fraction/256;r+=((int)v->samples[next+1]-r)*fraction/256;
            left+=l*(int)v->volume/256;right+=r*(int)v->volume/256;
            v->phase+=v->step;u32 consume=v->phase>>16;v->phase&=65535;
            if(consume>available)consume=available;v->tail+=consume;v->consumed+=consume;
        }
        target[f*2]=clip(left*(int)master_volume/256);target[f*2+1]=clip(right*(int)master_volume/256);
    }
    refills++;
}
static void power_poll(void)
{
    if(!power_pending || (u32)(sc_ticks-power_ticks)<(ready?251u:10u))return;
    int active=0;if(ready)for(int i=0;i<4;i++)if(effects[i].token)active=1;
    if(active && (u32)(sc_ticks-power_ticks)<=1000)return;
    if(ready)outb((u16)(master+0x1B),0);
    if(power_pending==2){u32 budget=65536;while(budget-- && (inb(0x64)&2)){}outb(0x64,0xFE);}
    else outw(0x604,0x2000);power_pending=0;
}
void audio_poll(void)
{
    if(!ready){power_poll();return;}
    /* 完成量来自CIV/真实DMA推进而非PIT=帧率。目标8个256帧块约
     * 43ms，给磁盘/GUI调度留余量而不积压半秒输入音效。 */
    if(engine_running){
        u32 current=inb((u16)(master+0x14))&31u,delta=(current-last_civ)&31u;
        if(delta>dma_queued)delta=dma_queued;completed+=delta;dma_queued-=delta;last_civ=current;
        u16 status=inw((u16)(master+0x16));
        if((status&1u) && dma_queued){
            /* LVI最后一块完成后CIV尚未向前移动，将尾块按真实DCH补记。
             * 不计未播放的正文；随后LVI更新会从PIV继续。 */
            if(current==((produced-1)&31u)){completed++;dma_queued--;last_civ=(current+1)&31u;}
            int starved=0;for(int i=0;i<VOICES;i++)if(voices[i].token && !voices[i].paused
                && voices[i].head!=voices[i].tail)starved=1;if(starved)underruns++;
        }
    }
    for(int e=0;e<4;e++)if(effects[e].token){
        effect_t *s=&effects[e];voice_t *v=voice(0,s->token);if(!v){s->token=0;continue;}
        for(u32 budget=4;budget && s->offset<s->end;budget--){
            u8 data[2048];u32 frames=QUEUE-(v->head-v->tail);if(frames>sizeof(data)/s->block)frames=sizeof(data)/s->block;
            u32 bytes=frames*s->block;if(bytes>s->end-s->offset)bytes=s->end-s->offset;
            if(bytes){int n=fs_read_at(s->path,data,bytes,s->offset);
                if(n!=(int)bytes || audio_submit(0,s->token,data,bytes/s->block)<0){v->finished=1;s->offset=s->end;break;}
                else s->offset+=bytes;}
            else break;
        }
        if(s->offset==s->end){v->finished=1;if(v->head==v->tail){v->token=0;s->token=0;}}
    }
    while(dma_queued<DMA_TARGET){
        int active=0;for(int i=0;i<VOICES;i++)if(voices[i].token && !voices[i].paused && voices[i].head!=voices[i].tail)active=1;
        if(!active)break;
        u32 index=produced&31u;mix(index);produced++;dma_queued++;submitted++;
        __asm__ __volatile__("":::"memory");outb((u16)(master+0x15),(u8)index);
    }
    if(!engine_running && dma_queued){last_civ=completed&31u;outb((u16)(master+0x1B),0x1D);engine_running=1;}
    if(engine_running && !dma_queued){outb((u16)(master+0x1B),0);engine_running=0;}
    power_poll();
}
int audio_open(int pid,u32 rate,u32 channels)
{
    if(!ready)return -2;if(rate<8000 || rate>192000 || channels<1 || channels>2 || next_token==0x7FFFFFFFu)return -1;
    /* 两路专留开关机/系统音效；普通用户按UID限两路，不能通过不断
     * 创建进程占满所有声道。剩余六路仍可跨用户并行混音。 */
    if(pid){int count=0;for(int i=0;i<VOICES;i++)if(voices[i].token && voices[i].owner
            && auth_uid(voices[i].owner)==auth_uid(pid))count++;
        if(count>=2)return -4;}
    for(int i=0;i<(pid?VOICES-2:VOICES);i++)if(!voices[i].token){voice_t *v=&voices[i];
        for(u32 k=0;k<sizeof(*v);k++)((u8 *)v)[k]=0;
        v->owner=pid;v->generation=task_generation(pid);v->rate=rate;v->channels=channels;
        v->step=(rate/48000u)*65536u+(rate%48000u)*65536u/48000u;v->volume=256;v->token=++next_token;return (int)v->token;}
    return -4;
}
int audio_submit(int pid,u32 token,const void *samples,u32 frames)
{
    if(!ready)return -2;voice_t *v=voice(pid,token);if(!v || frames>4096 || v->finished)return -1;
    u32 count=QUEUE-(v->head-v->tail);if(count>frames)count=frames;
    const s16 *input=(const s16 *)samples;
    for(u32 i=0;i<count;i++){u32 at=((v->head+i)&(QUEUE-1))*2;v->samples[at]=input[i*v->channels];
        v->samples[at+1]=input[i*v->channels+(v->channels==2)];}
    v->head+=count;return count?(int)count:frames?-6:0;
}
int audio_control(int pid,u32 token,u32 command,u32 value)
{
    if(!ready)return -2;
    if(command==5){if(!auth_can_manage(pid) || value>256)return -5;master_volume=value;return 0;}
    voice_t *v=voice(pid,token);if(!v)return -1;
    if(command==0){v->token=0;return 0;}if(command==1){v->paused=value!=0;return 0;}
    if(command==2 && value<=256){v->volume=value;return 0;}
    if(command==3){v->head=v->tail=v->phase=v->finished=v->consumed=0;return 0;}
    if(command==4){v->finished=1;return 0;}return -1;
}
int audio_status(int pid,u32 token,u32 out[16])
{
    if(!ready)return -2;voice_t *v=voice(pid,token);if(!v)return -1;for(u32 i=0;i<16;i++)out[i]=0;
    out[0]=1;out[1]=v->rate;out[2]=v->channels;out[3]=v->head-v->tail;out[4]=QUEUE-out[3];out[5]=v->paused;
    out[6]=v->finished;out[7]=v->consumed;out[8]=v->starved;out[9]=v->volume;return 0;
}
void audio_stop_owner(int pid){if(voices)for(int i=0;i<VOICES;i++)if(voices[i].token && voices[i].owner==pid)voices[i].token=0;}
void audio_info(u32 out[16])
{for(u32 i=0;i<16;i++)out[i]=0;out[0]=1;out[1]=ready;out[2]=48000;out[3]=2;out[4]=16;out[5]=master_volume;
    out[6]=submitted;out[7]=completed;out[8]=underruns;out[9]=total_clipped;out[10]=irq_events;out[11]=dma_queued;}
int audio_busy(void)
{
    if(!ready)return 0;if(dma_queued)return 1;
    for(int i=0;i<4;i++)if(effects[i].token)return 1;
    for(int i=0;i<VOICES;i++)if(voices[i].token && !voices[i].paused
        && voices[i].head!=voices[i].tail)return 1;
    return 0;
}
int audio_effect(int kind)
{
    static const char *paths[6]={"SYS/SOUND/START.WAV","SYS/SOUND/STOP.WAV","SYS/SOUND/NOTICE.WAV","SYS/SOUND/ERROR.WAV","SYS/SOUND/COMPLETE.WAV","SYS/SOUND/QUESTION.WAV"};
    if(!ready || kind<0 || kind>=6)return -2;
    if((effect_seen&(1u<<kind)) && (u32)(sc_ticks-last_effect[kind])<25)return -6;
    int slot=-1;for(int i=0;i<4;i++)if(!effects[i].token){slot=i;break;}if(slot<0)return -6;
    u8 h[44];if(fs_read_at(paths[kind],h,44,0)!=44)return -1;
    if(*(u32 *)h!=0x46464952u || *(u32 *)(h+8)!=0x45564157u || *(u32 *)(h+12)!=0x20746D66u
        || *(u32 *)(h+16)!=16 || *(u16 *)(h+20)!=1 || *(u16 *)(h+34)!=16 || *(u32 *)(h+36)!=0x61746164u)return -1;
    u32 channels=*(u16 *)(h+22),rate=*(u32 *)(h+24),bytes=*(u32 *)(h+40),info[2];
    if(channels<1 || channels>2 || bytes%(channels*2) || fs_stat(paths[kind],info) || bytes>info[1]-44)return -1;
    int token=audio_open(0,rate,channels);if(token<0)return token;effect_t *s=&effects[slot];
    for(u32 i=0;i<64;i++)s->path[i]=0;for(u32 i=0;paths[kind][i];i++)s->path[i]=paths[kind][i];
    s->offset=44;s->end=44+bytes;s->rate=rate;s->channels=channels;s->block=channels*2;s->token=(u32)token;
    last_effect[kind]=sc_ticks;effect_seen|=1u<<kind;return 0;
}
int audio_power(int pid,int restart)
{
    if(!auth_can_manage(pid) || (restart!=0 && restart!=1))return -5;
    if(power_pending)return -6;
    if(ready)(void)audio_effect(1);power_pending=restart?2:1;power_ticks=sc_ticks;
    /* 无声卡也从公共poll执行，留100ms让串口请求ACK和Shell退出码
     * 发送完；不能在三环系统调用内部直接断电，截掉已接受的回复。
     * 仍无声音等待，不因缺设备卡住电源管理。 */
    return 0;
}
