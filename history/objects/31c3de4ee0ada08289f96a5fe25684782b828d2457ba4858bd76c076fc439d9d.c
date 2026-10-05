/* =====================================================================
 * mio：DUNE RUN / M8。三路线、五车手、三站积分与驾驶策略；legacy保留原圈长/记录。
 * 道路按实际客户区逐物理像素透视采样；车辆是斜面三角网格、金属车架、
 * 反射玻璃、轮胎与灯组，使用SCENE真实射线求交和阴影/反射。
 * 车体与道路共享焦距和相机高度，不能再出现接近时尺寸不变的问题。
 * 仿真以4 PIT tick为一步，与绘制帧数解耦；鼠标控制和连续键同源。
 * 第一阶段源码未构建，现代画质与流畅程度须第二阶段实际验收。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
#include "RACEGAME.H"
static int speed,distance,steer,lap,crashes,lap_start,best,lap_time,sim_tick;
static int opponent_size[RACE_RIVALS],opponent_ground[RACE_RIVALS],drive_mouse;
static int race_track,race_mode=2,race_menu=1,race_legacy;
static int road_horizon,road_focal,road_view_height,road_eye=720;
static u32 asphalt_texture[16384],sand_texture[16384];
static char race_message[64]="Arrows or WASD / mouse pedals";
/* 路线是里程的周期函数，接缝处角度连续；三种频率组合形成
 * 沙漠长弯、海岸连续S弯和高地紧弯，物理与渲染共用此函数。 */
