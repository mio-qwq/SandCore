#ifndef SANDCORE_CODEC_RUNTIME_H
#define SANDCORE_CODEC_RUNTIME_H
#include "SCAPI.H"
#include <stddef.h>
/* mio：服务内部头，不是公开SCAPI。单次请求最多48MB真实页，
 * 小分配来自64KB以上的arena；退出由现有任务页回收统一兜底。 */
void codec_ticket(u32 ticket);
int codec_memory_failed(void);
void codec_release_all(void);
int codec_decode(const u8 *source,u32 bytes,u32 **pixels,int *width,int *height);
#endif
