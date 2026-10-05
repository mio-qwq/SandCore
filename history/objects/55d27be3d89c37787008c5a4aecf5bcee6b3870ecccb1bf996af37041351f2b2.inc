/* =====================================================================
 * mio：M8 原生软件界面。SCAPI.H仍是唯一公共头；此片段是应用私有
 * 绘制代码，SCCC在系统内按源文件include，不把控件搬入零环。
 *
 * 坐标分两层：ui_width/height是客户区物理像素，UI_W/H是布局单位。
 * 每次查询WININFO与DISPLAY，重新算布局，缩放作用于控件与同源字形。
 * 窗口放大增加可见行数/工作区，不把上一帧位图拉伸冒充原生适配。
 * 最大ARGB后台从本进程高端堆申请，不占旧映像4MB区/BSS；退出时
 * 私有页统一回收。FRAME32只交实际cw*ch*4正文。改变尺寸与提交
 * 之间可能切模式，失败等待下帧重绘，不把上一帧尺寸强送给新画布。
 * ===================================================================== */
static u32 *ui_pixels;
static u32 ui_colors[256],ui_theme[32];
static int ui_classic,ui_radius;
static int ui_win,ui_width,ui_height,ui_scale=100,UI_W,UI_H,ui_last_tick,ui_frames;
static int ui_x,ui_y,ui_buttons,ui_pressed,ui_released,ui_focus,ui_action;
static int ui_compact;
static u32 ui_glyph_codes[128];
static u16 ui_glyph_rows[128][16];
static int ui_glyph_width[128],ui_glyph_count;
static inline int ui_abs(int x){
    return x<0?-x:x;
}
static inline int ui_clamp(int x,int lo,int hi){
    return x<lo?lo:x>hi?hi:x;
}
static inline int ui_px(int value){
    return value*ui_scale/100;
}
static inline u32 ui_color(int slot){return 0xFF000000u|ui_colors[slot&255];}
static inline u32 ui_role(int role){return ui_theme[8+role];}
static inline void ui_theme_update(void)
{
    u32 next[32];
    if(sc_theme(next) || (ui_theme[0] && next[2]==ui_theme[2]))return;
    for(int i=0;i<32;i++)ui_theme[i]=next[i];
    sc_palette(ui_colors);
    /* 槽位是旧工具的绘制语义输入，输出现在是RGB。这里只映射UI
     * 角色，岩石/草/水等真实材质保留DAC RGB，画板/图片也可直写RGB。
     * 不改系统调色板：同一时间运行的旧赛车不会随主题变成白色道路。 */
    ui_colors[PAL_UI_PANEL]=ui_role(SC_THEME_FACE);
    ui_colors[PAL_UI_INK]=ui_role(SC_THEME_PAPER);
    ui_colors[PAL_UI_TEXT]=ui_role(SC_THEME_TEXT);
    ui_colors[PAL_UI_MUTED]=ui_role(SC_THEME_MUTED);
    ui_colors[PAL_UI_LINE]=ui_role(SC_THEME_LINE);
    ui_colors[PAL_UI_ALERT]=ui_role(SC_THEME_ALERT);
    for(int i=0;i<8;i++){
        ui_colors[PAL_UI_NIGHT+i]=ui_role(i>=6?SC_THEME_SELECT:SC_THEME_FACE_ALT);
        ui_colors[PAL_UI_CYAN+i]=ui_role(SC_THEME_ACCENT);
        ui_colors[PAL_UI_GOLD+i]=ui_role(SC_THEME_GOLD);
    }
    ui_classic=(int)next[1];ui_radius=(int)next[5];
}
static inline void ui_physical_rgb(int x,int y,int w,int h,u32 rgb)
{
    int right=ui_clamp(x+w,0,ui_width),bottom=ui_clamp(y+h,0,ui_height);
    x=ui_clamp(x,0,ui_width);y=ui_clamp(y,0,ui_height);
    for(int r=y;r<bottom;r++)for(int c=x;c<right;c++)ui_pixels[r*ui_width+c]=0xFF000000u|(rgb&0xFFFFFFu);
}
static inline void ui_physical(int x,int y,int w,int h,int color)
{
    ui_physical_rgb(x,y,w,h,ui_colors[color&255]);
}
static inline void ui_rect_rgb(int x,int y,int w,int h,u32 rgb)
{ ui_physical_rgb(ui_px(x),ui_px(y),ui_px(x+w)-ui_px(x),ui_px(y+h)-ui_px(y),rgb); }
static inline void ui_round_rgb(int x,int y,int w,int h,int radius,u32 rgb)
{
    if(radius>w/2)radius=w/2;
    if(radius>h/2)radius=h/2;
    /* 整数圆弧逐行求覆盖段。布局坐标放大以后仍按真实像素边界绘制，
     * 不先画320px再拉伸；经典样式传0即严格矩形。 */
    for(int r=0;r<h;r++){
        int inset=0;
        if(radius && (r<radius || r>=h-radius)){
            int dy=r<radius?radius-1-r:r-(h-radius),dx=radius;
            while(dx*dx+dy*dy>radius*radius)dx--;
            inset=radius-dx;
        }
        ui_rect_rgb(x+inset,y+r,w-2*inset,1,rgb);
    }
}
static inline void ui_rect(int x,int y,int w,int h,int color)
{
    ui_physical(ui_px(x),ui_px(y),ui_px(x+w)-ui_px(x),ui_px(y+h)-ui_px(y),color);
}
static inline void ui_span(int x,int y,int w,int color){
    ui_rect(x,y,w,1,color);
}
static inline void ui_pixel(int x,int y,int color){
    ui_rect(x,y,1,1,color);
}
static inline void ui_line(int x,int y,int xx,int yy,int color)
{

    int dx=ui_abs(xx-x),sx=x<xx?1:-1,dy=-ui_abs(yy-y),sy=y<yy?1:-1,error=dx+dy;
    for(;;){
        ui_pixel(x,y,color);
        if(x==xx&&y==yy)break;
        int t=2*error;
        if(t>=dy){
            error+=dy;
            x+=sx;

        }
        if(t<=dx){
            error+=dx;
            y+=sy;

        }
    }
}
static inline void ui_panel(int x,int y,int w,int h)
{
    ui_round_rgb(x,y,w,h,ui_classic?0:ui_radius,ui_role(SC_THEME_FACE));
    if(ui_classic){
        /* Classic是凸起/凹陷的真实矩形双边，不能给现代浮层换灰色
         * 以后就称为经典。内侧暗边表达面板凹入，按钮用相反顺序。 */
        ui_rect_rgb(x,y,w,1,ui_role(SC_THEME_DARK_EDGE));
        ui_rect_rgb(x,y,1,h,ui_role(SC_THEME_DARK_EDGE));
        ui_rect_rgb(x,y+h-1,w,1,ui_role(SC_THEME_HIGHLIGHT));
        ui_rect_rgb(x+w-1,y,1,h,ui_role(SC_THEME_HIGHLIGHT));
    }else{
        ui_span(x,y,w,PAL_UI_LINE);
        ui_rect(x,y+h-1,w,1,PAL_UI_LINE);
    }
}
static inline void ui_gradient(int x,int y,int w,int h,int base)
{
    for(int r=0;r<h;r++)ui_rect(x,y+r,w,1,base+r*7/(h?h:1));
}
static inline int ui_hit(int x,int y,int w,int h)
{
    return ui_focus&&ui_x>=x&&ui_x<x+w&&ui_y>=y&&ui_y<y+h;
}
static inline void ui_text(int x,int y,const char *text,int color)
{

    /* Unicode字形通过GLYPH16复制，128项缓存是本进程私有数据。
     * ASCII和汉字均来自凤凰字库；150%按像素边界分配1/2宽交替，
     * 而非插值模糊。输入源码保持原始UTF-8，显示缺字不破坏字节。 */
    int left=x;
    while(*text&&x<UI_W){
        u32 c=(u8)*text++;
        if(c=='\n'){
            x=left;
            y+=20;
            continue;

        }
        if(c>=0xC2&&c<=0xF4){
            int n=c<0xE0?1:c<0xF0?2:3;
            c&=n==1?31:n==2?15:7;
            for(int j=0;j<n;j++){
                if(((u8)*text&192)!=128){
                    c=0xFFFD;
                    break;

                }
                c=(c<<6)|((u8)*text++&63);

            }
        }
        else if(c>=128)c=0xFFFD;
        int slot=0;
        while(slot<ui_glyph_count&&ui_glyph_codes[slot]!=c)slot++;
        if(slot==ui_glyph_count){
            if(slot==128)slot=127;
            else ui_glyph_count++;
            ui_glyph_codes[slot]=c;
            int width=sc_glyph16(c,ui_glyph_rows[slot]);
            ui_glyph_width[slot]=width>0?width:16;

        }
        for(int r=0;r<16;r++)for(int col=0;col<ui_glyph_width[slot];col++)
        if(ui_glyph_rows[slot][r]&(0x8000u>>col))ui_rect(x+col,y+r,1,1,color);
        x+=ui_glyph_width[slot];

    }
}
static inline void ui_text16(int x,int y,const char *text,int color){
    ui_text(x,y,text,color);
}
static inline void ui_selected_text(int x,int y,const char *text,int selected)
{
    /* 单线程私有颜色表临时取选择字色，绘完立即恢复。按角色成对
     * 渲染同一选择行，确保经典深蓝底上的白字有足够对比度。 */
    u32 saved=ui_colors[PAL_UI_TEXT];
    if(selected)ui_colors[PAL_UI_TEXT]=ui_role(SC_THEME_SELECT_TEXT);
    ui_text(x,y,text,PAL_UI_TEXT);ui_colors[PAL_UI_TEXT]=saved;
}
static inline void ui_number(int x,int y,int v,int color){
    char s[12];
    decimal(s,v);
    ui_text(x,y,s,color);
}
static inline void ui_hex(char *s,u32 v){
    const char *d="0123456789ABCDEF";
    for(int i=7;i>=0;i--){
        s[i]=d[v&15];
        v>>=4;

    }
    s[8]=0;
}
static inline void ui_geometry(void)
{

    int info[5];
    u32 display[8];
    if(sc_info(ui_win,info)||sc_display(display))return;
    ui_width=info[2];
    ui_height=info[3];
    ui_scale=display[0]==320?50:(int)display[2];
    UI_W=ui_width*100/ui_scale;
    UI_H=ui_height*100/ui_scale;
    ui_compact=UI_H<350;
    ui_theme_update();
}
static inline int ui_open(const char *title)
{

    u32 d[8];
    if(sc_display(d))return -1;
    int w=(int)d[0]-48,h=(int)d[1]-80;
    if(d[0]==320){
        w=320;
        h=184;

    }
    if(w>1280)w=1280;
    if(h>900)h=900;
    ui_win=sc_open_rgb(title,w,h);
    if(ui_win<0)return -1;
    ui_pixels=sc_alloc(1920u*1080u*4u);
    if(!ui_pixels){sc_call(0x11,ui_win,0,0,0,0,0);return -1;}
    ui_geometry();
    ui_last_tick=sc_tick();
    return ui_win;
}
static inline void ui_pointer(void)
{

    int p[6];
    ui_geometry();
    ui_action=0;
    if(sc_pointer(ui_win,p)){
        ui_focus=ui_pressed=ui_buttons=0;
        return;

    }
    ui_x=p[0]*100/ui_scale;
    ui_y=p[1]*100/ui_scale;
    ui_buttons=p[2];
    ui_pressed=p[3];
    ui_released=p[4];
    ui_focus=p[5];
}
static inline void ui_header(const char *title,const char *subtitle)
{

    ui_rect_rgb(0,0,UI_W,UI_H,ui_role(ui_classic?SC_THEME_FACE:SC_THEME_PAPER));
    if(ui_classic)ui_rect_rgb(0,0,UI_W,ui_compact?30:58,ui_role(SC_THEME_FACE));
    ui_rect(16,ui_compact?6:14,3,ui_compact?16:32,PAL_UI_CYAN+7);
    ui_text(30,ui_compact?6:14,title,PAL_UI_TEXT);
    if(!ui_compact)ui_text(30,34,subtitle,PAL_UI_MUTED);
    ui_span(16,ui_compact?30:58,UI_W-32,PAL_UI_LINE);
}
static inline void ui_footer(const char *text)
{
    int h=ui_compact?22:30;
    ui_rect_rgb(0,UI_H-h,UI_W,h,ui_role(ui_classic?SC_THEME_FACE:SC_THEME_PAPER));
    ui_span(0,UI_H-h,UI_W,PAL_UI_LINE);
    ui_text(16,UI_H-h+3,text,PAL_UI_MUTED);
}
static inline void ui_button_box(int x,int y,int w,int height,const char *text,int active)
{
    int hover=ui_hit(x,y,w,height);
    int down=hover&&(ui_buttons&1);
    if(ui_classic){
        ui_rect_rgb(x,y,w,height,ui_role(SC_THEME_FACE));
        ui_rect_rgb(x,y,w,1,ui_role(down?SC_THEME_DARK_EDGE:SC_THEME_HIGHLIGHT));
        ui_rect_rgb(x,y,1,height,ui_role(down?SC_THEME_DARK_EDGE:SC_THEME_HIGHLIGHT));
        ui_rect_rgb(x,y+height-1,w,1,ui_role(down?SC_THEME_HIGHLIGHT:SC_THEME_DARK_EDGE));
        ui_rect_rgb(x+w-1,y,1,height,ui_role(down?SC_THEME_HIGHLIGHT:SC_THEME_DARK_EDGE));
        if(active){ui_rect_rgb(x+3,y+3,w-6,height-6,ui_role(SC_THEME_SELECT));}
    }else{
        ui_round_rgb(x,y,w,height,ui_radius,ui_role(SC_THEME_LINE));
        ui_round_rgb(x+1,y+1,w-2,height-2,ui_radius?ui_radius-1:0,
                     ui_role(active?SC_THEME_SELECT:hover?SC_THEME_FACE_ALT:SC_THEME_FACE));
    }
    /* 选择底色和文字必须成对，不把Classic的黑正文放到深蓝选择条。 */
    if(active){
        u32 saved=ui_colors[PAL_UI_TEXT];ui_colors[PAL_UI_TEXT]=ui_role(SC_THEME_SELECT_TEXT);
        ui_text(x+8+down,y+(height-16)/2+down,text,PAL_UI_TEXT);ui_colors[PAL_UI_TEXT]=saved;
    }else ui_text(x+8+down,y+(height-16)/2+down,text,PAL_UI_TEXT);
}
static inline void ui_button(int x,int y,int w,const char *text,int active)
{ ui_button_box(x,y,w,30,text,active); }
static inline void ui_control(int id,int x,int y,int w,const char *text,int active)
{
    ui_button(x,y,w,text,active);
    if((ui_pressed&1)&&ui_hit(x,y,w,30))ui_action=id;
}
static inline void ui_small_control(int id,int x,int y,int w,const char *text,int active)
{
    int hover=ui_hit(x,y,w,22);
    ui_button_box(x,y,w,22,text,active);
    if((ui_pressed&1)&&hover)ui_action=id;
}
static inline int ui_toolbar(const int *ids,const char **labels,int count,int y)
{

    int x=16,step=ui_compact?26:36;
    for(int i=0;i<count;i++){
        int w=length(labels[i])*8+20;
        if(x+w>UI_W-16){
            x=16;
            y+=step;

        }
        if(ui_compact)ui_small_control(ids[i],x,y,w,labels[i],0);
        else ui_control(ids[i],x,y,w,labels[i],0);
        x+=w+8;

    }
    return y+step+8;
}
static inline void ui_present(void)
{
    sc_frame32(ui_win,ui_pixels,(u32)ui_width*ui_height*4);
    ui_frames++;
}
static inline int ui_frame_due(void)
{
    int tick=sc_tick();
    if(tick-ui_last_tick<4){
        sc_yield();
        return 0;

    }
    ui_last_tick=tick;
    return 1;
}
static inline int ui_confirm(const char *title,const char *detail)
{

    /* 模态对话框只持有本进程的canvas；肯定/取消两条鼠标路径与键盘
     * 同样明确，不靠用户猜快捷键。返回之前不改变文件或编辑缓冲。 */
    for(;;){
        ui_pointer();
        int w=UI_W>600?560:UI_W-32,x=(UI_W-w)/2,y=(UI_H-148)/2;
        ui_panel(x,y,w,148);
        ui_text(x+16,y+16,title,PAL_UI_GOLD+7);
        ui_text(x+16,y+45,detail,PAL_UI_TEXT);
        ui_control(1,x+16,y+100,112,"Confirm",1);
        ui_control(2,x+w-128,y+100,112,"Cancel",0);
        ui_present();
        int key=sc_key();
        if(ui_action==1||key=='y'||key=='Y'||key==10)return 1;
        if(ui_action==2||key==27)return 0;
        sc_yield();

    }
}
static inline int ui_edit_path(char *path,int capacity,const char *title)
{

    /* 简洁路径对话框保留任意路径输入，另给真实目录浏览与选中文件。
     * 输入框默认全选：首次打印替换原路径，方向/退格取消全选，避免
     * 打开文件时不得不逐字清空63字节。选择目录再次读取直接子项。 */
    static char list[16384],names[192][64];
    static int kinds[192];
    char directory[64]="HOME";
    int n=length(path),select_all=1,selected=0,first=0,count=0,reload=1;
    for(;;){
        ui_pointer();
        if(reload){
            reload=0;
            count=0;
            char *p=list;
            if(sc_dir(directory,list,sizeof(list))>=0)while(*p&&count<192){
                int kind=*p++=='D'?2:1;
                if(*p!=' ')break;
                p++;
                char *name=p;
                while(*p&&*p!=' ')p++;
                if(!*p)break;
*p++=0;
                while(*p&&*p!='\n')p++;
                if(*p)p++;
                copy(names[count],name,64);
                kinds[count++]=kind;

            }
            selected=first=0;

        }
        int w=UI_W>720?680:UI_W-32,h=UI_H-48,x=(UI_W-w)/2,y=12,rows=(h-160)/24;
        if(rows<1)rows=1;
        ui_panel(x,y,w,h);
        ui_text(x+16,y+14,title,PAL_UI_CYAN+7);
        ui_rect(x+16,y+42,w-32,30,select_all?PAL_UI_NIGHT+7:PAL_UI_INK);
        int tail=n>(w-48)/8?n-(w-48)/8:0;
        ui_selected_text(x+24,y+49,path+tail,select_all);
        ui_text(x+16,y+84,directory,PAL_UI_MUTED);
        ui_control(3,x+w-152,y+78,60,"Up",0);
        ui_control(4,x+w-84,y+78,68,"Root",0);
        for(int r=0;r<rows&&first+r<count;r++){
            int i=first+r,yy=y+112+r*24;
            if(i==selected)ui_rect(x+16,yy,w-32,24,PAL_UI_NIGHT+6);
            ui_text(x+24,yy+4,kinds[i]==2?"/":" ",PAL_UI_GOLD+7);
            char label[64];
            copy(label,names[i],ui_clamp((w-64)/8,2,64));
            ui_selected_text(x+40,yy+4,label,i==selected);
            if((ui_pressed&1)&&ui_hit(x+16,yy,w-32,24)){
                selected=i;
                if(kinds[i]==2){
                    if(directory[0])append(directory,"/",64);
                    append(directory,names[i],64);
                    reload=1;

                }
                else{
                    copy(path,directory,capacity);
                    if(path[0])append(path,"/",capacity);
                    append(path,names[i],capacity);
                    n=length(path);
                    select_all=1;

                }
            }
        }
        ui_control(5,x+16,y+h-76,64,"Prev",0);
        ui_control(6,x+88,y+h-76,64,"Next",0);
        ui_control(1,x+w-252,y+h-44,112,"Accept",1);
        ui_control(2,x+w-128,y+h-44,112,"Cancel",0);
        ui_present();
        int key=sc_key();
        if(ui_action==1||key==10)return n>0;
        if(ui_action==2||key==27)return 0;
        if(ui_action==3){
            int a=length(directory);
            while(a&&directory[a-1]!='/')a--;
            if(a)a--;
            directory[a]=0;
            reload=1;

        }
        if(ui_action==4){
            directory[0]=0;
            reload=1;

        }
        if(ui_action==5||key==0x80){
            first-=rows;
            if(first<0)first=0;

        }
        if(ui_action==6||key==0x81){
            if(first+rows<count)first+=rows;

        }
        if(key==8){
            if(select_all){
                n=0;
                path[0]=0;

            }
            else if(n)path[--n]=0;
            select_all=0;

        }
        else if(key>=32&&key<127){
            if(select_all){
                n=0;
                path[0]=0;
                select_all=0;

            }
            if(n+1<capacity){
                path[n++]=(char)key;
                path[n]=0;

            }
        }
        sc_yield();

    }
}
static inline int ui_edit_text(char *text,int capacity,const char *title)
{

    int n=length(text),all=1;
    for(;;){
        ui_pointer();
        int w=UI_W>700?660:UI_W-32,x=(UI_W-w)/2,y=(UI_H-150)/2;
        ui_panel(x,y,w,150);
        ui_text(x+16,y+16,title,PAL_UI_CYAN+7);
        ui_rect(x+16,y+50,w-32,30,all?PAL_UI_NIGHT+7:PAL_UI_INK);
        int first=n>(w-48)/8?n-(w-48)/8:0;
        ui_selected_text(x+24,y+57,text+first,all);
        ui_control(1,x+16,y+104,112,"Accept",1);
        ui_control(2,x+w-128,y+104,112,"Cancel",0);
        ui_present();
        int k=sc_key();
        if(ui_action==1||k==10)return n>0;
        if(ui_action==2||k==27)return 0;
        if(k==8){
            if(all)n=0;
            else if(n)n--;
            text[n]=0;
            all=0;

        }
        else if(k>=32&&k<127){
            if(all){
                n=0;
                all=0;

            }
            if(n+1<capacity){
                text[n++]=(char)k;
                text[n]=0;

            }
        }
        sc_yield();

    }
}
