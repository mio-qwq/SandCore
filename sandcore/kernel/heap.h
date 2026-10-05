#ifndef SANDCORE_HEAP_H
#define SANDCORE_HEAP_H

#include "io.h"

/* SC-Heap v1 —— SandCore 内核堆 (分级空闲链, 详见 docs/MEM.md) */

void  heap_init(void);               /* 从物理页分配器征用 1MB 当堆区 */
void *kmalloc(u32 size);             /* 分配, 返回 0 = 失败 (size>2048 或满了) */
void  kfree(void *p);                /* 归还 (p 必须是 kmalloc 的原返回值) */

u32   heap_total_bytes(void);
u32   heap_used_bytes(void);
u32   heap_blocks(void);

#endif /* SANDCORE_HEAP_H */
