/* mio：Welcome to Graphics，庆祝 SandCore M7 的实时软件图形短片。
 * 六段叙事包含片名、光照几何、流动地形、设计工具、粒子空间与谢幕。
 * 定点旋转/透视/三角填充/深度排序全在三环执行，没有预渲染视频代替
 * 运行。Space 暂停，R 重播，1..6 选段，Esc 退出；由系统内 SCCC 编译。
 */
#include "SCAPI.H"
#include "UI.inc"
static int vertex[12][3]={
    {-50,81,0},{50,81,0},{-50,-81,0},{50,-81,0},
    {0,-50,81},{0,50,81},{0,-50,-81},{0,50,-81},
    {81,0,-50},{81,0,50},{-81,0,-50},{-81,0,50}
};
static int faces[20][3]={
    {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
    {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
    {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
    {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
};
static int sx[12],sy[12],depth[12],order[20],light[20];
/* 六个段落共 42 秒。时间来自 PIT 而非渲染帧数，帧慢时短片不会
 * 被动变成十分钟；暂停把时间基准一起冻结，恢复也不会跳过段落。 */
static int lengths[6]={600,800,800,800,600,600};
static const char *chapter_names[]={"A NEW CHAPTER","LIGHT / GEOMETRY","SPACE / MATERIAL","DESIGN / BUILD / PLAY","A WORLD YOU CAN SHAPE","WELCOME TO GRAPHICS"};
static int start_tick,pause_tick,paused,last_chapter=-1,transition_start;
static u8 previous_frame[UI_W*UI_H];
static void film_text(int x,int y,const char *text,int color,int scale)
{
    for(int i=0;text[i];i++,x+=8*scale) {
        int c=(u8)text[i];
        if(c>=128) c='?';
        for(int r=0;r<8;r++) for(int col=0;col<8;col++)
            if(ui_font[c*8+r]&(128>>col)) ui_rect(x+col*scale,y+r*scale,scale,scale,color);
    }
}
static void film_center(int y,const char *text,int color,int scale)
{film_text((UI_W-length(text)*8*scale)/2,y,text,color,scale);}
static void film_chinese(int y)
{
    /* 片名来自用户唯一的中文字形表，通过正式 GLYPH16 接口取得，
     * 不在短片里再解析一遍 SCF，不绕过内核的统一字库选择规则。 */
    ui_text16((UI_W-9*16)/2,y,"欢迎来到图形的世界",PAL_UI_MUTED);
}
static void stars(int time,int moving)
{
    for(int i=0;i<90;i++) {
        int z=120+(i*71+(moving?time*2:0))%700;
        int x=159+((i*137)%1200-600)*90/z;
        int y=82+((i*i*53)%700-350)*90/z;
        int brightness=ui_clamp(7-z/130,1,7);
        ui_pixel(x,y,PAL_UI_CYAN+brightness);
        if(z<180) {ui_pixel(x+1,y,PAL_UI_CYAN+brightness);
            ui_pixel(x,y+1,PAL_UI_CYAN+brightness);}
    }
}
static void orbit(int cx,int cy,int radius,int time,int tilt)
{
    int oldx=0,oldy=0;
    for(int a=0;a<=1024;a+=24) {
        int x=cx+ui_cos(a+time)*radius/256;
        int y=cy+ui_sin(a+time)*radius*tilt/(256*256);
        if(a) ui_line(oldx,oldy,x,y,PAL_UI_NIGHT+6);
        oldx=x;
        oldy=y;
    }
}
static void crystal(int cx,int cy,int radius,int time)
{
    int ca=ui_cos(time),sa=ui_sin(time),cb=ui_cos(time/2+70),sb=ui_sin(time/2+70);
    for(int i=0;i<12;i++) {
        int x=(vertex[i][0]*ca+vertex[i][2]*sa)/256;
        int z=(-vertex[i][0]*sa+vertex[i][2]*ca)/256;
        int y=(vertex[i][1]*cb-z*sb)/256;
        z=(vertex[i][1]*sb+z*cb)/256+350;
        sx[i]=cx+x*radius/z;
        sy[i]=cy-y*radius/z;
        depth[i]=z;
    }
    for(int i=0;i<20;i++) {
        order[i]=i;
        int a=faces[i][0],b=faces[i][1],c=faces[i][2];
        int cross=(sx[b]-sx[a])*(sy[c]-sy[a])-(sy[b]-sy[a])*(sx[c]-sx[a]);
        light[i]=ui_clamp(ui_abs(cross)*180/(radius*radius+1)+2,1,7);
    }
    for(int i=0;i<20;i++) for(int j=i+1;j<20;j++) {
        int a=order[i],b=order[j];
        int za=depth[faces[a][0]]+depth[faces[a][1]]+depth[faces[a][2]];
        int zb=depth[faces[b][0]]+depth[faces[b][1]]+depth[faces[b][2]];
        if(za<zb) {order[i]=b;
            order[j]=a;}
    }
    for(int i=0;i<20;i++) {
        int f=order[i],a=faces[f][0],b=faces[f][1],c=faces[f][2];
        int color=(f%5==0?PAL_UI_GOLD:PAL_UI_CYAN)+light[f];
        ui_triangle(sx[a],sy[a],sx[b],sy[b],sx[c],sy[c],color);
        ui_line(sx[a],sy[a],sx[b],sy[b],color==PAL_UI_CYAN+7?PAL_UI_TEXT:PAL_UI_CYAN+2);
    }
}
static void intro(int time)
{
    stars(time,1);
    orbit(159,83,110,time,100);
    /* 片名在大面积暗场中出现，清晰的字形与空白比堆满特效更有力量。
     * 暖色细线随后伸展，给后续画面的青色几何留下统一视觉线索。 */
    if(time>60) film_center(39,"WELCOME TO",PAL_UI_TEXT,2);
    if(time>150) film_center(66,"GRAPHICS",PAL_UI_CYAN+7,3);
    int width=ui_clamp((time-190)*2,0,160);
    ui_span(159-width/2,99,width,PAL_UI_GOLD+6);
    if(time>240) film_chinese(111);
    if(time>320) film_center(138,"SandCore M7 / mio",PAL_UI_MUTED,1);
}
static void geometry(int time)
{
    stars(time,0);
    orbit(221,85,69,time,95);
    orbit(221,85,75,-time,180);
    crystal(221,84,190,time/2+50);
    for(int i=0;i<3;i++) {
        int a=time/2+i*341,x=221+ui_cos(a)*79/256,y=84+ui_sin(a)*33/256;
        ui_circle(x,y,5,PAL_UI_GOLD+2,1);
        ui_circle(x-1,y-1,3,PAL_UI_GOLD+6,1);
    }
    film_text(14,49,"LIGHT.",PAL_UI_TEXT,2);
    film_text(14,69,"DEPTH.",PAL_UI_TEXT,2);
    film_text(14,89,"MOTION.",PAL_UI_CYAN+7,2);
    ui_span(14,116,75,PAL_UI_GOLD+6);
    ui_text(14,129,"A canvas",PAL_UI_MUTED);
    ui_text(14,140,"with dimension.",PAL_UI_MUTED);
}
static int terrain_height(int x,int z,int time)
{return ui_sin(x*3+time/2)*25/256+ui_cos(z*2-time/3)*18/256;}
static void landscape(int time)
{
    /* 横向流动的三角形地形，暖日/远山/冷海分层；扫描线三角填充
     * 能力与游戏同源，镜头的空间感来自投影而非预渲染背景。 */
    ui_gradient(0,0,UI_W,70,PAL_UI_NIGHT);
    ui_circle(243,40,19,PAL_UI_GOLD+6,1);
    for(int x=0;x<UI_W;x++) {
        int h=5+ui_sin(x*6+100)*8/256+ui_sin(x*13)*4/256;
        ui_rect(x,62-h,1,h+8,PAL_UI_GOLD+1);
    }
    ui_rect(0,70,UI_W,84,PAL_WATER+1);
    int shift=time/7%90;
    for(int z=9;z>0;z--) for(int x=-4;x<4;x++) {
        int a=x*90,b=(x+1)*90,near=z*90-shift+110,far=near+90;
        int h0=terrain_height(a,near,time),h1=terrain_height(b,near,time);
        int h2=terrain_height(b,far,time),h3=terrain_height(a,far,time);
        int x0=159+a*180/near,y0=65+(80-h0)*180/near;
        int x1=159+b*180/near,y1=65+(80-h1)*180/near;
        int x2=159+b*180/far,y2=65+(80-h2)*180/far;
        int x3=159+a*180/far,y3=65+(80-h3)*180/far;
        int color=PAL_WATER+ui_clamp(6-z/2+(x&1),1,7);
        ui_triangle(x0,y0,x1,y1,x2,y2,color);
        ui_triangle(x0,y0,x2,y2,x3,y3,color-1);
        ui_line(x0,y0,x1,y1,PAL_UI_CYAN+3);
    }
    film_text(14,31,"MAKE SPACE",PAL_UI_TEXT,2);
    ui_text(14,51,"YOUR OWN.",PAL_UI_GOLD+7);
    ui_text(14,139,"Every surface is yours to shape.",PAL_UI_TEXT);
}
static void interfaces(int time)
{
    stars(time,0);
    int offset=ui_clamp(60-time/3,0,60);
    ui_panel(164+offset,45,141,90);
    ui_text(173+offset,55,"FILES / WORLD",PAL_UI_GOLD+7);
    ui_text(176+offset,75,"D HOME",PAL_UI_MUTED);
    ui_text(176+offset,88,"D SYS",PAL_UI_MUTED);
    ui_text(176+offset,101,"F CHUNK.SCW",PAL_UI_TEXT);
    ui_panel(82+offset/2,62,150,81);
    ui_text(92+offset/2,72,"DEBUG / LIVE",PAL_UI_CYAN+7);
    ui_text(94+offset/2,91,"EIP 00400018",PAL_UI_TEXT);
    ui_text(94+offset/2,105,"step / inspect",PAL_UI_MUTED);
    ui_panel(12,79,154,71);
    ui_text(22,89,"SC STUDIO",PAL_UI_TEXT);
    ui_text(22,107,"int main(void)",PAL_UI_CYAN+6);
    ui_text(22,120,"{ draw(); }",PAL_UI_GOLD+6);
    if((time/30)&1) ui_rect(111,130,5,2,PAL_UI_CYAN+7);
    film_text(14,25,"DESIGN. BUILD.",PAL_UI_TEXT,2);
    ui_text(14,47,"PLAY.  ALL INSIDE SANDCORE.",PAL_UI_MUTED);
}
static void constellation(int time)
{
    stars(time,1);
    int rotate=time/2;
    for(int i=0;i<110;i++) {
        int phase=i*47+rotate,radius=50+(i%4)*14;
        int x=ui_cos(phase)*radius/256,z=ui_sin(phase)*radius/256;
        int y=ui_sin(i*71+rotate/2)*55/256;
        int xx=(x*ui_cos(rotate)+z*ui_sin(rotate))/256;
        int zz=(-x*ui_sin(rotate)+z*ui_cos(rotate))/256+260;
        int px=159+xx*170/zz,py=87+y*170/zz;
        int color=(i%8==0?PAL_UI_GOLD:PAL_UI_CYAN)+ui_clamp(8-zz/55,1,7);
        ui_circle(px,py,zz<250?2:1,color,1);
    }
    orbit(159,87,75,-time,100);
    film_center(23,"CUSTOM GRAPHICS",PAL_UI_TEXT,2);
    film_center(141,"A world you can shape.",PAL_UI_MUTED,1);
}
static void finale(int time)
{
    stars(time,0);
    crystal(159,39,85,time/3);
    orbit(159,39,35,time,100);
    film_center(78,"WELCOME TO",PAL_UI_TEXT,2);
    film_center(103,"GRAPHICS",PAL_UI_CYAN+7,2);
    film_chinese(128);
    film_center(148,"SandCore M7 / mio",PAL_UI_GOLD+6,1);
}
static void render(int chapter,int local,int elapsed)
{
    ui_gradient(0,0,UI_W,UI_H,PAL_UI_NIGHT);
    if(chapter==0) intro(local);
    else if(chapter==1) geometry(local);
    else if(chapter==2) landscape(local);
    else if(chapter==3) interfaces(local);
    else if(chapter==4) constellation(local);
    else finale(local);
    ui_rect(0,0,UI_W,19,PAL_UI_INK);
    ui_text(10,6,chapter_names[chapter],PAL_UI_MUTED);
    ui_rect(0,158,UI_W,12,PAL_UI_INK);
    ui_text(10,160,paused?"PAUSED":"M7 / mio",PAL_UI_MUTED);
    ui_text(162,160,"SPACE pause R replay",PAL_UI_NIGHT+7);
    ui_span(0,157,UI_W*ui_clamp(elapsed,0,4200)/4200,PAL_UI_GOLD+5);
    /* 段落交接用倾斜光幕，保留前帧真实内容在幕右侧；左侧已经是新
     * 场景。整个混合在私有缓冲中完成后才 FRAME，用户不见半幅更新。 */
    int age=sc_tick()-transition_start;
    if(chapter>0&&age<40) {
        int reveal=age*(UI_W+70)/40;
        for(int y=19;y<157;y++) {
            int boundary=reveal-(y-19)/2;
            for(int x=ui_clamp(boundary,0,UI_W);x<UI_W;x++) ui_pixels[y*UI_W+x]=previous_frame[y*UI_W+x];
            ui_pixel(boundary,y,PAL_UI_CYAN+7);
            ui_pixel(boundary-2,y,PAL_UI_GOLD+6);
        }
    }
}
int main(void)
{
    if(ui_open("WELCOME TO GRAPHICS / mio")<0) return 1;
    start_tick=sc_tick();
    char args[128];
    sc_args(args,sizeof(args));
    /* 数字 1..6 可从对应段落开始，用于截图 QA，也让用户自由重看。
     * 不跳过代码生成测试，仍是同一程序的真实绘制路径。 */
    if(args[0]>='1'&&args[0]<='6') for(int i=0;i<args[0]-'1';i++) start_tick-=lengths[i];
    for(;;) {
        int key=sc_key();
        if(key==27) return 0;
        if(key==' ') {if(paused) {start_tick+=sc_tick()-pause_tick;
                paused=0;}else {pause_tick=sc_tick();
                paused=1;}}
        if(key=='r'||key=='R') {start_tick=sc_tick();
            last_chapter=-1;
            paused=0;}
        if(key>='1'&&key<='6') {start_tick=sc_tick();
            for(int i=0;i<key-'1';i++) start_tick-=lengths[i];
            paused=0;
            last_chapter=-1;}
        if(!ui_frame_due()) continue;
        int elapsed=(paused?pause_tick:sc_tick())-start_tick,local=elapsed,chapter=0;
        while(chapter<5&&local>=lengths[chapter]) local-=lengths[chapter++];
        if(elapsed>4200) {elapsed=4200;
            local=600;}
        if(chapter!=last_chapter) {
            for(int i=0;i<UI_W*UI_H;i++) previous_frame[i]=ui_pixels[i];
            transition_start=sc_tick();
            last_chapter=chapter;
        }
        render(chapter,local,elapsed);
        ui_present();
    }
}
