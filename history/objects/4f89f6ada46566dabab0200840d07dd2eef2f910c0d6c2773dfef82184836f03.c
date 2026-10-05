/* =====================================================================
 * mio：SAND WORLD，原创的小型方块沙盒，第一人称软件光线步进。
 * 世界是 24×12×24 的有限体素，不加载 MC 素材/代码。玩家可以行走、
 * 转向、跳跃、破坏/放置草/土/石/玻璃；存档写 HOME/WORLD/CHUNK.SCW。
 * 几何/材质/操作完全是三环源码，FRAME 只负责把应用最终帧交给内核。
 *
 * M8使用Q8网格DDA，每个客户区物理像素独立求交，不放大低清帧。
 * 日光遮挡、环境遮蔽、世界锚定材质、玻璃反射/透射由三环计算。
 * 最近命中与前一个空格共用相同DDA，仍保留M7的6944B存档格式。
 * 第一阶段尚未构建，画质和帧耗时留待获批后的第二阶段验收。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
#define WORLD_X 24
#define WORLD_Y 12
#define WORLD_Z 24
#define WORLD_SIZE (WORLD_X*WORLD_Y*WORLD_Z)
static u8 world[WORLD_SIZE],save_bytes[WORLD_SIZE+32];
static int px=12*256,py=6*256,pz=17*256,yaw=512,pitch=-10,material=1;
static int target=-1,previous=-1,changed,jump_velocity;
static char message[40]="WASD move, arrows look";
static int cell(int x,int y,int z)
{ if(x<0||x>=WORLD_X||y<0||y>=WORLD_Y||z<0||z>=WORLD_Z) return -1;
    return (y*WORLD_Z+z)*WORLD_X+x; }
static int block(int x,int y,int z)
{ int i=cell(x/256,y/256,z/256);
    if(x<0||y<0||z<0||i<0) return 0;
    return world[i]; }
static void generate(void)
{
    for(int y=0;y<WORLD_Y;y++) for(int z=0;z<WORLD_Z;z++) for(int x=0;x<WORLD_X;x++) {
        int h=2+((x/7+z/8)%2),kind=0;
        if(y<h) kind=y==h-1?1:2;
        if(y==0) kind=3;
        world[cell(x,y,z)]=(u8)kind;
    }
    /* 一座可拆建的低塔与几棵方块树作为地标，出生点面向塔；位置
     * 固定，方便人工操作和自动截图重复验证，不引入随机外部种子。 */
    for(int y=2;y<6;y++) for(int x=10;x<14;x++) world[cell(x,y,12)]=3;
    for(int y=2;y<5;y++) world[cell(6,y,9)]=2;
    for(int z=8;z<=10;z++) for(int x=5;x<=7;x++) world[cell(x,5,z)]=1;
    px=12*256;
    py=6*256;
    pz=17*256;
    yaw=512;
    pitch=-10;
}
static u32 checksum(u8 *p,int n)
{ u32 sum=2166136261u;
    for(int i=0;i<n;i++) sum=(sum^p[i])*16777619u;
    return sum; }
static void store32(int offset,u32 value)
{ for(int i=0;i<4;i++) { save_bytes[offset+i]=(u8)value;
        value>>=8; } }
