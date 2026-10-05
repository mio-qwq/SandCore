/* =====================================================================
 * mio：Canvas真彩画板，作品尺寸与窗口几何分离。
 *
 * 文档固定512×320直通ARGB，保存既有SCB2MIO，不随缩放重采样后
 * 回写。旧SCB1读取原DAC色，主题UI重映射不能改变作品的颜色。
 * UI仍取THEME角色/凤凰字形，文档RGB是用户内容，切主题不变色。
 *
 * 文档、撤销、候选区各自拥有私有堆页。打开失败只释放候选，
 * 不借撤销区暂存文件；头/正文和丢弃确认通过才交换作品。保存
 * 完整成功才清dirty。按钮先处理，排队首字保留给下一个输入框。
 *
 * 工具/色样/画纸按真实客户区重排。画纸快路径写物理像素，必须
 * 裁客户区，采样分母保持完整画纸尺寸。右键/工具浮层独占点击，
 * 关闭浮层的同一次点击不会在纸上留下一个意外的点。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "IMAGE.inc"
#define PAPER_W 512
#define PAPER_H 320
#define PAPER_PIXELS (PAPER_W*PAPER_H)
#define PAPER_BYTES (PAPER_PIXELS*4)
#define DOCUMENT_BYTES (32+PAPER_BYTES)
static u8 *document;
static u32 *undo_pixels;
static u32 document_palette[256],color_rgb;
static const int swatches[]={PAL_UI_TEXT,PAL_UI_INK,PAL_UI_CYAN+7,PAL_UI_GOLD+7,
    PAL_UI_ALERT,PAL_SKY+10,PAL_SAND_L+6,PAL_STAR};
static char filename[64]="HOME/PAINT.SCB",status[96]="Round brush / Save SCB2 / Open old SCB1";
static int color_index=2,brush=3,drawing,last_x,last_y,dirty,have_undo;
static int menu_open,menu_x,menu_y,menu_w,menu_h;
/* mio：应用私有完成序号，只读诊断用于区分“路径框已关”和文件
 * 候选读完/提交完。验证器仍须比较真实正文/撤销/磁盘，不以这个
 * 序号当成功标记；不增加SCAPI入口，也不允许外部写状态驱动程序。 */
static volatile u32 canvas_operations;
static int paper_x,paper_y,paper_w,paper_h,swatch_y,swatch_size,swatch_columns,tools_y,small_window;
static u32 *pixels(void){return (u32 *)(document+32);}
static void snapshot(void)
{
    u32 *source=pixels();
    for(int i=0;i<PAPER_PIXELS;i++)undo_pixels[i]=source[i];
    have_undo=1;
}
/* 文档可能有透明度，预览image_over只对不透明棋盘合成，不能
 * 拿它修改透明作品。直通alpha合成：num=sa*255+da*(255-sa)，
 * RGB除num还原直通。各项最大约一千六百万，32位整数足够。 */
