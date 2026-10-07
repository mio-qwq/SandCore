/* =====================================================================
 * raider.c —— SandRaider：DOOM 式光栅枪战
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 玩法
 *   - 3 关 24×24 地图；DDA 光线投射 + 程序化纹理/精灵（零图片资源）
 *   - 敌人三种：近战/枪手/重装；视线检测、追击、攻击、受击硬直、
 *     死亡留尸（尸体不再阻挡/受击）
 *   - 武器三种：手枪 / 霰弹（5 弹丸散射）/ 机枪；1/2/3 切换
 *   - 道具：医疗包/护甲/弹药/霰弹/霰弹枪/机枪/红蓝钥匙/宝藏
 *   - 红蓝钥匙锁门；E 开门（近身格）；出口开关 E 过关
 *   - 过关结算（击杀/物品/宝藏/分数），通关终局 + 历史最佳
 *   - 鼠标捕获转视角（sc_mouse_capture/relative），Esc 自动释放并
 *     暂停；方向键转视角是无捕获回退
 *   - 受击红闪、拾取提示、程序化音效
 *
 * 工程约束
 *   - float 光线投射：目标机完整 32 位 x86，内核保存/恢复浮点
 *     上下文（M9 已实机验证）；freestanding 无 libm——sin/cos 用
 *     泰勒级数建 1024 项表，开方用牛顿迭代，double/long long 与
 *     libgcc 辅助符号全程不出现。
 *   - 3D 画面先入私有缓冲（RAIDER.CFG render=2 时宽度减半），
 *     整数倍 blit 进 NUI 的 ui_pixels；HUD 用 NUI 原语。1024+ 大窗
 *     不烧满 QEMU 单核。
 *   - 仿真固定 100Hz tick 累加，渲染随事件；长帧不加速穿墙。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"
#include "exaudio.h"

#define RD_MW 24
#define RD_MH 24
#define RD_LEVELS 3
#define RD_ENEMIES 24
#define RD_ITEMS 32
#define RD_TS 32
#define RD_SP 16
#define RD_TEXN 8
#define RD_HUD 34          /* 物理像素 HUD 高度 */
#define RD_SCORE_FILE "HOME/RAIDER.SCORE"

/* 图例：#石 B砖 W科技 r红门 b蓝门 d门 E出口 . 空
 * P 出生 g近战 s枪手 h重装
 * M医疗 A弹药 S霰弹 R护甲 1红钥匙 2蓝钥匙 T宝藏 G霰弹枪 C机枪 */
static const char *rd_maps[RD_LEVELS][RD_MH]={
 {
  "########################",
  "#......#........#......#",
  "#.g....#...M....#....g.#",
  "#......d........#......#",
  "#......#...A....########",
  "####B###........#......#",
  "#......#...g....#..T...#",
  "#.M....B........d......#",
  "#......#...S....#......#",
  "#..1...#........########",
  "#......####B#####......#",
  "#.g....#......#........#",
  "#......#..h...#...M....#",
  "####d###......#........#",
  "#......#..A...#####d####",
  "#..T...#......#.....#..#",
  "#......B......#..g..#A.#",
  "#......#......d.....#..#",
  "########......#.....dE1#",
  "#......#..M...#.....#..#",
  "#.A....d......#.....####",
  "#......#......B....g#..#",
  "#..P...#......#.....#2.#",
  "########################"
 },
 {
  "########################",
  "#..........#...........#",
  "#.P...M....#....h......#",
  "#..........d...........#",
  "#....g.....#.....g.....#",
  "#..........#...........#",
  "#####BB#####.....S.....#",
  "#...#..#...########d####",
  "#.A.#..#..............T#",
  "#...d..#...g.....g.....#",
  "#...#..#...........M...#",
  "##BB########W###########",
  "#..........#...........#",
  "#..R....2..#..1...h....#",
  "#....g.....d.....g.....#",
  "#..........#...........#",
  "##########d#.....G.....#",
  "#......#...#...........#",
  "#..M...#...#######d#####",
  "#......#...#.....#.....#",
  "#..h...#.C.#..T..#..M..#",
  "#......#...#.....d.....#",
  "#..E2..#...#.....#..A..#",
  "########################"
 },
 {
  "########################",
  "#....B.....#...........#",
  "#.P........d....h......#",
  "#....g.....#......h....#",
  "#..........#...........#",
  "#...M......#....M......#",
  "########d###...........#",
  "#...A..#...#...g...g...#",
  "#......#...#...........#",
  "#..g...#...#####BB######",
  "#......#...#...........#",
  "#......d...#..1.....T..#",
  "#......#...#...........#",
  "########...#...g...g...#",
  "#......#...#...........#",
  "#..T...#...########d####",
  "#......#..........2....#",
  "#..h...#...g.....g.....#",
  "#......#...........M...#",
  "####d###...............#",
  "#..R...#..C....S....R..#",
  "#..1...#...............#",
  "#..E...#...h.....h.....#",
  "########################"
 }
};

