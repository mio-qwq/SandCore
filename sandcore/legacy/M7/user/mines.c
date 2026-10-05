/* =====================================================================
 * mio：Mines，8×6 扫雷，完整状态在 ring3 私有页内。
 * 开局首次揭开后才布雷，并排除首格；随机种子来自 tick 的整数 LCG。
 * 邻格零雷时用有限队列扩展，visited 在入队时置位，队列最多 48 项。
 * 键盘方向/空格/标旗与鼠标左键同用 reveal，避免两套胜负规则分叉。
 * GUI 点击先校验窗口聚焦，再由 WININFO 转换坐标；后台不能点击穿透。
 * ===================================================================== */
#include "api.h"
#define CELLS 48
/* 状态拆成三张表：雷的真值、已揭状态、用户旗标。旗标不是自动确认雷，
 * 不应写进 bomb，否则标错旗会改变题目。finished=0 进行中、1 失败、
 * 2 胜利；revealed 只计安全格，胜利条件恒为 48-8=40。
 * selected 是行优先索引 y*8+x，键盘与鼠标都只更新它并复用 reveal。 */
static u8 bomb[CELLS],visible[CELLS],flag[CELLS];
static int win,selected,started,finished,revealed;
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
    started=1;
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
    if(bomb[cell]) { finished=1; visible[cell]=1; return; }
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
    if(revealed==CELLS-8) finished=2;
}
static void draw(void)
{
    page(win,"MINES / 8 x 6","arrows SPACE reveal F flag R reset");
    sc_text(win,180,10,finished==1?"BOOM":finished==2?"CLEAR":"8 mines",finished==1?PAL_CON_WARN:PAL_CON_TINT);
    for(int i=0;i<CELLS;i++) {
        int x=8+(i%8)*20,y=34+(i/8)*14;
        sc_fill(win,x,y,18,12,i==selected?PAL_CON_TINT:visible[i]?PAL_TASKBAR:PAL_WIN_TITLE);
        char glyph[2]={0,0};
        if(visible[i] || (finished==1&&bomb[i])) glyph[0]=bomb[i]?'*':near_count(i)?'0'+near_count(i):' ';
        else if(flag[i]) glyph[0]='F';
        sc_text(win,x+5,y+2,glyph,i==selected?PAL_CON_BG:PAL_TITLE);
    }
    sc_text(win,180,40,"first click",PAL_TITLE);
    sc_text(win,180,54,"is safe",PAL_CON_TINT);
    char number[16]; decimal(number,revealed);
    sc_text(win,180,82,number,PAL_TITLE); sc_text(win,180,96,"/ 40 clear",PAL_WIN_TITLE);
}
/* 清三张表和终局标志，保留窗口/画布对象。新局再次等首个有效揭格才
 * 布雷，因此 R 不复用旧雷阵，也不继承旗标；全局静态数据仍属本任务。 */
static void reset(void)
{
    for(int i=0;i<CELLS;i++) bomb[i]=visible[i]=flag[i]=0;
    selected=started=finished=revealed=0;
}
void main(void)
{
    win=sc_open("Mines",290,164); if(win<0) return;
    reset(); draw(); int previous=0;
    for(;;) {
        int key=sc_key(),dirty=0;
        if(key==27) return;
        if(key==0x80&&selected>=8) { selected-=8; dirty=1; }
        if(key==0x81&&selected<40) { selected+=8; dirty=1; }
        if(key==0x82&&selected%8) { selected--; dirty=1; }
        if(key==0x83&&selected%8!=7) { selected++; dirty=1; }
        if(key==' ') { reveal(selected); dirty=1; }
        if(key=='f'&&!visible[selected]&&!finished) { flag[selected]^=1; dirty=1; }
        if(key=='r') { reset(); dirty=1; }
        /* MOUSE 是全局按钮快照，previous 记录上一轮左键位，只在按下
         * 边沿揭一次，拖动时不会连续清格。WININFO 同时验证 owner/focus，
         * 后台游戏仍观察按钮变化，但不会响应覆盖在上面的另一个窗口。
         * WM 客户区起于外框 (1,13)，网格再偏移 (8,34)；须逐层减去，
         * 不能拿屏幕绝对坐标直接除格宽。先判非负与 160×84 再整数除法。 */
        int info[5]; int mouse=sc_mouse();
        int down=(mouse>>18)&1;
        if(down&&!previous&&sc_info(win,info)==0&&info[4]) {
            int x=(mouse&511)-info[0]-1-8;
            int y=((mouse>>9)&511)-info[1]-13-34;
            if(x>=0&&x<160&&y>=0&&y<84) {
                selected=(y/14)*8+x/20; reveal(selected); dirty=1;
            }
        }
        previous=down;
        if(dirty) draw();
    }
}
