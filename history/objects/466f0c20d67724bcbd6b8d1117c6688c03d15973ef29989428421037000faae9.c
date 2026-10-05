/* =====================================================================
 * mio：Canvas原生画板。作品是512x320槽位像素，不依赖窗口尺寸；
 * 缩放/拖窗口只重排画纸、工具和字号，文件不被拉伸后重新保存。
 * 鼠标线段使用Bresenham补齐采样空隙；按下时保存整张撤销副本，
 * 因此一次笔画是一次Undo。清空同样可撤销，保存长度必须完整匹配。
 * 文件含24B SCB头，Lens可直接打开；没有私有字体或额外颜色体系。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "IMAGE.inc"
#define PAPER_W 512
#define PAPER_H 320
static u8 document[24+PAPER_W*PAPER_H],undo_pixels[PAPER_W*PAPER_H];
static u32 document_palette[256];
static const int swatches[]={
    PAL_UI_TEXT,PAL_UI_INK,PAL_UI_CYAN+7,PAL_UI_GOLD+7,PAL_UI_ALERT,PAL_SKY+10,PAL_SAND_L+6,PAL_STAR
};
static char filename[64]="HOME/PAINT.SCB",status[96]="Draw with mouse / right-click for tools";
static int color=PAL_UI_CYAN+7,brush=3,drawing,last_x,last_y,dirty,have_undo,menu_open,menu_x,menu_y,paper_x,paper_y,paper_w,paper_h;
static inline void snapshot(void){
    for(int i=0;i<PAPER_W*PAPER_H;i++)undo_pixels[i]=document[24+i];
    have_undo=1;
}
static inline void dab(int x,int y)
{
    for(int yy=y-brush;yy<=y+brush;yy++)for(int xx=x-brush;xx<=x+brush;xx++)if(xx>=0&&xx<PAPER_W&&yy>=0&&yy<PAPER_H)document[24+yy*PAPER_W+xx]=(u8)color;
}
static inline void stroke(int x,int y,int xx,int yy)
{
    int dx=ui_abs(xx-x),sx=x<xx?1:-1,dy=-ui_abs(yy-y),sy=y<yy?1:-1,e=dx+dy;
    for(;;){
        dab(x,y);
        if(x==xx&&y==yy)break;
        int t=2*e;
        if(t>=dy){
            e+=dy;
            x+=sx;

        }
        if(t<=dx){
            e+=dx;
            y+=sy;

        }
    }
    dirty=1;
}
static inline void draw(void)
{

    ui_header("CANVAS / PIXEL STUDIO",filename);
    ui_control(1,16,70,72,"Open",0);
    ui_control(2,96,70,72,"Save",0);
    ui_control(3,176,70,72,"Undo",have_undo);
    ui_control(4,256,70,80,"Clear",0);
    ui_control(5,344,70,72,"Brush",0);
    for(int i=0;i<8;i++){
        int x=16+i*42;
        ui_rect_rgb(x,110,32,24,document_palette[swatches[i]]);
        if(color==swatches[i])ui_span(x,137,32,PAL_UI_CYAN+7);
        if((ui_pressed&1)&&ui_hit(x,110,32,24))color=swatches[i];

    }
    int available=UI_H-194;
    paper_w=UI_W-48;
    paper_h=PAPER_H*paper_w/PAPER_W;
    if(paper_h>available){
        paper_h=available;
        paper_w=PAPER_W*paper_h/PAPER_H;

    }
    if(paper_w<1)paper_w=1;
    if(paper_h<1)paper_h=1;
    paper_x=(UI_W-paper_w)/2;
    paper_y=152;
    ui_panel(paper_x-2,paper_y-2,paper_w+4,paper_h+4);
    int px=ui_px(paper_x),py=ui_px(paper_y),pw=ui_px(paper_w),ph=ui_px(paper_h);
    if(pw&&ph)for(int y=0;y<ph;y++)for(int x=0;x<pw;x++)ui_pixels[(py+y)*ui_width+px+x]=0xFF000000u|document_palette[document[24+(y*PAPER_H/ph)*PAPER_W+x*PAPER_W/pw]];
    if(menu_open){
        ui_panel(menu_x,menu_y,176,136);
        ui_control(2,menu_x+8,menu_y+8,160,"Save image",0);
        ui_control(3,menu_x+8,menu_y+40,160,"Undo stroke",0);
        ui_control(4,menu_x+8,menu_y+72,160,"Clear paper",0);
        ui_control(6,menu_x+8,menu_y+104,160,"Cancel",0);

    }
    ui_footer(status);
}
static inline void save(void)
{
    char p[64];
    copy(p,filename,64);
    if(!ui_edit_path(p,64,"Save SCB picture"))return;
    int n=sc_write(p,document,sizeof(document));
    if(n==(int)sizeof(document)){
        copy(filename,p,64);
        dirty=0;
        copy(status,"SCB saved / open it in Lens",sizeof(status));

    }
    else copy(status,n==-5?"Core path protected":"Save failed",sizeof(status));
}
static inline void open(void)
{

    if(dirty&&!ui_confirm("Discard current picture?","Unsaved pixels will be replaced."))return;
    char p[64];
    copy(p,filename,64);
    if(!ui_edit_path(p,64,"Open 512 x 320 SCB"))return;
    u8 head[24];
    u32 stat[2];
    if(sc_stat(p,stat)||stat[1]!=sizeof(document)||sc_read_at(p,head,24,0)!=24||!image_scb(head)
    ||image_u32(head+8)!=PAPER_W||image_u32(head+12)!=PAPER_H){
        copy(status,"Canvas requires a 512 x 320 SCB",sizeof(status));
        return;

    }
    if(sc_read_at(p,undo_pixels,sizeof(undo_pixels),24)!=(int)sizeof(undo_pixels)){
        copy(status,"Read failed; current picture kept",sizeof(status));
        return;

    }
    for(int i=0;i<PAPER_W*PAPER_H;i++)if(undo_pixels[i]>PAL_UI_LINE){
        copy(status,"Invalid palette index",sizeof(status));
        have_undo=0;
        return;

    }
    for(int i=0;i<PAPER_W*PAPER_H;i++)document[24+i]=undo_pixels[i];
    have_undo=0;
    dirty=0;
    copy(filename,p,64);
    copy(status,"Picture opened",sizeof(status));
}
int main(void)
{

    if(ui_open("Canvas")<0)return 1;
    sc_palette(document_palette); /* 作品颜色保持身份，不受主题UI角色覆盖 */
    image_header(document,PAPER_W,PAPER_H);
    for(int i=0;i<PAPER_W*PAPER_H;i++)document[24+i]=PAL_UI_TEXT;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        draw();
        ui_present();
        int k=sc_key(),a=ui_action;
        if(k==27){
            if(menu_open)menu_open=0;
            else if(!dirty||ui_confirm("Close Canvas?","Discard unsaved picture?"))return 0;

        }
        if(a==1||k==1)open();
        if(a==2||k==2)save();
        if((a==3||k=='u')&&have_undo){
            for(int i=0;i<PAPER_W*PAPER_H;i++){
                u8 p=document[24+i];
                document[24+i]=undo_pixels[i];
                undo_pixels[i]=p;

            }
            dirty=1;
            copy(status,"Undo / redo toggled",sizeof(status));

        }
        if(a==4&&ui_confirm("Clear paper?","The previous paper remains in Undo.")){
            snapshot();
            for(int i=0;i<PAPER_W*PAPER_H;i++)document[24+i]=PAL_UI_TEXT;
            dirty=1;

        }
        if(a==5)brush=brush==1?3:brush==3?6:1;
        if(a)menu_open=0;
        if(ui_pressed&2){
            menu_open=!menu_open;
            menu_x=ui_clamp(ui_x,0,UI_W-176);
            menu_y=ui_clamp(ui_y,0,UI_H-166);
            drawing=0;

        }
        if(!menu_open&&paper_h>0&&ui_hit(paper_x,paper_y,paper_w,paper_h)){

            int x=(ui_x-paper_x)*PAPER_W/paper_w,y=(ui_y-paper_y)*PAPER_H/paper_h;
            if(ui_pressed&1){
                snapshot();
                drawing=1;
                last_x=x;
                last_y=y;

            }
            if(drawing&&(ui_buttons&1)){
                stroke(last_x,last_y,x,y);
                last_x=x;
                last_y=y;
                copy(status,"Unsaved picture / Save or Undo",sizeof(status));

            }
        }
        if(!(ui_buttons&1))drawing=0;

    }
}
