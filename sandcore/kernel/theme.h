#ifndef SANDCORE_THEME_H
#define SANDCORE_THEME_H
#include "io.h"
void theme_session_destroy(u32 id);
int theme_session_prepare(u32 id);

/* mio：主题是语义颜色和组件样式的有效快照，不是替换全局DAC。
 * 这样旧应用的沙丘/赛车材质不因换主题变色，新工具按角色重新取色。
 * ABI的角色顺序必须与SCAPI.H/THEME.md同步；信息固定32个u32。 */
#define TH_FACE 0
#define TH_FACE_ALT 1
#define TH_PAPER 2
#define TH_TEXT 3
#define TH_MUTED 4
#define TH_LINE 5
#define TH_ACCENT 6
#define TH_ACCENT_DEEP 7
#define TH_SELECT 8
#define TH_SELECT_TEXT 9
#define TH_SHADOW 10
#define TH_ALERT 11
#define TH_GOLD 12
#define TH_GREEN 13
#define TH_VIOLET 14
#define TH_ROSE 15
#define TH_HIGHLIGHT 16
#define TH_DARK_EDGE 17
#define TH_TITLE_ACTIVE 18
#define TH_TITLE_INACTIVE 19
#define TH_TITLE_TEXT 20
#define TH_TITLE_MUTED 21
#define TH_DESKTOP_TEXT 22
#define TH_DESKTOP_SHADOW 23
#define TH_ROLES 24

void theme_init(void);
int theme_load(const char *path,int action);
/* 候选纯语法检查，不改变代数/有效主题/磁盘，与加载共用解析器。 */
int theme_check(const char *path);
void theme_info(u32 *out);
int theme_path(int kind,char *out,u32 capacity);
u32 theme_color(int role);
int theme_classic(void);
int theme_radius(void);
#endif
