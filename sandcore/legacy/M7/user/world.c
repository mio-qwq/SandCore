/* =====================================================================
 * mio：SAND WORLD，原创的小型方块沙盒，第一人称软件光线步进。
 * 世界是 24×12×24 的有限体素，不加载 MC 素材/代码。玩家可以行走、
 * 转向、跳跃、破坏/放置草/土/石/玻璃；存档写 HOME/WORLD/CHUNK.SCW。
 * 几何/材质/操作完全是三环源码，FRAME 只负责把应用最终帧交给内核。
 *
 * 128 格定点步进不需要浮点或 libc；每个小像素射一条光线，再放大
 * 到三倍。最近命中及其前一个空格同样用于交互，所见方块就是所改
 * 方块。有限距离/有限世界是明示边界，不能把小演示称为无限世界。
 * ===================================================================== */
#include "SCAPI.H"
#include "UI.inc"
#define WORLD_X 24
#define WORLD_Y 12
#define WORLD_Z 24
#define WORLD_SIZE (WORLD_X*WORLD_Y*WORLD_Z)
static u8 world[WORLD_SIZE],save_bytes[WORLD_SIZE+32];
static int px=12*256,py=6*256,pz=17*256,yaw=512,pitch=-10,material=1;
static int target=-1,previous=-1,changed,mouse_old,jump_velocity;
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
static int ray(int dx,int dy,int dz,int select)
{
    int x=px,y=py,z=pz,last=-1,oldx=x/256,oldy=y/256;
    for(int step=0;step<128;step++) {
        x+=dx/8;
        y+=dy/8;
        z+=dz/8;
        int ix=x/256,iy=y/256,iz=z/256,index=cell(ix,iy,iz);
        if(x<0||y<0||z<0||index<0) break;
        int kind=world[index];
        if(kind) {
            if(select) { target=index;
                previous=last; }
            int light=iy!=oldy?6:ix!=oldx?3:4;
            light=ui_clamp(light-step/25,0,7);
            int base=kind==1?PAL_GRASS:kind==2?PAL_EARTH:kind==3?PAL_ROCK:PAL_WATER;
            /* 每块边缘的纹理与面明度一起表现体积，玻璃使用冷色条纹。
             * 不读取宿主图像；点阵纹理由局部坐标确定，可自行修改。 */
            if(((x&255)<14)||((y&255)<14)||((z&255)<14)) light=ui_clamp(light-1,0,7);
            if(kind==4 && ((x+y+z)&127)<30) light=ui_clamp(light+2,0,7);
            return base+light;
        }
        last=index;
        oldx=ix;
        oldy=iy;
    }
    if(select) { target=previous=-1; }
    return PAL_UI_NIGHT+ui_clamp(4-dy/90,0,7);
}
static void draw(void)
{
    int forward_x=ui_sin(yaw),forward_z=ui_cos(yaw),right_x=ui_cos(yaw),right_z=-ui_sin(yaw);
    for(int y=0;y<43;y++) for(int x=0;x<106;x++) {
        int sideways=(x-53)*9/2,vertical=(21-y)*8+pitch;
        int dx=forward_x+right_x*sideways/256,dz=forward_z+right_z*sideways/256;
        int color=ray(dx,vertical,dz,0);
        ui_rect(x*3,25+y*3,3,3,color);
    }
    ray(forward_x,pitch,forward_z,1);
    ui_rect(0,0,UI_W,24,PAL_UI_INK);
    ui_text(8,8,"SAND WORLD",PAL_UI_TEXT);
    ui_text(184,8,changed?"UNSAVED":"SAVED",changed?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    ui_line(154,89,164,89,PAL_UI_TEXT);
    ui_line(159,84,159,94,PAL_UI_TEXT);
    ui_panel(8,125,175,22);
    ui_text(14,132,message,PAL_UI_MUTED);
    ui_panel(245,125,65,22);
    ui_number(251,132,material,PAL_UI_GOLD+7);
    ui_text(267,132,"BLOCK",PAL_UI_TEXT);
    ui_footer("Q break E place  F2 save  1-4 blocks");
}
static void edit(int place)
{
    int i=place?previous:target;
    if(i<0) { copy(message,"No block in reach",sizeof(message));
        return; }
    int y=i/(WORLD_X*WORLD_Z),z=(i/WORLD_X)%WORLD_Z,x=i%WORLD_X;
    int dx=x*256+128-px,dz=z*256+128-pz;
    if(ui_abs(dx)+ui_abs(dz)>6*256) { copy(message,"Block too far",sizeof(message));
        return; }
    if(y==0) { copy(message,"Foundation is solid",sizeof(message));
        return; }
    if(place && ui_abs(dx)<200 && ui_abs(dz)<200 && ui_abs(y*256+128-py)<450) {
        copy(message,"Player occupies block",sizeof(message));
        return;
    }
    world[i]=(u8)(place?material:0);
    changed=1;
    copy(message,place?"Block placed":"Block removed",sizeof(message));
}
static void move_player(int x,int z)
{
    if(x<256||x>=23*256||z<256||z>=23*256) return;
    if(block(x,py,z)||block(x,py-250,z)) return;
    px=x;
    pz=z;
}
int main(void)
{
    if(ui_open("SAND WORLD / mio")<0) return 1;
    generate();
    load_world();
    for(;;) {
        for(int key=sc_key();key>=0;key=sc_key()) {if(key==27) return 0;
        if(key>='1'&&key<='4') material=key-'0';
        if(key=='q'||key=='Q') edit(0);
        if(key=='e'||key=='E') edit(1);
        /* 原生 SCCC 的射线帧比宿主优化版慢；短按可能在整帧计算
         * 期间已经松开。队列里的方向事件仍必须至少转一次，不能
         * 只查询下一帧的 held 后把用户的短按吞掉。连续按住仍由
         * 下方 KEYDOWN 追加平滑转向，二者保证轻触与长按都有效。 */
        if(key==0x82) yaw=(yaw-20)&1023;
        if(key==0x83) yaw=(yaw+20)&1023;
        if(key==0x80) pitch=ui_clamp(pitch+24,-180,180);
        if(key==0x81) pitch=ui_clamp(pitch-24,-180,180);
        if(key==2) save_world();
        if(key==3) { if(!load_world()) copy(message,"No valid save",sizeof(message)); }
        if(key==' '&&!jump_velocity) jump_velocity=35;
        }
        if(!ui_frame_due()) continue;
        if(sc_down(0x82)) yaw=(yaw-10)&1023;
        if(sc_down(0x83)) yaw=(yaw+10)&1023;
        if(sc_down(0x80)) pitch=ui_clamp(pitch+8,-180,180);
        if(sc_down(0x81)) pitch=ui_clamp(pitch-8,-180,180);
        int dx=ui_sin(yaw)*18/256,dz=ui_cos(yaw)*18/256;
        if(sc_down('w')) move_player(px+dx,pz+dz);
        if(sc_down('s')) move_player(px-dx,pz-dz);
        if(sc_down('a')) move_player(px-dz,pz+dx);
        if(sc_down('d')) move_player(px+dz,pz-dx);
        if(jump_velocity>0 || !block(px,py-400,pz)) {
            py+=jump_velocity;
            jump_velocity-=3;
            if(py<400) py=400;
        } else jump_velocity=0;
        int mx,my,buttons=ui_mouse(&mx,&my);
        if((buttons&1)&&!(mouse_old&1)) edit(0);
        if((buttons&2)&&!(mouse_old&2)) edit(1);
        mouse_old=buttons;
        draw();
        ui_present();
    }
}
