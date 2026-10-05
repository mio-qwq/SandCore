/* =====================================================================
 * mio：SAND WORLD，原创的小型方块沙盒，第一人称软件光线步进。
 * 新远征/创造为64×24×64体素，16类材料、工具成长、合成、四座信标。
 * 夜影与日夜光照、建造/战斗/游泳/回营构成持续玩法；legacy保留
 * 原24×12×24世界与6944B CHUNK存档，不覆盖旧档。
 * 几何/材质/操作完全是三环源码，FRAME 只负责把应用最终帧交给内核。
 *
 * M8使用Q8网格DDA，每个客户区物理像素独立求交，不放大低清帧。
 * 日光遮挡、环境遮蔽、世界锚定材质、玻璃反射/透射由三环计算。
 * 最近命中与前一个空格共用相同DDA，仍保留M7的6944B存档格式。
 * 第一阶段尚未构建，画质和帧耗时留待获批后的第二阶段验收。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
#include "WORLDGAME.H"
#define WORLD_X 64
#define WORLD_Y 24
#define WORLD_Z 64
#define WORLD_SIZE (WORLD_X*WORLD_Y*WORLD_Z)
static u8 world[WORLD_SIZE],save_bytes[WORLD_SIZE+WORLD2_HEADER];
static int world_width=64,world_height=24,world_depth=64,world_cells=WORLD_SIZE;
static int world_mode=1;
static int px=12*256,py=6*256,pz=17*256,yaw=512,pitch=-10,material=1;
static int target=-1,previous=-1,changed,jump_velocity,world_grounded;
static int world_look_x,world_look_y,world_dragging,world_captured;
static char message[40]="WASD move, arrows look";
static int cell(int x,int y,int z)
{ if(x<0||x>=world_width||y<0||y>=world_height||z<0||z>=world_depth) return -1;
    return (y*world_depth+z)*world_width+x; }
static int block(int x,int y,int z)
{ int i=cell(x/256,y/256,z/256);
    if(x<0||y<0||z<0||i<0) return 0;
    return world[i]==16?0:world[i]; }
static void generate(void)
{
    if(world_mode){world_new_generate();return;}
    for(int y=0;y<world_height;y++) for(int z=0;z<world_depth;z++) for(int x=0;x<world_width;x++) {
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
    if(world_mode){world_new_save();return;}
    int bytes=world_cells+32;
    const char *magic="SBOX1MIO";
    for(int i=0;i<8;i++) save_bytes[i]=(u8)magic[i];
    store32(8,1);
    for(int i=0;i<world_cells;i++) save_bytes[16+i]=world[i];
    store32(16+world_cells,(u32)px);
    store32(20+world_cells,(u32)py);
    store32(24+world_cells,(u32)pz);
    store32(28+world_cells,(u32)yaw);
    store32(12,checksum(save_bytes+16,world_cells+16));
    sc_mkdir("HOME/WORLD");
    if(sc_write("HOME/WORLD/CHUNK.SCW",save_bytes,bytes)==(int)bytes) {
        copy(message,"World saved",sizeof(message));
        changed=0;
    } else copy(message,"Save failed",sizeof(message));
}
static int load_world(void)
{
    if(world_mode)return world_new_load();
    int bytes=world_cells+32;
    u32 info[2];
    if(sc_stat("HOME/WORLD/CHUNK.SCW",info)||info[1]!=bytes) return 0;
    if(sc_read("HOME/WORLD/CHUNK.SCW",save_bytes,bytes)!=(int)bytes) return 0;
    const char *magic="SBOX1MIO";
    for(int i=0;i<8;i++) if(save_bytes[i]!=(u8)magic[i]) return 0;
    if(load32(8)!=1||load32(12)!=checksum(save_bytes+16,world_cells+16)) return 0;
    int x=(int)load32(16+world_cells),y=(int)load32(20+world_cells),z=(int)load32(24+world_cells);
    if(x<256||x>=23*256||z<256||z>=23*256||y<256||y>=11*256) return 0;
    for(int i=0;i<world_cells;i++) if(save_bytes[16+i]>4) return 0;
    for(int i=0;i<world_cells;i++) world[i]=save_bytes[16+i];
    px=x;
    py=y;
    pz=z;
    yaw=(int)load32(28+world_cells)&1023;
    copy(message,"World restored",sizeof(message));
    changed=0;
    return 1;
}
#include "WGOLD.inc"

typedef struct {int found,index,previous,kind,distance;ScnVec p,n;} WorldHit;
static u32 world_materials[16][4096];
static int world_sim_tick,world_focal,world_view_top,world_view_bottom;
static ScnVec world_sun={-115,205,102};
/* mio：体素发光查询的保守空间索引。旧着色器在每个命中像素重复
 * 查看同一块周围125个单元；绝大多数块附近没有火把、营火或激活
 * 的信标，这些查询的结果必然全空。这里为每个体素只保存一个
 * “附近可能有光源”标志，绝不缓存某个像素的遮挡/亮度。
 *
 * 索引的有效代数沿用玩法的world_lighting_generation：采集、放置、
 * 信标激活、新世界和成功读档都会递增；日夜阶段也递增，虽会多
 * 重建一次但不会漏失效。尺寸同样参加验证，不能把旧小世界的标志
 * 用到新世界。一次重建只扫描世界，再把真实光源周围5×5×5标记，
 * 内存固定WORLD_SIZE字节，属于本三环任务，没有内核/存档新字段。
 *
 * 标志为零时严格等价于原125查询均不可能进入发光分支；标志为一
 * 时仍执行原距离衰减和每个像素的world_line_clear。因此阴影、
 * AO、昼夜光、玻璃、反射、分辨率和光源数量都保持原计算。 */