/* ---------------- 纹理与精灵（程序化） ---------------- */
static u32 rd_tex[RD_TEXN][RD_TS*RD_TS];
static u32 rd_spr[13][RD_SP*RD_SP];
static u32 rd_zbuf[1920];
static u32 rd_noise(u32 seed)
{
    u32 x=seed*2654435761u+0x9E3779B9u;
    x^=x>>13;x^=x<<17;x^=x>>5;
    return x&31;
}
static u32 rd_shade(u32 base,int n)
{
    /* 每通道同步加噪，保持色相；n 0..31 */
    int r=(base>>16&255)+n,g=(base>>8&255)+n,b=(base&255)+n;
    if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
    return 0xFF000000u|((u32)r<<16)|((u32)g<<8)|(u32)b;
}
static void rd_gen_textures(void)
{
    for(int i=0;i<RD_TS*RD_TS;i++){
        int x=i%RD_TS,y=i/RD_TS;
        u32 n=rd_noise((u32)(i*7+1));
        /* 1 石墙：灰石 + 砖缝 */
        u32 base=0xFF565C64;
        if((y&7)==0||((x&15)==0&&(y&7)!=0))base=0xFF444A50;
        rd_tex[1][i]=rd_shade(base,(int)n-8);
        /* 2 红砖：错缝 */
        int off=(y/8&1)?8:0;
        base=0xFF8A3A28;
        if((y&7)==0||(((x+off)&15)==0))base=0xFFB8A898;
        rd_tex[2][i]=rd_shade(base,(int)n-10);
        /* 3 科技蓝：面板+灯带 */
        base=0xFF28384E;
        if((x&7)==0||(y&7)==0)base=0xFF1C2836;
        if((x&7)==4&&(y&7)==4)base=0xFF486C98;
        if(y==14||y==17)base=0xFF38B8D8;
        rd_tex[3][i]=rd_shade(base,(int)n-12);
        /* 4/5/6 门：门板 + 锁条 + 把手 */
        base=0xFF6A5136;
        if(x==0||x==RD_TS-1||y==0||y==RD_TS-1)base=0xFF3E3020;
        if(y==8&&x>4&&x<27)base=0xFFC04040;
        if(y==20&&x>10&&x<22)base=0xFFC8C0B0;
        rd_tex[4][i]=rd_shade(base,(int)n-8);
        base=0xFF365C78;
        if(x==0||x==RD_TS-1||y==0||y==RD_TS-1)base=0xFF223648;
        if(y==8&&x>4&&x<27)base=0xFF4060C8;
        if(y==20&&x>10&&x<22)base=0xFFC8C0B0;
        rd_tex[5][i]=rd_shade(base,(int)n-8);
        base=0xFF6A5136;
        if(x==0||x==RD_TS-1||y==0||y==RD_TS-1)base=0xFF3E3020;
        if(y==8&&x>4&&x<27)base=0xFF98B048;
        if(y==20&&x>10&&x<22)base=0xFFC8C0B0;
        rd_tex[6][i]=rd_shade(base,(int)n-8);
        /* 7 出口开关：金属 + 绿灯 */
        base=0xFF3C4238;
        if((x&7)==0||(y&7)==0)base=0xFF2C322A;
        if(x>8&&x<23&&y>10&&y<21)base=(y&1)?0xFF40D048:0xFF28A038;
        rd_tex[7][i]=rd_shade(base,(int)n-10);
    }
}
static void rd_gen_sprites(void)
{
    for(int i=0;i<RD_SP*RD_SP;i++){
        int x=i%RD_SP,y=i/RD_SP;
        int cx=x-8,cy=y-8;
        int r2=cx*cx+cy*cy;
        u32 c=0;
        /* 0 近战：棕圆身红眼 */
        if(r2<49){c=0xFF6E4A2E;if(r2<20)c=0xFF7E5636;
            if(cy<-3&&cy>-8&&cx>-4&&cx<4)c=0xFFD03030;}
        rd_spr[0][i]=c;
        /* 1 枪手：暗红袍黄眼 */
        c=0;
        if(r2<49){c=0xFF7E2828;if(r2<20)c=0xFF963030;
            if(cy<-3&&cy>-8&&cx>-4&&cx<4)c=0xFFE0C030;}
        rd_spr[1][i]=c;
        /* 2 重装：绿甲蓝眼宽身 */
        c=0;
        if(cx*cx*2+cy*cy<60){c=0xFF2E5E2E;
            if(cy<-3&&cy>-8&&cx>-4&&cx<4)c=0xFF3030D0;}
        rd_spr[2][i]=c;
        /* 3 尸体：贴地深色 */
        c=0;
        if(cx*cx+cy*cy*4<49)c=0xFF3A2C22;
        rd_spr[3][i]=c;
        /* 4 医疗包 / 5 弹药 / 6 霰弹 / 7 护甲 */
        c=0;
        if(x>3&&x<12&&y>7&&y<14){c=0xFFE8E8E8;
            if((x>6&&x<9&&y>8&&y<13)||(y>9&&y<12&&x>5&&x<10))c=0xFFD02020;}
        rd_spr[4][i]=c;
        c=0;
        if(x>3&&x<12&&y>8&&y<14){c=0xFFC8A028;if(y==10)c=0xFF8A6E18;}
        rd_spr[5][i]=c;
        c=0;
        if(x>3&&x<12&&y>8&&y<14){c=0xFFB03028;if(y==10)c=0xFF782018;}
        rd_spr[6][i]=c;
        c=0;
        if(x>3&&x<12&&y>4&&y<14){c=0xFF2E8E3E;
            if(x>5&&x<10&&y>6&&y<12)c=0xFF3AB84E;}
        rd_spr[7][i]=c;
        /* 8 红钥匙 / 9 蓝钥匙 / 10 宝藏 / 11 霰弹枪 / 12 机枪 */
        c=0;
        if(y>5&&y<12&&x>6&&x<9)c=0xFFD04040;
        if(y==10&&x>9&&x<12)c=0xFFD04040;
        rd_spr[8][i]=c;
        c=0;
        if(y>5&&y<12&&x>6&&x<9)c=0xFF4058D0;
        if(y==10&&x>9&&x<12)c=0xFF4058D0;
        rd_spr[9][i]=c;
        c=0;
        if(x>4&&x<11&&y>6&&y<13){c=0xFFD8B028;
            if(y==7||x==5||x==10)c=0xFFB08818;}
        rd_spr[10][i]=c;
        c=0;
        if(y>8&&y<12&&x>2&&x<13){c=0xFF565656;if(x<5)c=0xFF6A4A2E;}
        rd_spr[11][i]=c;
        c=0;
        if(y>7&&y<12&&x>2&&x<13){c=0xFF484850;
            if(x<4)c=0xFF565650;
            if(x>8&&x<11&&y>9&&y<12)c=0xFF303030;}
        rd_spr[12][i]=c;
    }
}

/* ---------------- 三角函数（泰勒建表，运行期纯查表） ---------------- */
#define RD_TAB 1024
static float rd_sintab[RD_TAB];
static void rd_trig_init(void)
{
    /* 建 0..2π 全表：前 1/4 用泰勒，其余按对称复制。
     * 泰勒 9 项在 [0,pi/2] 误差 < 1e-6，查表阶段零浮点计算。 */
    const float PI=3.14159265f;
    for(int i=0;i<=RD_TAB/4;i++){
        float x=(float)i*(PI/2)/(RD_TAB/4);
        float x2=x*x;
        float s=x*(1.0f-x2/6.0f*(1.0f-x2/20.0f*(1.0f-x2/42.0f*
                 (1.0f-x2/72.0f))));
        rd_sintab[i]=s;
    }
    for(int i=RD_TAB/4+1;i<RD_TAB;i++){
        float pi2=2.0f*PI;
        if(i<RD_TAB/2)rd_sintab[i]=rd_sintab[RD_TAB/2-i];
        else if(i<3*RD_TAB/4)rd_sintab[i]=-rd_sintab[i-RD_TAB/2];
        else rd_sintab[i]=-rd_sintab[pi2==0?0:RD_TAB-i];
    }
}
static float rd_sin(float a)
{
    /* 角度 -> [0,2π) 索引；负角先归一 */
    const float PI2=6.28318530f;
    while(a<0)a+=PI2;
    while(a>=PI2)a-=PI2;
    int idx=(int)(a/(PI2)*RD_TAB);
    return rd_sintab[idx&1023];
}
static float rd_cos(float a){return rd_sin(a+1.5707963f);}

