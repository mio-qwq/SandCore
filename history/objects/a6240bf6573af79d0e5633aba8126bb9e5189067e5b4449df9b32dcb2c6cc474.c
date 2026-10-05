/* mio：真实G2目标，pulse每次改变私有计数便于核对运行/
 * 暂停。专属调试目标不会从宿主改状态或代码，断点仅由DEBUG API。
 * main实际开窗口并写文件，证明Continue已经真正执行应用代码。 */
#include "SCAPI.H"
volatile u32 target_counter;
void pulse(void){target_counter++;}
int main(void)
{
    int w=sc_open("Debug target / mio",240,100);if(w<0)return 1;
    sc_text(w,8,12,"True CPU target / mio",PAL_UI_TEXT);
    sc_write("HOME/DEBUG.OK","LIVE",4);
    for(;;){if(sc_key()==27)return 23;pulse();sc_yield();}
}
