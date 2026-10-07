/* =====================================================================
 * raider.c —— SandRaider：DOOM 式光栅枪战
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 玩法
 *   - 6 关 24×24 地图；DDA 光线投射 + 手绘像素精灵
 *   - 敌人五种：近战/枪手/重装/无人机/守卫；视线、追击、攻击、硬直、
 *     死亡留尸（尸体不再阻挡/受击）
 *   - 武器四种：手枪 / 霰弹（5 弹丸散射）/ 机枪 / 刀；弹匣和备弹分离
 *   - 道具：医疗包/护甲/弹药/霰弹/霰弹枪/机枪/红蓝钥匙/宝藏
 *   - 红蓝钥匙锁门；E 开门（近身格）；出口开关 E 过关
 *   - 过关结算（击杀/物品/宝藏/分数），通关终局 + 历史最佳
 *   - 鼠标捕获转视角（sc_mouse_capture/relative），Esc 自动释放并
 *     暂停；方向键转视角是无捕获回退
 *   - 受击红闪、拾取提示、程序化音效
 *
 * 工程约束
 *   - float 光线投射：目标机完整 32 位 x86，内核保存/恢复浮点
 *     上下文（按 M9 平台代码合同）；本轮仅宿主验证，无 libm——sin/cos 用
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
#include "../common/pack_ui.inc"

#define RD_MW 24
#define RD_MH 24
#define RD_LEVELS 6
#define RD_ENEMIES 40
#define RD_ITEMS 48
#define RD_TS 32
#define RD_SP 32
#define RD_TEXN 8
#define RD_SCORE_FILE "HOME/RAIDER.SCORE"

/* 图例：#石 B砖 W科技 r红门 b蓝门 d门 E出口 . 空
 * P 出生 g近战 s枪手 h重装
 * M医疗 A弹药 S霰弹 R护甲 1红钥匙 2蓝钥匙 T宝藏 G霰弹枪 C机枪 */
#include "raider_maps.inc"
#include "raider_art.inc"