/* ---------------- 状态 ---------------- */
typedef struct {
    float x,y;
    int type;          /* 0 近战 1 枪手 2 重装 */
    int hp,alive,hurt,cd,seeing,think;
} rd_enemy;
typedef struct {float x,y;int type,taken;} rd_item;

static rd_enemy rd_en[RD_ENEMIES];
static int rd_en_count;
static rd_item rd_it[RD_ITEMS];
static int rd_it_count;
static u8 rd_grid[RD_MH][RD_MW];
static int rd_level,rd_kills,rd_kill_total,rd_items_got,rd_treasure;

static float rd_px,rd_py,rd_ang=0.0f;
static int rd_hp=100,rd_armor=0,rd_bullets=48,rd_shells=0;
static int rd_weapon,rd_has_shotgun,rd_has_chain;
static int rd_key_red,rd_key_blue;
static int rd_score,rd_cd,rd_flash,rd_fire_vis,rd_msg_ticks;
static char rd_msg[48];
static int rd_difficulty=2;
static float rd_sens=0.0035f;
static int rd_render_shift=1;      /* 0 全分辨率 / 1 减半 */
static int rd_sound=1;

static u32 *rd_buf;
static int rd_rw,rd_rh,rd_scale=2;
static int rd_view_w,rd_view_h;   /* 物理视图区（HUD 之上） */
static int rd_state;    /* 0 菜单 1 游玩 2 过关 3 死亡 4 终局 5 暂停 */
static int rd_mouse_captured;
static int rd_best;
static int rd_menu_diff=2;

static float rd_fabsf2(float v){return v<0?-v:v;}
static float rd_floorf(float v){return v<0?(float)(int)v-1.0f:(float)(int)v;}
/* 牛顿迭代开方：6 次迭代达到 float 半精度；v<=0 返回 0 由调用方判错 */
static float rd_sqrtf2(float v)
{
    if(v<=0)return 0;
    float x=v>=1?v:v/4;
    for(int i=0;i<6&&x>0;i++)x=(x+v/x)*0.5f;
    return v>=1?x:x*2;
}
/* ---------------- 地图与碰撞 ---------------- */
static int rd_tile(int mx,int my)
{
    if(mx<0||mx>=RD_MW||my<0||my>=RD_MH)return 1;
    return rd_grid[my][mx];
}
static void rd_move(float *x,float *y,float dx,float dy,float r)
{
    /* 分轴尝试：撞墙时另一轴仍可滑动，贴墙行走不卡 */
    float nx=*x+dx;
    int hit=0;
    for(int my=(int)(*y-r);my<=(int)(*y+r)&&!hit;my++)
        for(int mx=(int)(nx-r);mx<=(int)(nx+r)&&!hit;mx++)
            if(rd_tile(mx,my))hit=1;
    if(!hit)*x=nx;
    float ny=*y+dy;
    hit=0;
    for(int my=(int)(ny-r);my<=(int)(ny+r)&&!hit;my++)
        for(int mx=(int)(*x-r);mx<=(int)(*x+r)&&!hit;mx++)
            if(rd_tile(mx,my))hit=1;
    if(!hit)*y=ny;
}
static int rd_los(float x0,float y0,float x1,float y1)
{
    float dx=x1-x0,dy=y1-y0;
    float dist=rd_sqrtf2(dx*dx+dy*dy);
    if(dist<0.001f)return 1;
    int steps=(int)(dist*4);
    for(int i=1;i<steps;i++){
        float t=(float)i/steps;
        if(rd_tile((int)(x0+dx*t),(int)(y0+dy*t)))return 0;
    }
    return 1;
}

/* ---------------- 关卡 ---------------- */
static void rd_load_level(int idx)
{
    rd_level=idx;
    rd_en_count=rd_it_count=0;
    rd_kills=rd_kill_total=rd_items_got=rd_treasure=0;
    for(int y=0;y<RD_MH;y++)for(int x=0;x<RD_MW;x++){
        char c=rd_maps[idx][y][x];
        u8 t=0;
        switch(c){
        case '#':t=1;break;case 'B':t=2;break;case 'W':t=3;break;
        case 'r':t=4;break;case 'b':t=5;break;case 'd':t=6;break;
        case 'E':t=7;break;default:t=0;
        }
        rd_grid[y][x]=t;
        float fx=x+0.5f,fy=y+0.5f;
        if(c=='P'){rd_px=fx;rd_py=fy;rd_ang=0;}
        else if(c=='g'||c=='s'||c=='h'){
            if(rd_en_count<RD_ENEMIES){
                rd_enemy *e=&rd_en[rd_en_count++];
                e->x=fx;e->y=fy;
                e->type=c=='g'?0:c=='s'?1:2;
                e->hp=e->type==0?20:e->type==1?30:60;
                e->alive=1;e->hurt=0;e->cd=0;e->seeing=0;e->think=0;
            }
        }else if(c=='M'||c=='A'||c=='S'||c=='R'||c=='1'||c=='2'||
                 c=='T'||c=='G'||c=='C'){
            if(rd_it_count<RD_ITEMS){
                rd_item *it=&rd_it[rd_it_count++];
                it->x=fx;it->y=fy;it->taken=0;
                it->type=c=='M'?4:c=='A'?5:c=='S'?6:c=='R'?7:
                         c=='1'?8:c=='2'?9:c=='T'?10:c=='G'?11:12;
            }
        }
    }
    rd_kill_total=rd_en_count;
}
static void rd_msg_set(const char *s)
{copy(rd_msg,s,sizeof(rd_msg));rd_msg_ticks=90;}

/* ---------------- 音效包装 ---------------- */
static void rd_sfx(int freq,int ms,int wave,int vol)
{if(rd_sound)exaudio_tone(freq,ms,wave,vol);}

