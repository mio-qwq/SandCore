#ifndef SANDCORE_BOOT_SCENE_H
#define SANDCORE_BOOT_SCENE_H
/* mio：保护模式初始化完成后的原生欢迎页。只能在任务0固定生命周期
 * 的合成区调用；真实显示/主题/用户字体已就绪，VGA由原scene_draw
 * 路径承担。此页不建立窗口、不替换桌面图标、不新增登录或密码。 */
void boot_scene_draw(void);
#endif
