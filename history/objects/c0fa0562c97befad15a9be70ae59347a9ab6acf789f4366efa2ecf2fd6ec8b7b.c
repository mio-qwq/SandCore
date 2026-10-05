/* mio：两份普通三环外层源码完全相同，IDE.inc分别为
 * 冻结旧Studio或当前草稿。仍调用其真实open_file/UTF-8光标/
 * draw/整帧提交，不抽取一个孤立索引函数冒充用户界面性能。
 * 强制连续完整重画只用于测成本，正式编辑器仍事件节流。 */
#define main studio_original_main
#include "IDE.inc"
#undef main
static volatile u32 bench_state,bench_loops,bench_start,bench_end;
int main(void)
{
    if(ui_open("Studio cost / mio")<0)return 1;
    char args[128];sc_args(args,sizeof(args));
    if(!open_file(args))return 2;
    cursor=used;ui_pointer();draw();ui_present();bench_state=1;
    for(;;){
        int key=sc_key();if(key==27)return 0;
        if(key=='r'&&bench_state!=2){bench_loops=0;bench_start=sc_tick();bench_state=2;}
        if(bench_state==2){
            draw();ui_present();bench_loops++;bench_end=sc_tick();
            if(bench_end-bench_start>=200)bench_state=3;
        }
        sc_yield();
    }
}
