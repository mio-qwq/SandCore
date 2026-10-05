/* mio：真实迁出的 CORE.SKM 桌面场景。函数/静态指针都位于模块映像，
 * 核心内核只保留引导期兜底与服务表。绘图仍使用注册的 palette 槽位。
 * SCX 的 ring3 API 不用于零环模块；这里用版本化、显式传入的服务表。 */
#include "../kernel/module.h"
#include "../kernel/palette.h"
static const module_api_t *services;
static int bump(int x,int center,int half,int height)
{
    int t=(x-center)*256/half;
    if(t < -256 || t>256) return 0;
    return height*(256-t*t/256)/256;
}
static void paint(void)
{
    int mode=services->wallpaper(),width=services->width(),height=services->height();
    for(int px=0;px<width;px++) {
        int x=px*320/width;
        int back=148-bump(x,90,150,18)-bump(x,235,120,12);
        int front=178-bump(x,160,220,10);
        for(int py=0;py<height;py++) {
            int y=py*200/height;
            int c,d;
            if(y<back) { d=y*14/145; if(d>14) d=14; c=PAL_SKY+d; }
            else if(y<front) { d=y-back; c=PAL_SAND_L+(d>42?7:d/6); }
            else { d=y-front; c=PAL_SAND_D+(d>42?7:d/6); }
            services->pixel(px,py,(u8)c);
        }
    }
    if(mode) for(int y=0;y<height;y++) services->fill(0,y,width,1,
        (u8)(mode==1?PAL_SKY+y*4/height:PAL_SAND_D+y*7/height));
    for(int i=0;i<48;i++) services->pixel(((i*97+13)%320)*width/320,((i*53+7)%22)*height/200,PAL_STAR);
    services->circle(282*width/320,106*height/200,14*height/200,PAL_SUN_EDGE,1);
    services->circle(282*width/320,106*height/200,9*height/200,PAL_SUN_CORE,1);
}
int module_init(const module_api_t *api)
{
    /* 先检查 ABI 再保存指针/注册回调；旧内核不能误执行新表尾字段。 */
    if(api->version!=1 || api->size<sizeof(module_api_t)) return -1;
    services=api; api->scene(paint); return 0;
}
