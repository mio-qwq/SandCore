/* mio：DUNE RUN，逐帧透视赛车。道路不是预存图片：每条扫描线按
 * 消失点、弯道与里程重新投影，路肩/标线/障碍和车辆都由软件几何绘制。
 * 连续键控制加速/刹车/方向，离开道路会减速，撞车损失速度与里程奖励。
 * 一圈 12000 单位，记录最快圈到 HOME/RACE/RECORD。Esc 正常退出。
 */
#include "SCAPI.H"
#include "UI.inc"
static int speed,distance,steer,lap,crashes,lap_start,best,lap_time;
static int opponent_size[3],opponent_ground[3]; /* 只读诊断：实际投影尺寸/触地点 */
static int curvature(int z) { return ui_sin(z/14)*34/256; }
static void car(int x,int y,int size,int material)
{
    ui_rect(x-size/2-2,y+size/3,size+4,size/2,PAL_UI_INK);
    ui_triangle(x-size/2,y+size,x+size/2,y+size,x+size/3,y,material+4);
    ui_triangle(x-size/2,y+size,x-size/3,y,x+size/3,y,material+6);
    ui_rect(x-size/5,y+size/4,size*2/5,size/4,PAL_WATER+6);
    ui_span(x-size/3,y+size-2,size*2/3,PAL_UI_GOLD+7);
    ui_rect(x-size/2-2,y+size*2/3,3,size/3,PAL_UI_INK);
    ui_rect(x+size/2-1,y+size*2/3,3,size/3,PAL_UI_INK);
}
static void draw(void)
{
    ui_gradient(0,0,UI_W,63,PAL_UI_NIGHT);
    ui_circle(251,40,15,PAL_UI_GOLD+6,1);
    for(int x=0;x<UI_W;x++) {
        int height=7+ui_sin(x*5+70)*7/256+ui_sin(x*11)*4/256;
        ui_rect(x,58-height,1,height+6,PAL_EARTH+2);
    }
    for(int y=63;y<154;y++) {
        int t=y-62,half=5+t*3/2,z=distance+7000/(t+2);
        int center=159+curvature(z)*t/70-steer*t/95;
        int stripe=(z/180)&1;
        ui_span(0,y,UI_W,PAL_UI_GOLD+(stripe?3:4));
        ui_span(center-half-5,y,2*half+10,stripe?PAL_UI_GOLD+7:PAL_EARTH+4);
        ui_span(center-half,y,2*half,PAL_ROCK+(stripe?1:2));
        if((z/100)&1) {
            int mark=1+t/24;
            ui_span(center-half/3,y,mark,PAL_ROCK+6);
            ui_span(center+half/3,y,mark,PAL_ROCK+6);
        }
    }
    /* mio：M8修复近车不变大。旧式4+700/(ahead+90)在大部分赛道
     * 都被整数除法压到4px，车身大小与道路用了两套焦距；而且y是
     * 车顶，放大后轮胎会浮在道路上。现在用道路相同的7000焦距和
     * near=80：t=f/(距离+near)，触地点=62+t，宽=22*t/86。
     * 与玩家车轮触地148的t=86同标尺，因此接近玩家时确实长到22px。
     * 先远后近绘制，较近车自然遮住远车；道路/车道/车身共享t。
     * 最远2px只是可辨识下限，近车不是再叠一个固定尺寸“图标”。 */
    int drawn=0;
    for(int rank=0;rank<3;rank++) {
        int chosen=-1,ahead=-1;
        for(int i=0;i<3;i++)if(!(drawn&(1<<i))) {
            int d=(i*1400+2800-distance%4200+4200)%4200;
            if(d>ahead){ahead=d;chosen=i;}
        }
        drawn|=1<<chosen;
        int t=7000/(ahead+80),ground=62+t,size=22*t/86;
        if(size<2)size=2;
        opponent_size[chosen]=size;opponent_ground[chosen]=ground;
        int lane=(chosen-1)*43,x=159+curvature(distance+ahead)*t/70+lane*t/80-steer*t/95;
        if(ground<154)car(x,ground-size,size,PAL_UI_CYAN);
    }
    car(159,126,22,PAL_EARTH);
    ui_panel(8,7,96,35);
    ui_text(14,13,"DUNE RUN",PAL_UI_TEXT);
    ui_number(14,27,speed*3,PAL_UI_GOLD+7);
    ui_text(54,27,"KM/H",PAL_UI_MUTED);
    ui_panel(226,7,84,35);
    ui_text(232,13,"LAP",PAL_UI_MUTED);
    ui_number(274,13,lap+1,PAL_UI_TEXT);
    ui_number(232,27,(ui_last_tick-lap_start)/100,PAL_UI_CYAN+7);
    ui_text(274,27,"SEC",PAL_UI_MUTED);
    ui_footer("arrows drive  R restart  ESC exit");
}
static void reset(void) { speed=distance=steer=lap=crashes=0;
    lap_start=sc_tick(); }
int main(void)
{
    if(ui_open("DUNE RUN / mio")<0) return 1;
    sc_mkdir("HOME/RACE");
    sc_read("HOME/RACE/RECORD",&best,sizeof(best));
    reset();
    for(;;) {
        int key=sc_key();
        if(key==27) return 0;
        if(key=='r'||key=='R') reset();
        if(!ui_frame_due()) continue;
        if(sc_down(0x80)||sc_down('w')) speed=ui_clamp(speed+2,0,90);
        else speed=ui_clamp(speed-1,0,90);
        if(sc_down(0x81)||sc_down('s')) speed=ui_clamp(speed-5,0,90);
        if(sc_down(0x82)||sc_down('a')) steer-=4;
        if(sc_down(0x83)||sc_down('d')) steer+=4;
        /* 弯道侧移小于每帧 4 单位的转向输入，玩家在最高速仍能
         * 主动修正车道；旧 600 分母会在最弯处盖过方向键力度。 */
        steer-=curvature(distance)*speed/1200;
        steer=ui_clamp(steer,-140,140);
        if(ui_abs(steer)>100) speed=speed*9/10;
        int before=distance;
        distance+=speed;
        for(int i=0;i<3;i++) {
            int ahead=(i*1400+2800-before%4200+4200)%4200;
            if(ahead<speed && ui_abs(steer-(i-1)*43)<20) { speed/=3;
                crashes++; }
        }
        if(distance>=12000) {
            distance-=12000;
            lap++;
            lap_time=ui_last_tick-lap_start;
            lap_start=ui_last_tick;
            if(!best || lap_time<best) { best=lap_time;
                sc_write("HOME/RACE/RECORD",&best,sizeof(best)); }
        }
        draw();
        ui_present();
    }
}