/* ---------------- 战斗 ---------------- */
static void rd_damage_player(int dmg)
{
    int to_armor=dmg*2/3;
    if(rd_armor>0){
        if(to_armor>rd_armor)to_armor=rd_armor;
        rd_armor-=to_armor;
        dmg-=to_armor;
    }
    rd_hp-=dmg;
    rd_flash=8;
    rd_sfx(140,90,EXWAVE_NOISE,220);
    if(rd_hp<=0){
        rd_hp=0;
        rd_state=3;
        rd_mouse_captured=0;
        sc_mouse_capture(ui_win,0);
        rd_sfx(80,400,EXWAVE_SQUARE,240);
    }
}
static void rd_hurt_enemy(rd_enemy *e,int dmg)
{
    e->hp-=dmg;
    e->hurt=25;
    if(e->hp<=0){
        e->alive=0;
        rd_kills++;
        rd_score+=e->type==2?100:50;
        rd_sfx(100,160,EXWAVE_NOISE,200);
    }else rd_sfx(220,50,EXWAVE_SQUARE,120);
}
/* 墙面距离：命中判定用（全向 DDA） */
static float rd_ray_dist(float ang)
{
    float rdx=rd_cos(ang),rdy=rd_sin(ang);
    int mx=(int)rd_px,my=(int)rd_py;
    float ddx=rd_fabsf2(rdx)<1e-6f?1e6f:rd_fabsf2(1.0f/rdx);
    float ddy=rd_fabsf2(rdy)<1e-6f?1e6f:rd_fabsf2(1.0f/rdy);
    float sdx,sdy;
    int stx,sty;
    if(rdx<0){stx=-1;sdx=(rd_px-mx)*ddx;}
    else{stx=1;sdx=(mx+1.0f-rd_px)*ddx;}
    if(rdy<0){sty=-1;sdy=(rd_py-my)*ddy;}
    else{sty=1;sdy=(my+1.0f-rd_py)*ddy;}
    for(int i=0;i<64;i++){
        float dist;
        if(sdx<sdy){dist=sdx;sdx+=ddx;mx+=stx;}
        else{dist=sdy;sdy+=ddy;my+=sty;}
        if(rd_tile(mx,my))return dist<0.01f?0.01f:dist;
    }
    return 64.0f;
}
/* 单条命中射线：先撞墙距离，再找更近的敌人 */
static void rd_hitscan(float ang,int dmg)
{
    float wall=rd_ray_dist(ang);
    float dx=rd_cos(ang),dy=rd_sin(ang);
    rd_enemy *best=0;
    float best_t=wall;
    for(int i=0;i<rd_en_count;i++){
        rd_enemy *e=&rd_en[i];
        if(!e->alive)continue;
        float tox=e->x-rd_px,toy=e->y-rd_py;
        float t=tox*dx+toy*dy;              /* 前向投影 */
        if(t<=0.1f||t>=best_t)continue;
        float perp=rd_fabsf2(tox*dy-toy*dx);/* 叉积=垂距 */
        if(perp<0.35f){best_t=t;best=e;}
    }
    if(best)rd_hurt_enemy(best,dmg);
}
static void rd_fire(void)
{
    if(rd_cd>0)return;
    if(rd_weapon==0){
        if(rd_bullets<=0){rd_msg_set("Out of bullets");return;}
        rd_bullets--;
        rd_cd=30;
        rd_hitscan(rd_ang,12+rd_difficulty*2);
        rd_sfx(880,60,EXWAVE_NOISE,230);
    }else if(rd_weapon==1){
        if(rd_shells<=0){rd_msg_set("Out of shells");return;}
        rd_shells--;
        rd_cd=55;
        for(int i=-2;i<=2;i++)              /* 5 弹丸水平散射 */
            rd_hitscan(rd_ang+i*0.035f,8+rd_difficulty);
        rd_sfx(240,140,EXWAVE_NOISE,240);
    }else{
        if(rd_bullets<=0){rd_msg_set("Out of bullets");return;}
        rd_bullets--;
        rd_cd=12;
        rd_hitscan(rd_ang+(float)ex_rand_range(-8,8)*0.004f,
                   10+rd_difficulty*2);
        rd_sfx(760,40,EXWAVE_NOISE,200);
    }
    rd_fire_vis=4;
}
static void rd_switch_weapon(int w)
{
    if(w==1&&!rd_has_shotgun){rd_msg_set("No shotgun yet");return;}
    if(w==2&&!rd_has_chain){rd_msg_set("No chaingun yet");return;}
    rd_weapon=w;
    ui_followup=1;
}

/* ---------------- 交互 ---------------- */
static void rd_use(void)
{
    /* 面前一格：门/出口 */
    int mx=(int)(rd_px+rd_cos(rd_ang)*0.9f);
    int my=(int)(rd_py+rd_sin(rd_ang)*0.9f);
    int t=rd_tile(mx,my);
    if(t==6){
        rd_grid[my][mx]=0;
        rd_msg_set("Door opened");
        rd_sfx(320,120,EXWAVE_TRIANGLE,180);
    }else if(t==4){
        if(rd_key_red){rd_grid[my][mx]=0;rd_msg_set("Red door opened");
            rd_sfx(320,120,EXWAVE_TRIANGLE,180);}
        else rd_msg_set("Need the RED key");
    }else if(t==5){
        if(rd_key_blue){rd_grid[my][mx]=0;rd_msg_set("Blue door opened");
            rd_sfx(320,120,EXWAVE_TRIANGLE,180);}
        else rd_msg_set("Need the BLUE key");
    }else if(t==7){
        rd_state=2;
        rd_score+=100*rd_level;
        rd_mouse_captured=0;
        sc_mouse_capture(ui_win,0);
        rd_sfx(660,150,EXWAVE_SQUARE,220);
        ui_followup=1;
    }
}
static void rd_pickups(void)
{
    for(int i=0;i<rd_it_count;i++){
        rd_item *it=&rd_it[i];
        if(it->taken)continue;
        float dx=it->x-rd_px,dy=it->y-rd_py;
        if(dx*dx+dy*dy>0.36f)continue;      /* 0.6 格半径 */
        int msg=1;
        switch(it->type){
        case 4:
            if(rd_hp>=100){msg=0;break;}
            rd_hp=ex_min(100,rd_hp+25);
            rd_msg_set("Medkit +25");
            break;
        case 5:rd_bullets=ex_min(200,rd_bullets+8);rd_msg_set("Bullets +8");break;
        case 6:rd_shells=ex_min(50,rd_shells+4);rd_msg_set("Shells +4");break;
        case 7:rd_armor=ex_min(100,rd_armor+50);rd_msg_set("Armor +50");break;
        case 8:rd_key_red=1;rd_msg_set("Got the RED key");break;
        case 9:rd_key_blue=1;rd_msg_set("Got the BLUE key");break;
        case 10:rd_score+=500;rd_treasure++;rd_msg_set("Treasure +500");break;
        case 11:
            if(!rd_has_shotgun){rd_has_shotgun=1;rd_weapon=1;
                rd_msg_set("Shotgun!");}
            else{rd_shells=ex_min(50,rd_shells+4);rd_msg_set("Shells +4");}
            break;
        case 12:
            if(!rd_has_chain){rd_has_chain=1;rd_weapon=2;
                rd_msg_set("Chaingun!");}
            else{rd_bullets=ex_min(200,rd_bullets+8);rd_msg_set("Bullets +8");}
            break;
        default:msg=0;
        }
        if(!msg)continue;
        it->taken=1;
        rd_items_got++;
        rd_score+=10;
        rd_sfx(520,80,EXWAVE_TRIANGLE,180);
    }
}

