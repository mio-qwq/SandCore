/* =====================================================================
 * exui_ext.inc —— ext 安装包在 NUI 之上追加的控件（源级片段，
 * 与主项目用法一致：应用 #include "NUI.inc" 之后包含本文件）。
 *
 * 命名：ex_ 前缀，全部依赖 NUI 已有原语（ui_rect/ui_text/ui_hit/
 * ui_pressed/ui_action/ui_role...），不重复造界面状态机。
 * 追加的控件统一走 ui_action 分派、ui_hover 记录由各控件自行
 * 登记进 NUI 的缓存（直接写 ui_hover_rects，与 NUI 控件同一契约）。
 * ===================================================================== */
#include "exutil.h"      /* ex_utf8_next：测宽/截断共用解码 */

/* 文本测宽（ASCII 8 / 汉字 16）：NUI 只有逐字绘制没有测宽，
 * 右对齐/省略号排版需要它。 */
static int ex_text_w(const char *text)
{
    int w=0;
    while(*text){
        u32 cp;
        int n=ex_utf8_next(text,&cp);
        w+=(n==1&&cp<0x80)?8:16;
        text+=n;
    }
    return w;
}
/* 视觉副本截断：按像素预算取完整标量，不切半个汉字。 */
static void ex_text_cut(char *out,int capacity,const char *text,int max_px)
{
    if(capacity<1)return;
    int w=0,cut=0;
    while(text[cut]&&cut<capacity-1){
        u32 cp;
        int n=ex_utf8_next(text+cut,&cp);
        int cw=(n==1&&cp<0x80)?8:16;
        if(w+cw>max_px)break;
        w+=cw;cut+=n;
    }
    for(int i=0;i<cut;i++)out[i]=text[i];
    out[cut]=0;
}

/* 复选框：方框+对勾+标签，点击翻转 *value 并置 ui_action=id。 */
static void ex_check(int id,int x,int y,const char *label,int *value)
{
    int w=20+ex_text_w(label);
    int hover=ui_hit(x,y,w,20);
    if(ui_hover_recording){
        if(ui_hover_count<256){
            int *r=ui_hover_rects[ui_hover_count++];
            r[0]=x;r[1]=y;r[2]=x+w;r[3]=y+20;r[4]=hover;
        }else ui_hover_overflow=1;
    }
    ui_rect_rgb(x,y+2,16,16,ui_role(SC_THEME_PAPER));
    ui_rect_rgb(x,y+2,16,1,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x,y+17,16,1,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x,y+2,1,16,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x+15,y+2,1,16,ui_role(SC_THEME_LINE));
    if(*value){
        for(int i=0;i<4;i++){
            ui_rect_rgb(x+3+i,y+8+i,2,1,ui_role(SC_THEME_ACCENT_DEEP));
            ui_rect_rgb(x+6+i,y+11-i,2,1,ui_role(SC_THEME_ACCENT_DEEP));
        }
    }
    ui_text(x+24,y+2,label,PAL_UI_TEXT);
    if((ui_pressed&1)&&hover){*value=!*value;ui_action=id;}
}

/* 进度条：per_mille 0..1000。安装/批量操作的可视反馈。 */
static void ex_progress(int x,int y,int w,int h,int per_mille)
{
    if(per_mille<0)per_mille=0;
    if(per_mille>1000)per_mille=1000;
    ui_rect_rgb(x,y,w,h,ui_role(SC_THEME_PAPER));
    ui_rect_rgb(x,y,w,1,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x,y+h-1,w,1,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x,y,1,h,ui_role(SC_THEME_LINE));
    ui_rect_rgb(x+w-1,y,1,h,ui_role(SC_THEME_LINE));
    int fill=(w-2)*per_mille/1000;
    if(fill>0)ui_rect_rgb(x+1,y+1,fill,h-2,ui_role(SC_THEME_ACCENT));
}

/* 列表行：hover 高亮 + 左键点击返回 1 并置 ui_action=id。
 * 表格/十六进制/包列表共用。 */
static int ex_row(int id,int x,int y,int w,int h)
{
    int hover=ui_hit(x,y,w,h);
    if(ui_hover_recording){
        if(ui_hover_count<256){
            int *r=ui_hover_rects[ui_hover_count++];
            r[0]=x;r[1]=y;r[2]=x+w;r[3]=y+h;r[4]=hover;
        }else ui_hover_overflow=1;
    }
    if(hover)ui_rect_rgb(x,y,w,h,ui_role(SC_THEME_FACE_ALT));
    if((ui_pressed&1)&&hover){ui_action=id;return 1;}
    return 0;
}

/* SCB2 图标预览：最近邻整数放大，alpha=0 透底。
 * 数据/尺寸来自内嵌载荷（构建期已由 mkpayload.py 校验过，
 * 这里再做一次严格校验，坏数据宁可空一块也不画越界）。 */
static int ex_icon_header(const u8 *data,u32 size,int *w,int *h)
{
    static const u8 magic[8]={'S','C','B','2','M','I','O',0};
    if(size<32||!data)return -1;
    for(int i=0;i<8;i++)if(data[i]!=magic[i])return -1;
    *w=data[8]|(data[9]<<8)|(data[10]<<16)|(data[11]<<24);
    *h=data[12]|(data[13]<<8)|(data[14]<<16)|(data[15]<<24);
    u32 fmt=data[16]|(data[17]<<8)|(data[18]<<16)|(data[19]<<24);
    u32 flags=data[20]|(data[21]<<8)|(data[22]<<16)|(data[23]<<24);
    u32 n=data[24]|(data[25]<<8)|(data[26]<<16)|(data[27]<<24);
    if(fmt!=2||flags)return -1;
    if(*w<1||*w>128||*h<1||*h>128)return -1;
    if(n!=(u32)(*w)*(*h)*4||size!=32u+n)return -1;
    if(data[28]!='M'||data[29]!='I'||data[30]!='O'||data[31])return -1;
    return 0;
}
static void ex_icon_draw(const u8 *data,u32 size,int x,int y,int scale)
{
    int w,h;
    if(ex_icon_header(data,size,&w,&h))return;
    if(scale<1)scale=1;
    const u8 *px=data+32;
    for(int r=0;r<h;r++)
        for(int c=0;c<w;c++){
            const u8 *b=px+((u32)r*w+c)*4;
            if(!b[3])continue;
            u32 rgb=(u32)b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|0xFF000000u;
            ui_rect_rgb(x+c*scale,y+r*scale,scale,scale,rgb);
        }
}

/* 把校验通过的 SCB2 落盘（安装步骤：图标 -> SYS/ICONS）。 */
static int ex_icon_write(const char *path,const u8 *data,u32 size)
{
    int w,h;
    if(ex_icon_header(data,size,&w,&h))return -1;
    return sc_write(path,data,(int)size);
}

