#ifndef SANDCORE_IMAGE_SERVICE_H
#define SANDCORE_IMAGE_SERVICE_H
#include "image.h"

/* mio：压缩图片代理只管票据、身份、像素所有权；不在零环解析压缩流。
 * 每个请求者最多一个作业，owner由陷入入口给出，三环不能指定他人。
 * 票据是独立递增的31位编号，另保存任务代数，PID复用不继承权限。
 * result为新32B快照：version,width,height,bytes,format,ticket,0,0。
 * format=2表示直通ARGB32；0成功、1等待，负数失败。只查快照不会
 * 消耗结果；实际完整复制才释放缓存。take仅任务0接收壁纸所有权。
 * 以下接口默认IF=0；内部大拷贝短暂开IRQ但固定调用任务与CR3，
 * 返回恢复IF=0。poll只由主循环调用，统一启动受保护的三环服务。
 */
int image_service_request(int owner,const char *path);
int image_service_result(int owner,u32 ticket,u32 *info,void *pixels,u32 capacity);
int image_service_claim(int worker,u32 ticket,char *path,u32 capacity);
int image_service_submit(int worker,u32 ticket,u32 width,u32 height,u32 address,int status);
int image_service_cancel(int owner,u32 ticket);
int image_service_take(u32 ticket,image_surface_t *out);
void image_service_poll(void);
void image_service_owner_stopped(int owner);
#endif
