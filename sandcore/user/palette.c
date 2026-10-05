/* mio：M8 Palette。原始DAC快照与主题UI颜色分离，防止把主题的白纸
 * 冒充旧槽位颜色。保留原WALLPAPER模式，第一阶段源码尚未运行验证。 */
#include "SCAPI.H"
#include "NUI.inc"
static u32 raw_colors[256];
static int selected=PAL_SKY,first_slot=PAL_SKY,columns,rows,page_slots=1,palette_valid;
static char status[96]="Select a color / F5 refresh";
static const int actions[5]={1,2,3,4,5};
static const char *labels[5]={"Dunes","Night","Sand","Prev","Next"};
static void snapshot(void)
{
    u32 candidate[256];
    if(sc_palette(candidate)){copy(status,"Palette unavailable",sizeof(status));return;}
    for(int i=0;i<256;i++)raw_colors[i]=candidate[i];
    palette_valid=1;
}
static void wallpaper(int mode)
{
    if(sc_wallpaper(mode)){copy(status,"Wallpaper save failed",sizeof(status));return;}
    char path[64];path[0]=0;sc_theme_path(0,path,sizeof(path));
    /* 模式保存与图片选择是两个既有配置：不为制造切换效果擅自清空
     * 用户图片。真实返回0才报保存，主题图片优先时给出明确说明。 */
    copy(status,*path?"Mode saved; theme image has priority":"Wallpaper mode saved",sizeof(status));
}
static void select_slot(int value)
{selected=ui_clamp(value,PAL_SKY,PAL_UI_LINE);ui_followup=1;}
static void draw(void)
{
    ui_header("Palette","Actual registered colors / independent of interface theme");
    if(UI_W<260||UI_H<172){
        ui_text(16,36,"Enlarge to inspect colors",PAL_UI_MUTED);
        ui_small_control(98,16,60,112,"Enlarge",0);return;
    }
    int y=ui_toolbar(actions,labels,5,ui_compact?34:72);
    int grid_y=y+(ui_compact?20:34),grid_h=UI_H-(ui_compact?22:30)-grid_y-4;
    /* 紧凑工具条可能占两行，因此色块步长跟可用高度一起收缩；
     * 始终留出一行，不把底部色块画到状态栏或隐藏可点击区里。 */
    int step=ui_compact?24:56;if(step>grid_h)step=grid_h;
    if(step<12){ui_text(16,y,"Enlarge to inspect colors",PAL_UI_MUTED);ui_footer(status);return;}
    columns=(UI_W-32)/step;if(columns<1)columns=1;if(columns>16)columns=16;
    rows=grid_h/step;if(rows<1)rows=1;page_slots=columns*rows;
    first_slot=PAL_SKY+((selected-PAL_SKY)/page_slots)*page_slots;
    char line[64],hex[9],number[12];
    copy(line,"Slot ",sizeof(line));decimal(number,selected);append(line,number,sizeof(line));
    if(palette_valid){ui_hex(hex,raw_colors[selected]);append(line,"  #",sizeof(line));append(line,hex+2,sizeof(line));}
    else append(line,"  unavailable",sizeof(line));
    ui_text(16,y,line,PAL_UI_TEXT);
    int x=(UI_W-columns*step)/2;ui_clip_set(0,grid_y,UI_W,grid_h);
    for(int i=0;i<page_slots&&first_slot+i<=PAL_UI_LINE;i++){
        int slot=first_slot+i,bx=x+(i%columns)*step,by=grid_y+(i/columns)*step;
        ui_rect_rgb(bx,by,step-2,step-2,ui_role(slot==selected?SC_THEME_ACCENT:SC_THEME_LINE));
        ui_rect_rgb(bx+3,by+3,step-8,step-8,palette_valid?raw_colors[slot]:ui_role(SC_THEME_FACE_ALT));
        if((ui_pressed&1)&&ui_hit(bx,by,step-2,step-2))ui_action=100+slot;
    }
    ui_clip_clear();ui_footer(status);
}
int main(void)
{
    if(ui_open("Palette / mio")<0)return 1;
    snapshot();ui_followup=1;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        int action=ui_action,key=action?-1:sc_key();
        if(action==98){sc_window(ui_win,1);ui_followup=1;continue;}
        if(action>=101&&action<=204)select_slot(action-100);
        else if(action>=1&&action<=3){wallpaper(action-1);ui_followup=1;}
        else if(action==4)select_slot(selected-page_slots);
        else if(action==5)select_slot(selected+page_slots);
        if(key==27)return 0;
        if(key>='1'&&key<='3'){wallpaper(key-'1');ui_followup=1;}
        else if(key==5){snapshot();ui_followup=1;}
        else if(key==0x82)select_slot(selected-1);
        else if(key==0x83)select_slot(selected+1);
        else if(key==0x80)select_slot(selected-columns);
        else if(key==0x81)select_slot(selected+columns);
        else if(key=='[')select_slot(selected-page_slots);
        else if(key==']')select_slot(selected+page_slots);
    }
}