/* ---------------- 敌人 AI ---------------- */
static void rd_enemies_tick(void)
{
    for(int i=0;i<rd_en_count;i++){
        rd_enemy *e=&rd_en[i];
        if(!e->alive)continue;
        if(e->hurt){e->hurt--;continue;}    /* 硬直：不思考不移动 */
        if(e->cd>0)e->cd--;
        float dx=rd_px-e->x,dy=rd_py-e->y;
        float dist=rd_sqrtf2(dx*dx+dy*dy);
        /* 视线每 20 tick 查一次：100Hz 下对每个敌人做射线太浪费 */
        if(e->think-->0&&e->seeing)/*沿用上次结论*/;
        else{e->think=20;e->seeing=dist<14&&rd_los(e->x,e->y,rd_px,rd_py);}
        if(!e->seeing)continue;
        float speed=(e->type==0?0.010f:e->type==1?0.012f:0.008f)
                    *(0.8f+0.2f*rd_difficulty);
        if(dist>1.2f){
            rd_move(&e->x,&e->y,dx/dist*speed,dy/dist*speed,0.3f);
        }
        /* 攻击 */
        int range=e->type==0?90:800;        /* 近战 0.9 格 / 枪手 8 格 */
        int cd_max=e->type==0?45:e->type==1?70:110;
        if(e->cd<=0&&dist*100<range&&rd_los(e->x,e->y,rd_px,rd_py)){
            e->cd=cd_max+(3-rd_difficulty)*8;
            if(e->type==0){
                rd_damage_player(6+rd_difficulty*3);
            }else{
                /* 射击命中率随难度与距离 */
                int hit=ex_rand_range(1,100)<(40+rd_difficulty*15
                                              -(int)dist*3);
                rd_sfx(300,70,EXWAVE_NOISE,150);
                if(hit)rd_damage_player(e->type==1?4+rd_difficulty*2
                                        :8+rd_difficulty*3);
            }
        }
    }
}

typedef struct {float x,y;const u32 *pix;float dist;} rd_vis_entry;
/* ---------------- 渲染 ---------------- */
static void rd_render_world(void)
{
    int w=rd_rw,h=rd_rh;
    float dirx=rd_cos(rd_ang),diry=rd_sin(rd_ang);
    float planex=-diry*0.66f,planey=dirx*0.66f;
    /* 天花板/地面：按行距离渐变，一次成带（比逐像素便宜一个量级） */
    for(int y=0;y<h;y++){
        int shade;
        u32 color;
        if(y<h/2){
            int t=y*100/(h/2);
            shade=8+t/3;
            color=0xFF000000u|((u32)shade<<16)|((u32)(shade+4)<<8)
                 |(u32)(shade+12);
        }else{
            int t=(y-h/2)*100/(h-h/2);
            shade=88-t/4;
            color=0xFF000000u|((u32)(shade/2)<<16)|((u32)(shade/2)<<8)
                 |(u32)shade;
        }
        u32 *row=rd_buf+(u32)y*w;
        for(int x=0;x<w;x++)row[x]=color;
    }
    /* 墙：逐列 DDA，标准 Lodev 算法 */
    for(int x=0;x<w;x++){
        float cam=2.0f*x/w-1.0f;
        float rdx=dirx+planex*cam;
        float rdy=diry+planey*cam;
        int mx=(int)rd_px,my=(int)rd_py;
        float ddx=rd_fabsf2(rdx)<1e-6f?1e6f:rd_fabsf2(1.0f/rdx);
        float ddy=rd_fabsf2(rdy)<1e-6f?1e6f:rd_fabsf2(1.0f/rdy);
        float sdx,sdy;
        int stx,sty;
        if(rdx<0){stx=-1;sdx=(rd_px-mx)*ddx;}
        else{stx=1;sdx=(mx+1.0f-rd_px)*ddx;}
        if(rdy<0){sty=-1;sdy=(rd_py-my)*ddy;}
        else{sty=1;sdy=(my+1.0f-rd_py)*ddy;}
        int side=0,tile=1;
        for(int i=0;i<64;i++){
            if(sdx<sdy){sdx+=ddx;mx+=stx;side=0;}
            else{sdy+=ddy;my+=sty;side=1;}
            tile=rd_tile(mx,my);
            if(tile)break;
        }
        float dist=side==0?sdx-ddx:sdy-ddy;
        if(dist<0.01f)dist=0.01f;
        rd_zbuf[x]=(u32)(dist*1000.0f);
        int lh=(int)((float)h/dist);
        int y0=h/2-lh/2,y1=h/2+lh/2;
        int cy0=y0<0?0:y0,cy1=y1>h?h:y1;
        float wallx=side==0?rd_py+dist*rdy:rd_px+dist*rdx;
        wallx-=rd_floorf(wallx);
        int tx=(int)(wallx*(float)RD_TS);
        if((side==0&&rdx>0)||(side==1&&rdy<0))tx=RD_TS-1-tx;
        if(tx<0)tx=0;
        if(tx>=RD_TS)tx=RD_TS-1;
        const u32 *tex=rd_tex[tile&7];
        float tstep=(float)RD_TS/(float)lh;
        float tpos=(float)(cy0-y0)*tstep;
        int shade=side?190:255;
        for(int y=cy0;y<cy1;y++){
            int ty=(int)tpos;
            if(ty>=RD_TS)ty=RD_TS-1;
            tpos+=tstep;
            u32 c=tex[ty*RD_TS+tx];
            u32 r=(c>>16&255)*shade/255,g=(c>>8&255)*shade/255,
                b=(c&255)*shade/255;
            rd_buf[(u32)y*w+x]=0xFF000000u|(r<<16)|(g<<8)|b;
        }
    }
    /* 精灵：按距离降序（远的先画）。命名类型：两个匿名结构体
     * 布局相同也是不同类型，跨类型初始化非法。 */
    rd_vis_entry vis[RD_ENEMIES+RD_ITEMS];
    int vn=0;
    for(int i=0;i<rd_en_count;i++){
        rd_enemy *e=&rd_en[i];
        /* 活着画本体；死后（hp<=0）永远画尸体，不再阻挡/受击 */
        if(!(e->alive||e->hp<=0))continue;
        float dx=e->x-rd_px,dy=e->y-rd_py;
        vis[vn].x=e->x;vis[vn].y=e->y;
        vis[vn].dist=dx*dx+dy*dy;
        vis[vn].pix=rd_spr[e->alive?e->type:3];
        vn++;
    }
    for(int i=0;i<rd_it_count;i++){
        rd_item *it=&rd_it[i];
        if(it->taken)continue;
        float dx=it->x-rd_px,dy=it->y-rd_py;
        vis[vn].x=it->x;vis[vn].y=it->y;
        vis[vn].dist=dx*dx+dy*dy;
        vis[vn].pix=rd_spr[it->type];
        vn++;
    }
    for(int i=1;i<vn;i++){                   /* 插入排序，稳定零分配 */
        rd_vis_entry k=vis[i];
        int j=i-1;
        while(j>=0&&vis[j].dist<k.dist){vis[j+1]=vis[j];j--;}
        vis[j+1]=k;
    }
    float invDet=1.0f/(planex*diry-dirx*planey);
    for(int i=0;i<vn;i++){
        float sx=vis[i].x-rd_px,sy=vis[i].y-rd_py;
        float tx=invDet*(diry*sx-dirx*sy);
        float ty=invDet*(-planey*sx+planex*sy);
        if(ty<0.15f)continue;
        int screenx=(int)((w/2)*(1.0f+tx/ty));
        int sh=(int)((float)h/ty);
        int sx0=screenx-sh/2,sy0=h/2-sh/2;
        for(int col=0;col<sh;col++){
            int px=sx0+col;
            if(px<0||px>=w)continue;
            if((u32)(ty*1000.0f)>=rd_zbuf[px])continue;
            int texcol=col*RD_SP/sh;
            for(int row=0;row<sh;row++){
                int py=sy0+row;
                if(py<0||py>=h)continue;
                u32 c=vis[i].pix[(row*RD_SP/sh)*RD_SP+texcol];
                if(!(c&0xFF000000u))continue;
                rd_buf[(u32)py*w+px]=c;
            }
        }
    }
    /* 受击红闪：整视图叠加半透明红（只在受击的 8 tick 内付成本） */
    if(rd_flash>0){
        int alpha=rd_flash*12;
        if(alpha>80)alpha=80;
        u32 words=(u32)w*h;
        for(u32 i2=0;i2<words;i2++){
            u32 c=rd_buf[i2];
            u32 r=(c>>16&255)*(100-alpha)/100+255*alpha/100;
            rd_buf[i2]=(c&0xFF00FFFFu)|((r&255)<<16);
        }
    }
}

