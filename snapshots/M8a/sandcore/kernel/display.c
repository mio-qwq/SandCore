/* =====================================================================
 * mio：M8 显示后端。实模式引导仍使用13h，进入桌面后才探测 PCI 标准
 * VGA 的 Bochs DISPI。缺扩展时不执行端口写模式，原 VGA 路径继续运行。
 * 显存地址必须取固件分配的 BAR0，不能把某台 QEMU 上的 FD000000
 * 当成所有机器通用地址。旧槽位仍来自GFX.md；新RGB颜色不再被
 * 调色板量化，控件用已登记语义色，照片保留自己的真实采样值。
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
u32 display_rgb(u8 slot) { return colors[slot]; }
u8 display_index(u32 rgb)
{
    /* 回退设备只有DAC槽位，确实不能显示任意RGB。最近色只发生在
     * 这个兼容边界，绝不能先把照片量化，再称32bpp输出为真彩色。 */
    u32 best=0xFFFFFFFFu; u8 selected=0;
    int r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    for(int i=0;i<256;i++) {
        int dr=r-(int)((colors[i]>>16)&255),dg=g-(int)((colors[i]>>8)&255),db=b-(int)(colors[i]&255);
        u32 distance=(u32)(dr*dr+dg*dg+db*db);
        if(distance<best) { best=distance; selected=(u8)i; if(!distance) break; }
    }
    return selected;
}
void display_present_rgb(const u32 *pixels,int width,int height)
{
    /* 原生后台已经是RGB，直接搬到PCI BAR0。高字节的透明度只在
     * 软件合成阶段有意义，显存的XRGB保留字节清零以避免设备差异。 */
    if(!backend) return;
    u32 *out=(u32 *)framebuffer;u32 count=(u32)width*height;
    /* gfx的原生后台严格为XRGB（高字节0），可整段字串搬移；不用
     * 每像素掩码+分支+一次C循环控制。x86整数REP，不引入SIMD/libc。
     * 显存与RAM仍由CPU复制，无GPU加速/双硬件页翻转的虚假声明。 */
    __asm__ __volatile__("cld; rep movsl":"+D"(out),"+S"(pixels),"+c"(count)::"memory");
}
void display_present_rgb_rect(const u32 *pixels,int x,int y,int w,int h,int stride)
{
    if(!backend || w<=0 || h<=0)return;
    int right=x+w,bottom=y+h;
    if(x<0)x=0;
    if(y<0)y=0;
    if(right>GFX_W)right=GFX_W;
    if(bottom>GFX_H)bottom=GFX_H;
    if(right<=x || bottom<=y)return;
    for(int row=y;row<bottom;row++) {
        u32 *out=(u32 *)framebuffer+row*stride+x;const u32 *source=pixels+row*stride+x;
        u32 count=(u32)(right-x);
        __asm__ __volatile__("cld; rep movsl":"+D"(out),"+S"(source),"+c"(count)::"memory");
    }
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
int display_config_check(const char *path)
{
    /* mio：编辑器不能使用“只查找到第一条width”的宽松启动读取
     * 作为保存合同，否则重复/尾部坏值隐藏在已接受的前缀之后。
     * 独立新调用严格检查所有行；不改变旧启动的设备回退策略。 */
    u32 info[2];char config[256];
    if(fs_stat(path,info) || info[0]!=1 || !info[1] || info[1]>=sizeof(config))return -2;
    int n=fs_read(path,config,sizeof(config)-1);
    if(n<0 || (u32)n!=info[1])return -2;
    for(int i=0;i<n;i++)if(!config[i])return -1;
    config[n]=0;
    char *p=config;int first=1,seen=0,w=0,h=0,s=0;
    while(*p){
        char *row=p;while(*p && *p!='\n')p++;if(*p)*p++=0;
        int len=0;while(row[len])len++;if(len && row[len-1]=='\r')row[--len]=0;
        if(first){first=0;const char *magic="SCFG1MIO";int i=0;while(magic[i] && row[i]==magic[i])i++;if(i!=8 || row[i])return -1;continue;}
        if(!*row || *row=='#')continue;
        int bit=0,*out=0;const char *value=0;
        if(row[0]=='w' && row[1]=='i' && row[2]=='d' && row[3]=='t' && row[4]=='h' && row[5]=='='){bit=1;out=&w;value=row+6;}
        else if(row[0]=='h' && row[1]=='e' && row[2]=='i' && row[3]=='g' && row[4]=='h' && row[5]=='t' && row[6]=='='){bit=2;out=&h;value=row+7;}
        else if(row[0]=='s' && row[1]=='c' && row[2]=='a' && row[3]=='l' && row[4]=='e' && row[5]=='='){bit=4;out=&s;value=row+6;}
        else return -1;
        if(seen&bit || !*value)return -1;
        seen|=bit;
        int count=0,v=0;while(*value){if(*value<'0' || *value>'9' || ++count>5)return -1;v=v*10+*value++-'0';}*out=v;
    }
    return seen==7 && mode_index(w,h)>=0 && (s==100 || s==150 || s==200)?0:-1;
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
