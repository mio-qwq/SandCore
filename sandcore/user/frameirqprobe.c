/* =====================================================================
 * mio：大帧提交期间的硬件接收探针。它是普通三环程序，不导入任何
 * 内核地址，也不自行开关中断。旧 FRAME / 新 FRAME32 的合同仍由
 * 系统调用检验；宿主只读结果、私有页表及画布，不替客体执行函数。
 *
 * 为什么只改第一行：每次仍提交完整约 8MB ARGB 缓冲，但省掉用户
 * 自己重算几百万像素的成本，便于把“硬件接收是否在提交内继续”
 * 与“用户绘制本身很慢”区分。Enter 封存结果，Esc 正常退出。
 * ===================================================================== */
#include "SCAPI.H"

/* 结果不作为新 ABI。volatile 仅防止优化器删除宿主观察的值；它
 * 不是跨进程写权限。stage=1 接收真实输入，stage=2 保留最后一帧。 */
static volatile u32 irq_probe[16];
static char irq_text[128];
static u32 irq_cpu_before[64],irq_cpu_after[64];
static u32 *pixels,theme[32];
static int window,width,height,indexed;

static void check(int condition,int where)
{
    if(!condition){irq_probe[1]++;irq_probe[2]=(u32)where;}
}

static u32 mix(u32 a,u32 b,int weight)
{
    /* 颜色来自已登记的凤凰/界面槽位。用整数通道插值形成连续
     * 真彩色，不在诊断窗口随手引入另一组无来源色号。 */
    int inverse=255-weight;
    u32 r=(((a>>16)&255)*inverse+((b>>16)&255)*weight)/255;
    u32 g=(((a>>8)&255)*inverse+((b>>8)&255)*weight)/255;
    u32 blue=((a&255)*inverse+(b&255)*weight)/255;
    return 0xFF000000u|(r<<16)|(g<<8)|blue;
}

static void text(int x,int y,const char *s,u32 color)
{
    /* 按系统规定字库取得位图，文字写进本人的源帧。这使封存后的
     * 整张源帧和内核画布能逐字节比较，而不是跳过标题或测试区域。 */
    u16 rows[16];
    while(*s){
        int advance=sc_glyph16((u32)(u8)*s++,rows);
        for(int row=0;row<16;row++)for(int col=0;col<advance;col++)
            if(x+col>=0&&x+col<width&&y+row>=0&&y+row<height&&(rows[row]&(0x8000u>>col)))
                if(indexed)((u8 *)pixels)[(y+row)*width+x+col]=PAL_UI_TEXT;
                else pixels[(y+row)*width+x+col]=0xFF000000u|color;
        x+=advance;
    }
}

int main(void)
{
    char args[128];sc_args(args,sizeof(args));indexed=equal(args,"indexed");
    window=indexed?sc_open2("Frame / IRQ",1880,1000):sc_open_rgb("Frame / IRQ",1880,1000);
    if(window<0)return 1;
    int info[5];check(sc_info(window,info)==0,10);
    width=info[2];height=info[3];u32 bytes=(u32)width*height*(indexed?1:4);
    pixels=(u32 *)sc_alloc(bytes);if(!pixels)return 2;
    sc_theme(theme);sc_cpu(irq_cpu_before);
    u32 face=theme[8+SC_THEME_FACE],accent=theme[8+SC_THEME_ACCENT];
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        if(indexed)((u8 *)pixels)[y*width+x]=(u8)(PAL_UI_CYAN+((x*7/width+y*3/height)&7));
        else pixels[y*width+x]=mix(face,accent,((x*173/width)+(y*81/height))&255);
    }
    for(int y=0;y<150&&y<height;y++)for(int x=0;x<width;x++){
        if(indexed)((u8 *)pixels)[y*width+x]=PAL_UI_PANEL;
        else pixels[y*width+x]=0xFF000000u|face;
    }
    text(32,28,"FULL FRAME / LIVE INPUT",theme[8+SC_THEME_TEXT]);
    text(32,58,"Integer ARGB. Phoenix font. Real PS/2 input.",theme[8+SC_THEME_TEXT]);
    text(32,94,"ENTER seals this frame; ESC releases all resources.",theme[8+SC_THEME_TEXT]);
    /* 参数错误必须在改变任何像素之前失败，不能因为开 IRQ 放松
     * owner、完整用户映射、格式或精确字节数检查。 */
    if(indexed){
        check(sc_frame(window,pixels,bytes-1)==-1,20);
        check(sc_frame(window,(const void *)0x10000,bytes)==-1,21);
        check(sc_frame(window+1000,pixels,bytes)==-1,22);
        check(sc_frame32(window,pixels,bytes)==-1,23);
        check(sc_frame(window,pixels,bytes)==0,24);
    }else{
        check(sc_frame32(window,pixels,bytes-4)==-1,20);
        check(sc_frame32(window,(const u32 *)0x10000,bytes)==-1,21);
        check(sc_frame32(window+1000,pixels,bytes)==-1,22);
        check(sc_frame(window,pixels,width*height)==-1,23);
        check(sc_frame32(window,pixels,bytes)==0,24);
    }
    irq_probe[4]=bytes;irq_probe[11]=(u32)pixels;irq_probe[12]=(u32)window;
    irq_probe[13]=(u32)width;irq_probe[14]=(u32)height;irq_probe[0]=1;
    while(irq_probe[0]==1){
        int key;
        while((key=sc_key())>=0){
            if(key==27){sc_free(pixels);return 0;}
            if(key==10){irq_probe[0]=2;break;}
            if(key>=32&&key<127&&irq_probe[10]<sizeof(irq_text)-1){
                irq_text[irq_probe[10]++]=(char)key;irq_text[irq_probe[10]]=0;
            }
        }
        int pointer[6];check(sc_pointer(window,pointer)==0,30);
        irq_probe[5]+=pointer[3]&1;irq_probe[6]+=pointer[4]&1;
        irq_probe[7]=(u32)pointer[0];irq_probe[8]=(u32)pointer[1];irq_probe[9]=(u32)pointer[2];
        /* 源帧大部分不变，但每次第一行都改变，不能只测试“不置
         * dirty”的相同帧分支。alpha 交替证明不透明旁表也能更新。 */
        if(indexed){((u8 *)pixels)[0]=(u8)(PAL_UI_CYAN+(irq_probe[3]&7));check(sc_frame(window,pixels,bytes)==0,31);}
        else{
            pixels[0]=(pixels[0]&0x00FFFFFFu)|((irq_probe[3]&1)?0x80000000u:0xFF000000u);
            check(sc_frame32(window,pixels,bytes)==0,31);
        }
        irq_probe[3]++;
        sc_yield();
    }
    sc_cpu(irq_cpu_after);irq_probe[15]=1;
    while(sc_key()!=27)sc_yield();sc_free(pixels);return irq_probe[1]?3:0;
}
