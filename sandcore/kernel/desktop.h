#ifndef SANDCORE_DESKTOP_H
#define SANDCORE_DESKTOP_H
#include "io.h"
void desktop_session_destroy(u32 id);
int desktop_session_prepare(u32 id);
void desktop_session_activate(u32 id);
/* 只推进可见桌面原始图片，task0每轮统一16KiB/两tick软预算。
 * 隐藏会话保留自己的候选并停止IO，切回继续；注销释放未完成页。 */
void desktop_poll(void);
/* mio：桌面和开始菜单的条目来自文件。内核只负责注册表的尺寸/语法
 * 与命中，不硬编码应用名称列表；command 可带 EXEC 已支持的参数。 */
typedef struct {char label[32],command[128],link[64],iconspec[64];int used;} desk_item_t;
int desktop_reload(void);
/* mio：只检查候选，不提交菜单/图标、不加载资源、不写任何文件。
 * kind=0菜单/1三行快捷方式，0成功、-1语法、-2读取。图标缺失
 * 仍允许显示轮廓回退，执行文件在启动时检查；保存前共用解析器。 */
int desktop_config_check(int kind,const char *path);
int desktop_count(int menu);
const desk_item_t *desktop_item(int menu,int index);
void desktop_icon(int x,int y,int size,const desk_item_t *item);
/* 在任务0合成阶段调用：读取主题壁纸并按屏幕比例居中裁剪。
 * 返回1表示已画图片，0表示调用方应继续原有程序壁纸。 */
int desktop_wallpaper(void);
#endif
