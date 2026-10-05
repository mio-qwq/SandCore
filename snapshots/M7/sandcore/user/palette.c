/* mio：调色板查看与壁纸设置，ring3 不写 VGA DAC。
 * 槽位号来自 palette.h 的正式分配表，色块展示实际已生效的像素颜色。
 * 1/2/3 选择预设氛围，内核先保存 sys/wall.cfg 再切换画面；重启会恢复。
 * 用户得到明确 saved/failed 状态，不能把一次看起来变色当作落盘成功。 */
#include "api.h"
void main(void)
{
    int win=sc_open("Palette",290,164); if(win<0) return;
    page(win,"PALETTE / SandCore","1 dunes  2 night  3 sand  ESC close");
    /* 正式槽位恰好 1..42，14 列×3 行；此处的 i 是遍历既有槽位，
     * 不是新造裸色号。窗口只填调色板索引，不直接向 VGA DAC 端口写 RGB，
     * 因此 ring3 隔离与 GFX.md 的颜色权威始终保持一致。 */
    for(int i=PAL_SKY;i<=PAL_TASKBAR;i++) {
        int row=(i-1)/14,col=(i-1)%14;
        sc_fill(win,8+col*19,40+row*24,16,16,i);
    }
    sc_text(win,8,114,"registered slots 1..42",PAL_TITLE);
    for(;;) {
        int key=sc_key(); if(key==27) return;
        if(key>='1'&&key<='3') {
            /* UI 按键从 1 起，ABI 模式从 0 起，显式相减；返回成功表示
             * 内核保存与切换都完成，失败不可显示 saved。仅更新状态标签，
             * 色块表无需重画，因为三个预设复用同一套调色板槽位。 */
            int ok=sc_wallpaper(key-'1');
            sc_fill(win,200,8,78,18,PAL_CON_BG);
            sc_text(win,200,10,ok==0?"saved":"failed",ok==0?PAL_CON_TINT:PAL_CON_WARN);
        }
    }
}