static int curvature(int z)
{
    if(race_legacy)return scn_sin(z/14)*34/256;
    int angle=(z%race_length())*1024/race_length();
    if(race_track==1)return scn_sin(angle*4)*40/256+scn_sin(angle*7)*14/256;
    if(race_track==2)return scn_sin(angle*3)*52/256+scn_sin(angle*9)*20/256;
    return scn_sin(angle*2)*34/256+scn_sin(angle*5)*12/256;
}
static int road_center(int z){return curvature(distance+z/4)*z/256;}
#include "race_game.inc"
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
    /* 矮客户区把最后76布局单位留给状态和踏板，让相机投影也
     * 使用同一个缩短后的视口。只移动HUD而不改焦距/消失点，
     * 近处玩家车会落在按钮后面，实际能操作却完全看不到车。
     * 这里减少的是明确的界面保留区，每个可见物理像素仍独立
     * 求交；不是先渲低清图再放大到客户区。 */
    if(UI_H<240)height-=ui_px(76);
    road_view_height=height;
    road_horizon=height*38/100;road_focal=height*9/10;if(road_focal<1)road_focal=1;
    for(int y=0;y<height;y++){
        u32 *out=ui_pixels+y*width;
        if(y<=road_horizon){
            int blend=ui_clamp(y*256/(road_horizon?road_horizon:1),0,256);
            u32 sky=scn_mix(race_track==2?SCN_SHADE:SCN_SKY_TOP,SCN_HORIZON,blend);
            for(int x=0;x<width;x++){
                int ridge=road_horizon-12-scn_sin(x*500/width+150)*height/900-scn_sin(x*1700/width)*height/(race_track==2?90:1600);
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
                else if(race_track==1 && lateral>1500){
                    int wave=scn_sin(track_z/9+world_x/17)*22/256;
                    color=scn_light(SCN_SEA,240+wave);
                }else if(race_track==2)color=scn_light(SCN_GRASS,220+scn_noise(world_x,track_z,128)/8);
                else color=z<5000?sand_texture[((track_z&127)<<7)+(world_x&127)]:SCN_SAND;
                /* 起点维修区实际占道路右肩前2400单位。玩家必须
                 * 降到低速并靠右才能维修，世界标线与物理区域同源。 */
                if((track_z/4)%race_length()<2400 && lateral>720 && lateral<1000)color=scn_mix(color,SCN_GLASS,100);
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
            int leaf=170*road_focal/z;if(leaf<1)leaf=1;if(race_track==2)leaf*=2;
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
/* mio：物件在同一道路相机中求交，范围先按几何投影裁剪。
 * 车、路边建筑、树和障碍共享此函数；场景原点与道路射线一致，
 * 避免每类道具使用另一套缩放公式。阴影真实查询当前物件场景。 */
static void race_trace_region(int left,int top,int right,int bottom)
{
    ScnVec eye,light;scn_set(&eye,steer*8,road_eye,0);scn_set(&light,-115,205,102);scn_unit(&light);
    for(int y=top;y<bottom;y++)for(int px=left;px<right;px++){
        ScnVec ray;scn_set(&ray,px-ui_width/2,road_horizon-y,road_focal);
        ScnHit hit;scn_floor=0;scn_intersect(&eye,&ray,&hit,scn_ratio16(14000,scn_length(&ray)));
        if(hit.found){scn_floor=1;ui_pixels[y*ui_width+px]=0xFF000000u|scn_trace(&eye,&ray);}
        else if(ray.y<0){
            int t=scn_ratio16(-eye.y,ray.y);ScnVec point;scn_set(&point,eye.x+scn_displace(ray.x,t),4,scn_displace(ray.z,t));
            ScnHit shadow;scn_intersect(&point,&light,&shadow,scn_ratio16(16000,scn_length(&light)));
            if(shadow.found)ui_pixels[y*ui_width+px]=0xFF000000u|scn_light(ui_pixels[y*ui_width+px],166);
        }
    }
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
    top=ui_clamp(top,0,road_view_height);bottom=ui_clamp(bottom,0,road_view_height);
    race_trace_region(left,top,right,bottom);
}
/* 每4200单位固定一处道路景观，进入镜头时由里程确定出现位置。
 * 沙漠站台、沿海灯塔、高地松树均有多部分实体几何和材质。
 * 近中远景尺度同源；构图侧留出道路与消失点，避免装饰挡住驾驶。 */
static void race_props_draw(void)
{
    for(int i=9;i>=0;i--){
        int z=1200+((i*780-distance*4)%7800+7800)%7800;
        if(z>7200)continue;
        int side=i&1?1:-1,x=road_center(z)+side*1850;
        scn_reset();scn_floor=0;scn_floor_color=SCN_ASPHALT;
        if(race_track==1 && i%3==0){
            scn_box(x-150,0,z-140,x+150,1000,z+140,SCN_IVORY,12,1);
            scn_box(x-220,900,z-210,x+220,1050,z+210,SCN_CERAMIC,38,3);
            scn_box(x-165,1050,z-155,x+165,1150,z+155,SCN_GLASS,160,0);
            scn_sphere(x,1190,z,80,SCN_SUN,0,0);
        }else if(race_track==2){
            scn_box(x-32,0,z-32,x+32,800,z+32,SCN_OAK,2,2);
            for(int layer=0;layer<3;layer++){
                int base=220+layer*180,r=420-layer*110;
                ScnVec a,b,c;scn_set(&a,x-r,base,z-r);scn_set(&b,x+r,base,z-r);scn_set(&c,x,base+560,z);
                scn_triangle(&a,&b,&c,SCN_GRASS,2);
                scn_set(&a,x+r,base,z+r);scn_triangle(&b,&a,&c,SCN_GRASS,2);
                scn_set(&b,x-r,base,z+r);scn_triangle(&a,&b,&c,SCN_GRASS,2);
                scn_set(&a,x-r,base,z-r);scn_triangle(&b,&a,&c,SCN_GRASS,2);
            }
        }else if(i%3==0){
            scn_box(x-410,0,z-180,x+410,300,z+180,SCN_STONE,4,1);
            scn_box(x-470,300,z-220,x+470,350,z+220,SCN_OAK,16,2);
            scn_box(x-250,80,z-185,x+250,230,z-178,SCN_GLASS,100,0);
            scn_box(x-24,350,z-24,x+24,710,z+24,SCN_METAL,64,0);
        }else{
            scn_box(x-45,0,z-45,x+45,480,z+45,SCN_OAK,2,2);
            scn_sphere(x,490,z,260,SCN_GRASS,3,1);
            scn_sphere(x-130,360,z-70,170,SCN_GRASS,3,1);
        }
        int near=z-500;if(near<300)near=300;
        int left=ui_width/2+(x-steer*8-520)*road_focal/near;
        int right=ui_width/2+(x-steer*8+520)*road_focal/near;
        int top=road_horizon+(road_eye-1250)*road_focal/near;
        int bottom=road_horizon+road_eye*road_focal/near;
        race_trace_region(ui_clamp(left,0,ui_width),ui_clamp(top,0,ui_height),
            ui_clamp(right,0,ui_width),ui_clamp(bottom,0,ui_height));
    }
}
static void draw(void)
{
    int begun=sc_tick();backdrop();race_props_draw();int drawn=0;
    if(race_has_rivals())for(int rank=0;rank<RACE_RIVALS;rank++){
        int chosen=-1,ahead=-1,total=lap*race_length()+distance;
        for(int i=0;i<RACE_RIVALS;i++)if(!(drawn&(1<<i))){
            int d=rival_distance[i]-total;
            if(d>=0 && d<=4300 && d>ahead){ahead=d;chosen=i;}
        }
        if(chosen<0)break;
        drawn|=1<<chosen;int z=1100+ahead*3/2;
        opponent_size[chosen]=480*road_focal/z;
        opponent_ground[chosen]=road_horizon+road_eye*road_focal/z;
        u32 colors[5]={SCN_IVORY,SCN_GLASS,SCN_SAND,SCN_METAL,SCN_GRASS};
        render_car(road_center(z)+rival_lane[chosen]*8,z,colors[chosen]);
    }
    render_car(steer*8,1100,SCN_PAINT);
    scn_frame_ticks=sc_tick()-begun;scn_render_width=ui_width;scn_render_height=road_view_height;
    if(UI_H<240){
        ui_rect_rgb(0,UI_H-76,UI_W,76,ui_role(SC_THEME_PAPER));
        ui_panel(8,4,184,22);ui_text(14,7,"DUNE RUN",PAL_UI_TEXT);
        ui_number(100,7,speed*3,PAL_UI_CYAN+7);ui_text(136,7,"km/h",PAL_UI_MUTED);
    }else{
        ui_panel(12,10,ui_compact?120:176,ui_compact?46:66);
        ui_text(20,14,"DUNE RUN",PAL_UI_TEXT);ui_number(20,34,speed*3,PAL_UI_CYAN+7);
        ui_text(66,34,"km/h",PAL_UI_MUTED);
        if(UI_W>=420){
            ui_panel(UI_W-196,10,184,66);ui_text(UI_W-188,16,"LAP",PAL_UI_MUTED);
            ui_number(UI_W-148,16,lap+1,PAL_UI_TEXT);
            ui_number(UI_W-188,40,(race_elapsed-lap_start)/100,PAL_UI_TEXT);
            ui_text(UI_W-148,40,"seconds",PAL_UI_MUTED);
        }
    }
    /* 驾驶中也有鼠标暂停入口：窄窗避开左侧速度卡，宽窗避开
     * 右侧圈数卡；不能只在文案写M而让鼠标用户找不到选项。 */
    ui_small_control(116,UI_W-76,UI_H<240 || UI_W<420?4:88,64,"Menu",0);
    int y=UI_H-52;drive_mouse=0;
    const char *names[4]={"Left","Gas","Brake","Right"};
    for(int i=0;i<4;i++){
        int x=12+i*68;ui_small_control(10+i,x,y,62,names[i],0);
        if(ui_hit(x,y,62,22)&&(ui_buttons&1))drive_mouse|=1<<i;
    }
    if(UI_W>=370)ui_small_control(1,292,y,68,"New",0);
    if(UI_W>=500){
        ui_small_control(14,368,y,60,"Boost",race_boosting);
        ui_small_control(15,434,y,60,"Drift",race_drift);
        if(ui_hit(368,y,60,22)&&(ui_buttons&1))drive_mouse|=16;
        if(ui_hit(434,y,60,22)&&(ui_buttons&1))drive_mouse|=32;
    }
    ui_footer(race_message);race_content_draw();
}
static void reset(void)
{
    speed=distance=steer=lap=crashes=lap_start=lap_time=0;
    sim_tick=sc_tick();race_restart();
}
static void simulation(void)
{
    if(race_menu||race_finished)return;
    if(race_countdown){race_countdown=ui_clamp(race_countdown-4,0,300);return;}
    race_drift=(sc_down(' ')||(drive_mouse&32)) && speed>40;
    race_boosting=(sc_down('n')||(drive_mouse&16)) && race_boost>=12 && race_condition>250;
    int top=72+race_condition*18/1000+(race_boosting?22:0);
    if(race_boosting)race_boost-=12;else race_boost=ui_clamp(race_boost+(race_drift?4:1),0,1000);
    if(sc_down(0x80)||sc_down('w')||(drive_mouse&2))speed=ui_clamp(speed+2,0,top);
    else speed=ui_clamp(speed-1,0,top);
    if(sc_down(0x81)||sc_down('s')||(drive_mouse&4))speed=ui_clamp(speed-5,0,top);
    int turn=(race_drift?6:4)*speed/90+1;
    if(sc_down(0x82)||sc_down('a')||(drive_mouse&1))steer-=turn;
    if(sc_down(0x83)||sc_down('d')||(drive_mouse&8))steer+=turn;
    steer-=curvature(distance)*speed/(race_drift?1700:1200);steer=ui_clamp(steer,-140,140);
    if(ui_abs(steer)>112){speed=speed*9/10;race_condition=ui_clamp(race_condition-1,180,1000);}
    race_pit=distance<2400 && steer>90 && speed<32;
    if(race_pit){race_condition=ui_clamp(race_condition+8,0,1000);race_boost=ui_clamp(race_boost+8,0,1000);}
    int before=lap*race_length()+distance;distance+=speed;race_rivals_step(before);
    if(distance>=race_length()){
        distance-=race_length();lap++;lap_time=race_elapsed-lap_start;lap_start=race_elapsed;
        if(race_legacy && (!best||lap_time<best)){
            if(sc_write("HOME/RACE/RECORD",&lap_time,4)==4)best=lap_time;
            else copy(race_message,"Lap finished / save failed",sizeof(race_message));
        }
        race_complete_lap();
    }
}
int main(void)
{
    if(ui_open("DUNE RUN / mio")<0)return 1;
    ui_pointer();ui_header("DUNE RUN","Preparing circuits and materials");ui_footer("mio / loading race");ui_present();
    sc_mkdir("HOME/RACE");
    if(sc_read("HOME/RACE/RECORD",&best,sizeof(best))!=4||best<0)best=0;
    char args[64];sc_args(args,sizeof(args));
    if(equal(args,"legacy")){race_legacy=1;race_mode=race_menu=race_setup=0;}
    race_records_load();textures();reset();
    for(;;){
        int key=sc_key();
        if(key==27){
            if(race_menu || race_legacy)return 0;
            race_choice_track=race_track;race_choice_mode=race_mode;race_choice_difficulty=race_difficulty;
            race_menu=1;drive_mouse=0;continue;
        }
        race_content_key(key);
        /* 已结算的站次已经把六名车手的积分提交给锦标赛。
         * 此时R不能只重置赛道：再次冲线会重复提交同一站积分。
         * 只有未完赛时可重开本场；结果页Replay会重建整个系列，
         * Next event才是保留现有积分并进入下一站的唯一入口。 */
        if(!race_menu && !race_finished && (key=='r'||key=='R'))reset();
        /* 队列短按提供一次最小响应，持续按住再由固定步长仿真处理。 */
        if(!race_menu&&!race_finished&&!race_countdown&&(key==0x80||key=='w'))speed=ui_clamp(speed+2,0,90);
        if(!race_menu&&!race_finished&&!race_countdown&&(key==0x81||key=='s'))speed=ui_clamp(speed-5,0,90);
        if(!race_menu&&!race_finished&&!race_countdown&&(key==0x82||key=='a'))steer=ui_clamp(steer-4,-140,140);
        if(!race_menu&&!race_finished&&!race_countdown&&(key==0x83||key=='d'))steer=ui_clamp(steer+4,-140,140);
        ui_pointer();
        if(!ui_focus&&!race_legacy&&!race_menu){
            race_choice_track=race_track;race_choice_mode=race_mode;race_choice_difficulty=race_difficulty;
            race_menu=1;drive_mouse=0;
        }
        int now=sc_tick(),steps=0;
        while(now-sim_tick>=4&&steps<8){sim_tick+=4;simulation();steps++;}
        if(now-sim_tick>32)sim_tick=now;
        if(UI_W<280||UI_H<160){
            if(!race_legacy)race_menu=1;drive_mouse=0;
            ui_header("DUNE RUN","Enlarge to drive");ui_small_control(98,16,60,112,"Enlarge",0);
            ui_present();if(ui_action==98)sc_window(ui_win,1);sc_yield();continue;
        }
        if(race_menu)race_menu_draw();else draw();
        ui_present();race_content_action(ui_action);
        if(!race_menu&&!race_finished&&ui_action==1)reset();sc_yield();
    }
}
