/* mio：预生成方案固定搬运/FRAME/合成成本夹具。
 * 两张不同测试图案载入时生成，之后全尺寸交替，不能将静止帧的
 * 相同内容检测当成传输能力。图案不作为游戏/宣传片资源发布。 */
#include "SCAPI.H"
#include "NUI.inc"
int main(void){
    if(ui_open("Resource transfer budget / mio")<0)return 1;
    ui_pointer();ui_header("RESOURCE TRANSFER","Preparing two diagnostic patterns");ui_present();
    int width=ui_width,height=ui_height;u32 bytes=(u32)(width*height*4);
    u32 *a=(u32 *)sc_alloc(bytes),*b=(u32 *)sc_alloc(bytes);if(!a||!b)return 2;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        u32 color=(u32)((x*255/width)<<16)|(u32)((y*255/height)<<8)|(u32)((x+y)&255);
        a[y*width+x]=0xFF000000u|color;b[y*width+x]=0xFF000000u|(color^0x00FFFFFFu);
    }
    for(;;){
        if(sc_key()==27){sc_free(a);sc_free(b);return 0;}
        ui_pointer();if(ui_width!=width||ui_height!=height)return 3;
        ui_header("RESOURCE TRANSFER","Two test patterns / not a game frame");
        // header按NUI合同会先清整画布，必须在它之后搬运，否则两图
        // 均被背景覆盖，wm相同内容检测的67次假帧不能作为传输成本。
        sc_mem_copy(ui_pixels,(ui_frames&1)?a:b,bytes);
        ui_footer("Full canvas copy + FRAME32 + compositor / mio");ui_present();sc_yield();
    }
}
