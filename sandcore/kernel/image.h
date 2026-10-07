#ifndef SANDCORE_IMAGE_H
#define SANDCORE_IMAGE_H
#include "io.h"

/* mio：先提供头部/长度校验，再提供分配；调用者不能从一张外来图片
 * 的宽高直接做乘法申请内存。offset允许SCX尾部复用同一个SCB2合同。
 * surface只持有连续物理页，释放必须交回这里，不能交给用户私有堆。
 * 本模块无通用压缩解码器；复杂格式后续由三环服务承担。 */
typedef struct {u32 width,height,format,bytes,body;} image_info_t;
typedef struct {u32 *pixels,width,height,pages;} image_surface_t;
/* 桌面原始资源按小批次读盘。候选在完整验证前不交给合成器；
 * metadata保存对象代数/权限，批次间替换、删改或撤权均拒绝旧候选。
 * 调用者拥有状态与候选页，隐藏时暂停，取消/退出必须显式释放。 */
typedef struct {
    image_info_t info;
    image_surface_t candidate;
    u32 metadata[8],offset,length,position,background,prepared_pixels;
    int phase,icon,flatten;
    char path[64];
} image_reader_t;
int image_reader_begin(image_reader_t *reader,const char *path,u32 offset,u32 length,int icon);
/* 调用者在task0固定调用栈/CR3并开IRQ的保护内推进；最多读取budget
 * 字节，且每512B检查两tick软预算。1等待、0完整交付、-1安全失败。 */
int image_reader_step(image_reader_t *reader,u32 budget,image_surface_t *out);
void image_reader_cancel(image_reader_t *reader);
int image_info(const char *path,u32 offset,u32 length,int icon,image_info_t *out);
int image_load(const char *path,u32 offset,u32 length,int icon,image_surface_t *out);
void image_release(image_surface_t *surface);
/* 校验完整SCX容器；成功输出36B原头，icon_offset/length为0表示无图标。
 * 不读取整个载荷，防止可选图标挤占262144B装载中转区。 */
int image_scx_info(const char *path,u8 *header,u32 *icon_offset,u32 *icon_length);
#endif
