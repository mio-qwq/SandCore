/* =====================================================================
 * blocks.c —— SandBlocks：方块游戏
 * 所属：SandCore_ExtraSoftware_Pack_1（ext/SoftwarePack1）
 *
 * 玩法
 *   - 10×20 场地；7-bag 随机器（洗牌整袋，杜绝长旱）
 *   - 旋转带踢墙（0/-1/+1/-2/+2 横移试探）；Z 逆旋
 *   - Hold（每次落子一次）；Next 队列 3；幽灵落点
 *   - 软降 1 分/格、硬降 2 分/格、消行 100/300/500/800×等级
 *   - 锁定延迟 5 tick：贴底后仍可微调，方块游戏手感的核心
 *   - 每 10 行升一级，下落加速（30 tick -> 最低 4）
 *   - 前五成绩榜持久化 HOME/BLOCKS.SCORE；进入榜单录名
 *   - 配置 BLOCKS.CFG：起始等级 / 幽灵 / 音效
 *
 * 实现立场：固定步长仿真——输入处理与重力按 PIT tick 推进，
 * 渲染随事件+每 tick 兜底；不把"每画一帧掉一格"当速度模型。
 * 方块配色是游戏材质常量（同 World/Race 的固定材质做法），
 * 界面镶边/文字全部走主题角色。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "exutil.h"
#include "exaudio.h"

#define BK_W 10
#define BK_H 20
#define BK_CELL 14
#define BK_SCORE_FILE "HOME/BLOCKS.SCORE"

/* 7 种四格方块，4 个旋转态，每态 4 个 (dx,dy)。
 * 编码 0..6：I J L O S T Z。旋转态用坐标表而不是矩阵运算，
 * 直观可核对，也避免每帧乘法。 */