static u8 world_light_near[WORLD_SIZE];
static int world_light_generation=-1,world_light_width,world_light_height,world_light_depth;
static int world_emitter(int index)
{
    int kind=world[index];
    if(kind==12||kind==15)return 1;
    if(kind!=13)return 0;
    for(int site=0;site<4;site++)
        if((world_sites&(1<<site))&&index==cell(site_x[site],site_y[site]+1,site_z[site]))return 1;
    return 0;
}
static void world_light_index(void)
{
    if(world_light_generation==world_lighting_generation&&world_light_width==world_width
        &&world_light_height==world_height&&world_light_depth==world_depth)return;
    for(int i=0;i<world_cells;i++)world_light_near[i]=0;
    for(int i=0;i<world_cells;i++)if(world_emitter(i)){
        int x=i%world_width,z=(i/world_width)%world_depth,y=i/(world_width*world_depth);
        for(int dy=-2;dy<=2;dy++)for(int dz=-2;dz<=2;dz++)for(int dx=-2;dx<=2;dx++){
            int nearby=cell(x+dx,y+dy,z+dz);
            if(nearby>=0)world_light_near[nearby]=1;
        }
    }
    world_light_width=world_width;world_light_height=world_height;world_light_depth=world_depth;
    // 最后发布代数；三环在完整重建后才允许下一条射线使用新索引。
    world_light_generation=world_lighting_generation;
}

