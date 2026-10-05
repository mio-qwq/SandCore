#ifndef SANDCORE_DESKTOP_H
#define SANDCORE_DESKTOP_H
#include "io.h"
/* mio：桌面和开始菜单的条目来自文件。内核只负责注册表的尺寸/语法
 * 与命中，不硬编码应用名称列表；command 可带 EXEC 已支持的参数。 */
typedef struct {char label[32],command[128],link[64],iconspec[64];int used;} desk_item_t;
int desktop_reload(void);
int desktop_count(int menu);
const desk_item_t *desktop_item(int menu,int index);
void desktop_icon(int x,int y,int size,const desk_item_t *item);
/* 在任务0合成阶段调用：读取主题壁纸并按屏幕比例居中裁剪。
 * 返回1表示已画图片，0表示调用方应继续原有程序壁纸。 */
int desktop_wallpaper(void);
#endif
