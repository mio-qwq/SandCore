#ifndef SANDCORE_IMAGE_H
#define SANDCORE_IMAGE_H
#include "io.h"

/* mio：先提供头部/长度校验，再提供分配；调用者不能从一张外来图片
 * 的宽高直接做乘法申请内存。offset允许SCX尾部复用同一个SCB2合同。
 * surface只持有连续物理页，释放必须交回这里，不能交给用户私有堆。
 * 本模块无通用压缩解码器；复杂格式后续由三环服务承担。 */
typedef struct {u32 width,height,format,bytes,body;} image_info_t;
typedef struct {u32 *pixels,width,height,pages;} image_surface_t;
int image_info(const char *path,u32 offset,u32 length,int icon,image_info_t *out);
int image_load(const char *path,u32 offset,u32 length,int icon,image_surface_t *out);
void image_release(image_surface_t *surface);
/* 校验完整SCX容器；成功输出36B原头，icon_offset/length为0表示无图标。
 * 不读取整个载荷，防止可选图标挤占262144B装载中转区。 */
int image_scx_info(const char *path,u8 *header,u32 *icon_offset,u32 *icon_length);
#endif
