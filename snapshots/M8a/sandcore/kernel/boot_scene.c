/* mio：M8原生开机页。品牌与照片仍取用户选定主题壁纸，不重新生成
 * 被用户否定的数学Logo。页面只有安静的欢迎卡、真实显示信息与任意
 * 键提示，避免在摄影画面上叠满假进度条。Classic使用硬边浮雕，
 * Aurora使用浅色半透明卡；字形只查询已经加载的凤凰SCF。
 */
#include "boot_scene.h"
#include "gfx.h"
#include "display.h"
#include "desktop.h"
#include "theme.h"

static int boot_unit(int value){return value*display_scale()/100;}
static void boot_text(int x,int y,const char *text,int multiplier,u32 color)
{
    while(*text){
        u32 scalar;u16 rows[16];gfx_utf8_next(&text,&scalar);
        int advance=gfx_glyph16(scalar,rows);if(!advance)advance=16;
        for(int r=0;r<16;r++)for(int c=0;c<advance;c++)if(rows[r]&(0x8000u>>c))
            gfx_rgb_fill(x+boot_unit(c*multiplier),y+boot_unit(r*multiplier),
                boot_unit((c+1)*multiplier)-boot_unit(c*multiplier),
                boot_unit((r+1)*multiplier)-boot_unit(r*multiplier),color);
        x+=boot_unit(advance*multiplier);
    }
}
static void boot_number(char *out,int value)
{char reverse[12];int n=0,at=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);while(n)out[at++]=reverse[--n];out[at]=0;}
static void boot_append(char *out,const char *text)
{int n=0;while(out[n])n++;while(*text && n<95)out[n++]=*text++;out[n]=0;}
void boot_scene_draw(void)
{
    gfx_clip_reset();
    if(!desktop_wallpaper()){
        /* 壁纸尚在三环解码/缺文件时用主题语义渐变兜底，不能让
         * 未初始化的显存作欢迎页；原先选定的桌面背景稍后正常载入。 */
        u32 top=theme_color(TH_FACE_ALT),bottom=theme_color(TH_FACE);
        for(int y=0;y<GFX_H;y++){
            u32 color=0;int weight=y*256/GFX_H;
            for(int shift=0;shift<24;shift+=8)
                color|=((((top>>shift)&255)*(256-weight)+((bottom>>shift)&255)*weight)/256)<<shift;
            gfx_rgb_fill(0,y,GFX_W,1,color);
        }
    }
    int w=boot_unit(440);if(w>GFX_W-boot_unit(32))w=GFX_W-boot_unit(32);
    int h=boot_unit(104),x=(GFX_W-w)/2,y=GFX_H-h-boot_unit(20);
    int radius=theme_classic()?0:boot_unit(theme_radius());
    u32 color=theme_color(TH_PAPER),shadow=theme_color(TH_SHADOW);
    for(int row=0;row<h;row++){
        int inset=0;
        if(radius && (row<radius || row>=h-radius)){
            int dy=row<radius?radius-1-row:row-(h-radius),dx=radius;
            while(dx*dx+dy*dy>radius*radius)dx--;inset=radius-dx;
        }
        for(int col=inset;col<w-inset;col++){
            gfx_argb_pset(x+col+boot_unit(2),y+row+boot_unit(3),(theme_classic()?0xFF000000u:0x38000000u)|shadow);
            gfx_argb_pset(x+col,y+row,(theme_classic()?0xFF000000u:0xE8000000u)|color);
        }
    }
    if(theme_classic()){
        gfx_rgb_fill(x,y,w,1,theme_color(TH_HIGHLIGHT));gfx_rgb_fill(x,y,1,h,theme_color(TH_HIGHLIGHT));
        gfx_rgb_fill(x,y+h-1,w,1,theme_color(TH_DARK_EDGE));gfx_rgb_fill(x+w-1,y,1,h,theme_color(TH_DARK_EDGE));
    }
    gfx_rgb_fill(x+boot_unit(20),y+boot_unit(18),boot_unit(3),boot_unit(16),theme_color(TH_ACCENT));
    boot_text(x+boot_unit(32),y+boot_unit(16),"SANDCORE M8",1,theme_color(TH_TEXT));
    boot_text(x+boot_unit(20),y+boot_unit(40),"Welcome to true color.",1,theme_color(TH_TEXT));
    char mode[96],number[12];boot_number(mode,GFX_W);boot_append(mode," x ");
    boot_number(number,GFX_H);boot_append(mode,number);boot_append(mode," / ");
    boot_number(number,display_scale());boot_append(mode,number);boot_append(mode,"% / mio");
    boot_text(x+boot_unit(20),y+boot_unit(64),mode,1,theme_color(TH_MUTED));
    boot_text(x+boot_unit(20),y+boot_unit(84),"PRESS ANY KEY",1,theme_color(TH_ACCENT));
    gfx_swap();
}
