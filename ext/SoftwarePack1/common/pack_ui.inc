/* 包内布局辅助：平台 NUI/SCAPI 保持原样，所有位置仍用逻辑单位。 */
#include "pack_colors.h"
#include "pack_identity.h"
static void pk_text(int x,int y,const char *s,u32 rgb)
{
    u32 saved=ui_colors[PAL_UI_TEXT];
    ui_colors[PAL_UI_TEXT]=rgb;ui_text(x,y,s,PAL_UI_TEXT);
    ui_colors[PAL_UI_TEXT]=saved;
}
static void pk_border(int x,int y,int w,int h,u32 rgb)
{
    if(w<1||h<1)return;
    ui_rect_rgb(x,y,w,1,rgb);ui_rect_rgb(x,y+h-1,w,1,rgb);
    ui_rect_rgb(x,y,1,h,rgb);ui_rect_rgb(x+w-1,y,1,h,rgb);
}
static void pk_label(int x,int y,int w,const char *s,int slot)
{
    char text[160];ex_text_cut(text,sizeof(text),s,w);
    ui_text(x,y,text,slot);
}
/* SC_READ 不承诺结尾 NUL。只在实际读到的位置终止，避免栈垃圾
 * 被当成配置、成绩或路径继续解析。 */
static int pk_read_text(const char *path,char *buf,int cap)
{
    if(cap<1)return -1;
    int n=sc_read(path,buf,cap-1);
    if(n<0){buf[0]=0;return -1;}
    if(n>=cap)n=cap-1;
    buf[n]=0;return n;
}
static void pk_footer(const char *s)
{
    char text[160];ex_text_cut(text,sizeof(text),s,UI_W-108);ui_footer(text);
    ui_small_control(PK_ABOUT_ID,UI_W-76,UI_H-(ui_compact?22:30),68,"About",0);
}
/* 爱心直接画成像素图，不依赖凤凰字库是否含 Unicode/emoji 字形。 */
static void pk_heart(int x,int y)
{
    static const u16 rows[11]={0x71C,0xFBE,0xFFF,0xFFF,0xFFF,0x7FE,0x3FC,0x1F8,0x0F0,0x060,0};
    for(int r=0;r<11;r++)for(int c=0;c<12;c++)if(rows[r]&(1<<(11-c)))ui_rect_rgb(x+c,y+r,1,1,PK_RED);
}
static void pk_about_draw(const char *name,const char *description)
{
    char title[80]="About / ";append(title,name,sizeof(title));ui_header(title,description);
    int x=24,y=ui_compact?44:96,w=UI_W-48;
    ui_rect_rgb(x,y,3,110,ui_role(SC_THEME_ACCENT));x+=12;w-=12;
    pk_label(x,y,w,name,PAL_UI_CYAN+7);
    pk_label(x,y+24,w,PK_PACK_NAME,PAL_UI_TEXT);
    pk_label(x,y+48,w,PK_COPYRIGHT,PAL_UI_TEXT);
    pk_label(x,y+72,w,PK_AUTHOR_URL,PAL_UI_CYAN+7);
    ui_text(x,y+96,PK_MADE_WITH,PAL_UI_MUTED);pk_heart(x+ex_text_w(PK_MADE_WITH)+8,y+98);
    ui_control(PK_ABOUT_CLOSE,24,UI_H-66,112,"Back",1);
    ui_footer("Enter / Esc returns");
}
/* 关于页仅持有画布，消费自己的关闭事件后恢复调用方；既不改编辑
 * 缓冲，也不退出程序。游戏调用方先暂停，并重置补步时钟。 */
static void pk_about(const char *name,const char *description)
{
    int previous=ui_modal;ui_modal=4;ui_action=0;ui_followup=1;
    for(;;){
        if(!ui_frame_due_event())continue;
        ui_pointer();pk_about_draw(name,description);ui_present();
        int key=sc_key();if(key==27||key==10||ui_action==PK_ABOUT_CLOSE)break;
    }
    ui_modal=previous;ui_action=ui_pressed=ui_released=0;ui_followup=1;
}
