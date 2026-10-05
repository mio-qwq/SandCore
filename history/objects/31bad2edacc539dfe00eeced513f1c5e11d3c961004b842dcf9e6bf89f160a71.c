/* =====================================================================
 * mio：M8 显示后端。实模式引导仍使用13h，进入桌面后才探测 PCI 标准
 * VGA 的 Bochs DISPI。缺扩展时不执行端口写模式，原 VGA 路径继续运行。
 * 显存地址必须取固件分配的 BAR0，不能把某台 QEMU 上的 FD000000
 * 当成所有机器通用地址。所有前台颜色仍来自 GFX.md 权威调色板。
 * ===================================================================== */
#include "display.h"
#include "gfx.h"
#include "fs.h"
#include "timer.h"
#include "wm.h"
static const int modes[6][2]={
    {
        320,200
    },{
        640,480
    },{
        800,600
    },{
        1024,768
    },{
        1280,720
    },{
        1920,1080
    }
};
static u32 framebuffer,colors[256],generation;
static int backend,scale_percent=100,old_w,old_h,old_scale,pending;
static u32 deadline;
static u16 reg_read(u16 index) {
    outw(0x1CE,index);
    return inw(0x1CF);
}
static void reg_write(u16 index,u16 value) {
    outw(0x1CE,index);
    outw(0x1CF,value);
}
static u32 pci_read(int bus,int dev,int fn,int offset)
{
    outl(0xCF8,0x80000000u|((u32)bus<<16)|((u32)dev<<11)|((u32)fn<<8)|(offset&252));
    return inl(0xCFC);
}
static int mode_index(int w,int h)
{
    for(int i=0;i<6;i++) if(modes[i][0]==w && modes[i][1]==h)return i;
    return -1;
}
static int set_mode(int w,int h,int scale)
{

    int index=mode_index(w,h);
    if(index<0 || (scale!=100 && scale!=150 && scale!=200) || (index && !framebuffer))return -1;
    /* gfx_resize 先备好 RAM。显卡尚未切换时 OOM 可以无痕失败；成功后
     * 所有画布仍独立存在，窗口管理器按新工作区重排，不终止用户程序。 */
    if(gfx_resize(w,h))return -2;
    if(framebuffer) {

        reg_write(4,0);
        if(index) {

            reg_write(1,(u16)w);
            reg_write(2,(u16)h);
            reg_write(3,32);
            reg_write(6,(u16)w);
            reg_write(8,0);
            reg_write(9,0);
            reg_write(4,0x41);
/* ENABLE|LFB，不保留上一模式的随机显存内容 */

        }

    }
    backend=index?1:0;
    scale_percent=scale;
    generation++;
    wm_display_changed();
    return 0;
}
void display_color(int slot,u8 r,u8 g,u8 b)
{
    if(slot>=0 && slot<256)colors[slot]=((u32)r<<16)|((u32)g<<8)|b;
}
int display_scale(void){
    return scale_percent;
}
void display_palette(u32 *out){
    for(int i=0;i<256;i++)out[i]=colors[i];
}
void display_present(const u8 *pixels,int width,int height)
{

    if(backend) {

        volatile u32 *out=(volatile u32 *)framebuffer;
        for(int i=0;i<width*height;i++)out[i]=colors[pixels[i]];

    }
    else {

        /* 保留垂直回扫路径，但老设备读不到回扫位时不能永远忙等。
         * 有界等待后仍提交，使 panic/回退桌面总有机会显示。 */
        u32 limit=1000000;
        while(limit-- && (inb(0x3DA)&8));
        limit=1000000;
        while(limit-- && !(inb(0x3DA)&8));
        volatile u32 *out=(volatile u32 *)0xA0000;
        const u32 *src=(const u32 *)pixels;
        for(int i=0;i<width*height/4;i++)out[i]=src[i];

    }
}
static int parse_integer(const char *text,const char *key,int *value)
{

    int n=0;
    while(key[n])n++;
    for(int i=0;text[i];i++)if(i==0 || text[i-1]=='\n') {

        int j=0;
        while(j<n && text[i+j]==key[j])j++;
        if(j!=n)continue;
        int v=0,count=0;
        const char *p=text+i+n;
        while(*p>='0' && *p<='9') {
            if(++count>5)return -1;
            v=v*10+*p++-'0';

        }
        if(!count || (*p && *p!='\r' && *p!='\n'))return -1;
        *value=v;
        return 0;

    }
    return -1;
}
void display_init(void)
{

    framebuffer=0;
    for(int bus=0;bus<256 && !framebuffer;bus++)for(int dev=0;dev<32;dev++) {

        if(pci_read(bus,dev,0,0)!=0x11111234u)continue;
        u32 bar=pci_read(bus,dev,0,0x10),cmd=pci_read(bus,dev,0,4);
        /* 仅支持32位 memory BAR。被固件禁用或未分配的设备不能猜测映射。 */
        if((bar&7)==0 && (bar&~15u) && (cmd&2) && reg_read(0)>=0xB0C2 && reg_read(0)<=0xB0C5)
        framebuffer=bar&~15u;

    }
    int w=1024,h=768,s=100;
    char config[256];
    int n=fs_read("SYS/DISPLAY.CFG",config,255);
    if(n>0) {

        config[n]=0;
        int valid=1;
        const char *magic="SCFG1MIO";
        for(int i=0;i<8;i++)if(config[i]!=magic[i])valid=0;
        if(n<9 || (config[8]!='\n' && config[8]!='\r'))valid=0;
        if(!valid || parse_integer(config,"width=",&w) || parse_integer(config,"height=",&h)
        || parse_integer(config,"scale=",&s) || mode_index(w,h)<0 || (s!=100&&s!=150&&s!=200)) {
            w=1024;
            h=768;
            s=100;

        }

    }
    if(set_mode(w,h,s))set_mode(320,200,100);
}
static char *put_dec(char *out,int value)
{
    char b[12];
    int n=0;
    do{
        b[n++]=(char)('0'+value%10);
        value/=10;

    }
    while(value);
    while(n)*out++=b[--n];
    return out;
}
static char *put_text(char *out,const char *text)
{
    while(*text)*out++=*text++;
    return out;
}
int display_apply(int w,int h,int s,int action)
{

    if(action==2) {

        if(pending){
            int r=set_mode(old_w,old_h,old_scale);
            if(r)return r;
            pending=0;

        }
        return 0;

    }
    if(action==1) {

        char config[96],*p=put_text(config,"SCFG1MIO\nwidth=");
        p=put_dec(p,GFX_W);
        p=put_text(p,"\nheight=");
        p=put_dec(p,GFX_H);
        p=put_text(p,"\nscale=");
        p=put_dec(p,scale_percent);
*p++='\n';
        if(fs_write("SYS/DISPLAY.CFG",(u8 *)config,(u32)(p-config))!=p-config)return -3;
        pending=0;
        return 0;

    }
    if(action!=0)return -1;
    /* 第二次预览仍回到最初的已确认模式，不能用上一个未经确认的模式
     * 覆盖安全锚点。15秒截止取 PIT，不依赖某个设置程序是否继续运行。 */
    int previous_w=GFX_W,previous_h=GFX_H,previous_s=scale_percent;
    int result=set_mode(w,h,s);
    if(result)return result;
    if(!pending){
        old_w=previous_w;
        old_h=previous_h;
        old_scale=previous_s;

    }
    pending=1;
    deadline=sc_ticks+1500;
    return 0;
}
void display_poll(void)
{
    if(pending && (i32)(sc_ticks-deadline)>=0)display_apply(0,0,0,2);
}
void display_info(u32 *out)
{
    out[0]=GFX_W;
    out[1]=GFX_H;
    out[2]=scale_percent;
    out[3]=backend;
    out[4]=generation;
    out[5]=framebuffer?63:1;
    out[6]=pending?(deadline>sc_ticks?(deadline-sc_ticks+99)/100:0):0;
    out[7]=1920;
}
