#ifndef SANDCORE_MEMORY_H
#define SANDCORE_MEMORY_H

#include "io.h"

/* 物理内存管理: E820 内存图解析 + 位图物理页分配器 (详见 docs/MEM.md) */

/* E820 条目 (引导器已规格化成 24 字节: 基址8/长8/类型8, 见 docs/BOOT.md) */
typedef struct {
    u32 base_lo, base_hi;
    u32 len_lo,  len_hi;
    u32 type_lo, type_hi;
} e820_entry_t;

#define E820_TYPE_USABLE   1    /* 可用 RAM */
#define E820_TYPE_RESERVED 2    /* 保留 */
#define E820_TYPE_ACPI     3    /* ACPI 可回收 */
#define E820_TYPE_NVS      4    /* ACPI NVS */
#define E820_TYPE_BAD      5    /* 坏内存 */

void memory_init(void);          /* 解析 E820, 初始化位图分配器 */

/* 物理页 (4KB) 分配: 返回物理地址, 0 = 没页了 */
u32  pframe_alloc(void);
void pframe_free(u32 addr);
/* 大帧缓冲须物理连续，才能经恒等映射线性绘制。先完整找到空闲游程
 * 再置位，失败不占半段；释放必须使用原地址/页数，不影响其他分配。 */
u32 pframe_alloc_run(u32 pages);
void pframe_free_run(u32 addr,u32 pages);
void pf_reserve(u32 addr, u32 size);   /* 预留区段: 位图标记已用 (任务槽等) */

/* 统计 (shell 的 mem 命令用), 单位: 字节 */
u32  memory_managed_bytes(void);
u32  memory_used_bytes(void);
u32  memory_free_bytes(void);

/* E820 原始条目访问 (mem 命令展示用) */
u32  e820_count(void);
e820_entry_t *e820_entry(u32 i);

#endif /* SANDCORE_MEMORY_H */
