/* =====================================================================
 * mio：Mines，8×6 扫雷，完整状态在 ring3 私有页内。
 * 开局首次揭开后才布雷，并排除首格；随机种子来自 tick 的整数 LCG。
 * 邻格零雷时用有限队列扩展，visited 在入队时置位，队列最多 48 项。
 * 键盘方向/空格/标旗与鼠标左键同用 reveal，避免两套胜负规则分叉。
 * GUI 点击先校验窗口聚焦，再由 WININFO 转换坐标；后台不能点击穿透。
 * ===================================================================== */
#include "SCAPI.H"
#include "SCENENUI.inc"
#define CELLS 48
/* 状态拆成三张表：雷的真值、已揭状态、用户旗标。旗标不是自动确认雷，
 * 不应写进 bomb，否则标错旗会改变题目。finished=0 进行中、1 失败、
 * 2 胜利；revealed 只计安全格，胜利条件恒为 48-8=40。
 * selected 是行优先索引 y*8+x，键盘与鼠标都只更新它并复用 reveal。 */
static u8 bomb[CELLS],visible[CELLS],flag[CELLS];
static int selected,started,finished,revealed;
static int board_x,board_y,cell_size,flag_mode,game_start,finish_time;
static char mines_status[64]="First reveal is safe";
static u32 seed;
/* 边缘/角落只遍历有效邻格，不能把上一行末尾当作下一行左邻。
 * 循环包含中心格，但只在已知安全格显示邻雷数时使用此结果；安全格
 * 自身 bomb=0。踩雷格直接画 '*'，不拿包含自身的计数作数字显示。 */
static int near_count(int cell)
{
    int x=cell%8,y=cell/8,n=0;
    for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
        int px=x+dx,py=y+dy;
        if(px>=0&&px<8&&py>=0&&py<6) n+=bomb[py*8+px]!=0;
    }
    return n;
}
/* 第一次实际揭开才生成八颗雷；标旗/移动不触发布雷，因此首个有效
 * 揭格可作为 safe 排除。重复抽到已放雷的格只重试，不减少目标雷数。
 * u32 LCG 的自然溢出就是模 2^32，不能改成有符号乘法；tick 只用于
 * 小游戏变化种子，没有密码学随机性的宣称，也没有浮点依赖。 */
static void place(int safe)
{
    int n=0; seed=(u32)sc_tick()+1;
    while(n<8) {
        seed=seed*1664525u+1013904223u; int cell=(int)((seed>>8)%CELLS);
        if(cell!=safe && !bomb[cell]) { bomb[cell]=1; n++; }
    }
    started=1;game_start=sc_tick();
}
/* 上层保证 cell 在 0..47。已结束/已揭/旗标格全部幂等返回，防止长按
 * 或重复事件多次增加 revealed。踩雷立即结束；安全格走广度优先扩展。
 * 关键不变量：visible 在入队那一刻置位，而不是出队才置位。这样两个
 * 相邻零格不会把同一邻格重复入队，每个格最多一次，queue[48] 足够。
 * 非零数字格进入队列后只计数/显示，不再向外扩展；旗标格不会自动揭开。 */
static void reveal(int cell)
{
    if(finished || flag[cell] || visible[cell]) return;
    if(!started) place(cell);
    if(bomb[cell]) { finished=1;finish_time=sc_tick(); visible[cell]=1; return; }
    u8 queue[CELLS]; int head=0,tail=0;
    queue[tail++]=(u8)cell; visible[cell]=1; revealed++;
    while(head<tail) {
        int c=queue[head++];
        if(near_count(c)) continue;
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
            int x=c%8+dx,y=c/8+dy;
            if(x<0||x>=8||y<0||y>=6) continue;
            int at=y*8+x;
            if(!bomb[at] && !visible[at] && !flag[at]) {
                visible[at]=1; revealed++; queue[tail++]=(u8)at;
            }
        }
    }
    if(revealed==CELLS-8){finished=2;finish_time=sc_tick();}
}
/* mio：棋盘是真彩实体表面，逐物理像素计算圆角、倒角光照和细颗粒。
 * 控件遵循主题，场景材质取SCENE命名端点；Classic仍保留方角风格。
 * 分辨率改变时直接重算几何，不把旧320像素截图拉大。 */