static const signed char bk_shapes[7][4][4][2]={
 /* I */ {{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},
          {{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}},
 /* J */ {{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},
          {{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}},
 /* L */ {{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},
          {{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}},
 /* O */ {{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},
          {{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}}},
 /* S */ {{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},
          {{1,1},{2,1},{0,2},{1,2}},{{0,0},{0,1},{1,1},{1,2}}},
 /* T */ {{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},
          {{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}},
 /* Z */ {{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},
          {{0,1},{1,1},{1,2},{2,2}},{{1,0},{0,1},{1,1},{0,2}}}
};
/* 固定材质色（游戏材质不随主题变化，界面向导色才走主题） */
static const u32 bk_colors[7]={
 0x00B8E0F2, 0x003E6FE0, 0x00F29C3E, 0x00F2E13E,
 0x0048D24C, 0x00B448D2, 0x00E04848
};

/* ---------- 状态 ---------- */
static u8 bk_grid[BK_H][BK_W];          /* 0 空，1..7 颜色 */
static int bk_px,bk_py,bk_piece,bk_rot;
static int bk_next[3];                  /* 预览队列 */
static int bk_hold;                     /* -1 无 */
static int bk_hold_used;
static int bk_bag[7],bk_bag_count;      /* 7-bag */
static int bk_score,bk_lines,bk_level;
static int bk_tick_accum;               /* 重力步长累加 */
static int bk_lock_wait;                /* 锁定延迟倒计时 */
static int bk_state;                    /* 0 进行 1 暂停 2 结束 3 录名 */
static int bk_ghost,bk_sound,bk_start_level=1;
static char bk_name[12];
static int bk_new_rank;                 /* -1 未上榜，否则 0..4 */
static int bk_score_saved;              /* 榜单一次性触发旗标 */
/* 榜单 */
static u32 bk_hs_score[5];
static u32 bk_hs_lines[5],bk_hs_level[5];
static char bk_hs_name[5][12];

/* ---------- 随机 ---------- */
static int bk_bag_next(void)
{
    if(!bk_bag_count){
        for(int i=0;i<7;i++)bk_bag[i]=i;
        for(int i=6;i>0;i--){              /* Fisher-Yates 洗整袋 */
            int j=ex_rand_range(0,i);
            int t=bk_bag[i];bk_bag[i]=bk_bag[j];bk_bag[j]=t;
        }
        bk_bag_count=7;
    }
    return bk_bag[--bk_bag_count];
}

/* ---------- 碰撞 ---------- */
static int bk_fits(int px,int py,int piece,int rot)
{
    for(int i=0;i<4;i++){
        int x=px+bk_shapes[piece][rot][i][0];
        int y=py+bk_shapes[piece][rot][i][1];
        if(x<0||x>=BK_W||y>=BK_H)return 0;
        if(y>=0&&bk_grid[y][x])return 0;
    }
    return 1;
}
/* 踢墙：旋转后依次试 0/-1/+1/-2/+2 横移 */
static int bk_rotate(int dir)
{
    int nr=(bk_rot+dir+4)&3;
    static const int kick[5]={0,-1,1,-2,2};
    for(int k=0;k<5;k++){
        if(bk_fits(bk_px+kick[k],bk_py,bk_piece,nr)){
            bk_px+=kick[k];
            bk_rot=nr;
            return 1;
        }
    }
    return 0;
}
static void bk_ghost_y(int *out)
{
    int y=bk_py;
    while(bk_fits(bk_px,y+1,bk_piece,bk_rot))y++;
    *out=y;
}

/* ---------- 消行与计分 ---------- */
static void bk_clear_lines(void)
{
    int cleared=0;
    for(int y=BK_H-1;y>=0;y--){
        int full=1;
        for(int x=0;x<BK_W;x++)if(!bk_grid[y][x]){full=0;break;}
        if(!full)continue;
        cleared++;
        for(int yy=y;yy>0;yy--)
            for(int x=0;x<BK_W;x++)bk_grid[yy][x]=bk_grid[yy-1][x];
        for(int x=0;x<BK_W;x++)bk_grid[0][x]=0;
        y++;                                /* 原地再查一次 */
    }
    if(!cleared)return;
    static const u32 base[5]={0,100,300,500,800};
    bk_score+=(int)base[cleared]*bk_level;
    bk_lines+=cleared;
    bk_level=bk_start_level+bk_lines/10;
    if(bk_sound){
        exaudio_tone(cleared==4?880:520,cleared==4?260:120,
                     EXWAVE_SQUARE,200);
        if(cleared==4)exaudio_tone(1174,180,EXWAVE_SQUARE,160);
    }
}

/* ---------- 生成与锁定 ---------- */
static void bk_spawn(int piece)
{
    bk_piece=piece;
    bk_rot=0;
    bk_px=3;
    bk_py=piece==0?-1:0;                    /* I 露半格出生 */
    bk_hold_used=0;
    bk_lock_wait=0;
    if(!bk_fits(bk_px,bk_py,bk_piece,bk_rot))bk_state=2;
}
static void bk_lock(void)
{
    for(int i=0;i<4;i++){
        int x=bk_px+bk_shapes[bk_piece][bk_rot][i][0];
        int y=bk_py+bk_shapes[bk_piece][bk_rot][i][1];
        if(y>=0)bk_grid[y][x]=(u8)(bk_piece+1);
    }
    if(bk_sound)exaudio_tone(160,50,EXWAVE_SQUARE,140);
    bk_clear_lines();
    bk_spawn(bk_next[0]);
    bk_next[0]=bk_next[1];
    bk_next[1]=bk_next[2];
    bk_next[2]=bk_bag_next();
}
static void bk_drop(int hard)
{
    int dist=0;
    while(bk_fits(bk_px,bk_py+1,bk_piece,bk_rot)){
        bk_py++;
        dist++;
        if(!hard)break;                     /* 软降一次一格 */
    }
    if(hard){
        bk_score+=dist*2;
        bk_lock();
        ui_followup=1;
    }else if(dist)bk_score++;
}

static void bk_hold_swap(void)
{
    if(bk_hold_used)return;
    bk_hold_used=1;
    int cur=bk_piece;
    if(bk_hold<0){bk_hold=cur;bk_spawn(bk_next[0]);
        bk_next[0]=bk_next[1];bk_next[1]=bk_next[2];bk_next[2]=bk_bag_next();}
    else{int h=bk_hold;bk_hold=cur;bk_spawn(h);}
    if(bk_sound)exaudio_tone(392,60,EXWAVE_TRIANGLE,150);
}

/* ---------- 榜单 ---------- */
static void bk_scores_load(void)
{
    for(int i=0;i<5;i++){bk_hs_score[i]=0;bk_hs_name[i][0]=0;
        bk_hs_lines[i]=bk_hs_level[i]=0;}
    char buf[512];
    if(sc_read(BK_SCORE_FILE,buf,sizeof(buf)-1)<0)return;
    buf[sizeof(buf)-1]=0;
    char *p=buf;
    int i=0;
    while(*p&&i<5){
        char *line=p;
        while(*p&&*p!='\n')p++;
        if(*p)*p++=0;
        /* 行格式：score lines level name（空格分隔，名字在尾部） */
        char score[16],lines[16],level[16];
        char *f[4]={score,lines,level,bk_hs_name[i]};
        int got=0;
        char *q=line;
        while(got<4){
            while(*q==' ')q++;
            if(!*q)break;
            char *start=q;
            while(*q&&*q!=' ')q++;
            if(*q)*q++=0;
            char *dst=f[got];
            int cap=got==3?12:16;
            int n=0;
            while(start[n]&&n<cap-1){dst[n]=start[n];n++;}
            dst[n]=0;
            got++;
        }
        if(got>=3){
            int v;
            if(ex_parse_int(score,&v)==0)bk_hs_score[i]=(u32)v;
            if(ex_parse_int(lines,&v)==0)bk_hs_lines[i]=(u32)v;
            if(ex_parse_int(level,&v)==0)bk_hs_level[i]=(u32)v;
            i++;
        }
    }
}
static void bk_scores_save(void)
{
    char buf[512];
    int used=0;
    for(int i=0;i<5&&bk_hs_score[i];i++){
        char row[80];
        char s1[12],s2[12],s3[12];
        ex_udec(s1,sizeof(s1),bk_hs_score[i]);
        ex_udec(s2,sizeof(s2),bk_hs_lines[i]);
        ex_udec(s3,sizeof(s3),bk_hs_level[i]);
        copy(row,s1,sizeof(row));
        append(row," ",sizeof(row));
        append(row,s2,sizeof(row));
        append(row," ",sizeof(row));
        append(row,s3,sizeof(row));
        append(row," ",sizeof(row));
        append(row,bk_hs_name[i],sizeof(row));
        append(row,"\n",sizeof(row));
        if(used+length(row)<(int)sizeof(buf)){
            for(const char *q=row;*q;q++)buf[used++]=*q;
        }
    }
    buf[used]=0;
    sc_write(BK_SCORE_FILE,buf,used);
}
static void bk_submit_score(void)
{
    /* 从后往前挤位：新成绩插入 rank，其后顺移 */
    for(int i=4;i>bk_new_rank;i--){
        bk_hs_score[i]=bk_hs_score[i-1];
        bk_hs_lines[i]=bk_hs_lines[i-1];
        bk_hs_level[i]=bk_hs_level[i-1];
        copy(bk_hs_name[i],bk_hs_name[i-1],12);
    }
    bk_hs_score[bk_new_rank]=(u32)bk_score;
    bk_hs_lines[bk_new_rank]=(u32)bk_lines;
    bk_hs_level[bk_new_rank]=(u32)bk_level;
    copy(bk_hs_name[bk_new_rank],bk_name,12);
    bk_scores_save();
    bk_new_rank=-1;
    bk_score_saved=1;
    bk_state=2;
    ui_followup=1;
}

/* ---------- 开局 ---------- */
static void bk_new_game(void)
{
    for(int y=0;y<BK_H;y++)for(int x=0;x<BK_W;x++)bk_grid[y][x]=0;
    bk_score=0;
    bk_lines=0;
    bk_level=bk_start_level;
    bk_hold=-1;
    bk_bag_count=0;
    bk_new_rank=-1;
    bk_score_saved=0;
    bk_name[0]=0;
    bk_next[0]=bk_bag_next();
    bk_next[1]=bk_bag_next();
    bk_next[2]=bk_bag_next();
    bk_spawn(bk_bag_next());
    bk_tick_accum=0;
    bk_state=0;
    ui_followup=1;
}

/* ---------- 重力 ---------- */
static void bk_gravity(void)
{
    if(bk_state)return;
    int drop_ticks=bk_level>=14?4:30-bk_level*2;
    if(++bk_tick_accum<drop_ticks)return;
    bk_tick_accum=0;
    if(bk_fits(bk_px,bk_py+1,bk_piece,bk_rot)){
        bk_py++;
        bk_lock_wait=0;
    }else if(++bk_lock_wait>5){              /* 贴底 5 tick 后锁定 */
        bk_lock_wait=0;
        bk_lock();
    }
}

/* ---------- 绘制 ---------- */
static void bk_draw_cell(int x,int y,u32 rgb)
{
    int fx=(UI_W-BK_W*BK_CELL)/2-40;
    int fy=40;
    ui_rect_rgb(fx+x*BK_CELL,fy+y*BK_CELL,BK_CELL-1,BK_CELL-1,rgb|0xFF000000u);
}
static void bk_draw_piece(int px,int py,int piece,int rot,u32 rgb)
{
    for(int i=0;i<4;i++){
        int x=px+bk_shapes[piece][rot][i][0];
        int y=py+bk_shapes[piece][rot][i][1];
        if(y>=0)bk_draw_cell(x,y,rgb);
    }
}
static void bk_draw_mini(int x,int y,int piece)
{
    /* 预览格 10px，以形状包围盒居中 */
    if(piece<0)return;
    int minx=9,miny=9,maxx=0,maxy=0;
    for(int i=0;i<4;i++){
        int dx=bk_shapes[piece][0][i][0],dy=bk_shapes[piece][0][i][1];
        if(dx<minx)minx=dx;if(dx>maxx)maxx=dx;
        if(dy<miny)miny=dy;if(dy>maxy)maxy=dy;
    }
    int w=(maxx-minx+1)*10,h=(maxy-miny+1)*10;
    int ox=x+(44-w)/2,oy=y+(40-h)/2;
    for(int i=0;i<4;i++){
        int dx=bk_shapes[piece][0][i][0]-minx;
        int dy=bk_shapes[piece][0][i][1]-miny;
        ui_rect_rgb(ox+dx*10,oy+dy*10,9,9,bk_colors[piece]|0xFF000000u);
    }
}
static void bk_draw_panel(int x,int y,int w,int h,const char *title)
{
    ui_panel(x,y,w,h);
    ui_text(x+8,y+6,title,PAL_UI_MUTED);
}
static void bk_draw(void)
{
    ui_header("SandBlocks","stack / clear / survive");
    /* 场地外框 */
    int fx=(UI_W-BK_W*BK_CELL)/2-40;
    int fy=40;
    int fw=BK_W*BK_CELL+4,fh=BK_H*BK_CELL+4;
    ui_rect(fx-2,fy-2,fw+4,fh+4,PAL_UI_LINE);
    ui_rect(fx,fy,fw,fh,PAL_UI_INK);
    /* 已锁定的格子 */
    for(int y=0;y<BK_H;y++)for(int x=0;x<BK_W;x++)
        if(bk_grid[y][x])bk_draw_cell(x,y,bk_colors[bk_grid[y][x]-1]);
    if(bk_state==0){
        int gy;
        bk_ghost_y(&gy);
        if(bk_ghost&&gy!=bk_py){
            /* 幽灵：画 1px 外框示意落点 */
            for(int i=0;i<4;i++){
                int x=bk_px+bk_shapes[bk_piece][bk_rot][i][0];
                int y=gy+bk_shapes[bk_piece][bk_rot][i][1];
                if(y>=0){
                    int cx=fx+x*BK_CELL,cy=fy+y*BK_CELL;
                    ui_rect_rgb(cx,cy,BK_CELL-1,1,bk_colors[bk_piece]|0xFF000000u);
                    ui_rect_rgb(cx,cy,1,BK_CELL-1,bk_colors[bk_piece]|0xFF000000u);
                }
            }
        }
        bk_draw_piece(bk_px,bk_py,bk_piece,bk_rot,bk_colors[bk_piece]);
    }
    /* 左：分数 */
    bk_draw_panel(16,40,120,120,"Score");
    char num[16];
    ex_udec(num,sizeof(num),(u32)bk_score);
    ui_text(24,62,num,PAL_UI_TEXT);
    ui_text(24,86,"Lines",PAL_UI_MUTED);
    ex_udec(num,sizeof(num),(u32)bk_lines);
    ui_text(24,102,num,PAL_UI_TEXT);
    ui_text(24,126,"Level",PAL_UI_MUTED);
    ex_udec(num,sizeof(num),(u32)bk_level);
    ui_text(24,142,num,PAL_UI_TEXT);
    /* 右：Hold + Next */
    bk_draw_panel(UI_W-136,40,120,60,"Hold");
    bk_draw_mini(UI_W-136,60,bk_hold);
    bk_draw_panel(UI_W-136,108,120,150,"Next");
    for(int i=0;i<3;i++)bk_draw_mini(UI_W-136,108+8+i*46,bk_next[i]);
    /* 覆盖层 */
    if(bk_state==1){
        ui_text(fx+30,fy+150,"Paused - P to resume",PAL_UI_GOLD+7);
    }else if(bk_state==2||bk_state==3){
        ui_rect_rgb(fx,fy+120,fw,120,0xE6000000u);
        char line[48];
        copy(line,"Game over  score ",sizeof(line));
        char n[16];ex_udec(n,sizeof(n),(u32)bk_score);
        append(line,n,sizeof(line));
        ui_text(fx+12,fy+140,line,PAL_UI_ALERT);
        if(bk_state==3)
            ui_text(fx+12,fy+164,"Name? (max 11, Enter ok)",PAL_UI_TEXT);
        else{
            ui_text(fx+12,fy+164,"R restart / Esc exit",PAL_UI_TEXT);
            if(bk_sound)ui_text(fx+12,fy+188,"",PAL_UI_MUTED);
        }
    }
    ui_footer("Arrows move / Up Z rotate / Space hard drop / C hold / P pause / R restart");
}

/* ---------- 主循环 ---------- */
int main(void)
{
    if(ui_open("SandBlocks / SandCore ext")<0)return 1;
    ex_rand_seed((u32)sc_tick());
    char cfg[2048];
    bk_ghost=1;
    bk_sound=1;
    if(sc_read("HOME/BLOCKS.CFG",cfg,sizeof(cfg)-1)>=0){
        cfg[sizeof(cfg)-1]=0;
        char v[16];
        if(ex_cfg_get(cfg,"ghost",v,sizeof(v))==0)bk_ghost=!equal(v,"0");
        if(ex_cfg_get(cfg,"sound",v,sizeof(v))==0)bk_sound=!equal(v,"0");
        if(ex_cfg_get(cfg,"startlevel",v,sizeof(v))==0){
            int l;
            if(ex_parse_int(v,&l)==0)bk_start_level=ex_clamp(l,1,10);
        }
    }
    bk_scores_load();
    bk_new_game();
    for(;;){
        if(!ui_frame_due_interval(1))continue;      /* 100Hz 仿真+渲染兜底 */
        ui_pointer();
        /* 覆盖层状态下不做背景仿真 */
        if(bk_state==3){
            /* 录名页：独立小循环仍走统一帧管线 */
        }else bk_gravity();
        bk_draw();
        ui_present();
        int key=ui_action?-1:sc_key();
        if(key<0){exaudio_pump();continue;}
        if(key==27){
            if(bk_state==0&&!ui_confirm("Quit SandBlocks?","Progress will be lost."))
                continue;
            exaudio_shutdown();
            return 0;
        }
        if(bk_state==3){
            /* 录名：ASCII 追加/退格/Enter */
            int n=length(bk_name);
            if(key==10){bk_submit_score();}
            else if(key==8){if(n)bk_name[n-1]=0;ui_followup=1;}
            else if(key>=32&&key<127&&n<11){bk_name[n++]=(char)key;bk_name[n]=0;
                ui_followup=1;}
        }else if(bk_state==2){
            if(key=='r'||key=='R')bk_new_game();
        }else{
            if(key=='p'||key=='P'){bk_state=bk_state==1?0:1;ui_followup=1;}
            else if(key=='r'||key=='R')bk_new_game();
            else if(bk_state==1)continue;
            else if(key==0x82){if(bk_fits(bk_px-1,bk_py,bk_piece,bk_rot)){bk_px--;
                bk_lock_wait=0;}}
            else if(key==0x83){if(bk_fits(bk_px+1,bk_py,bk_piece,bk_rot)){bk_px++;
                bk_lock_wait=0;}}
            else if(key==0x80)bk_rotate(1);
            else if(key=='z'||key=='Z')bk_rotate(-1);
            else if(key==0x81)bk_drop(0);
            else if(key==' ')bk_drop(1);
            else if(key=='c'||key=='C')bk_hold_swap();
        }
        /* 结束瞬间：判断是否上榜并转入录名（一次性） */
        if(bk_state==2&&!bk_score_saved){
            int rank=-1;
            for(int i=0;i<5&&rank<0;i++)
                if((u32)bk_score>bk_hs_score[i])rank=i;
            if(rank>=0){
                bk_new_rank=rank;
                bk_name[0]=0;
                bk_state=3;
                ui_followup=1;
            }else bk_score_saved=1;
        }
        exaudio_pump();
    }
}
