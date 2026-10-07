/* =====================================================================
 * mio：UI.inc 是各 M7 应用的私有软件绘制片段，公共接口仍只有 SCAPI.H。
 * 画布归三环程序，FRAME 提交前不会被合成器读到半帧。任何图形都是
 * 普通像素/线段/三角形，不绑死于内核控件，因此 IDE 与游戏可以共用
 * 渐变、材质光照、阴影与动画。所有颜色引用 GFX.md 已登记的槽位。
 * ===================================================================== */
#define UI_W 318
#define UI_H 170
static u8 ui_pixels[UI_W*UI_H],ui_font[1024];
static int ui_win,ui_last_tick,ui_frames;
static int ui_visible=1;
static u32 ui_visibility_epoch;
static inline int ui_abs(int x) { return x<0?-x:x; }
static inline int ui_clamp(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }
static inline void ui_pixel(int x,int y,int color)
{ if(ui_visible && x>=0 && x<UI_W && y>=0 && y<UI_H) ui_pixels[y*UI_W+x]=(u8)color; }
static inline void ui_span(int x,int y,int w,int color)
{
    if(!ui_visible)return;
    if(y<0 || y>=UI_H || w<=0 || x>=UI_W) return;
    if(x<0) { w+=x;
        x=0; } if(x+w>UI_W) w=UI_W-x;
    for(int i=0;i<w;i++) ui_pixels[y*UI_W+x+i]=(u8)color;
}
static inline void ui_rect(int x,int y,int w,int h,int color)
{
    if(!ui_visible)return;
    int bottom=ui_clamp(y+h,0,UI_H);
    if(y<0) y=0;
    for(int row=y;row<bottom;row++) ui_span(x,row,w,color);
}
static inline void ui_gradient(int x,int y,int w,int h,int base)
{ if(!ui_visible)return;for(int i=0;i<h;i++) ui_span(x,y+i,w,base+i*7/(h?h:1)); }
static inline void ui_line(int x,int y,int xx,int yy,int color)
{
    if(!ui_visible)return;
    int dx=ui_abs(xx-x),sx=x<xx?1:-1,dy=-ui_abs(yy-y),sy=y<yy?1:-1,error=dx+dy;
    for(;;) {
        ui_pixel(x,y,color);
        if(x==xx && y==yy) break;
        int twice=2*error;
        if(twice>=dy) { error+=dy;
            x+=sx; } if(twice<=dx) { error+=dx;
            y+=sy; }
    }
}
static inline void ui_circle(int x,int y,int radius,int color,int filled)
{
    if(!ui_visible)return;
    int a=radius,b=0,error=1-radius;
    while(a>=b) {
        if(filled) {
            ui_span(x-a,y+b,2*a+1,color);
            ui_span(x-a,y-b,2*a+1,color);
            ui_span(x-b,y+a,2*b+1,color);
            ui_span(x-b,y-a,2*b+1,color);
        } else {
            ui_pixel(x+a,y+b,color);
            ui_pixel(x-a,y+b,color);
            ui_pixel(x+a,y-b,color);
            ui_pixel(x-a,y-b,color);
            ui_pixel(x+b,y+a,color);
            ui_pixel(x-b,y+a,color);
            ui_pixel(x+b,y-a,color);
            ui_pixel(x-b,y-a,color);
        }
        b++;
        if(error<0) error+=2*b+1;
        else { a--;
            error+=2*(b-a)+1; }
    }
}
static inline void ui_panel(int x,int y,int w,int h)
{
    /* 阴影先画，边界再画，四角留掉一像素；主体保持足够留白。
     * 三层明度区分悬浮面板与远景，细高光只放上边，避免四边抢眼。 */
    ui_rect(x+3,y+4,w,h,PAL_UI_INK);
    ui_rect(x,y+1,w,h-2,PAL_UI_PANEL);
    ui_rect(x+1,y,w-2,h,PAL_UI_PANEL);
    ui_span(x+2,y,w-4,PAL_UI_LINE);
}
static inline void ui_text(int x,int y,const char *s,int color)
{
    if(!ui_visible)return;
    if(!ui_visible)return;
    for(int i=0;s[i] && x<UI_W;i++,x+=8) {
        int c=(u8)s[i];
        if(c>=128) c='?';
        for(int row=0;row<8;row++) {
            int bits=ui_font[c*8+row];
            for(int col=0;col<8;col++) if(bits&(128>>col)) ui_pixel(x+col,y+row,color);
        }
    }
}
static inline void ui_number(int x,int y,int value,int color)
{ char text[12];
    decimal(text,value);
    ui_text(x,y,text,color); }