static u32 load32(int offset)
{ return (u32)save_bytes[offset]|((u32)save_bytes[offset+1]<<8)|((u32)save_bytes[offset+2]<<16)|((u32)save_bytes[offset+3]<<24); }
static void save_world(void)
{
    const char *magic="SBOX1MIO";
    for(int i=0;i<8;i++) save_bytes[i]=(u8)magic[i];
    store32(8,1);
    for(int i=0;i<WORLD_SIZE;i++) save_bytes[16+i]=world[i];
    store32(16+WORLD_SIZE,(u32)px);
    store32(20+WORLD_SIZE,(u32)py);
    store32(24+WORLD_SIZE,(u32)pz);
    store32(28+WORLD_SIZE,(u32)yaw);
    store32(12,checksum(save_bytes+16,WORLD_SIZE+16));
    sc_mkdir("HOME/WORLD");
    if(sc_write("HOME/WORLD/CHUNK.SCW",save_bytes,sizeof(save_bytes))==(int)sizeof(save_bytes)) {
        copy(message,"World saved",sizeof(message));
        changed=0;
    } else copy(message,"Save failed",sizeof(message));
}
static int load_world(void)
{
    u32 info[2];
    if(sc_stat("HOME/WORLD/CHUNK.SCW",info)||info[1]!=sizeof(save_bytes)) return 0;
    if(sc_read("HOME/WORLD/CHUNK.SCW",save_bytes,sizeof(save_bytes))!=(int)sizeof(save_bytes)) return 0;
    const char *magic="SBOX1MIO";
    for(int i=0;i<8;i++) if(save_bytes[i]!=(u8)magic[i]) return 0;
    if(load32(8)!=1||load32(12)!=checksum(save_bytes+16,WORLD_SIZE+16)) return 0;
    int x=(int)load32(16+WORLD_SIZE),y=(int)load32(20+WORLD_SIZE),z=(int)load32(24+WORLD_SIZE);
    if(x<256||x>=23*256||z<256||z>=23*256||y<256||y>=11*256) return 0;
    for(int i=0;i<WORLD_SIZE;i++) if(save_bytes[16+i]>4) return 0;
    for(int i=0;i<WORLD_SIZE;i++) world[i]=save_bytes[16+i];
    px=x;
    py=y;
    pz=z;
    yaw=(int)load32(28+WORLD_SIZE)&1023;
    copy(message,"World restored",sizeof(message));
    changed=0;
    return 1;
}
typedef struct {int found,index,previous,kind,distance;ScnVec p,n;} WorldHit;
static u32 world_materials[4][4096];
static int world_sim_tick,world_focal,world_view_top,world_view_bottom;
static int world_mouse_move,world_look_x,world_look_y,world_dragging;
static void prepare_materials(void)
{
    for(int kind=0;kind<4;kind++)for(int y=0;y<64;y++)for(int x=0;x<64;x++){
        int fine=(int)(scn_hash(x,kind,y)&31)-15;
        int broad=scn_noise(x*4,y*4,64);
        u32 color=kind==0?SCN_GRASS:kind==1?SCN_OAK:kind==2?SCN_STONE:SCN_GLASS;
        int light=230+broad/7+fine/2;
        if(kind==0){
            /* 稀疏细叶脉改变色调和局部明度，锚定在块面UV而非屏幕。
             * 相邻像素连续采样，镜头移动不重新抽随机纹理。 */
            if(((x+y*3)&15)==0)light+=16;
            color=scn_mix(SCN_GRASS,SCN_SAND,broad/7);
        }else if(kind==1){light=210+broad/5+fine; if(((y+x/9)&15)==0)light-=12;}
        else if(kind==2){if(y%32<2||(x+(y/32)*32)%64<2)light-=38;}
        else light=246+fine/3;
        world_materials[kind][y*64+x]=scn_light(color,light);
    }
}
/* Amanatides-Woo式网格遍历：每步恰好跨一个体素边界，不再固定走
 * 128次小步。t/delta为Q16参数，方向保留像素/焦距比例，零方向轴
 * 设无穷大，不会除零；命中距离换算回世界Q8供雾/交互使用。
 * 交互的previous只记最后空格，不能把跳过的玻璃体素当可放置空气。 */