/* blit 私有缓冲 -> NUI 画布（整数倍放大） */
static void rd_blit(void)
{
    int vw=rd_view_w,vh=rd_view_h;
    for(int y=0;y<rd_rh&&y*rd_scale<vh;y++){
        u32 *src=rd_buf+(u32)y*rd_rw;
        for(int dy=0;dy<rd_scale;dy++){
            u32 *out=ui_pixels+(u32)(y*rd_scale+dy)*ui_width;
            for(int x=0;x<rd_rw&&x*rd_scale<vw;x++){
                u32 c=src[x];
                for(int dx=0;dx<rd_scale;dx++)
                    out[x*rd_scale+dx]=c;
            }
        }
    }
}

/* 武器手势：HUD 上以主题色块画简形，随 fire_vis 抖动 */
static void rd_draw_weapon(void)
{
    if(rd_state!=1&&rd_state!=5)return;
    int cx=ui_width/2;
    int base=ui_height-RD_HUD;
    int kick=rd_fire_vis>0?4:0;
    if(rd_weapon==0){
        ui_rect_rgb(cx-6,base-46+kick,12,26,0xFF3A3A3Eu);
        ui_rect_rgb(cx-4,base-22+kick,8,10,0xFF2A2A2Eu);
    }else if(rd_weapon==1){
        ui_rect_rgb(cx-16,base-40+kick,32,10,0xFF4A4038u);
        ui_rect_rgb(cx-8,base-30+kick,10,14,0xFF6A4A2Eu);
    }else{
        ui_rect_rgb(cx-18,base-42+kick,36,12,0xFF484850u);
        ui_rect_rgb(cx-10,base-30+kick,8,12,0xFF303034u);
    }
    if(rd_fire_vis>0)
        ui_rect_rgb(cx-4,base-52+kick,8,8,0xFFFFD860u);
}

static void rd_draw_hud(void)
{
    int h=RD_HUD;
    ui_rect_rgb(0,ui_height-h,ui_width,h,ui_role(SC_THEME_FACE));
    ui_rect_rgb(0,ui_height-h,ui_width,1,ui_role(SC_THEME_LINE));
    char buf[32];
    int y=ui_height-h+8;
    /* 健康与护甲：色随状态 */
    copy(buf,"HP ",sizeof(buf));
    char n[8];ex_dec(n,sizeof(n),rd_hp);
    append(buf,n,sizeof(buf));
    ui_text(12,y,buf,rd_hp>50?PAL_UI_TEXT:rd_hp>25?PAL_UI_GOLD+7:PAL_UI_ALERT);
    copy(buf,"AM ",sizeof(buf));
    ex_dec(n,sizeof(n),rd_armor);
    append(buf,n,sizeof(buf));
    ui_text(92,y,buf,PAL_UI_TEXT);
    copy(buf,"AMMO ",sizeof(buf));
    ex_dec(n,sizeof(n),rd_weapon==1?rd_shells:rd_bullets);
    append(buf,n,sizeof(buf));
    ui_text(160,y,buf,PAL_UI_TEXT);
    const char *wn=rd_weapon==0?"PISTOL":rd_weapon==1?"SHOTGUN":"CHAIN";
    ui_text(236,y,wn,PAL_UI_CYAN+7);
    /* 钥匙指示 */
    ui_rect_rgb(306,y-1,10,10,rd_key_red?0xFFD04040u:0xFF282828u);
    ui_rect_rgb(320,y-1,10,10,rd_key_blue?0xFF4058D0u:0xFF282828u);
    /* 分数与关卡（右侧） */
    copy(buf,"SCORE ",sizeof(buf));
    ex_udec(n,sizeof(n),(u32)rd_score);
    append(buf,n,sizeof(buf));
    ui_text(360,y,buf,PAL_UI_TEXT);
    copy(buf,"L",sizeof(buf));
    ex_dec(n,sizeof(n),rd_level+1);
    append(buf,n,sizeof(buf));
    ui_text(480,y,buf,PAL_UI_MUTED);
    /* 提示行 */
    if(rd_msg_ticks>0)
        ui_text(12,ui_height-h+22,rd_msg,PAL_UI_GOLD+7);
}