static u32 ink_over(u32 destination,int coverage)
{
    u32 a=(u32)coverage,inverse=255-a,old_alpha=destination>>24;
    u32 numerator=a*255+old_alpha*inverse;
    if(!numerator)return 0;
    u32 result=((numerator+127)/255)<<24;
    for(int shift=0;shift<24;shift+=8){
        u32 value=(((color_rgb>>shift)&255)*a*255
            +((destination>>shift)&255)*old_alpha*inverse+numerator/2)/numerator;
        result|=value<<shift;
    }
    return result;
}
static void dab(int x,int y)
{
    int outside=brush*brush,inside=(brush-1)*(brush-1);u32 *out=pixels();
    for(int yy=y-brush;yy<=y+brush;yy++)for(int xx=x-brush;xx<=x+brush;xx++){
        int dx=xx-x,dy=yy-y,distance=dx*dx+dy*dy;
        if(xx<0||xx>=PAPER_W||yy<0||yy>=PAPER_H||distance>=outside)continue;
        int coverage=distance<=inside?255:(outside-distance)*255/(outside-inside);
        int index=yy*PAPER_W+xx;out[index]=ink_over(out[index],coverage);
    }
}
static void stroke(int x,int y,int xx,int yy)
{
    /* 一次按下/释放只有一次snapshot；连接离散PS/2采样，不因每
     * 个采样点都保存副本而把Undo退化成只撤销最后一个小点。 */
    int dx=ui_abs(xx-x),sx=x<xx?1:-1,dy=-ui_abs(yy-y),sy=y<yy?1:-1,error=dx+dy;
    for(;;){
        dab(x,y);if(x==xx&&y==yy)break;int twice=2*error;
        if(twice>=dy){error+=dy;x+=sx;}
        if(twice<=dx){error+=dx;y+=sy;}
    }
    dirty=1;
}
static void layout(void)
{
    small_window=UI_W<276||UI_H<148;
    tools_y=ui_compact?38:70;swatch_y=tools_y+(ui_compact?30:38);
    swatch_size=ui_compact?18:24;
    swatch_columns=(UI_W-32)/(swatch_size+8);
    if(swatch_columns<1)swatch_columns=1;
    if(swatch_columns>8)swatch_columns=8;
    int rows=(8+swatch_columns-1)/swatch_columns;
    paper_y=swatch_y+rows*(swatch_size+8)+8;
    int aw=UI_W-48,ah=UI_H-(ui_compact?22:30)-8-paper_y;
    paper_w=paper_h=0;
    if(aw>0&&ah>0){
        paper_w=aw;paper_h=PAPER_H*paper_w/PAPER_W;
        if(paper_h>ah){paper_h=ah;paper_w=PAPER_W*paper_h/PAPER_H;}
    }
    paper_x=(UI_W-paper_w)/2;
    menu_w=UI_W-32;if(menu_w>240)menu_w=240;if(menu_w<1)menu_w=1;
    menu_h=4*(ui_compact?26:36)+16;
    menu_x=ui_clamp(menu_x,16,UI_W-16-menu_w);
    menu_y=ui_clamp(menu_y,ui_compact?32:62,UI_H-(ui_compact?22:30)-menu_h);
}
static void draw_paper(void)
{
    if(paper_w<1||paper_h<1)return;
    ui_panel(paper_x-2,paper_y-2,paper_w+4,paper_h+4);
    int px=ui_px(paper_x),py=ui_px(paper_y);
    int pw=ui_px(paper_x+paper_w)-px,ph=ui_px(paper_y+paper_h)-py;
    if(pw<1||ph<1)return;
    int left=px<0?0:px,top=py<0?0:py,right=px+pw,bottom=py+ph;
    if(right>ui_width)right=ui_width;
    if(bottom>ui_height)bottom=ui_height;
    /* 150%宽度由两端点相减，裁后仍减原px/py并除原pw/ph。
     * 文档alpha保持原字节，仅预览与THEME命名棋盘颜色合成。
     * 每个直接写入的行/列都有物理边界，不依赖面板画点的裁剪。 */
    u32 *source=pixels();int square=ui_px(12);if(square<1)square=1;
    for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){
        int sx=(x-px)*PAPER_W/pw,sy=(y-py)*PAPER_H/ph;
        int alternate=((x-px)/square+(y-py)/square)&1;
        u32 background=0xFF000000u|ui_role(alternate?SC_THEME_FACE_ALT:SC_THEME_PAPER);
        ui_pixels[y*ui_width+x]=image_over(source[sy*PAPER_W+sx],background);
    }
}
static void draw(void)
{
    ui_clip_clear();layout();
    if(small_window){
        /* 96px最小外框在200%下只有47个布局单位，容不下四个
         * 工具。给一个实际可点的放大入口，而不是画被裁碎的按钮。
         * 高度可少于22单位，命中与图形使用同一实际可用高度。
         * 作品/Undo仍在独占区；放大后恢复完整界面，不改图片尺寸。 */
        menu_open=0;paper_w=paper_h=0;ui_background(SC_THEME_FACE_ALT);
        int h=UI_H<22?UI_H:22,w=UI_W-8,y=(UI_H-h)/2;
        if(w>0&&h>0){
            ui_button_box(4,y,w,h,UI_W>=104?"Enlarge":"+",1);
            if((ui_pressed&1)&&ui_hit(4,y,w,h))ui_action=9;
        }
        return;
    }
    ui_header("CANVAS / RGB STUDIO",filename);
    int focus=ui_focus;if(menu_open)ui_focus=0;
    if(ui_compact){
        ui_small_control(1,16,tools_y,52,"Open",0);ui_small_control(2,76,tools_y,52,"Save",0);
        ui_small_control(3,136,tools_y,52,"Undo",have_undo);ui_small_control(8,196,tools_y,60,"Tools",0);
    }else{
        ui_control(1,16,tools_y,72,"Open",0);ui_control(2,96,tools_y,72,"Save",0);
        ui_control(3,176,tools_y,72,"Undo",have_undo);ui_control(8,256,tools_y,80,"Tools",0);
    }
    for(int i=0;i<8;i++){
        int x=16+(i%swatch_columns)*(swatch_size+8),y=swatch_y+(i/swatch_columns)*(swatch_size+8);
        ui_rect_rgb(x,y,swatch_size,swatch_size,document_palette[swatches[i]]);
        if(color_index==i)ui_span(x,y+swatch_size+3,swatch_size,PAL_UI_CYAN+7);
        if((ui_pressed&1)&&ui_hit(x,y,swatch_size,swatch_size))ui_action=20+i;
    }
    ui_focus=focus;draw_paper();
    if(!paper_w||!paper_h)ui_text(16,paper_y,"Enlarge window to paint",PAL_UI_MUTED);
    if(menu_open){
        ui_panel(menu_x,menu_y,menu_w,menu_h);int step=ui_compact?26:36;char size[24],value[12];
        copy(size,"Brush: ",24);decimal(value,brush);append(size,value,24);
        if(ui_compact){
            ui_small_control(4,menu_x+8,menu_y+8,menu_w-16,"Clear paper",0);
            ui_small_control(5,menu_x+8,menu_y+8+step,menu_w-16,size,0);
            ui_small_control(7,menu_x+8,menu_y+8+step*2,menu_w-16,"RGB color",0);
            ui_small_control(6,menu_x+8,menu_y+8+step*3,menu_w-16,"Close tools",0);
        }else{
            ui_control(4,menu_x+8,menu_y+8,menu_w-16,"Clear paper",0);
            ui_control(5,menu_x+8,menu_y+8+step,menu_w-16,size,0);
            ui_control(7,menu_x+8,menu_y+8+step*2,menu_w-16,"RGB color",0);
            ui_control(6,menu_x+8,menu_y+8+step*3,menu_w-16,"Close tools",0);
        }
    }
    ui_footer(status);
}
/* 独占候选先检查头/精确长度，再逐行读正文；所有失败释放候选。
 * SCB1槽位仍限0..PAL_UI_LINE，SCB2保留直通alpha。每8行让出，
 * 避免单次655KB读堵住PIO/IRQ；当前作品与Undo不参与暂存。 */
