/* mio：场景库与NUI的薄适配层。独立SCENE.inc不包含NUI，不知道窗口
 * 或主题。圆盘是游戏HUD/棋子工具，才在这里用布局像素画进当前窗口。 */
#include "NUI.inc"
#include "SCENE.inc"
static void scn_disc(int cx,int cy,int radius,u32 color)
{
    if(radius<0||radius>4096)return;
    for(int y=-radius;y<=radius;y++){
        int width=scn_isqrt((u32)(radius*radius-y*y));
        ui_rect_rgb(cx-width,cy+y,2*width+1,1,color);
    }
}