/* ---------------- 页面绘制（菜单/过关/死亡/终局/暂停） ---------------- */
static void rd_fill_page(void)
{ui_rect_rgb(0,0,UI_W,UI_H,ui_role(SC_THEME_PAPER));}

static void rd_center_text(int y,const char *s,int slot)
{ui_text(ui_width/2-ex_utf8_width(s)/2,y,s,slot);}

static void rd_draw_menu(void)
{
    rd_fill_page();
    rd_center_text(56,"S A N D   R A I D E R",PAL_UI_TEXT);
    rd_center_text(84,"retro raycast shooter / SandCore ext",PAL_UI_MUTED);
    char line[64];
    copy(line,"Difficulty: ",sizeof(line));
    append(line,rd_menu_diff==1?"1 easy":rd_menu_diff==2?"2 normal"
           :"3 nightmare",sizeof(line));
    rd_center_text(140,line,PAL_UI_CYAN+7);
    rd_center_text(166,"1 / 2 / 3 select difficulty",PAL_UI_MUTED);
    rd_center_text(190,"Enter start  (mouse capture on play)",PAL_UI_MUTED);
    rd_center_text(214,"W A S D move / arrows turn / Space fire / E use",PAL_UI_MUTED);
    copy(line,"Best score: ",sizeof(line));
    char n[12];ex_udec(n,sizeof(n),(u32)rd_best);
    append(line,n,sizeof(line));
    rd_center_text(246,line,PAL_UI_GOLD+7);
}
static void rd_draw_intermission(void)
{
    rd_fill_page();
    char line[64],n[8];
    copy(line,"Level ",sizeof(line));
    ex_dec(n,sizeof(n),rd_level+1);
    append(line,n,sizeof(line));
    append(line," complete",sizeof(line));
    rd_center_text(96,line,PAL_UI_TEXT);
    copy(line,"Kills ",sizeof(line));
    ex_dec(n,sizeof(n),rd_kills);
    append(line,n,sizeof(line));
    append(line," / ",sizeof(line));
    ex_dec(n,sizeof(n),rd_kill_total);
    append(line,n,sizeof(line));
    rd_center_text(128,line,PAL_UI_TEXT);
    copy(line,"Items ",sizeof(line));
    ex_dec(n,sizeof(n),rd_items_got);
    append(line,n,sizeof(line));
    append(line,"   Treasure ",sizeof(line));
    ex_dec(n,sizeof(n),rd_treasure);
    append(line,n,sizeof(line));
    rd_center_text(150,line,PAL_UI_TEXT);
    copy(line,"Score ",sizeof(line));
    char s[12];ex_udec(s,sizeof(s),(u32)rd_score);
    append(line,s,sizeof(line));
    rd_center_text(180,line,PAL_UI_GOLD+7);
    rd_center_text(216,"Enter next level",PAL_UI_MUTED);
}
static void rd_draw_death(void)
{
    rd_fill_page();
    rd_center_text(116,"Y O U   D I E D",PAL_UI_ALERT);
    rd_center_text(150,"R restart level / Esc quit",PAL_UI_MUTED);
}
static void rd_draw_win(void)
{
    rd_fill_page();
    rd_center_text(100,"M I S S I O N   C L E A R",PAL_UI_TEXT);
    char line[64];
    copy(line,"Final score ",sizeof(line));
    char n[12];ex_udec(n,sizeof(n),(u32)rd_score);
    append(line,n,sizeof(line));
    rd_center_text(140,line,PAL_UI_GOLD+7);
    if(rd_score>rd_best){
        rd_best=rd_score;
        char out[16];
        ex_udec(out,sizeof(out),(u32)rd_best);
        sc_write(RD_SCORE_FILE,out,length(out));
        rd_center_text(170,"New best score!",PAL_UI_CYAN+7);
    }
    rd_center_text(210,"R menu / Esc quit",PAL_UI_MUTED);
}
static void rd_draw_pause(void)
{
    /* 暂停页直接复用世界最后一帧再叠说明：状态没变不重画世界 */
    rd_blit();
    int y=rd_view_h/2;
    ui_rect_rgb(0,y-40,rd_view_w,80,0xE0202028u);
    char line[64];
    copy(line,"Paused - mouse released",sizeof(line));
    ui_text(ui_width/2-ex_text_w(line)/2,y-22,line,PAL_UI_TEXT);
    copy(line,"Click recapture / Esc quit",sizeof(line));
    ui_text(ui_width/2-ex_text_w(line)/2,y+6,line,PAL_UI_MUTED);
}

/* ---------------- 生命周期助手 ---------------- */
static int rd_try_capture(void)
{
    int ok=sc_mouse_capture(ui_win,1)==0;
    rd_mouse_captured=ok;
    return ok;
}
static void rd_release_mouse(void)
{
    sc_mouse_capture(ui_win,0);
    rd_mouse_captured=0;
}
static void rd_reset_player(void)
{
    rd_hp=100;rd_armor=0;
    rd_bullets=48;rd_shells=0;
    rd_weapon=0;rd_has_shotgun=0;rd_has_chain=0;
    rd_key_red=rd_key_blue=0;
    rd_flash=rd_fire_vis=rd_cd=0;
    rd_msg[0]=0;rd_msg_ticks=0;
}
static void rd_start_game(void)
{
    rd_score=0;
    rd_reset_player();
    rd_load_level(0);
    rd_state=1;
    rd_try_capture();
    ui_followup=1;
}

/* ---------------- 帧几何与缓冲 ---------------- */
static void rd_geometry(void)
{
    rd_view_w=ui_width;
    rd_view_h=ui_height-RD_HUD;
    if(rd_view_h<24)rd_view_h=24;
    rd_scale=1<<rd_render_shift;
    rd_rw=rd_view_w/rd_scale;
    rd_rh=rd_view_h/rd_scale;
    if(rd_rw>1920)rd_rw=1920;
    if(rd_rw<80){rd_render_shift=1;rd_scale=2;rd_rw=rd_view_w/2;}
    if(rd_rw<1)rd_rw=1;
    if(rd_rh<1)rd_rh=1;
}
static int rd_ensure_buffer(void)
{
    static int old_w,old_h;
    if(rd_buf&&old_w==rd_rw&&old_h==rd_rh)return 0;
    if(rd_buf)sc_free(rd_buf);
    rd_buf=sc_alloc((u32)rd_rw*rd_rh*4);
    if(!rd_buf){
        if(rd_render_shift==0){rd_render_shift=1;rd_geometry();
            return rd_ensure_buffer();}
        return -1;
    }
    old_w=rd_rw;old_h=rd_rh;
    return 0;
}

