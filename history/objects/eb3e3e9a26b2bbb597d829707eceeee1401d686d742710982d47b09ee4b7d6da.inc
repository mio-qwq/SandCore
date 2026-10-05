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
static int ui_followup;
/* mio：鼠标每个坐标都不是视觉变化。过去鼠标在同一按钮/空白内
 * 移动也完整重绘和复制数MB，Monitor快速移动时把客体烧到约87%。
 * 记录上一张实际提交帧的按钮命中矩形和hover状态，只在进入/离开
 * 它们时重绘。256项是私有有界缓存；超限回退原坐标策略，不漏hover。
 * 按下/松开、按键、焦点、尺寸、主题仍立即唤醒；按住时坐标变化
 * 仍重绘，保证画笔/滚动条/图像拖动逐点响应。不降低原生画布大小。
 * ui_pointer开始收集，ui_present结束；提交后的动作查询不能改写
 * 已显示帧的hover基线。该缓存不改变SCAPI或三环窗口布局。 */
static int ui_hover_rects[256][5],ui_hover_count,ui_hover_overflow,ui_hover_recording;
/* mio：首帧提交后记录应用私有模态输入状态：1文本、2路径、3确认。
 * 0表示普通页面。诊断/验收须等待实际可输入，不把发出点击等同于
 * 目标对话框已经完成渲染；该字段不是公共SCAPI/ABI或外部写入口。 */
static volatile int ui_modal;
/* mio：可滚动页面只允许在客户区的工作视口内绘制/命中。裁剪是
 * 本进程布局机制，不新增内核控件或改变SCAPI；模态框/页脚前必须
 * clear，避免把滚出视口的按钮画到页签上、仍能被点击。 */
static int ui_clip_enabled,ui_clip_x,ui_clip_y,ui_clip_right,ui_clip_bottom;
static inline void ui_clip_clear(void){ui_clip_enabled=0;}
static inline void ui_clip_set(int x,int y,int w,int h)
{ui_clip_enabled=1;ui_clip_x=x;ui_clip_y=y;ui_clip_right=x+w;ui_clip_bottom=y+h;}
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
/* mio：文件名的63B容量与凤凰字形的像素宽是两种限制。视觉副本
 * 可以短一些，但不能复制半个UTF-8标量再让字库显示缺字框。
 * cut指向下一字节；落在续字节时回到该标量的首字节。完整真实
 * 路径仍保存在调用者缓冲，此函数只生成标签，不用于截断打开路径。 */
static inline void ui_copy_utf8(char *out,const char *text,int capacity)
{
    if(capacity<1)return;
    int n=length(text),cut=n<capacity?n:capacity-1;
    while(cut>0 && ((u8)text[cut]&192)==128)cut--;
    for(int i=0;i<cut;i++)out[i]=text[i];
    out[cut]=0;
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
    if(ui_clip_enabled){
        int left=ui_px(ui_clip_x),top=ui_px(ui_clip_y);
        if(x<left)x=left;
        if(y<top)y=top;
        if(right>ui_px(ui_clip_right))right=ui_px(ui_clip_right);
        if(bottom>ui_px(ui_clip_bottom))bottom=ui_px(ui_clip_bottom);
    }
    /* mio：SCCC不会像宿主优化器一样自动外提行乘法/颜色掩码。
     * 每行只算一次起点，逐像素递增指针，保持相同半开裁剪区间；
     * 不降低分辨率、不省略像素，也不改变ARGB或主题角色。 */
    u32 color=0xFF000000u|(rgb&0xFFFFFFu);
    for(int r=y;r<bottom;r++){
        u32 *out=ui_pixels+r*ui_width+x;
        for(int c=x;c<right;c++)*out++=color;
    }
}
static inline void ui_physical(int x,int y,int w,int h,int color)
{
    ui_physical_rgb(x,y,w,h,ui_colors[color&255]);
}
/* mio：整页底图必须按物理客户区铺满。UI_W/H是整数布局单位，
 * 例如590px/150%得到393单位，再乘回去只有589px；用逻辑矩形
 * 清屏会漏一列，resize后还能留下旧stride像素。局部控件照常
 * 用逻辑端点，只有整页底图清除裁剪并覆盖实际ui_width/height。 */