static void world_cast(ScnVec *origin,ScnVec *direction,int max_t,int skip,WorldHit *hit)
{
    hit->found=0;hit->index=hit->previous=-1;
    int ix=origin->x>>8,iy=origin->y>>8,iz=origin->z>>8;
    int sx=direction->x<0?-1:1,sy=direction->y<0?-1:1,sz=direction->z<0?-1:1;
    int length=scn_length(direction);if(!length)return;
    max_t=scn_ratio16(max_t,length);
    int tx=direction->x?scn_ratio16((ix+(sx>0))*256-origin->x,direction->x):0x3FFFFFFF;
    int ty=direction->y?scn_ratio16((iy+(sy>0))*256-origin->y,direction->y):0x3FFFFFFF;
    int tz=direction->z?scn_ratio16((iz+(sz>0))*256-origin->z,direction->z):0x3FFFFFFF;
    int dx=direction->x?16777216/ui_abs(direction->x):0x3FFFFFFF;
    int dy=direction->y?16777216/ui_abs(direction->y):0x3FFFFFFF;
    int dz=direction->z?16777216/ui_abs(direction->z):0x3FFFFFFF;
    int t=0,last=-1,nx=0,ny=0,nz=0;
    for(int step=0;step<96&&t<=max_t;step++){
        int index=cell(ix,iy,iz);if(index<0)return;
        int kind=world[index];
        if(kind&&index!=skip&&t>1){
            hit->found=1;hit->index=index;hit->previous=last;hit->kind=kind;hit->distance=t*length/65536;
            scn_set(&hit->p,origin->x+direction->x*t/65536,origin->y+direction->y*t/65536,origin->z+direction->z*t/65536);
            scn_set(&hit->n,nx,ny,nz);return;
        }
        if(!kind)last=index;
        nx=ny=nz=0;
        if(tx<=ty&&tx<=tz){t=tx;tx+=dx;ix+=sx;nx=-sx*256;}
        else if(ty<=tz){t=ty;ty+=dy;iy+=sy;ny=-sy*256;}
        else{t=tz;tz+=dz;iz+=sz;nz=-sz*256;}
    }
}
static u32 world_environment(ScnVec *origin,ScnVec *ray)
{
    if(ray->y>=0)return scn_sky(ray);
    int t=(256-origin->y)*256/ray->y;
    if(t<0)return scn_sky(ray);
    int x=origin->x+ray->x*t/256,z=origin->z+ray->z*t/256;
    ScnVec reflected;scn_set(&reflected,ray->x,-ray->y,ray->z);
    int ripple=scn_noise(x,z,128);
    u32 color=scn_mix(SCN_SEA,scn_sky(&reflected),86+ripple/3);
    return scn_mix(color,SCN_HORIZON,ui_clamp(t*scn_length(ray)/256/96,0,220));
}
static u32 world_surface(WorldHit *hit)
{
    int u=hit->n.x?hit->p.z:hit->p.x,v=hit->n.y?hit->p.z:hit->p.y;
    int uu=(u&255)>>2,vv=(v&255)>>2;
    int kind=hit->kind;
    u32 color=world_materials[kind-1][vv*64+uu];
    if(kind==1&&hit->n.y<=0){
        /* 草顶保留土壤剖面，顶部薄草皮随真实命中高度出现。 */
        color=(hit->p.y&255)>224?world_materials[0][vv*64+uu]:world_materials[1][vv*64+uu];
    }
    ScnVec sun,start;scn_set(&sun,-115,205,102);scn_unit(&sun);
    scn_set(&start,hit->p.x+hit->n.x/64,hit->p.y+hit->n.y/64,hit->p.z+hit->n.z/64);
    WorldHit shadow;world_cast(&start,&sun,8192,hit->index,&shadow);
    int diffuse=ui_clamp(scn_dot(&hit->n,&sun),0,256);
    int light=92+(shadow.found?(shadow.kind==4?diffuse/2:diffuse/8):diffuse*3/4);
    /* 接缝AO来自相邻实块，而非每块都描黑框。只在接触边附近衰减，
     * 对着天空的独立边缘仍然明亮；避免旧版均匀网格线覆盖材质。 */
    int ix=hit->index%24,iz=(hit->index/24)%24,iy=hit->index/(24*24);
    int margin=24,occlusion=0,a=u&255,b=v&255;
    if(a<margin){
        int index=hit->n.x?cell(ix,iy,iz-1):cell(ix-1,iy,iz);
        if(index>=0&&world[index])occlusion+=(margin-a)*2;
    }
    if(a>255-margin){
        int index=hit->n.x?cell(ix,iy,iz+1):cell(ix+1,iy,iz);
        if(index>=0&&world[index])occlusion+=(a-255+margin)*2;
    }
    if(b<margin){
        int index=hit->n.y?cell(ix,iy,iz-1):cell(ix,iy-1,iz);
        if(index>=0&&world[index])occlusion+=(margin-b)*2;
    }
    if(b>255-margin){
        int index=hit->n.y?cell(ix,iy,iz+1):cell(ix,iy+1,iz);
        if(index>=0&&world[index])occlusion+=(b-255+margin)*2;
    }
    color=scn_light(color,ui_clamp(light-occlusion,40,320));
    return scn_mix(color,SCN_HORIZON,ui_clamp(hit->distance/64,0,140));
}
static u32 world_trace(ScnVec *eye,ScnVec *ray)
{
    WorldHit hit;world_cast(eye,ray,10000,-1,&hit);
    if(!hit.found)return world_environment(eye,ray);
    u32 color=world_surface(&hit);
    if(hit.kind==4){
        /* 一次透射加一次反射均查询真实体素；玻璃后的方块实际可见。
         * 跳过当前玻璃格而非删世界数据，避免另一条射线或交互看到假洞。 */
        ScnVec start,reflected;int dot=scn_dot(ray,&hit.n);
        scn_set(&reflected,ray->x-2*dot*hit.n.x/256,ray->y-2*dot*hit.n.y/256,ray->z-2*dot*hit.n.z/256);
        scn_set(&start,hit.p.x+hit.n.x/64,hit.p.y+hit.n.y/64,hit.p.z+hit.n.z/64);
        WorldHit bounce;world_cast(&start,&reflected,8192,hit.index,&bounce);
        u32 reflected_color=bounce.found?world_surface(&bounce):world_environment(&start,&reflected);
        scn_set(&start,hit.p.x+ray->x/64,hit.p.y+ray->y/64,hit.p.z+ray->z/64);
        WorldHit behind;world_cast(&start,ray,8192,hit.index,&behind);
        u32 through=behind.found?world_surface(&behind):world_environment(&start,ray);
        int fresnel=ui_clamp(256-ui_abs(scn_ratio(dot,scn_length(ray))),0,256);fresnel=36+fresnel*fresnel/350;
        color=scn_mix(scn_mix(through,color,40),reflected_color,fresnel);
    }
    return color;
}
static void aim(void)
{
    ScnVec eye,ray;scn_set(&eye,px,py,pz);scn_set(&ray,scn_sin(yaw),pitch,scn_cos(yaw));
    WorldHit hit;world_cast(&eye,&ray,8192,-1,&hit);
    target=hit.found?hit.index:-1;previous=hit.found?hit.previous:-1;
}
static void draw(void)
{
    int begun=sc_tick();world_view_top=ui_px(ui_compact?26:36);
    world_view_bottom=ui_height-ui_px(ui_compact?54:66);
    if(world_view_bottom<world_view_top)world_view_bottom=world_view_top;
    int h=world_view_bottom-world_view_top,w=ui_width;
    world_focal=w*3/4;if(world_focal<1)world_focal=1;
    int fx=scn_sin(yaw),fz=scn_cos(yaw),rx=fz,rz=-fx;
    ScnVec eye;scn_set(&eye,px,py,pz);
    for(int y=0;y<h;y++){
        int vertical=(h-2*y-1)/2+pitch*world_focal/256;u32 *out=ui_pixels+(world_view_top+y)*w;
        for(int x=0;x<w;x++){
            int sideways=2*x+1-w;
            ScnVec ray;scn_set(&ray,(fx*world_focal*2+rx*sideways)/512,vertical,(fz*world_focal*2+rz*sideways)/512);
            *out++=0xFF000000u|world_trace(&eye,&ray);
        }
    }
    aim();scn_frame_ticks=sc_tick()-begun;scn_render_width=w;scn_render_height=h;
    ui_rect_rgb(0,0,UI_W,ui_compact?26:36,ui_role(SC_THEME_PAPER));
    ui_text(12,5,"SAND WORLD",PAL_UI_TEXT);
    if(UI_W>350)ui_text(UI_W-100,5,changed?"UNSAVED":"SAVED",changed?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    int center=(world_view_top+h/2)*100/ui_scale;
    ui_rect_rgb(UI_W/2-6,center,13,1,SCN_IVORY);ui_rect_rgb(UI_W/2,center-6,1,13,SCN_IVORY);
    int y=UI_H-(ui_compact?54:66);ui_rect_rgb(0,y,UI_W,UI_H-y,ui_role(SC_THEME_PAPER));
    ui_small_control(1,8,y+4,56,"Save",0);ui_small_control(2,68,y+4,56,"Load",0);
    for(int i=0;i<4;i++){
        char label[2];label[0]=(char)('1'+i);label[1]=0;
        ui_small_control(10+i,132+i*30,y+4,26,label,material==i+1);
    }
    if(UI_W>450){ui_small_control(3,262,y+4,64,"Break",0);ui_small_control(4,332,y+4,64,"Place",0);}
    ui_text(12,y+32,message,PAL_UI_MUTED);
}
/* 旧玩法的修改边界保持：底层基础不可破坏、玩家身体不能被放置块包住，
 * 所见中心射线必须真正命中，不能仅凭鼠标屏幕坐标直接写体素数组。 */
static void edit(int place)
{
    aim();int i=place?previous:target;
    if(i<0){copy(message,"No block in reach",sizeof(message));return;}
    int y=i/(WORLD_X*WORLD_Z),z=(i/WORLD_X)%WORLD_Z,x=i%WORLD_X;
    int dx=x*256+128-px,dz=z*256+128-pz;
    if(ui_abs(dx)+ui_abs(dz)>6*256){copy(message,"Block too far",sizeof(message));return;}
    if(y==0){copy(message,"Foundation is solid",sizeof(message));return;}
    if(place&&ui_abs(dx)<200&&ui_abs(dz)<200&&ui_abs(y*256+128-py)<450){
        copy(message,"Player occupies block",sizeof(message));return;
    }
    world[i]=(u8)(place?material:0);changed=1;
    copy(message,place?"Block placed":"Block removed",sizeof(message));
}
static void move_player(int x,int z)
{
    if(x<256||x>=23*256||z<256||z>=23*256)return;
    /* 四角碰撞留出身体半径，避免镜头跨过方块侧面后从内部发射光线。 */
    for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2)
        if(block(x+a*48,py,z+b*48)||block(x+a*48,py-250,z+b*48))return;
    px=x;pz=z;
}
static void simulation(void)
{
    if(sc_down(0x82))yaw=(yaw-10)&1023;
    if(sc_down(0x83))yaw=(yaw+10)&1023;
    if(sc_down(0x80))pitch=ui_clamp(pitch+8,-180,180);
    if(sc_down(0x81))pitch=ui_clamp(pitch-8,-180,180);
    int dx=scn_sin(yaw)*18/256,dz=scn_cos(yaw)*18/256;
    if(sc_down('w'))move_player(px+dx,pz+dz);
    if(sc_down('s'))move_player(px-dx,pz-dz);
    if(sc_down('a'))move_player(px-dz,pz+dx);
    if(sc_down('d'))move_player(px+dz,pz-dx);
    if(jump_velocity>0||!block(px,py-400,pz)){
        int next=py+jump_velocity;
        if(jump_velocity>0&&block(px,next+32,pz))jump_velocity=0;
        else py=next;
        jump_velocity-=3;if(py<400)py=400;
    }else jump_velocity=0;
}
int main(void)
{
    if(ui_open("SAND WORLD / mio")<0)return 1;
    generate();load_world();prepare_materials();world_sim_tick=sc_tick();aim();
    for(;;){
        for(int key=sc_key();key>=0;key=sc_key()){
            if(key==27)return 0;
            if(key>='1'&&key<='4')material=key-'0';
            if(key=='q'||key=='Q')edit(0);
            if(key=='e'||key=='E')edit(1);
            if(key==0x82)yaw=(yaw-20)&1023;
            if(key==0x83)yaw=(yaw+20)&1023;
            if(key==0x80)pitch=ui_clamp(pitch+24,-180,180);
            if(key==0x81)pitch=ui_clamp(pitch-24,-180,180);
            if(key==2)save_world();
            if(key==3&&!load_world())copy(message,"No valid save",sizeof(message));
            if(key==' '&&!jump_velocity&&block(px,py-400,pz))jump_velocity=35;
        }
        int now=sc_tick(),steps=0;
        while(now-world_sim_tick>=4&&steps<8){world_sim_tick+=4;simulation();steps++;}
        if(now-world_sim_tick>32)world_sim_tick=now;
        ui_pointer();
        if(UI_W<260||UI_H<172){
            ui_header("SAND WORLD","Enlarge to explore");ui_small_control(98,16,60,112,"Enlarge",0);
            ui_present();if(ui_action==98)sc_window(ui_win,1);sc_yield();continue;
        }
        /* 中键拖动只改变视角，左/右键仍按旧约定破坏/放置。窗口
         * 失焦或松开就结束拖动，避免后台移动鼠标让世界自行转向。 */
        if(ui_focus&&(ui_buttons&4)){
            if(world_dragging){yaw=(yaw+(ui_x-world_look_x)*2)&1023;pitch=ui_clamp(pitch-(ui_y-world_look_y)*2,-180,180);}
            world_look_x=ui_x;world_look_y=ui_y;world_dragging=1;
        }else world_dragging=0;
        draw();ui_present();
        if(ui_action==1)save_world();
        else if(ui_action==2){if(!load_world())copy(message,"No valid save",sizeof(message));}
        else if(ui_action==3)edit(0);
        else if(ui_action==4)edit(1);
        else if(ui_action>=10&&ui_action<=13)material=ui_action-9;
        else if(ui_focus&&ui_y*ui_scale/100>=world_view_top&&ui_y*ui_scale/100<world_view_bottom){
            if(ui_pressed&1)edit(0);if(ui_pressed&2)edit(1);
        }
        sc_yield();
    }
}