/* ---------------- 仿真（固定 100Hz） ---------------- */
static void rd_sim(void)
{
    if(rd_flash)rd_flash--;
    if(rd_fire_vis)rd_fire_vis--;
    if(rd_msg_ticks)rd_msg_ticks--;
    if(rd_cd)rd_cd--;
    if(rd_state!=1)return;
    float fx=rd_cos(rd_ang),fy=rd_sin(rd_ang);
    int fwd=0,str=0;
    if(sc_down('w'))fwd++;
    if(sc_down('s'))fwd--;
    if(sc_down('a'))str--;
    if(sc_down('d'))str++;
    if(rd_mouse_captured){
        int snap[8];
        if(!sc_mouse_relative(ui_win,snap)){
            rd_ang+=(float)snap[1]*rd_sens;
        }else rd_mouse_captured=0;      /* 内核已释放（Esc/失焦） */
        if(sc_down(0x82))rd_ang-=0.05f; /* 键盘转向兜底 */
        if(sc_down(0x83))rd_ang+=0.05f;
    }else{
        if(sc_down(0x80))fwd++;
        if(sc_down(0x81))fwd--;
        if(sc_down(0x82))rd_ang-=0.05f;
        if(sc_down(0x83))rd_ang+=0.05f;
    }
    if(fwd||str){
        float sp=0.045f;
        /* 右向量=(-fy,fx)：y 向下地图坐标里的顺时针 90 度 */
        rd_move(&rd_px,&rd_py,fx*sp*fwd-fy*sp*str,fy*sp*fwd+fx*sp*str,0.25f);
    }
    rd_pickups();
    rd_enemies_tick();
}

/* ---------------- 配置与最佳分 ---------------- */
static void rd_load_config(void)
{
    char cfg[2048];
    if(sc_read("HOME/RAIDER.CFG",cfg,sizeof(cfg)-1)>=0){
        cfg[sizeof(cfg)-1]=0;
        char v[16];
        if(ex_cfg_get(cfg,"difficulty",v,sizeof(v))==0){
            int d;
            if(ex_parse_int(v,&d)==0)rd_difficulty=ex_clamp(d,1,3);
            rd_menu_diff=rd_difficulty;
        }
        if(ex_cfg_get(cfg,"sensitivity",v,sizeof(v))==0){
            int s;
            if(ex_parse_int(v,&s)==0)
                rd_sens=0.0012f+0.0006f*ex_clamp(s,1,10);
        }
        if(ex_cfg_get(cfg,"render",v,sizeof(v))==0){
            int r;
            if(ex_parse_int(v,&r)==0)rd_render_shift=ex_clamp(r,1,2)-1;
        }
        if(ex_cfg_get(cfg,"sound",v,sizeof(v))==0)rd_sound=!equal(v,"0");
    }
    char best[16];
    if(sc_read(RD_SCORE_FILE,best,sizeof(best)-1)>=0){
        best[sizeof(best)-1]=0;
        int b;
        if(ex_parse_int(best,&b)==0)rd_best=b;
    }
}

/* ---------------- 主循环 ---------------- */
int main(void)
{
    if(ui_open("SandRaider / SandCore ext")<0)return 1;
    rd_trig_init();
    rd_gen_textures();
    rd_gen_sprites();
    rd_load_config();
    rd_geometry();
    int last=sc_tick(),accum=0;
    int was_captured=0;
    ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(1))continue;
        ui_pointer();
        rd_geometry();
        if(rd_ensure_buffer()){exaudio_shutdown();return 1;}
        /* 固定步长：tick 累加补步，长帧不加速穿墙 */
        int now=sc_tick();
        int dt=now-last;
        last=now;
        if(dt<0)dt=0;
        if(dt>10)dt=10;
        accum+=dt;
        while(accum>=1){accum--;rd_sim();}
        if(rd_state==1&&was_captured&&rd_mouse_captured==0){
            rd_state=5;                 /* 内核释放捕获 -> 暂停 */
            was_captured=0;
            ui_followup=1;
        }
        switch(rd_state){
        case 0:rd_draw_menu();break;
        case 1:
            rd_render_world();
            rd_blit();
                        rd_render_world();
            rd_blit();
            rd_draw_weapon();
            rd_draw_hud();
            break;
        case 2:rd_draw_intermission();break;
        case 3:rd_draw_death();break;
        case 4:rd_draw_win();break;
        case 5:rd_draw_pause();break;
        }
        ui_present();
        /* ---------- 事件键 ---------- */
        int key=ui_action?-1:sc_key();
        if(rd_state==0&&ui_action>=10&&ui_action<=12){
            rd_menu_diff=ui_action-9;
            rd_difficulty=rd_menu_diff;
            key=-1;
        }
        if(key<0){exaudio_pump();continue;}
        switch(rd_state){
        case 0:
            if(key>='1'&&key<='3'){rd_menu_diff=key-'0';rd_difficulty=key-'0';}
            else if(key==10)rd_start_game();
            else if(key==27){exaudio_shutdown();return 0;}
            break;
        case 1:
            /* 捕获时鼠标开火：POINTER 点击沿与新接口沿独立保留 */
            if(rd_mouse_captured&&((ui_pressed&1)||(ui_buttons&1&&rd_weapon==2)))
                rd_fire();
            if(key==27){rd_release_mouse();rd_state=5;}
            else if(key==' ')rd_fire();
            else if(key=='e'||key=='E')rd_use();
            else if(key>='1'&&key<='3')rd_switch_weapon(key-'1');
            break;
        case 2:
            if(key==10||key==' '){
                if(rd_level+1<RD_LEVELS){
                    rd_reset_player();
                    rd_load_level(rd_level+1);
                    rd_try_capture();
                    rd_state=1;
                }else rd_state=4;
            }
            break;
        case 3:
            if(key=='r'||key=='R'){
                rd_reset_player();
                rd_load_level(rd_level);
                rd_try_capture();
                rd_state=1;
            }else if(key==27){exaudio_shutdown();return 0;}
            break;
        case 4:
            if(key=='r'||key=='R')rd_state=0;
            else if(key==27){exaudio_shutdown();return 0;}
            break;
        case 5:
            if((ui_released&1)&&rd_try_capture()){
                rd_state=1;
                was_captured=1;
            }else if(key==10&&rd_try_capture()){rd_state=1;was_captured=1;}
            else if(key==27){exaudio_shutdown();return 0;}
            break;
        }
        if(rd_state==1&&rd_mouse_captured)was_captured=1;
        exaudio_pump();
    }
}