/* 每帧只用本人缓存的同源位图，避免窗口直画又被FRAME覆盖。
 * 新完整字库与旧API回退统一在GLYPHS；满缓存轮转且按版本失效。 */
#include "GLYPHS.inc"
static inline void ui_text16(int x,int y,const char *text,int color)
{
    if(!ui_visible)return;
    int left=x;
    while(*text) {
        u32 scalar=(u8)*text++;
        if(scalar=='\n') { x=left; y+=18; continue; }
        if(scalar>=0xC2 && scalar<=0xF4) {
            int n=scalar<0xE0?1:scalar<0xF0?2:3;
            scalar&=n==1?31:n==2?15:7;
            for(int j=0;j<n;j++) {
                if(((u8)*text&0xC0)!=0x80) {scalar=0xFFFD;break;}
                scalar=(scalar<<6)|((u8)*text++&63);
            }
        } else if(scalar>=128) scalar=0xFFFD;
        int slot=ui_glyph_slot(scalar);
        for(int r=0;r<16;r++) for(int c=0;c<ui_glyph_width[slot];c++)
            if(ui_glyph_rows[slot][r]&(0x8000u>>c)) ui_pixel(x+c,y+r,color);
        x+=ui_glyph_width[slot];
    }
}
static inline void ui_hex(char *text,u32 value)
{ const char *digits="0123456789ABCDEF";
    for(int i=7;i>=0;i--) { text[i]=digits[value&15];
        value>>=4; } text[8]=0; }
static inline void ui_header(const char *title,const char *subtitle)
{
    ui_gradient(0,0,UI_W,UI_H,PAL_UI_NIGHT);
    ui_rect(10,10,3,18,PAL_UI_CYAN+7);
    ui_text(21,10,title,PAL_UI_TEXT);
    ui_text(21,22,subtitle,PAL_UI_MUTED);
    ui_span(10,35,UI_W-20,PAL_UI_LINE);
}
static inline void ui_footer(const char *text)
{ ui_rect(0,154,UI_W,16,PAL_UI_INK);
    ui_text(8,159,text,PAL_UI_MUTED); }
