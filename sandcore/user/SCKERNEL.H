#ifndef SCKERNEL_H
#define SCKERNEL_H
#include "SCAPI.H"
/* 与kernel/module.h只增服务表一致；此头仅可信SYSTEM编写的SKM使用。
 * 三环包含此结构不会获得任何服务指针，只有真实模块入口收到API。 */
typedef struct {
    u32 version,size;
    void (*pixel)(int,int,u8);void (*fill)(int,int,int,int,u8);void (*circle)(int,int,int,u8,int);
    int (*wallpaper)(void);void (*scene)(void (*)(void));int (*width)(void);int (*height)(void);
    void *(*allocate)(u32);void (*release)(void *,u32);u32 (*ticks)(void);
    int (*read)(const char *,void *,u32,u32);int (*write)(const char *,const u8 *,u32);
    int (*irq_register)(u32,void (*)(u32,void *),void *);
    int (*service_register)(void (*)(void *),void *,u32);
    int (*service_wake)(int);
    u8 (*port_read8)(u16);void (*port_write8)(u16,u8);
    u16 (*port_read16)(u16);void (*port_write16)(u16,u16);
    u32 (*port_read32)(u16);void (*port_write32)(u16,u32);
} sc_kernel_api;
#endif
