/* mio：两份探针使用完全同一源码，只有私有NUI片段不同。
 * 不渲染文字/菜单，避免其它布局改动影响填色成本；两个颜色取
 * 既有主题角色，每帧完整覆盖同一实际客户区，并走相同FRAME32。
 * 首帧/分配结束才置ready，不能把初始化算入其中一种实现的耗时。 */
#include "SCAPI.H"
#include "NUI.inc"
static volatile u32 bench_state,bench_loops,bench_start,bench_end,bench_last;
int main(void){
    if(ui_open("NUI row cost / mio")<0)return 1;
    ui_physical_rgb(0,0,ui_width,ui_height,ui_role(SC_THEME_PAPER));ui_present();bench_state=1;
    for(;;){
        int key=sc_key();if(key==27)return 0;
        /* 使用客体PIT计时，不把宿主发送命令/截屏时间当CPU吞吐。
         * 完成最后一整帧后再停止，报告真实超出的tick而非固定填200。 */
        if(key=='r'&&bench_state!=2){bench_loops=0;bench_start=sc_tick();bench_state=2;}
        if(bench_state==2){
            bench_last=ui_role((bench_loops&1)?SC_THEME_FACE:SC_THEME_PAPER);
            ui_physical_rgb(0,0,ui_width,ui_height,bench_last);ui_present();bench_loops++;
            bench_end=sc_tick();if(bench_end-bench_start>=200)bench_state=3;
        }
        sc_yield();
    }
}