static inline void ui_background(int role)
{
    ui_clip_clear();
    ui_physical_rgb(0,0,ui_width,ui_height,ui_role(role));
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
    return ui_focus&&ui_x>=x&&ui_x<x+w&&ui_y>=y&&ui_y<y+h
        &&(!ui_clip_enabled||(ui_x>=ui_clip_x&&ui_x<ui_clip_right&&ui_y>=ui_clip_y&&ui_y<ui_clip_bottom));
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
    /* 新窗口必须立即产生首帧；不能等一秒的静止刷新或鼠标移动
     * 才绘制正文，让刚建立的画布被误认为黑屏/卡在初始化。 */
    ui_followup=1;
    return ui_win;
}
static inline void ui_pointer(void)
{

    int p[6];
    ui_geometry();
    ui_action=0;
    ui_hover_count=ui_hover_overflow=0;ui_hover_recording=1;
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

    ui_background(ui_classic?SC_THEME_FACE:SC_THEME_PAPER);
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
    if(ui_hover_recording){
        int left=x,top=y,right=x+w,bottom=y+height;
        if(ui_clip_enabled){
            if(left<ui_clip_x)left=ui_clip_x;if(top<ui_clip_y)top=ui_clip_y;
            if(right>ui_clip_right)right=ui_clip_right;if(bottom>ui_clip_bottom)bottom=ui_clip_bottom;
        }
        if(left<right&&top<bottom){
            if(ui_hover_count<256){
                int *rect=ui_hover_rects[ui_hover_count++];
                rect[0]=left;rect[1]=top;rect[2]=right;rect[3]=bottom;rect[4]=hover;
            }else ui_hover_overflow=1;
        }
    }
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
    ui_hover_recording=0;
    /* 点击帧展示的是操作前标签，随后分发动作。补一帧以立即
     * 呈现操作结果，不能把状态更新延迟到下一次hover或一秒刷新。 */
    if(ui_action)ui_followup=1;
    ui_frames++;
}
static inline int ui_frame_due(void)
{
    int tick=sc_tick();
    int p[6],info[5];u32 display[8],theme[32];
    if(sc_pointer_peek(ui_win,p) || sc_info(ui_win,info) || sc_display(display) || sc_theme(theme)){
        sc_yield();return 0;
    }
    int key=sc_key_peek();
    int scale=display[0]==320?50:(int)display[2];
    int px=p[0]*100/ui_scale,py=p[1]*100/ui_scale;
    int position=px!=ui_x||py!=ui_y,hover_changed=0;
    for(int i=0;i<ui_hover_count;i++){
        int *rect=ui_hover_rects[i];
        int hover=p[5]&&px>=rect[0]&&py>=rect[1]&&px<rect[2]&&py<rect[3];
        if(hover!=rect[4]){hover_changed=1;break;}
    }
    int changed=info[2]!=ui_width || info[3]!=ui_height || scale!=ui_scale
        || theme[2]!=ui_theme[2] || hover_changed
        || (p[5]&&position&&(p[2]||ui_buttons||ui_hover_overflow))
        || p[2]!=ui_buttons || p[5]!=ui_focus;
    /* 静止工具每秒刷新状态即可，不再固定25次重画同一整帧。按键/
     * 点击立即唤醒；跨按钮hover最多隔一个PITtick，同区域移动不重画。
     * peek不消费输入，
     * 随后的ui_pointer/sc_key仍收到同一个事件。动作之后补一帧，
     * 让更新的标签/页面当即可见，不能等待下一秒才显示保存结果。 */
    if(!ui_followup && key<0 && !p[3] && !p[4] && !changed && tick-ui_last_tick<100) {
        sc_yield();return 0;
    }
    if(changed && key<0 && !p[3] && !p[4] && !ui_followup && tick==ui_last_tick) {
        sc_yield();return 0;
    }
    ui_followup=key>=0 || p[3] || p[4];
    ui_last_tick=tick;
    return 1;
}
static inline int ui_confirm(const char *title,const char *detail)
{
    ui_followup=1;
    /* 模态对话框只持有本进程的canvas；肯定/取消两条鼠标路径与键盘
     * 同样明确，不靠用户猜快捷键。返回之前不改变文件或编辑缓冲。 */
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        ui_background(SC_THEME_FACE_ALT);
        int w=UI_W>600?560:UI_W-32,x=(UI_W-w)/2,y=(UI_H-148)/2;
        ui_panel(x,y,w,148);
        ui_text(x+16,y+16,title,PAL_UI_GOLD+7);
        ui_text(x+16,y+45,detail,PAL_UI_TEXT);
        ui_control(1,x+16,y+100,112,"Confirm",1);
        ui_control(2,x+w-128,y+100,112,"Cancel",0);
        ui_present();
        ui_modal=3;
        int key=ui_action?-1:sc_key(); /* 按钮先决定模态去向，下一输入框保留排队首字 */
        if(ui_action==1||key=='y'||key=='Y'||key==10){ui_modal=0;return 1;}
        if(ui_action==2||key==27){ui_modal=0;return 0;}
        sc_yield();

    }
}
static inline int ui_edit_path(char *path,int capacity,const char *title)
{

    /* 简洁路径对话框保留任意路径输入，另给真实目录浏览与选中文件。
     * 输入框默认全选：首次打印替换原路径，方向/退格取消全选，避免
     * 打开文件时不得不逐字清空63字节。选择目录再次读取直接子项。 */
    static char list[65536],names[512][64];
    static int kinds[512];
    char directory[64]="HOME";
    char hint[64]="";
    int n=length(path),select_all=1,selected=0,first=0,count=0,reload=1;
    ui_followup=1;
    for(;;){
        if(!reload && !ui_frame_due())continue;
        ui_pointer();
        /* 改窗口尺寸会改变物理stride，不能把旧宽度的像素排列当新
         * 背景；模态自己铺完整主题底图，不依赖调用页残留的裁剪。 */
        ui_background(SC_THEME_FACE_ALT);
        if(reload){
            reload=0;
            count=0;
            char *p=list;
            if(sc_dir(directory,list,sizeof(list))>=0)while(*p&&count<512){
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
        /* mio：高缩放下逻辑客户区可能只有约175行，旧112行列表
         * 起点与底部固定按钮会相互覆盖。紧凑版用22行控件/18行
         * 列表，175单位高时仍留一条可点文件行；更矮rows可为0，
         * 宁可只保留路径输入也不画一行穿过按钮。
         * 所有位置从同一组变量派生，绘制与命中不能各用一套坐标。 */
        int compact=UI_H<300,w=UI_W>720?680:UI_W-32;
        int h=UI_H-(compact?16:48),x=(UI_W-w)/2,y=compact?8:12;
        int input_y=y+(compact?30:42),input_h=compact?24:30;
        int directory_y=y+(compact?58:84),list_y=y+(compact?80:112);
        int row_h=compact?18:24,nav_y=y+h-(compact?56:76);
        int accept_y=y+h-(compact?28:44),rows=(nav_y-4-list_y)/row_h;
        if(rows<0)rows=0;
        ui_panel(x,y,w,h);
        ui_text(x+16,y+(compact?8:14),title,PAL_UI_CYAN+7);
        ui_rect(x+16,input_y,w-32,input_h,select_all?PAL_UI_NIGHT+7:PAL_UI_INK);
        int tail=n>(w-48)/8?n-(w-48)/8:0;
        if(tail<0)tail=0;
        if(tail>n)tail=n;
        while(tail<n && ((u8)path[tail]&192)==128)tail++;
        ui_selected_text(x+24,input_y+(input_h-16)/2,path+tail,select_all);
        char folder_label[64];ui_copy_utf8(folder_label,directory,ui_clamp((w-184)/8,2,64));
        ui_text(x+16,directory_y,folder_label,PAL_UI_MUTED);
        if(compact){
            ui_small_control(3,x+w-152,directory_y-2,60,"Up",0);
            ui_small_control(4,x+w-84,directory_y-2,68,"Root",0);
        }else{
            ui_control(3,x+w-152,directory_y-6,60,"Up",0);
            ui_control(4,x+w-84,directory_y-6,68,"Root",0);
        }
        int shown=rows-(hint[0] && rows>0?1:0);
        for(int r=0;r<shown&&first+r<count;r++){
            int i=first+r,yy=list_y+r*row_h;
            if(i==selected)ui_rect(x+16,yy,w-32,row_h,PAL_UI_NIGHT+6);
            ui_text(x+24,yy+2,kinds[i]==2?"/":" ",PAL_UI_GOLD+7);
            char label[64];
            ui_copy_utf8(label,names[i],ui_clamp((w-64)/8,2,64));
            ui_selected_text(x+40,yy+2,label,i==selected);
            if((ui_pressed&1)&&ui_hit(x+16,yy,w-32,row_h)){
                selected=i;
                /* 候选先拼入128B临时区，长度检查通过才复制。不能
                 * 用append的容量截断把合法长路径变成另一个文件，
                 * 也不能留下半个汉字；失败保留原输入/目录和选择。 */
                char joined[128];copy(joined,directory,128);
                if(joined[0])append(joined,"/",128);
                append(joined,names[i],128);
                if(kinds[i]==2){
                    if(length(joined)<64){copy(directory,joined,64);reload=1;hint[0]=0;}
                    else copy(hint,"Directory exceeds 63 bytes",64);

                }
                else{
                    if(length(joined)<capacity){
                        copy(path,joined,capacity);n=length(path);select_all=1;hint[0]=0;
                    }else copy(hint,"Path exceeds input capacity",64);

                }
            }
        }
        if(compact){
            ui_small_control(5,x+16,nav_y,64,"Prev",0);
            ui_small_control(6,x+88,nav_y,64,"Next",0);
            ui_small_control(1,x+w-252,accept_y,112,"Accept",1);
            ui_small_control(2,x+w-128,accept_y,112,"Cancel",0);
        }else{
            ui_control(5,x+16,nav_y,64,"Prev",0);
            ui_control(6,x+88,nav_y,64,"Next",0);
            ui_control(1,x+w-252,accept_y,112,"Accept",1);
            ui_control(2,x+w-128,accept_y,112,"Cancel",0);
        }
        if(hint[0] && rows>0)ui_text(x+16,list_y+shown*row_h,hint,PAL_UI_ALERT);
        ui_present();
        ui_modal=2;
        int key=ui_action?-1:sc_key();
        if(ui_action==1||key==10){ui_modal=0;return n>0;}
        if(ui_action==2||key==27){ui_modal=0;return 0;}
        if(ui_action==3){
            hint[0]=0;
            int a=length(directory);
            while(a&&directory[a-1]!='/')a--;
            if(a)a--;
            directory[a]=0;
            reload=1;

        }
        if(ui_action==4){
            hint[0]=0;
            directory[0]=0;
            reload=1;

        }
        if(ui_action==5||key==0x80){
            first-=rows;
            if(first<0)first=0;

        }
        if(ui_action==6||key==0x81){
            if(rows>0 && first+rows<count)first+=rows;

        }
        /* 路径框沿用末尾编辑，没有隐藏的字符光标。左右键只取消
         * 初始全选，让退格能删除最后一个完整UTF-8标量；不能让
         * 注释承诺的操作无效，再把整条中文路径误清空。 */
        if(key==0x82||key==0x83)select_all=0;
        if(key==8){
            hint[0]=0;
            if(select_all){
                n=0;
                path[0]=0;

            }
            else if(n){n--;while(n&&((u8)path[n]&192)==128)n--;path[n]=0;}
            select_all=0;

        }
        else if(key>=32&&key<127){
            hint[0]=0;
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
static inline int ui_edit_value(char *text,int capacity,const char *title,int allow_empty)
{

    int n=length(text),all=1;
    ui_followup=1;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        ui_background(SC_THEME_FACE_ALT);
        int w=UI_W>700?660:UI_W-32,x=(UI_W-w)/2,y=(UI_H-150)/2;
        ui_panel(x,y,w,150);
        ui_text(x+16,y+16,title,PAL_UI_CYAN+7);
        ui_rect(x+16,y+50,w-32,30,all?PAL_UI_NIGHT+7:PAL_UI_INK);
        int first=n>(w-48)/8?n-(w-48)/8:0;
        while(first<n&&((u8)text[first]&192)==128)first++;
        ui_selected_text(x+24,y+57,text+first,all);
        ui_control(1,x+16,y+104,112,"Accept",1);
        ui_control(2,x+w-128,y+104,112,"Cancel",0);
        ui_present();
        ui_modal=1;
        int k=ui_action?-1:sc_key();
        if(ui_action==1||k==10){ui_modal=0;return allow_empty||n>0;}
        if(ui_action==2||k==27){ui_modal=0;return 0;}
        if(k==8){
            if(all)n=0;
            else if(n){n--;while(n&&((u8)text[n]&192)==128)n--;}
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
static inline int ui_edit_text(char *text,int capacity,const char *title)
{return ui_edit_value(text,capacity,title,0);}