static void tile(int x,int y,int size,int opened,int focused)
{
    int px=ui_px(x),py=ui_px(y),right=ui_px(x+size-2),bottom=ui_px(y+size-2);
    int w=right-px,h=bottom-py;if(w<2||h<2)return;
    int radius=ui_classic?0:w/10,edge=w/12;if(edge<1)edge=1;
    ui_round_rgb(x+1,y+2,size-2,size-2,ui_classic?0:size/10,scn_mix(SCN_SHADE,ui_role(SC_THEME_FACE),160));
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
        int dx=xx<radius?radius-xx:xx>=w-radius?xx-(w-radius-1):0;
        int dy=yy<radius?radius-yy:yy>=h-radius?yy-(h-radius-1):0;
        if(dx*dx+dy*dy>radius*radius)continue;
        int shade=252;
        if(xx<edge)shade+=20*(edge-xx)/edge;
        if(yy<edge)shade+=26*(edge-yy)/edge;
        if(xx>=w-edge)shade-=36*(xx-w+edge+1)/edge;
        if(yy>=h-edge)shade-=44*(yy-h+edge+1)/edge;
        if(opened)shade=234+(yy*14/h);
        shade+=(int)(scn_hash(xx,0,yy)&3)-1;
        u32 base=opened?scn_mix(SCN_CERAMIC,ui_role(SC_THEME_PAPER),176):ui_role(SC_THEME_FACE);
        if(focused)base=scn_mix(base,ui_role(SC_THEME_ACCENT),55);
        int ox=px+xx,oy=py+yy;
        if(ox>=0&&ox<ui_width&&oy>=0&&oy<ui_height)
            ui_pixels[oy*ui_width+ox]=0xFF000000u|scn_light(base,shade);
    }
}
static void draw_mine(int x,int y,int size)
{
    int radius=size/5;if(radius<3)radius=3;
    int cx=x+(size-2)/2,cy=y+(size-2)/2;
    for(int a=0;a<8;a++){
        int dx=scn_cos(a*128),dy=scn_sin(a*128);
        ui_line(cx+dx*radius/256,cy+dy*radius/256,cx+dx*(radius+size/9)/256,cy+dy*(radius+size/9)/256,PAL_UI_MUTED);
    }
    scn_disc(cx,cy,radius,SCN_RUBBER);
    scn_disc(cx-radius/3,cy-radius/3,radius/2,SCN_METAL);
    scn_disc(cx-radius/3,cy-radius/3,radius/5+1,SCN_IVORY);
}
static void draw_flag(int x,int y,int size)
{
    int cx=x+size/2,top=y+size/5,height=size*3/5;
    ui_rect_rgb(cx-1,top,2,height,SCN_METAL);
    for(int row=0;row<height/2;row++)
        ui_rect_rgb(cx,top+row,height/2-row/2,1,scn_light(SCN_PAINT,272-row*30/(height+1)));
    ui_rect_rgb(cx-size/6,y+size*4/5,size/3,2,SCN_RUBBER);
}
static void reset(void)
{
    for(int i=0;i<CELLS;i++)bomb[i]=visible[i]=flag[i]=0;
    selected=started=finished=revealed=flag_mode=0;game_start=finish_time=0;
    copy(mines_status,"First reveal is safe",sizeof(mines_status));ui_followup=1;
}
static void flag_selected(void)
{if(!finished&&!visible[selected])flag[selected]^=1;ui_followup=1;}
static void draw(void)
{
    ui_header("Mines","Eight hidden mines / porcelain and brushed metal");
    if(UI_W<260||UI_H<172){
        cell_size=0;ui_text(16,36,"Enlarge to play",PAL_UI_MUTED);
        ui_small_control(98,16,60,112,"Enlarge",0);return;
    }
    board_y=ui_compact?34:76;
    int available=UI_H-(ui_compact?22:30)-board_y-6;
    cell_size=available/6;int maxw=(UI_W-136)/8;if(cell_size>maxw)cell_size=maxw;
    if(cell_size>100)cell_size=100;board_x=16;
    int sx=board_x+8*cell_size+12,sw=UI_W-sx-16;
    ui_round_rgb(board_x-4,board_y-4,cell_size*8+6,cell_size*6+7,ui_classic?0:8,ui_role(SC_THEME_FACE_ALT));
    for(int i=0;i<CELLS;i++){
        int x=board_x+(i%8)*cell_size,y=board_y+(i/8)*cell_size;
        tile(x,y,cell_size,visible[i],i==selected);
        if(visible[i]||(finished==1&&bomb[i])){
            if(bomb[i])draw_mine(x,y,cell_size);
            else{
                int count=near_count(i);
                if(count){
                    int role=count==1?SC_THEME_ACCENT:count==2?SC_THEME_GREEN:count==3?SC_THEME_ALERT:SC_THEME_VIOLET;
                    u32 old=ui_colors[PAL_UI_TEXT];ui_colors[PAL_UI_TEXT]=ui_role(role);
                    ui_number(x+(cell_size-8)/2-1,y+(cell_size-16)/2-1,count,PAL_UI_TEXT);ui_colors[PAL_UI_TEXT]=old;
                }
            }
        }else if(flag[i])draw_flag(x,y,cell_size);
    }
    /* 紧凑窗口把操作放在棋盘侧面，六行棋格仍有完整16像素字高。
     * 不在172单位高度里堆叠第二排工具条，从而保留整局可见。 */
    ui_small_control(1,sx,board_y,sw,"Reveal",0);
    ui_small_control(2,sx,board_y+26,sw,"Flag",flag_mode);
    ui_small_control(3,sx,board_y+52,sw,"New",0);
    char value[64],number[16];decimal(number,revealed);copy(value,number,sizeof(value));append(value," / 40",sizeof(value));
    ui_text(sx,board_y+80,value,PAL_UI_TEXT);
    if(!ui_compact){
        int flags=0;for(int i=0;i<CELLS;i++)flags+=flag[i]!=0;
        decimal(number,8-flags);copy(value,"Flags left ",sizeof(value));append(value,number,sizeof(value));
        ui_text(sx,board_y+110,value,PAL_UI_MUTED);
        decimal(number,started?((finished?finish_time:sc_tick())-game_start)/100:0);
        ui_text(sx,board_y+136,number,PAL_UI_TEXT);
        ui_text(sx,board_y+156,"seconds",PAL_UI_MUTED);
        ui_clip_set(sx,board_y+190,sw,UI_H-board_y-224);
        ui_text(sx,board_y+190,"Right click: flag\nArrows: select\nSpace: reveal\nF: flag / R: new",PAL_UI_MUTED);ui_clip_clear();
    }
    ui_footer(finished==1?"Mine found / R or New restarts":finished==2?"All forty safe cells cleared":flag_mode?"Flag mode / click a tile":"First reveal safe / right click flags");
}
int main(void)
{
    if(ui_open("Mines / mio")<0)return 1;reset();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        int action=ui_action,key=action?-1:sc_key();
        if(action==98){sc_window(ui_win,1);ui_followup=1;continue;}
        if(action==1)reveal(selected);
        if(action==2)flag_mode=!flag_mode;
        if(action==3)reset();
        if(!action&&cell_size&&(ui_pressed&3)&&ui_hit(board_x,board_y,cell_size*8,cell_size*6)){
            selected=((ui_y-board_y)/cell_size)*8+(ui_x-board_x)/cell_size;
            if((ui_pressed&2)||flag_mode)flag_selected();else reveal(selected);
            ui_followup=1;
        }
        if(key==27)return 0;
        if(key==0x80&&selected>=8)selected-=8;
        if(key==0x81&&selected<40)selected+=8;
        if(key==0x82&&selected%8)selected--;
        if(key==0x83&&selected%8!=7)selected++;
        if(key==' ')reveal(selected);
        if(key=='f'||key=='F')flag_selected();
        if(key=='r'||key=='R')reset();
        if(action||key>=0)ui_followup=1;
    }
}
