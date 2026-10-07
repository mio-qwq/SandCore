/* =====================================================================
 * mio：Welcome to true color. / M8连续实时真彩光追短片。
 * 每个客户区物理像素发出主射线；球/盒/地面解析求交，物体遮挡太阳，
 * 反射最多两次，镜头与物体随PIT时间运动。没有预录画面或宿主渲染。
 * 六幕保留M7的Space/R/1..6/Esc控制和42秒时间轴，旧片原字节在LEGACY。
 * 实际分辨率与上一帧耗时随画面展示；不把100Hz时钟宣称成100FPS。
 * 第一阶段仅写实现，帧率、画质、全部交互留待批准后的第二阶段。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
static int lengths[6]={600,800,800,800,600,600};
static const char *chapter_names[6]={"THE FIRST LIGHT","MATTER AND REFLECTION","OPEN HORIZONS",
    "A SPACE TO CREATE","EVERY COLOR, ALIVE","WELCOME TO TRUE COLOR."};
static int start_tick,pause_tick,paused,last_chapter=-1,film_exit;
static int film_chapter,film_local,film_elapsed;
static int film_frame_valid,film_frame_width,film_frame_height,film_frame_chapter,film_frame_local;
static ScnScene film_context;
static ScnObject film_objects[64];
static ScnPrepared film_prepared[64];
static u32 film_spectrum[7]={SCN_RED,SCN_ORANGE,SCN_YELLOW,SCN_GREEN,SCN_CYAN,SCN_BLUE,SCN_VIOLET};
static void film_quad(ScnVec *a,ScnVec *b,ScnVec *c,ScnVec *d,u32 color,int reflection)
{scn_triangle(a,b,c,color,reflection);scn_triangle(a,c,d,color,reflection);}
static void film_prism(int time)
{
    /* mio：独立的光谱艺术装置。三角柱由真实八个三角形封闭，金属/
     * 玻璃色表面参与交点、太阳阴影和反射；七条彩带是台面上的
     * 光谱装置实体，每个像素仍走同一射线求交，不贴预录彩虹图片。
     * 当前整数材质库尚无波长/Snell折射模型，因此不把彩带称为
     * 物理色散或路径追踪。这一段明确表达“丰富真彩”，视觉和
     * 几何各有真实实现，同时如实保留光学模型的边界。
     * 私有64物体数组足够装置与建筑，不依赖默认32项发生静默截断。 */
    ScnVec v[6];
    scn_set(&v[0],-340,154,270);scn_set(&v[1],340,154,270);scn_set(&v[2],0,970,270);
    scn_set(&v[3],-340,154,630);scn_set(&v[4],340,154,630);scn_set(&v[5],0,970,630);
    scn_triangle(&v[0],&v[1],&v[2],SCN_GLASS,182);
    scn_triangle(&v[3],&v[5],&v[4],SCN_GLASS,182);
    film_quad(&v[0],&v[3],&v[4],&v[1],SCN_METAL,124);
    film_quad(&v[0],&v[2],&v[5],&v[3],SCN_GLASS,192);
    film_quad(&v[1],&v[4],&v[5],&v[2],SCN_GLASS,192);
    for(int i=0;i<7;i++){
        int start=-170+i*48,end=-1300+i*360;
        ScnVec a,b,c,d;
        scn_set(&a,start,64,650);scn_set(&b,start+46,64,650);
        scn_set(&c,end+340,64,1420);scn_set(&d,end,64,1420);
        film_quad(&a,&b,&c,&d,film_spectrum[i],38);
        int phase=i*146+time/3;
        scn_sphere(scn_sin(phase)*4,500+scn_sin(phase*2)/2,430+scn_cos(phase)*4,
                   64,film_spectrum[i],90,0);
    }
}
static void film_scene(int chapter,int time)
{
    scn_reset();
    int drift=scn_sin(time/8),orbit=time/6;
    /* 建筑骨架固定，镜头改变仍能认出同一世界。大石台、细金属边、
     * 木片与透明感玻璃球共同提供近景细节和反射参照。 */
    scn_floor_color=chapter==2?SCN_SEA:SCN_STONE;
    scn_floor_reflection=chapter==2?150:48;
    scn_box(-1800,0,-700,1800,60,1500,SCN_CERAMIC,18,3);
    scn_box(-1850,0,-760,1850,24,-700,SCN_METAL,120,0);
    scn_box(-950,60,180,200,140,750,SCN_OAK,22,2);
    scn_box(-1050,140,240,150,154,690,SCN_METAL,110,0);
    if(chapter==0){
        scn_sphere(0,430,330,280,SCN_METAL,205,0);
        scn_sphere(-520,270,100,116,SCN_SAND,90,1);
        scn_camera(1700-time,640+drift/4,-1400,0,300,300);
    }else if(chapter==1){
        scn_sphere(-470,410,390,256,SCN_PAINT,76,0);
        scn_sphere(160,530,500,380,SCN_METAL,216,0);
        scn_sphere(750,260,80,200,SCN_GLASS,158,0);
        scn_camera(scn_sin(orbit+80)*6,580+drift/3,-scn_cos(orbit+80)*7,80,360,300);
    }else if(chapter==2){
        /* 水镜平台真实反射建筑；开敞柱列的阴影由同一求交器计算，
         * 不预先画一块斜黑色贴片冒充随镜头一致的遮挡。 */
        for(int i=0;i<5;i++){
            int x=-1300+i*600;
            scn_box(x,60,1050,x+60,1350,1110,SCN_CERAMIC,10,1);
            scn_box(x,1320,-200,x+60,1380,1110,SCN_OAK,12,2);
        }
        scn_box(-1320,1300,1050,1180,1380,1150,SCN_CERAMIC,12,1);
        scn_sphere(-240,440,360,290,SCN_IVORY,60,0);
        scn_camera(-2200+time*3,620,-1800+drift,0,480,600);
    }else if(chapter==3){
        /* 物体是工作台上的实体面板，边框、屏面和底座均参与求交。
         * 屏面只象征创作工具；不伪造正在运行的调试寄存器数据。 */
        for(int i=0;i<3;i++){
            int x=-900+i*650,up=(scn_sin(time/3+i*130)+256)/8;
            scn_box(x,170+up,340,x+500,650+up,370,SCN_METAL,90,0);
            scn_box(x+22,192+up,326,x+478,628+up,340,i==0?SCN_GLASS:i==1?SCN_PAINT:SCN_SAND,45,0);
            scn_box(x+220,60,390,x+280,170+up,450,SCN_METAL,96,0);
        }
        scn_camera(1300-time*2,780,-1500,30,410,350);
    }else if(chapter==4){
        film_prism(time);
        scn_camera(1750+drift/2,1150,-1650,0,340,540);
    }else{
        scn_sphere(0,570,510,410,SCN_METAL,200,0);
        scn_sphere(-670,250,210,170,SCN_SAND,100,1);
        scn_sphere(650,290,340,220,SCN_GLASS,146,0);
        // 收尾保留七色弧阵，与主视觉呼应；它是空间球阵，不是屏幕贴花。
        for(int i=0;i<7;i++){
            int phase=64+i*64;
            scn_sphere(scn_cos(phase)*5,240+scn_sin(phase)*4,1120,74,film_spectrum[i],86,0);
        }
        scn_camera(400+drift/2,740,-1900-time/2,0,400,450);
    }
    /* 前景实体细石珠产生接触阴影，避免材质展示只剩三个空悬球。 */
    for(int i=0;i<4;i++)scn_sphere(-850+i*180,100,-300+(i&1)*50,40,SCN_STONE,12,1);
}
static void film_center(int physical_y,const char *text,int multiplier,u32 color)
{
    /* 直接在当前物理帧绘凤凰字形。改变的是这一段文字的整数缩放，
     * 不是把整场低分辨率渲染放大；结束立即还原NUI布局尺度。 */
    int old_scale=ui_scale,old_width=UI_W,old_height=UI_H;
    int target_scale=old_scale*multiplier;
    if(length(text)*8*target_scale/100>ui_width-32)target_scale=old_scale;
    ui_scale=target_scale;
    UI_W=ui_width*100/ui_scale;UI_H=ui_height*100/ui_scale;
    int x=(ui_width*100/ui_scale-length(text)*8)/2,y=physical_y*100/ui_scale;
    u32 saved=ui_colors[PAL_UI_TEXT];ui_colors[PAL_UI_TEXT]=SCN_SHADE;
    ui_text(x+1,y+1,text,PAL_UI_TEXT);ui_colors[PAL_UI_TEXT]=color;
    ui_text(x,y,text,PAL_UI_TEXT);ui_colors[PAL_UI_TEXT]=saved;ui_scale=old_scale;
    UI_W=old_width;UI_H=old_height;
}
static int render(void)
{
    int begun=sc_tick(),width=ui_width,height=ui_height;
    scn_render_width=width;scn_render_height=height;
    film_scene(film_chapter,film_local);
    if(scn_prepare_primary(&scn_eye,film_prepared,64)){film_exit=1;return 0;}
    int focal=width*3/4;if(focal<1)focal=1;
    for(int y=0;y<height;y++){
        u32 *out=ui_pixels+y*width;
        for(int x=0;x<width;x++){
            ScnVec direction;scn_camera_ray(&direction,x,y,width,height,focal);
            u32 color=scn_trace(&scn_eye,&direction);
            /* 镜头四角仅轻微减光，不用模糊掩盖射线或材料边界。 */
            int nx=(x-width/2)*256/(width?width:1),ny=(y-height/2)*256/(height?height:1);
            color=scn_light(color,256-(nx*nx+ny*ny)/1200);
            *out++=0xFF000000u|color;
        }
        /* 耗时帧也定期让Esc生效；不消费其它队列事件，下一主循环
         * 仍能收到短按Space或跳幕。窗口被叉掉时由内核正常回收。 */
        if((y&15)==0&&sc_key_peek()==27){sc_key();film_exit=1;return 0;}
    }
    scn_frame_ticks=sc_tick()-begun;
    film_frame_width=width;film_frame_height=height;
    film_frame_chapter=film_chapter;film_frame_local=film_local;film_frame_valid=1;
    return 1;
}
static void film_hud(void)
{
    int top=ui_compact?24:32;
    ui_rect_rgb(0,0,UI_W,top,ui_role(SC_THEME_PAPER));
    ui_text(12,4,chapter_names[film_chapter],PAL_UI_TEXT);
    int y=UI_H-(ui_compact?54:62);
    ui_rect_rgb(0,y,UI_W,UI_H-y,ui_role(SC_THEME_PAPER));
    ui_rect_rgb(0,y,UI_W*ui_clamp(film_elapsed,0,4200)/4200,2,ui_role(SC_THEME_ACCENT));
    ui_small_control(1,12,y+6,64,paused?"Play":"Pause",0);
    ui_small_control(2,82,y+6,72,"Replay",0);
    for(int i=0;i<6;i++){
        char label[2];label[0]=(char)('1'+i);label[1]=0;
        int x=164+i*27;
        if(x+24<UI_W-8)ui_small_control(10+i,x,y+6,24,label,i==film_chapter);
    }
    char status[96],number[16];decimal(number,scn_render_width);copy(status,number,sizeof(status));
    append(status,"x",sizeof(status));decimal(number,scn_render_height);append(status,number,sizeof(status));
    append(status,"  ",sizeof(status));decimal(number,scn_frame_ticks*10);append(status,number,sizeof(status));
    append(status,"ms / traced frame",sizeof(status));ui_text(12,y+32,status,PAL_UI_MUTED);
    if(film_chapter==0||film_chapter==5){
        film_center(ui_height*67/100,"Welcome to true color.",UI_W>=500?3:1,SCN_IVORY);
        film_center(ui_height*76/100,"SandCore M8 / mio",1,SCN_IVORY);
    }else film_center(ui_height*75/100,film_chapter==1?"Light belongs to the surface.":
        film_chapter==2?"Room for a wider world.":film_chapter==3?"Create. Build. Play.":"Every color, alive.",1,SCN_IVORY);
}
static void jump_to(int chapter)
{
    start_tick=sc_tick();for(int i=0;i<chapter;i++)start_tick-=lengths[i];
    paused=0;last_chapter=-1;ui_followup=1;
}
int main(void)
{
    if(ui_open("Welcome to true color. / mio")<0)return 1;
    if(scn_bind(&film_context,film_objects,64))return 2;
    // 第一张实际提交的主题准备页给出反馈，不等数十万条射线结束才出画面。
    ui_background(SC_THEME_PAPER);ui_pointer();ui_text(20,24,"Welcome to true color.",PAL_UI_TEXT);
    ui_text(20,50,"Preparing the first traced frame...",PAL_UI_MUTED);ui_present();
    start_tick=sc_tick();char args[128];sc_args(args,sizeof(args));
    if(args[0]>='1'&&args[0]<='6')jump_to(args[0]-'1');
    for(;;){
        if(!ui_visibility_ready())continue;
        int key=sc_key(),now=sc_tick();
        if(key==27||film_exit)return 0;
        if(key==' '){if(paused){start_tick+=now-pause_tick;paused=0;}else{pause_tick=now;paused=1;}ui_followup=1;}
        if(key=='r'||key=='R')jump_to(0);
        if(key>='1'&&key<='6')jump_to(key-'1');
        if(paused&&!ui_frame_due())continue;
        ui_pointer();film_elapsed=ui_clamp((paused?pause_tick:sc_tick())-start_tick,0,4200);
        film_local=film_elapsed;film_chapter=0;
        while(film_chapter<5&&film_local>=lengths[film_chapter])film_local-=lengths[film_chapter++];
        /* mio：暂停后的hover、主题或定期UI唤醒并不改变场景。
         * 原实现仍重跑每条射线，暂停也能持续100%忙碌。私有ui_pixels
         * 已保存最后完整场景，HUD覆盖自己拥有的顶/底带，直接重画即可。
         * 尺寸改变可能使NUI换缓冲，所以宽/高必须参加有效性判断；
         * 时间/跳幕改变也要重算，绝不沿用另一镜头的图。
         * 没有减少播放中的射线/反射，没有额外数MB影像或宿主缓存。
         * 暂停第一帧若时间确实改变仍计算一次，后续控件不重复光追。 */
        if(!paused||!film_frame_valid||film_frame_width!=ui_width||film_frame_height!=ui_height
            ||film_frame_chapter!=film_chapter||film_frame_local!=film_local){
            if(!render())continue;
        }
        film_hud();ui_present();last_chapter=film_chapter;
        if(ui_action==1){now=sc_tick();if(paused){start_tick+=now-pause_tick;paused=0;}else{pause_tick=now;paused=1;}ui_followup=1;}
        else if(ui_action==2)jump_to(0);
        else if(ui_action>=10&&ui_action<=15)jump_to(ui_action-10);
        sc_yield();
    }
}