/* ---------------- 三角函数（泰勒建表，运行期纯查表） ---------------- */
#define RD_TAB 1024
static float rd_sintab[RD_TAB];
static void rd_trig_init(void)
{
    /* 建 0..2π 全表：前 1/4 用泰勒，其余按对称复制。 */
    const float PI=3.14159265f;
    for(int i=0;i<=RD_TAB/4;i++){
        float x=(float)i*(PI/2)/(RD_TAB/4);
        float x2=x*x;
        float s=x*(1.0f-x2/6.0f*(1.0f-x2/20.0f*(1.0f-x2/42.0f*
                 (1.0f-x2/72.0f))));
        rd_sintab[i]=s;
    }
    for(int i=RD_TAB/4+1;i<RD_TAB;i++){
        if(i<RD_TAB/2)rd_sintab[i]=rd_sintab[RD_TAB/2-i];
        else if(i<3*RD_TAB/4)rd_sintab[i]=-rd_sintab[i-RD_TAB/2];
        else rd_sintab[i]=-rd_sintab[RD_TAB-i];
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
static float rd_sens=0.00054f;
static int rd_render_shift=0;      /* 0 全分辨率 / 1 减半 */
static int rd_sound=1;

static u32 *rd_buf;
static int rd_rw,rd_rh,rd_scale=2;
static int rd_view_w,rd_view_h;   /* 物理视图区（HUD 之上） */
static int rd_state;    /* 0 菜单 1 游玩 2 过关 3 死亡 4 终局 5 暂停 */
static int rd_mouse_captured;
static int rd_best;
static int rd_menu_diff=2;
static int rd_sensitivity=3,rd_crosshair=1,rd_minimap=1,rd_show_map;
static int rd_clip[3],rd_reload,rd_hitmark,rd_boss_dead,rd_elapsed,rd_walk;
static int rd_mouse_fire,rd_resume_state=0,rd_setting_status;
static int rd_doc_scroll,rd_doc_total,rd_doc_visible;
static u8 rd_seen[RD_MH][RD_MW];
static const int rd_capacity[3]={12,6,36};
static int rd_checkpoint_score;
static int rd_checkpoint_hp,rd_checkpoint_armor,rd_checkpoint_bullets,rd_checkpoint_shells;
static int rd_checkpoint_weapon,rd_checkpoint_shotgun,rd_checkpoint_chain;
static int rd_checkpoint_clips[3];

static float rd_fabsf2(float v){return v<0?-v:v;}
static float rd_floorf(float v){int i=(int)v;return v<i?(float)i-1.0f:(float)i;}
/* 牛顿迭代开方：6 次迭代达到 float 半精度；v<=0 返回 0 由调用方判错 */
static float rd_sqrtf2(float v)
{
    if(v<=0)return 0;
    float scale=1;
    while(v>4){v*=0.25f;scale*=2;}
    while(v<1){v*=4;scale*=0.5f;}
    float x=2;for(int i=0;i<8;i++)x=(x+v/x)*0.5f;
    return x*scale;
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
    rd_boss_dead=0;rd_elapsed=0;rd_key_red=rd_key_blue=0;
    for(int y=0;y<RD_MH;y++)for(int x=0;x<RD_MW;x++)rd_seen[y][x]=0;
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
        else if(c=='g'||c=='s'||c=='h'||c=='v'||c=='o'){
            if(rd_en_count<RD_ENEMIES){
                rd_enemy *e=&rd_en[rd_en_count++];
                e->x=fx;e->y=fy;
                e->type=c=='g'?0:c=='s'?1:c=='h'?2:c=='v'?3:4;
                e->hp=e->type==0?28:e->type==1?40:e->type==2?90:e->type==3?32:300;
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
    rd_hitmark=12;
    if(e->hp<=0){
        e->alive=0;
        rd_kills++;
        rd_score+=e->type==4?1000:e->type==2?150:75;
        if(e->type==4){rd_boss_dead=1;rd_msg_set("Warden defeated! Head for extraction.");}
        /* 击杀补给保证长关卡有持续弹药，同时刀刃保留无弹兜底。 */
        rd_bullets=ex_min(rd_bullets+6,240);
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
    if(rd_weapon==3&&wall>1.4f)wall=1.4f;
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
    if(rd_cd>0||rd_reload>0)return;
    if(rd_weapon==3){rd_cd=35;rd_hitscan(rd_ang,28);rd_fire_vis=8;rd_sfx(420,60,EXWAVE_TRIANGLE,180);return;}
    if(rd_clip[rd_weapon]<=0){rd_msg_set("Magazine empty. R reload / 4 blade.");return;}
    if(rd_weapon==0){
        rd_clip[0]--;
        rd_cd=30;
        rd_hitscan(rd_ang,22);
        rd_sfx(880,60,EXWAVE_NOISE,230);
    }else if(rd_weapon==1){
        rd_clip[1]--;
        rd_cd=55;
        for(int i=-2;i<=2;i++)              /* 5 弹丸水平散射 */
            rd_hitscan(rd_ang+i*0.035f,18);
        rd_sfx(240,140,EXWAVE_NOISE,240);
    }else{
        rd_clip[2]--;
        rd_cd=12;
        rd_hitscan(rd_ang+(float)ex_rand_range(-8,8)*0.004f,
                   16);
        rd_sfx(760,40,EXWAVE_NOISE,200);
    }
    rd_fire_vis=8;
}
static void rd_switch_weapon(int w)
{
    if(w==1&&!rd_has_shotgun){rd_msg_set("No shotgun yet");return;}
    if(w==2&&!rd_has_chain){rd_msg_set("No chaingun yet");return;}
    rd_weapon=w;
    rd_reload=0;
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
        if(!rd_key_red||!rd_key_blue){rd_msg_set("Find both keycards before extraction.");return;}
        if(rd_level==RD_LEVELS-1&&!rd_boss_dead){rd_msg_set("Defeat the Warden before extraction.");return;}
        rd_state=2;
        rd_score+=200*(rd_level+1);
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
        case 5:if(rd_bullets>=240){msg=0;break;}rd_bullets=ex_min(240,rd_bullets+24);rd_msg_set("Bullets +24");break;
        case 6:if(rd_shells>=60){msg=0;break;}rd_shells=ex_min(60,rd_shells+8);rd_msg_set("Shells +8");break;
        case 7:if(rd_armor>=100){msg=0;break;}rd_armor=ex_min(100,rd_armor+50);rd_msg_set("Armor +50");break;
        case 8:rd_key_red=1;rd_msg_set("Got the RED key");break;
        case 9:rd_key_blue=1;rd_msg_set("Got the BLUE key");break;
        case 10:rd_score+=500;rd_treasure++;rd_msg_set("Treasure +500");break;
        case 11:
            if(!rd_has_shotgun){rd_has_shotgun=1;rd_weapon=1;rd_reload=0;rd_clip[1]=6;
                rd_msg_set("Shotgun!");}
            else{if(rd_shells>=60){msg=0;break;}rd_shells=ex_min(60,rd_shells+4);rd_msg_set("Shells +4");}
            break;
        case 12:
            if(!rd_has_chain){rd_has_chain=1;rd_weapon=2;rd_reload=0;rd_clip[2]=36;
                rd_msg_set("Chaingun!");}
            else{if(rd_bullets>=240){msg=0;break;}rd_bullets=ex_min(240,rd_bullets+8);rd_msg_set("Bullets +8");}
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
        if(e->think>0)e->think--;
        else{e->think=20;e->seeing=dist<14&&rd_los(e->x,e->y,rd_px,rd_py);}
        if(!e->seeing)continue;
        float speed=(e->type==0?0.010f:e->type==1?0.012f:0.008f)
                    *(0.8f+0.2f*rd_difficulty);
        float stop=e->type==0?1.2f:e->type==3?3.0f:4.5f;
        if(dist>stop){
            rd_move(&e->x,&e->y,dx/dist*speed,dy/dist*speed,0.3f);
        }
        /* 攻击 */
        int range=e->type==0?130:800;       /* 接触半径比停步距离大，近战才真正能攻击。 */
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
        if(rd_state!=1)return;             /* 致死后本步不再追加别的攻击。 */
    }
}

typedef struct {float x,y;const u32 *pix;float dist;int hurt,small;} rd_vis_entry;
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
        int lh=(int)((float)w/(1.32f*dist));
        if(lh<1)lh=1;
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
        vis[vn].pix=rd_spr[e->alive?(e->type>=3?e->type+10:e->type):3];
        vis[vn].hurt=e->alive&&e->hurt>0;
        vis[vn].small=0;
        vn++;
    }
    for(int i=0;i<rd_it_count;i++){
        rd_item *it=&rd_it[i];
        if(it->taken)continue;
        float dx=it->x-rd_px,dy=it->y-rd_py;
        vis[vn].x=it->x;vis[vn].y=it->y;
        vis[vn].dist=dx*dx+dy*dy;
        vis[vn].pix=rd_spr[it->type];
        vis[vn].hurt=0;
        vis[vn].small=1;
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
        int fullsh=(int)((float)w/(1.32f*ty));
        int sh=vis[i].small?(int)(fullsh*0.55f):fullsh;
        if(sh<1)continue;
        int sx0=screenx-sh/2,sy0=vis[i].small?h/2+fullsh/2-sh:h/2-sh/2;
        /* 近处精灵投影可达数千像素；先裁可见范围，再循环纹理。
         * 不能先遍历整片屏外投影再逐点 continue。 */
        int col0=ex_max(0,-sx0),col1=ex_min(sh,w-sx0);
        int row0=ex_max(0,-sy0),row1=ex_min(sh,h-sy0);
        for(int col=col0;col<col1;col++){
            int px=sx0+col;
            if(px<0||px>=w)continue;
            if((u32)(ty*1000.0f)>=rd_zbuf[px])continue;
            int texcol=col*RD_SP/sh;
            for(int row=row0;row<row1;row++){
                int py=sy0+row;
                if(py<0||py>=h)continue;
                u32 c=vis[i].pix[(row*RD_SP/sh)*RD_SP+texcol];
                if(!(c&0xFF000000u))continue;
                if(vis[i].hurt)c=rd_mix(c,PK_WHITE,100);
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
        for(int dy=0;dy<rd_scale&&y*rd_scale+dy<vh;dy++){
            u32 *out=ui_pixels+(u32)(y*rd_scale+dy)*ui_width;
            for(int x=0;x<rd_rw&&x*rd_scale<vw;x++){
                u32 c=src[x];
                for(int dx=0;dx<rd_scale&&x*rd_scale+dx<vw;dx++)
                    out[x*rd_scale+dx]=c;
            }
        }
    }
}

#include "raider_play.inc"
#include "raider_ui.inc"
#include "raider_loop.inc"
