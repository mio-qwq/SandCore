#ifndef SANDCORE_MODULE_H
#define SANDCORE_MODULE_H
#include "io.h"

/* mio：模块只依赖这个零环服务表，不把宿主 ELF 的外部符号当 ABI。
 * version/size 放最前，便于模块在注册回调之前检查自己的服务需求。
 * 回调代码与静态数据属于永久装载区；不热卸载，避免合成器悬空指针。
 * 此头给可信 SKM 使用；三环程序仍只有完全大写的 SCAPI.H。 */
typedef struct {
    u32 version,size;
    void (*pixel)(i32 x,i32 y,u8 color);
    void (*fill)(i32 x,i32 y,i32 w,i32 h,u8 color);
    void (*circle)(i32 x,i32 y,i32 radius,u8 color,int filled);
    int (*wallpaper)(void);
    void (*scene)(void (*paint)(void));
} module_api_t;
void modules_init(void);                 /* CORE 先于 MOD；文件系统初始化后 */
int modules_paint(void);                 /* 1 已用模块场景，0 需要内核兜底 */
#define MODULE_BASE 0x01800000u
#define MODULE_BYTES 0x00100000u
#endif