static void prepare_materials(void)
{
    for(int kind=0;kind<16;kind++)for(int y=0;y<64;y++)for(int x=0;x<64;x++){
        int fine=(int)(scn_hash(x,kind,y)&31)-15;
        int broad=scn_noise(x*4,y*4,64);
        u32 color=kind==0?SCN_GRASS:kind==1?SCN_OAK:kind==2?SCN_STONE:kind==3?SCN_GLASS:
            kind==4?SCN_OAK:kind==5?SCN_GRASS:kind==6?SCN_SAND:kind==7?SCN_ASPHALT:
            kind==8?SCN_METAL:kind==9?SCN_GLASS:kind==10?SCN_OAK:kind==11?SCN_SUN:
            kind==12?SCN_CERAMIC:kind==13?SCN_PAINT:kind==14?SCN_PAINT:SCN_SEA;
        int light=230+broad/7+fine/2;
        if(kind==0){
            /* 稀疏细叶脉改变色调和局部明度，锚定在块面UV而非屏幕。
             * 相邻像素连续采样，镜头移动不重新抽随机纹理。 */
            if(((x+y*3)&15)==0)light+=16;
            color=scn_mix(SCN_GRASS,SCN_SAND,broad/7);
        }else if(kind==1){light=210+broad/5+fine; if(((y+x/9)&15)==0)light-=12;}
        else if(kind==2){if(y%32<2||(x+(y/32)*32)%64<2)light-=38;}
        else if(kind==4||kind==10){light=224+broad/5+fine/2;if(((x+y/9)&15)<2)light-=18;}
        else if(kind==7||kind==8||kind==9){light=196+fine*2;if(((x/7+y/9)&3)==0)light+=42;}
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
    int width=world_width,height=world_height,depth=world_depth;
    int vertical_stride=width*depth,index=cell(ix,iy,iz);
    if(index<0)return;
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
    for(int step=0;step<192&&t<=max_t;step++){
        /* mio：DDA每次只跨一个轴，线性体素地址的变化恰为
         * ±1、±width或±width*depth。原循环每步调用cell，重新
         * 读三项全局尺寸、乘两次并检查六个边界；百万条主/阴影
         * 射线会反复执行同样的坐标到地址换算。查询入口保存尺寸
         * 和起始index，跨轴时同源增量维护。每次读world之前仍
         * 检查三个坐标，不能只检查线性index以免X越界串到下一行。
         * 将负坐标转u32后必大于合法尺寸，一次比较覆盖两端。
         * 尺寸仅在主循环生成/成功读档切换，一次同步查询内不变；
         * 不持有跨编辑缓存，也不改变跨轴同距的X/Y/Z优先顺序。 */
        if((u32)ix>=(u32)width||(u32)iy>=(u32)height||(u32)iz>=(u32)depth)return;
        int kind=world[index];
        if(kind&&index!=skip&&t>1){
            hit->found=1;hit->index=index;hit->previous=last;hit->kind=kind;hit->distance=scn_displace(length,t);
            scn_set(&hit->p,origin->x+scn_displace(direction->x,t),origin->y+scn_displace(direction->y,t),origin->z+scn_displace(direction->z,t));
            scn_set(&hit->n,nx,ny,nz);return;
        }
        if(!kind)last=index;
        nx=ny=nz=0;
        if(tx<=ty&&tx<=tz){t=tx;tx+=dx;ix+=sx;index+=sx;nx=-sx*256;}
        else if(ty<=tz){t=ty;ty+=dy;iy+=sy;index+=sy*vertical_stride;ny=-sy*256;}
        else{t=tz;tz+=dz;iz+=sz;index+=sz*width;nz=-sz*256;}
    }
}
#include "world_entities.inc"
static u32 world_environment(ScnVec *origin,ScnVec *ray)
{
    if(ray->y>=0){
        u32 color=scn_sky(ray);
        if(world_mode)color=scn_mix(SCN_SHADE,color,world_daylight);
        return color;
    }
    int t=scn_ratio16(256-origin->y,ray->y);
    if(t<0)return scn_sky(ray);
    int x=origin->x+scn_displace(ray->x,t),z=origin->z+scn_displace(ray->z,t);
    ScnVec reflected;scn_set(&reflected,ray->x,-ray->y,ray->z);
    int ripple=scn_noise(x,z,128);
    u32 color=scn_mix(SCN_SEA,scn_sky(&reflected),86+ripple/3);
    return scn_mix(color,SCN_HORIZON,ui_clamp(scn_displace(scn_length(ray),t)/96,0,220));
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
    ScnVec sun,start;scn_set(&sun,world_sun.x,world_sun.y,world_sun.z);
    scn_set(&start,hit->p.x+hit->n.x/64,hit->p.y+hit->n.y/64,hit->p.z+hit->n.z/64);
    int diffuse=ui_clamp(scn_dot(&hit->n,&sun),0,256);
    int light=92;
    /* mio：背向太阳/与光垂直的表面diffuse恰为0。原三种结果
     * diffuse/2、diffuse/8、diffuse*3/4均严格等于0，查遍天空
     * 体素也不会改变一个颜色分量。只在这个可证明的零贡献
     * 条件跳过太阳遮挡；附近火把/信标、AO、昼夜、玻璃反射/
     * 透射继续完整计算。没有把所有阴影删除，也没有缓存按
     * 像素变化的亮度。轴对齐体素背面很常见，逐像素省去的
     * 是一次DDA初始化和完整向外遍历，远比空查找更值得消除。 */
    if(diffuse){
        WorldHit shadow;world_cast(&start,&sun,8192,hit->index,&shadow);
        light+=shadow.found?(shadow.kind==4?diffuse/2:diffuse/8):diffuse*3/4;
    }
    /* 接缝AO来自相邻实块，而非每块都描黑框。只在接触边附近衰减，
     * 对着天空的独立边缘仍然明亮；避免旧版均匀网格线覆盖材质。 */
    int ix=hit->index%world_width,iz=(hit->index/world_width)%world_depth,iy=hit->index/(world_width*world_depth);
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
    if(world_mode){
        light=28+(light-28)*world_daylight/256;
        /* 发光材料必须照到附近真实表面；最多扫描当前块周围
         * 5x5x5，不遍历全世界。照明强度取距离衰减最大值，避免
         * 许多火把相加溢出或把夜景整体洗白。 */
        world_light_index();
        /* 附近光源的最大glow是240；已达到该值时原比较glow>light
         * 永远为假。保留索引失效检查，只省去125次不可能贡献的
         * 查询；夜景/阴影中的较暗表面仍完整计算每个真实光源。 */
        if(light<240&&world_light_near[hit->index])for(int dy=-2;dy<=2;dy++)for(int dz=-2;dz<=2;dz++)for(int dx=-2;dx<=2;dx++){
            int n=cell(ix+dx,iy+dy,iz+dz);
            int lit=0;
            if(n>=0 && world[n]==13)for(int site=0;site<4;site++)
                if((world_sites&(1<<site)) && n==cell(site_x[site],site_y[site]+1,site_z[site]))lit=1;
            if(n>=0&&(world[n]==12||world[n]==15||lit)){
                int glow=240-(ui_abs(dx)+ui_abs(dy)+ui_abs(dz))*28;if(glow>light && world_line_clear(start.x,start.y,start.z,
                    (ix+dx)*256+128,(iy+dy)*256+128,(iz+dz)*256+128))light=glow;
            }
        }
        if(kind==12||kind==15)light=300;
        if(kind==13){
            int active=0;for(int site=0;site<4;site++)if((world_sites&(1<<site))&&hit->index==cell(site_x[site],site_y[site]+1,site_z[site]))active=1;
            color=scn_mix(color,active?SCN_GLASS:SCN_SUN,100);
        }
    }
    color=scn_light(color,ui_clamp(light-occlusion,24,320));
    return scn_mix(color,SCN_HORIZON,ui_clamp(hit->distance/64,0,140));
}
static u32 world_trace(ScnVec *eye,ScnVec *ray)
{
    WorldHit hit;world_cast(eye,ray,10000,-1,&hit);
    if(!hit.found)return world_environment(eye,ray);
    u32 color=world_surface(&hit);
    if(hit.kind==4 || hit.kind==16){
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
    int begun=sc_tick();
    if(world_mode){
        int arc=(world_elapsed%60000)*1024/60000+160;
        scn_set(&world_sun,-scn_cos(arc)*210/256,ui_clamp(scn_sin(arc),32,256),102);scn_unit(&world_sun);
        scn_set(&scn_active->sun,world_sun.x,world_sun.y,world_sun.z);
        scn_active->sky_top=scn_mix(SCN_SHADE,SCN_SKY_TOP,world_daylight);
        scn_active->horizon=scn_mix(SCN_SHADE,SCN_HORIZON,world_daylight);
    }
    world_view_top=ui_px(ui_compact?26:36);
    /* 矮窗口仍需留出可见世界。现代完整HUD占98布局单位，
     * 不足240高时使用74单位紧凑栏；射线视口和HUD共用此规则，
     * 不能仅把按钮移上去却让整帧世界仍被状态面板盖住。 */
    world_view_bottom=ui_height-ui_px(world_mode?(UI_H<240?74:98):ui_compact?54:66);
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
    world_enemies_draw();aim();scn_frame_ticks=sc_tick()-begun;scn_render_width=w;scn_render_height=h;
    ui_rect_rgb(0,0,UI_W,ui_compact?26:36,ui_role(SC_THEME_PAPER));
    ui_text(12,5,"SAND WORLD",PAL_UI_TEXT);
    if(world_mode && UI_H<240){
        ui_text(UI_W-104,5,"HP",PAL_UI_TEXT);ui_number(UI_W-80,5,world_health,PAL_UI_TEXT);
        ui_text(UI_W-48,5,world_sun_phase>=7&&world_sun_phase<=10?"NIGHT":"DAY",PAL_UI_MUTED);
    }else if(UI_W>350)ui_text(UI_W-100,5,changed?"UNSAVED":"SAVED",changed?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    int center=(world_view_top+h/2)*100/ui_scale;
    ui_rect_rgb(UI_W/2-6,center,13,1,SCN_IVORY);ui_rect_rgb(UI_W/2,center-6,1,13,SCN_IVORY);
    if(world_mode){world_overlay();return;}
    int y=UI_H-(ui_compact?54:66);ui_rect_rgb(0,y,UI_W,UI_H-y,ui_role(SC_THEME_PAPER));
    ui_small_control(1,8,y+4,56,"Save",0);ui_small_control(2,68,y+4,56,"Load",0);
    for(int i=0;i<4;i++){
        char label[2];label[0]=(char)('1'+i);label[1]=0;
        ui_small_control(10+i,132+i*30,y+4,26,label,material==i+1);
    }
    if(UI_W>450){ui_small_control(3,262,y+4,64,"Break",0);ui_small_control(4,332,y+4,64,"Place",0);}
    ui_text(12,y+32,message,PAL_UI_MUTED);
    world_overlay();
}
/* 旧玩法的修改边界保持：底层基础不可破坏、玩家身体不能被放置块包住，
 * 所见中心射线必须真正命中，不能仅凭鼠标屏幕坐标直接写体素数组。 */
static void edit(int place)
{
    if(!place && world_enemy_attack())return;
    aim();int i=place?previous:target;
    if(i<0){copy(message,"No block in reach",sizeof(message));return;}

    int y=i/(world_width*world_depth),z=(i/world_width)%world_depth,x=i%world_width;
    int dx=x*256+128-px,dz=z*256+128-pz;
    if(ui_abs(dx)+ui_abs(dz)+ui_abs(y*256+128-py)>6*256){copy(message,"Block too far",sizeof(message));return;}
    if(place && target>=0 && world_activate(target))return;
    if(y==0){copy(message,"Foundation is solid",sizeof(message));return;}
    if(place&&ui_abs(dx)<200&&ui_abs(dz)<200&&ui_abs(y*256+128-py)<450){
        copy(message,"Player occupies block",sizeof(message));return;
    }
    if(world_mode && !(place?world_place(i):world_harvest(i)))return;
    world[i]=(u8)(place?material:0);changed=1;world_lighting_generation++;
    copy(message,place?"Block placed":"Block removed",sizeof(message));
}
/* 玩家使用眼高400、半径48的轴向盒；水平分轴与竖直小步碰撞
 * 共用同一检查，不能在冲刺/下落较快时跨过一整块薄墙或地板。 */
static void move_player(int x,int z)
{
    if(world_body_clear(x,py,z)){px=x;pz=z;changed=1;return;}
    if(world_grounded && world_body_clear(x,py+128,z)){
        px=x;pz=z;py+=128;changed=1;
    }
}
static void simulation(void)
{
    if(world_panel)return;
    world_progress_tick();
    if(sc_down(0x82))yaw=(yaw-10)&1023;if(sc_down(0x83))yaw=(yaw+10)&1023;
    if(sc_down(0x80))pitch=ui_clamp(pitch+8,-180,180);if(sc_down(0x81))pitch=ui_clamp(pitch-8,-180,180);
    int running=world_mode && sc_down('v') && world_stamina>10;
    int pace=running?32:18;
    if(running)world_stamina-=6;else world_stamina=ui_clamp(world_stamina+3,0,1000);
    int dx=scn_sin(yaw)*pace/256,dz=scn_cos(yaw)*pace/256,mx=0,mz=0;
    if(sc_down('w')){mx+=dx;mz+=dz;}if(sc_down('s')){mx-=dx;mz-=dz;}
    if(sc_down('a')){mx-=dz;mz+=dx;}if(sc_down('d')){mx+=dz;mz-=dx;}
    if(mx)move_player(px+mx,pz);if(mz)move_player(px,pz+mz);
    int n=cell(px>>8,py>>8,pz>>8),wet=n>=0&&world[n]==16;
    if(world_mode && wet){
        world_breath=ui_clamp(world_breath-2,0,1000);
        if(sc_down(' '))jump_velocity=14;
        else jump_velocity=ui_clamp(jump_velocity-1,-8,14);
        if(!world_breath&&world_elapsed-world_hurt_tick>=150){
            world_hurt_tick=world_last_damage=world_elapsed;world_health--;changed=1;
            if(world_health<=0){world_deaths++;world_health=20;world_spawn();world_breath=1000;}
        }
    }else{world_breath=ui_clamp(world_breath+12,0,1000);jump_velocity=ui_clamp(jump_velocity-3,-48,35);}
    int remaining=jump_velocity;world_grounded=0;
    while(remaining){
        int step=ui_clamp(remaining,-8,8);
        if(!world_body_clear(px,py+step,pz)){
            if(step<0)world_grounded=1;jump_velocity=0;break;
        }
        py+=step;remaining-=step;changed=1;
    }
}
#include "world_panels.inc"
int main(void)
{
    if(ui_open("SAND WORLD / mio")<0)return 1;
    /* 首先提交已设计的准备页，生成地形和纹理期间也有明确反馈。
     * 这里只改善应用首次帧；WM分配到应用首次提交的黑帧仍列入
     * 第二阶段测量，不能把此处一句提示当作整个开窗问题已解决。 */
    ui_pointer();ui_header("SAND WORLD","Preparing your expedition");ui_footer("mio / loading terrain and materials");ui_present();
    char args[128];sc_args(args,sizeof(args));
    if(equal(args,"legacy")){world_mode=world_panel=0;world_width=24;world_height=12;world_depth=24;world_cells=6912;}
    else if(equal(args,"creative"))world_mode=2;
    generate();load_world();prepare_materials();world_sim_tick=sc_tick();aim();
    for(;;){
        for(int key=sc_key();key>=0;key=sc_key()){
            ui_followup=1;
            if(key==27){
                sc_mouse_capture(ui_win,0);world_captured=0;
                if(world_mode){world_panel_open(4);continue;}
                return 0;
            }
            world_game_key(key);if(world_panel)continue;
            if(world_mode==0 && key>='1'&&key<='4')material=key-'0';
            if(key=='q'||key=='Q')edit(0);if(key=='e'||key=='E')edit(1);
            if(key==0x82)yaw=(yaw-20)&1023;if(key==0x83)yaw=(yaw+20)&1023;
            if(key==0x80)pitch=ui_clamp(pitch+24,-180,180);if(key==0x81)pitch=ui_clamp(pitch-24,-180,180);
            if(key==2)save_world();if(key==3&&!load_world())copy(message,"No valid save",sizeof(message));
            if(key==' '&&world_grounded)jump_velocity=35;
        }
        /* 菜单/最小窗口不需要持续整帧提交。冻结真实仿真基点，
         * 等待输入/布局/主题/状态刷新；关闭菜单不追算暂停时间。
         * 键已经由上面的队列消费，因此用followup保留其唤醒事实。 */
        if(world_panel||UI_W<260||UI_H<160){
            world_sim_tick=sc_tick();
            if(!ui_frame_due())continue;
        }
        ui_pointer();int relative[8];
        /* legacy的中键拖视角仍走原绝对客户坐标，不启动捕获。
         * 新远征使用原始相对位移，旧世界则保留用户已经熟悉的
         * 中键操作；开始拖动先建基点，防止从上次拖动末点跳转。
         * 松键、失焦或移出世界视口立即丢弃基点。 */
        if(!world_mode && ui_focus && (ui_buttons&4)
            && ui_px(ui_y)>=world_view_top && ui_px(ui_y)<world_view_bottom){
            if(world_dragging){
                yaw=(yaw+(ui_x-world_look_x)*2)&1023;
                pitch=ui_clamp(pitch+(world_look_y-ui_y)*2,-180,180);
            }
            world_look_x=ui_x;world_look_y=ui_y;world_dragging=1;
        }else world_dragging=0;
        if(world_mode&&!ui_focus&&!world_panel)world_panel_open(4);
        if(world_panel || !ui_focus){sc_mouse_capture(ui_win,0);world_captured=0;}
        if(sc_mouse_relative(ui_win,relative))relative[6]=0;
        if(!world_panel && world_captured && !relative[6]){world_panel_open(4);world_captured=0;}
        if(!world_panel && world_mode && ui_focus && !world_captured){
            if(!sc_mouse_capture(ui_win,1)){
                world_captured=1;sc_mouse_relative(ui_win,relative);
                /* 捕获开始先消耗触发菜单Resume的点击，不能关闭菜单
                 * 同一沿又挖掉脚下地板。下一轮才允许相对按钮编辑。 */
                relative[4]=0;
            }
        }
        if(world_captured && relative[6]){
            yaw=(yaw+relative[1]*2)&1023;pitch=ui_clamp(pitch-relative[2]*2,-180,180);
            if(relative[4]&1)edit(0);if(relative[4]&2)edit(1);
            ui_pressed=ui_buttons=0;
        }
        int now=sc_tick(),steps=0;
        while(now-world_sim_tick>=4&&steps<8){world_sim_tick+=4;simulation();steps++;}
        if(now-world_sim_tick>32)world_sim_tick=now;
        if(UI_W<260||UI_H<160){
            sc_mouse_capture(ui_win,0);world_captured=0;if(world_mode)world_panel_open(4);
            ui_header("SAND WORLD","Enlarge to explore");ui_small_control(98,16,60,112,"Enlarge",0);
            ui_present();if(ui_action==98)sc_window(ui_win,1);sc_yield();continue;
        }
        int had_panel=world_panel;
        if(world_panel){ui_background(SC_THEME_FACE_ALT);world_overlay();}
        else draw();
        ui_present();world_game_action(ui_action);
        if(world_exit){sc_mouse_capture(ui_win,0);return 0;}
        if(had_panel || world_panel || world_captured || ui_action>=40){sc_yield();continue;}
        if(ui_action==1)save_world();else if(ui_action==2){if(!load_world())copy(message,"No valid save",sizeof(message));}
        else if(ui_action==3)edit(0);else if(ui_action==4)edit(1);
        else if(ui_action>=10&&ui_action<=13)material=ui_action-9;
        else if(ui_focus&&ui_y*ui_scale/100>=world_view_top&&ui_y*ui_scale/100<world_view_bottom){
            if(ui_pressed&1)edit(0);if(ui_pressed&2)edit(1);
        }
        sc_yield();
    }
}