static u8 *read_candidate(const char *path)
{
    u32 stat[2];u8 header[32],row[PAPER_W];
    if(sc_stat(path,stat)||stat[0]!=1||sc_read_at(path,header,24,0)!=24)return 0;
    int old=image_scb(header);
    if(!old&&sc_read_at(path,header,32,0)!=32)return 0;
    if(image_u32(header+8)!=PAPER_W||image_u32(header+12)!=PAPER_H)return 0;
    if(old){if(stat[1]!=24+PAPER_PIXELS)return 0;}
    else if(!image_scb2(header)||image_u32(header+24)!=PAPER_BYTES||stat[1]!=DOCUMENT_BYTES)return 0;
    u8 *candidate=sc_alloc(DOCUMENT_BYTES);if(!candidate)return 0;
    image_header2(candidate,PAPER_W,PAPER_H);u32 *out=(u32 *)(candidate+32);
    for(int y=0;y<PAPER_H;y++){
        if(old){
            if(sc_read_at(path,row,PAPER_W,24+y*PAPER_W)!=PAPER_W){sc_free(candidate);return 0;}
            for(int x=0;x<PAPER_W;x++){
                if(row[x]>PAL_UI_LINE){sc_free(candidate);return 0;}
                out[y*PAPER_W+x]=0xFF000000u|document_palette[row[x]];
            }
        }else if(sc_read_at(path,out+y*PAPER_W,PAPER_W*4,32+y*PAPER_W*4)!=PAPER_W*4){
            sc_free(candidate);return 0;
        }
        if((y&7)==7)sc_yield();
    }
    return candidate;
}
static void open(void)
{
    char path[64];copy(path,filename,64);
    if(!ui_edit_path(path,64,"Open 512 x 320 SCB1 / SCB2")){canvas_operations++;return;}
    u8 *candidate=read_candidate(path);
    if(!candidate){copy(status,"Open rejected / picture and Undo kept",96);canvas_operations++;return;}
    if(dirty&&!ui_confirm("Discard current picture?","The new file is valid; replace unsaved pixels?")){
        sc_free(candidate);canvas_operations++;return;
    }
    /* 成功打开沿用旧程序清撤销策略；只有合法候选和确认均通过
     * 才交换文档/释放旧正文。失败连撤销字节都不改，不清dirty。 */
    u8 *previous=document;document=candidate;sc_free(previous);
    have_undo=dirty=drawing=0;copy(filename,path,64);copy(status,"Picture opened / true color",96);
    canvas_operations++;
}
static void save(void)
{
    char path[64];copy(path,filename,64);
    if(!ui_edit_path(path,64,"Save SCB2 picture")){canvas_operations++;return;}
    int written=sc_write(path,document,DOCUMENT_BYTES);
    if(written==DOCUMENT_BYTES){copy(filename,path,64);dirty=0;copy(status,"SCB2 saved / open in Lens",96);}
    else copy(status,written==-5?"Core path protected / picture kept":"Save failed / unsaved picture kept",96);
    canvas_operations++;
}
static void choose_color(void)
{
    char text[10];const char *digits="0123456789ABCDEF";
    for(int i=0;i<6;i++)text[i]=digits[(color_rgb>>(20-i*4))&15];
    text[6]=0;
    if(!ui_edit_value(text,10,"RGB color: six hexadecimal digits",0))return;
    char *p=text;if(*p=='#')p++;
    if(length(p)!=6){copy(status,"Color requires six hexadecimal digits",96);return;}
    u32 value=0;
    for(int i=0;i<6;i++){
        int c=(u8)p[i],digit=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;
        if(digit<0){copy(status,"Invalid RGB / previous brush kept",96);return;}
        value=(value<<4)|(u32)digit;
    }
    color_rgb=value;color_index=-1;copy(status,"Custom RGB brush selected",96);
}
int main(void)
{
    if(ui_open("Canvas")<0)return 1;
    document=sc_alloc(DOCUMENT_BYTES);undo_pixels=sc_alloc(PAPER_BYTES);
    if(!document||!undo_pixels)return 1; /* 退出统一回收已成功的独占区 */
    sc_palette(document_palette);image_header2(document,PAPER_W,PAPER_H);
    u32 *out=pixels();for(int i=0;i<PAPER_PIXELS;i++)out[i]=0xFF000000u|document_palette[PAL_UI_TEXT];
    color_rgb=document_palette[swatches[color_index]];
    char argument[128];sc_args(argument,128);
    if(argument[0]){
        if(length(argument)<64){
            u8 *candidate=read_candidate(argument);
            if(candidate){sc_free(document);document=candidate;copy(filename,argument,64);copy(status,"Picture opened / true color",96);}
            else copy(status,"Argument rejected / blank picture kept",96);
        }else copy(status,"Argument path exceeds 63 bytes",96);
    }
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();int overlay=menu_open;draw();ui_present();
        int action=ui_action,key=action?-1:sc_key();
        if(key==27){
            if(menu_open)menu_open=0;
            else if(!dirty||ui_confirm("Close Canvas?","Discard unsaved picture?"))return 0;
        }
        if(action==1||key==1||key=='o')open();
        if(action==2||key==2||key=='s')save();
        if((action==3||key=='u')&&have_undo){
            u32 *data=pixels();for(int i=0;i<PAPER_PIXELS;i++){u32 value=data[i];data[i]=undo_pixels[i];undo_pixels[i]=value;}
            dirty=1;copy(status,"Undo / redo toggled",96);
        }
        if(action==4&&ui_confirm("Clear paper?","The previous paper remains in Undo.")){
            snapshot();u32 *data=pixels();for(int i=0;i<PAPER_PIXELS;i++)data[i]=0xFF000000u|document_palette[PAL_UI_TEXT];
            dirty=1;copy(status,"Paper cleared / Undo available",96);
        }
        if(action==5){brush=brush==1?3:brush==3?6:1;copy(status,"Round brush radius changed",96);}
        if(action==7)choose_color();
        if(action==9){drawing=0;sc_window(ui_win,1);}
        if(action>=20&&action<28){color_index=action-20;color_rgb=document_palette[swatches[color_index]];}
        if(action&&action!=8)menu_open=0;
        if(action==8||(ui_pressed&2)){
            menu_open=!menu_open;menu_x=action==8?16:ui_x;menu_y=action==8?tools_y+26:ui_y;drawing=0;
        }
        if(overlay&&(ui_pressed&1)&&!action&&!ui_hit(menu_x,menu_y,menu_w,menu_h))menu_open=0;
        /* 关闭浮层/按钮动作的同一次点击不得穿透；只在原来和现在
         * 都无浮层且无控件动作时启动或延续笔画，工作区可为零。 */
        if(!overlay&&!menu_open&&!action&&paper_w>0&&paper_h>0&&ui_hit(paper_x,paper_y,paper_w,paper_h)){
            int x=(ui_x-paper_x)*PAPER_W/paper_w,y=(ui_y-paper_y)*PAPER_H/paper_h;
            /* VGA现场表明短点击的按下/释放可在绘制前都已排队：
             * pressed仍保留，但buttons已为0。第一点必须按按下
             * 沿立即绘制，不能等当前仍按住才着墨，否则单击丢点。
             * 后续采样再连线，避免首点在同一帧被重复混两遍软边。 */
            if(ui_pressed&1){snapshot();drawing=1;stroke(x,y,x,y);last_x=x;last_y=y;}
            else if(drawing&&(ui_buttons&1)){stroke(last_x,last_y,x,y);last_x=x;last_y=y;}
            if(drawing)copy(status,"Unsaved SCB2 / Save or Undo",96);
        }
        if(!(ui_buttons&1))drawing=0;
    }
}