static inline void ui_button(int x,int y,int w,const char *text,int active)
{
    ui_rect(x,y,w,14,active?PAL_UI_CYAN+1:PAL_UI_NIGHT+4);
    ui_span(x,y,w,active?PAL_UI_CYAN+6:PAL_UI_LINE);
    ui_text(x+5,y+4,text,active?PAL_UI_TEXT:PAL_UI_MUTED);
}
static inline void ui_triangle(int x0,int y0,int x1,int y1,int x2,int y2,int color)
{
    if(!ui_visible)return;
    /* 按 y 排序，再逐扫描行插值两条边；每行只算两次除法，内部跨度
     * 直接写像素，避免每个像素重新算三次叉积。顶点越界也先裁 y。 */
    int temp;
    if(y0>y1) { temp=y0;
        y0=y1;
        y1=temp;
        temp=x0;
        x0=x1;
        x1=temp; }
    if(y1>y2) { temp=y1;
        y1=y2;
        y2=temp;
        temp=x1;
        x1=x2;
        x2=temp; }
    if(y0>y1) { temp=y0;
        y0=y1;
        y1=temp;
        temp=x0;
        x0=x1;
        x1=temp; }
    if(y0==y2) return;
    int first=ui_clamp(y0,0,UI_H),end=ui_clamp(y2,0,UI_H-1);
    for(int y=first;y<=end;y++) {
        int a=x0+(x2-x0)*(y-y0)/(y2-y0),b;
        if(y<y1 && y1!=y0) b=x0+(x1-x0)*(y-y0)/(y1-y0);
        else if(y2!=y1) b=x1+(x2-x1)*(y-y1)/(y2-y1);
        else b=x1;
        if(a>b) { temp=a;
            a=b;
            b=temp; } ui_span(a,y,b-a+1,color);
    }
}
static inline int ui_sin(int angle)
{
    /* 一圈 1024，幅值 256；Bhaskara 有理近似，全整数、确定性。
     * 最大乘积低于有符号 32 位上限，不引入软浮点或 libgcc 除法库。 */
    angle&=1023;
    int sign=angle>=512?-1:1;
    if(angle>=512) angle-=512;
    int product=angle*(512-angle);
    return sign*(16*product*256/(5*512*512-4*product));
}
static inline int ui_cos(int angle) { return ui_sin(angle+256); }
static inline int ui_open(const char *title)
{
    ui_win=sc_open(title,320,184);
    if(ui_win<0) return -1;
    u32 visibility[8];if(sc_visibility(ui_win,visibility)>=0){ui_visible=(int)visibility[1];ui_visibility_epoch=visibility[2];}
    if(sc_font8(ui_font)!=1024) return -1;
    ui_last_tick=sc_tick();
    return ui_win;
}
static inline void ui_present(void) { if(!ui_visible)return;sc_frame(ui_win,ui_pixels,sizeof(ui_pixels));
    ui_frames++; }
static inline int ui_frame_due(void)
{
    if(ui_font_refresh()){sc_font8(ui_font);ui_last_tick=sc_tick()-4;}
    u32 visibility[8];if(sc_visibility(ui_win,visibility)>=0){ui_visible=(int)visibility[1];
        if(!ui_visible){sc_event_wait(SC_EVENT_VISIBILITY,0xFFFFFFFFu);return 0;}
        if(ui_visibility_epoch!=visibility[2] || visibility[4]){ui_visibility_epoch=visibility[2];ui_last_tick=sc_tick()-4;}}
    /* PIT=100Hz，游戏/动画以最多 25fps 前进，帧慢时也不空转补几十帧。
     * 后台仍可合成，但输入接口只对聚焦窗口有效，切窗不偷操作。 */
    int tick=sc_tick();
    if(tick-ui_last_tick<4) { sc_yield();
        return 0; }
    ui_last_tick=tick;
    return 1;
}
static inline int ui_mouse(int *x,int *y)
{
    int info[5];
    if(sc_info(ui_win,info) || !info[4]) return 0;
    int packed=sc_mouse();
    *x=(packed&511)-info[0]-1;
    *y=((packed>>9)&511)-info[1]-13;
    return (packed>>18)&7;
}
static inline int ui_edit_path(char *path,int capacity,const char *title)
{
    int n=length(path);
    for(;;) {
        u32 visibility[8];if(sc_visibility(ui_win,visibility)>=0){ui_visible=(int)visibility[1];
            if(!ui_visible){sc_event_wait(SC_EVENT_VISIBILITY,0xFFFFFFFFu);continue;}}
        ui_panel(12,48,294,65);
        ui_text(22,58,title,PAL_UI_CYAN+7);
        ui_rect(22,77,274,15,PAL_UI_INK);
        int first=n>32?n-32:0;
        ui_text(24,81,path+first,PAL_UI_TEXT);
        ui_span(24+(n-first)*8,89,6,PAL_UI_GOLD+7);
        ui_text(22,99,"ENTER accept  ESC cancel",PAL_UI_MUTED);
        ui_present();
        int k=sc_key();
        if(k<0) { sc_yield();
            continue; }
        if(k==27) return 0;
        if(k==10) return n>0;
        if(k==8 && n) path[--n]=0;
        else if(k>=32 && k<127 && n+1<capacity) { path[n++]=(char)k;
            path[n]=0; }
    }
}
