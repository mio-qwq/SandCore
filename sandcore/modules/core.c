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
    /* 地形与渐变本来就在320×200逻辑坐标上计算。按颜色连续段填充，
     * 保留每个原生像素的floor采样结果，避免1080p每次调用两百万次
     * pixel；fill会在内核先裁损伤区，任务栏更新也不用逐点遍历整屏。
     * ceil边界是floor(px*320/width)的逆区间，不降尺寸、不改色槽。 */
    if(!mode)for(int x=0;x<320;x++) {
        int left=(x*width+319)/320,right=((x+1)*width+319)/320;
        int back=148-bump(x,90,150,18)-bump(x,235,120,12);
        int front=178-bump(x,160,220,10);
        int first=0,previous=PAL_SKY;
        for(int y=1;y<=200;y++) {
            int c=-1,d;
            if(y<200){
                if(y<back) { d=y*14/145; if(d>14)d=14; c=PAL_SKY+d; }
                else if(y<front) { d=y-back; c=PAL_SAND_L+(d>42?7:d/6); }
                else { d=y-front; c=PAL_SAND_D+(d>42?7:d/6); }
            }
            if(c!=previous){
                int top=(first*height+199)/200,bottom=(y*height+199)/200;
                services->fill(left,top,right-left,bottom-top,(u8)previous);
                first=y;previous=c;
            }
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
