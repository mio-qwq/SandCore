/* =====================================================================
 * mio：DUNE RUN / M8。原玩法、12000单位圈长与i32最佳圈记录继续保留。
 * 道路按实际客户区逐物理像素透视采样；车辆是斜面三角网格、金属车架、
 * 反射玻璃、轮胎与灯组，使用SCENE真实射线求交和阴影/反射。
 * 车体与道路共享焦距和相机高度，不能再出现接近时尺寸不变的问题。
 * 仿真以4 PIT tick为一步，与绘制帧数解耦；鼠标控制和连续键同源。
 * 第一阶段源码未构建，现代画质与流畅程度须第二阶段实际验收。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
static int speed,distance,steer,lap,crashes,lap_start,best,lap_time,sim_tick;
static int opponent_size[3],opponent_ground[3],drive_mouse;
static int road_horizon,road_focal,road_eye=720;
static u32 asphalt_texture[16384],sand_texture[16384];
static char race_message[64]="Arrows or WASD / mouse pedals";
static int curvature(int z){return scn_sin(z/14)*34/256;}
static int road_center(int z){return curvature(distance+z/4)*z/256;}
static void textures(void)
{
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        int grain=(int)(scn_hash(x,0,y)&31)-15;
        asphalt_texture[y*128+x]=scn_light(SCN_ASPHALT,246+grain);
        int dune=scn_noise(x*4,y*4,64);
        sand_texture[y*128+x]=scn_light(SCN_SAND,232+dune/9+grain/4);
    }
}
static void backdrop(void)
{
    int width=ui_width,height=ui_height;
    road_horizon=height*38/100;road_focal=height*9/10;if(road_focal<1)road_focal=1;
    for(int y=0;y<height;y++){
        u32 *out=ui_pixels+y*width;
        if(y<=road_horizon){
            int blend=ui_clamp(y*256/(road_horizon?road_horizon:1),0,256);
            u32 sky=scn_mix(SCN_SKY_TOP,SCN_HORIZON,blend);
            for(int x=0;x<width;x++){
                int ridge=road_horizon-12-scn_sin(x*500/width+150)*height/900-scn_sin(x*1700/width)*height/1600;
                u32 color=y>ridge?scn_mix(SCN_STONE,SCN_HORIZON,170):sky;
                *out++=0xFF000000u|color;
            }
        }else{
            int z=road_eye*road_focal/(y-road_horizon);if(z>16000)z=16000;
            int center=road_center(z),track_z=distance*4+z;
            int fog=ui_clamp(z/50,0,210);
            for(int x=0;x<width;x++){
                int world_x=steer*8+(x-width/2)*z/road_focal;
                int lateral=world_x-center,absolute=ui_abs(lateral);
                u32 color;
                if(absolute<920){
                    color=z<5000?asphalt_texture[((track_z&127)<<7)+(world_x&127)]:SCN_ASPHALT;
                    if((absolute>890)||((ui_abs(lateral-307)<9||ui_abs(lateral+307)<9)&&((track_z/420)&1)))color=SCN_IVORY;
                    /* 长条低反射路面只在轻微粗糙变化中透出天空，不把
                     * 道路涂成镜子；远处自动消除纹理跳采样闪烁。 */
                    if(z<3800&&absolute<820&&((track_z/280)&7)==1)color=scn_mix(color,SCN_HORIZON,24);
                }else if(absolute<1000)color=((track_z/240)&1)?SCN_IVORY:SCN_PAINT;
                else color=z<5000?sand_texture[((track_z&127)<<7)+(world_x&127)]:SCN_SAND;
                *out++=0xFF000000u|scn_mix(color,SCN_HORIZON,fog);
            }
        }
    }
    /* 三组沿路防护柱与灌木随真实深度缩放，远/中/近景具有尺度参照。
     * 世界坐标锚定里程，重画同一时间不会随机闪出另一棵植物。 */
    for(int i=19;i>=0;i--){
        int z=900+((i*420-distance*4)%8400+8400)%8400;
        int ground=road_horizon+road_eye*road_focal/z;
        int center=road_center(z),h=260*road_focal/z,w=45*road_focal/z;if(w<1)w=1;
        for(int side=-1;side<=1;side+=2){
            int x=ui_width/2+(center+side*1180-steer*8)*road_focal/z;
            ui_physical_rgb(x,ground-h,w,h,SCN_METAL);
            ui_physical_rgb(x,ground-h,w,h/5+1,SCN_IVORY);
            int bx=ui_width/2+(center+side*1650-steer*8)*road_focal/z;
            int leaf=170*road_focal/z;if(leaf<1)leaf=1;
            for(int row=-leaf;row<=0;row++){
                int half=scn_isqrt((u32)(leaf*leaf-row*row));
                ui_physical_rgb(bx-half,ground+row,half*2+1,1,scn_light(SCN_GRASS,220+row*40/(leaf+1)));
            }
        }
    }
}
static void quad(ScnVec *a,ScnVec *b,ScnVec *c,ScnVec *d,u32 color,int shine)
{scn_triangle(a,b,c,color,shine);scn_triangle(a,c,d,color,shine);}
static void car_model(int x,int z,u32 paint)
{
    scn_reset();scn_floor_color=SCN_ASPHALT;scn_floor_reflection=12;
    scn_box(x-228,90,z-390,x+228,184,z+390,paint,65,0);
    scn_box(x-230,86,z-400,x+230,100,z+400,SCN_METAL,100,0);
    /* 八顶点斜面车舱：下缘长、上缘短，后玻璃和前风挡形成真正斜面。
     * 金属车漆与玻璃各自反射，几何随镜头变化，不是固定屏幕图标。 */
    ScnVec v[8];
    scn_set(&v[0],x-210,184,z-290);scn_set(&v[1],x+210,184,z-290);
    scn_set(&v[2],x+210,184,z+260);scn_set(&v[3],x-210,184,z+260);
    scn_set(&v[4],x-165,350,z-140);scn_set(&v[5],x+165,350,z-140);
    scn_set(&v[6],x+165,350,z+110);scn_set(&v[7],x-165,350,z+110);
    quad(&v[0],&v[1],&v[5],&v[4],SCN_GLASS,166);
    quad(&v[3],&v[7],&v[6],&v[2],SCN_GLASS,166);
    quad(&v[0],&v[4],&v[7],&v[3],SCN_GLASS,140);
    quad(&v[1],&v[2],&v[6],&v[5],SCN_GLASS,140);
    quad(&v[4],&v[5],&v[6],&v[7],paint,88);
    for(int side=-1;side<=1;side+=2){
        scn_sphere(x+side*214,96,z-260,96,SCN_RUBBER,8,0);
        scn_sphere(x+side*214,96,z+265,96,SCN_RUBBER,8,0);
        scn_box(x+side*146-48,143,z-405,x+side*146+48,163,z-397,SCN_PAINT,36,0);
        scn_box(x+side*146-42,151,z+389,x+side*146+42,174,z+399,SCN_IVORY,40,0);
    }
    scn_box(x-75,109,z-406,x+75,136,z-400,SCN_IVORY,5,0);
}
static void render_car(int x,int z,u32 color)
{
    car_model(x,z,color);
    int near=z-500;if(near<300)near=300;
    int left=ui_width/2+(x-steer*8-380)*road_focal/near;
    int right=ui_width/2+(x-steer*8+380)*road_focal/near;
    int top=road_horizon+(road_eye-430)*road_focal/(z+500);
    int bottom=road_horizon+road_eye*road_focal/near;
    left=ui_clamp(left,0,ui_width);right=ui_clamp(right,0,ui_width);
    top=ui_clamp(top,0,ui_height);bottom=ui_clamp(bottom,0,ui_height);
    ScnVec eye,light;scn_set(&eye,steer*8,road_eye,0);scn_set(&light,-115,205,102);scn_unit(&light);
    for(int y=top;y<bottom;y++)for(int px=left;px<right;px++){
        ScnVec ray;scn_set(&ray,px-ui_width/2,road_horizon-y,road_focal);
        ScnHit hit;scn_floor=0;scn_intersect(&eye,&ray,&hit,scn_ratio16(14000,scn_length(&ray)));
        if(hit.found){scn_floor=1;ui_pixels[y*ui_width+px]=0xFF000000u|scn_trace(&eye,&ray);}
        else if(ray.y<0){
            int t=scn_ratio16(-eye.y,ray.y);ScnVec point;scn_set(&point,eye.x+ray.x*t/65536,4,ray.z*t/65536);
            ScnHit shadow;scn_intersect(&point,&light,&shadow,scn_ratio16(16000,scn_length(&light)));
            if(shadow.found)ui_pixels[y*ui_width+px]=0xFF000000u|scn_light(ui_pixels[y*ui_width+px],166);
        }
    }
}
static void draw(void)
{
    int begun=sc_tick();backdrop();int drawn=0;
    for(int rank=0;rank<3;rank++){
        int chosen=-1,ahead=-1;
        for(int i=0;i<3;i++)if(!(drawn&(1<<i))){
            int d=(i*1400+2800-distance%4200+4200)%4200;
            if(d>ahead){ahead=d;chosen=i;}
        }
        drawn|=1<<chosen;int z=1100+ahead*3/2;
        /* 同一深度影响车体、地面投影和碰撞参照；完整车尾也必须
         * 留在场景8192坐标界内。远车不能因超界被求交器整车拒绝。 */
        opponent_size[chosen]=480*road_focal/z;
        opponent_ground[chosen]=road_horizon+road_eye*road_focal/z;
        render_car(road_center(z)+(chosen-1)*344,z,chosen==0?SCN_IVORY:chosen==1?SCN_GLASS:SCN_SAND);
    }
    render_car(steer*8,1100,SCN_PAINT);
    scn_frame_ticks=sc_tick()-begun;scn_render_width=ui_width;scn_render_height=ui_height;
    ui_panel(12,10,ui_compact?120:176,ui_compact?46:66);
    ui_text(20,14,"DUNE RUN",PAL_UI_TEXT);ui_number(20,34,speed*3,PAL_UI_CYAN+7);
    ui_text(66,34,"km/h",PAL_UI_MUTED);
    if(UI_W>=420){
        ui_panel(UI_W-196,10,184,66);ui_text(UI_W-188,16,"LAP",PAL_UI_MUTED);
        ui_number(UI_W-148,16,lap+1,PAL_UI_TEXT);
        ui_number(UI_W-188,40,(sc_tick()-lap_start)/100,PAL_UI_TEXT);
        ui_text(UI_W-148,40,"seconds",PAL_UI_MUTED);
    }
    int y=UI_H-52;drive_mouse=0;
    const char *names[4]={"Left","Gas","Brake","Right"};
    for(int i=0;i<4;i++){
        int x=12+i*68;ui_small_control(10+i,x,y,62,names[i],0);
        if(ui_hit(x,y,62,22)&&(ui_buttons&1))drive_mouse|=1<<i;
    }
    if(UI_W>=370)ui_small_control(1,292,y,68,"New",0);
    ui_footer(race_message);
}
static void reset(void)
{speed=distance=steer=lap=crashes=0;lap_start=sim_tick=sc_tick();copy(race_message,"Arrows or WASD / mouse pedals",sizeof(race_message));}
static void simulation(void)
{
    if(sc_down(0x80)||sc_down('w')||(drive_mouse&2))speed=ui_clamp(speed+2,0,90);
    else speed=ui_clamp(speed-1,0,90);
    if(sc_down(0x81)||sc_down('s')||(drive_mouse&4))speed=ui_clamp(speed-5,0,90);
    if(sc_down(0x82)||sc_down('a')||(drive_mouse&1))steer-=4;
    if(sc_down(0x83)||sc_down('d')||(drive_mouse&8))steer+=4;
    steer-=curvature(distance)*speed/1200;steer=ui_clamp(steer,-140,140);
    if(ui_abs(steer)>100)speed=speed*9/10;
    int before=distance;distance+=speed;
    for(int i=0;i<3;i++){
        int ahead=(i*1400+2800-before%4200+4200)%4200;
        if(ahead<speed&&ui_abs(steer-(i-1)*43)<20){speed/=3;crashes++;copy(race_message,"Contact / recover your line",sizeof(race_message));}
    }
    if(distance>=12000){
        distance-=12000;lap++;lap_time=sim_tick-lap_start;lap_start=sim_tick;
        if(!best||lap_time<best){
            if(sc_write("HOME/RACE/RECORD",&lap_time,sizeof(lap_time))==(int)sizeof(lap_time)){
                best=lap_time;copy(race_message,"New best lap / record saved",sizeof(race_message));
            }else copy(race_message,"Lap finished / record save failed",sizeof(race_message));
        }
    }
}
int main(void)
{
    if(ui_open("DUNE RUN / mio")<0)return 1;
    sc_mkdir("HOME/RACE");
    if(sc_read("HOME/RACE/RECORD",&best,sizeof(best))!=4||best<0)best=0;
    textures();reset();
    for(;;){
        int key=sc_key();if(key==27)return 0;
        if(key=='r'||key=='R')reset();
        /* 队列短按提供一次最小响应，持续按住再由固定步长仿真处理。 */
        if(key==0x80||key=='w')speed=ui_clamp(speed+2,0,90);
        if(key==0x81||key=='s')speed=ui_clamp(speed-5,0,90);
        if(key==0x82||key=='a')steer=ui_clamp(steer-4,-140,140);
        if(key==0x83||key=='d')steer=ui_clamp(steer+4,-140,140);
        int now=sc_tick(),steps=0;
        while(now-sim_tick>=4&&steps<8){sim_tick+=4;simulation();steps++;}
        if(now-sim_tick>32)sim_tick=now;
        ui_pointer();
        if(UI_W<280||UI_H<172){
            ui_header("DUNE RUN","Enlarge to drive");ui_small_control(98,16,60,112,"Enlarge",0);
            ui_present();if(ui_action==98)sc_window(ui_win,1);sc_yield();continue;
        }
        draw();ui_present();if(ui_action==1)reset();sc_yield();
    }
}
