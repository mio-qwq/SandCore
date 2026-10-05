/* =====================================================================
 * mio：局部与完整合成等价的普通三环探针，两层窗口/真实ARGB变化。
 * 数字键只改变一小段行；F对已置顶且未最大化窗口执行原恢复调用，
 * 只请求完整合成，画布/几何不变。验证器分别读PCI，比较同一场景
 * 两种真实路径，不向内核dirty/视口/页表写值来制造测试结果。
 * ===================================================================== */
#include "SCAPI.H"
volatile u32 damage_probe[12];
static u32 *pixels,theme[32];
static int win,width,height;
static u32 mix(u32 a,u32 b,int t)
{
    u32 rgb=0;
    for(int shift=0;shift<24;shift+=8){
        int first=(int)((a>>shift)&255),last=(int)((b>>shift)&255);
        rgb|=(u32)(first+(last-first)*t/255)<<shift;
    }
    return rgb;
}
static void label(int x,int y,const char *text)
{
    u16 rows[16];u32 ink=theme[8+SC_THEME_TEXT];
    while(*text){
        int advance=sc_glyph16((u32)(u8)*text++,rows);if(advance<1)advance=8;
        for(int r=0;r<16;r++)for(int c=0;c<advance;c++)
            if(x+c<width && y+r<height && (rows[r]&(0x8000u>>c)))pixels[(y+r)*width+x+c]=0xFF000000u|ink;
        x+=advance;
    }
}
int main(void)
{
    sc_theme(theme);
    int back=sc_open_rgb("Layer behind",840,480);if(back<0)return 1;
    sc_fill_rgb(back,0,0,1920,1080,theme[8+SC_THEME_FACE_ALT]);
    sc_text_rgb(back,20,24,"Layer behind / rounded corners / shadow",theme[8+SC_THEME_TEXT]);
    win=sc_open_rgb("Damage check",640,360);if(win<0)return 1;
    int info[5];sc_info(win,info);width=info[2];height=info[3];
    pixels=sc_alloc((u32)width*height*4);if(!pixels)return 1;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)
        pixels[y*width+x]=0xFF000000u|mix(theme[8+SC_THEME_PAPER],theme[8+SC_THEME_FACE_ALT],y*255/height);
    label(24,24,"PARTIAL / SAME SCENE / FULL");
    label(24,52,"1 opaque / 2 alpha / 3 clear / 4 edge");
    label(24,80,"F: full redraw / Esc: close both");
    sc_frame32(win,pixels,(u32)width*height*4);
    damage_probe[0]=1;damage_probe[3]=(u32)win;damage_probe[4]=(u32)back;
    damage_probe[5]=(u32)width;damage_probe[6]=(u32)height;damage_probe[7]=(u32)pixels;
    for(;;){
        int key=sc_key();if(key==27)break;
        if(key>='1' && key<='4'){
            int edge=key=='4';int yy=edge?height-4:120;
            int xx=edge?width-36:40,w=edge?36:100,h=edge?4:36;
            if(yy+h>height)h=height-yy;
            u32 alpha=key=='2'?0x80000000u:key=='3'?0u:0xFF000000u;
            for(int y=yy;y<yy+h;y++)for(int x=xx;x<xx+w;x++)
                pixels[y*width+x]=alpha|mix(theme[8+SC_THEME_ACCENT],theme[8+SC_THEME_GOLD],(x-xx)*255/w);
            if(sc_frame32(win,pixels,(u32)width*height*4))damage_probe[1]++;
            damage_probe[8]=(u32)key;damage_probe[2]++;
        }
        if(key=='f'){
            if(sc_window(win,2))damage_probe[1]++;
            damage_probe[9]++;
        }
        sc_yield();
    }
    sc_free(pixels);return damage_probe[1]?1:0;
}
