/* =====================================================================
 *  SandCore 内核堆 SC-Heap v1 (kernel/heap.c)
 *  ---------------------------------------------------------------------
 *  【设计】分级空闲链 + 区尾顺切, 全内核自己的方案:
 *    - 堆区 = 启动时从物理页分配器整取 1MB (256 帧, first-fit 下天然连续)
 *    - 8 个尺寸级: 16/32/64/128/256/512/1024/2048 字节
 *    - 每块头部 4 字节: bit31=在用, bit7..0=尺寸级号 (自由块头不填,
 *      归还信息全在调用方手里 —— kfree 的 p 必须是 kmalloc 原返回值)
 *    - 分配: 先看本级空闲链 (块体前 4 字节当 next 指针, 零元数据开销),
 *      没有就从区尾顺切一块新块
 *    - 释放: 头插回本级空闲链
 *  【v1 已知取舍】不合并相邻自由块 (碎片长期看会碎, M5 升级时加合并);
 *  单块上限 2048 字节 —— 内核里要大块就去 pframe_alloc 拿整页。
 * ===================================================================== */
#include "io.h"
#include "heap.h"
#include "memory.h"

#define HEAP_FRAMES 256               /* 1MB */
#define NCLASS      8

static const u32 class_size[NCLASS] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

static u8  *heap_base;                /* 堆区起点 (物理=线性, 无分页) */
static u32  heap_cursor;              /* 区尾顺切游标 (字节) */
static u32  heap_capacity;
static void *free_list[NCLASS];       /* 每级空闲链头 */
static u32   used_bytes, blocks;

static u32 size_to_class(u32 size)
{
    for (u32 i = 0; i < NCLASS; i++)
        if (size <= class_size[i])
            return i;
    return 0xFFFFFFFF;                /* 超大, v1 不伺候 */
}

void heap_init(void)
{
    /* 从物理分配器连续取 256 帧。first-fit + 此刻堆是第一个用户,
     * 拿到的必然是 [2MB, 3MB) 连续区 —— v1 依赖这个顺序 (docs/MEM.md) */
    u32 first = pframe_alloc();
    if (first == 0) {
        heap_base = 0;
        heap_capacity = 0;
        return;                       /* 内存枯竭的降级模式: 堆不可用 */
    }
    heap_base = (u8 *)first;
    heap_capacity = HEAP_FRAMES * 4096 - 0;   /* 含头部开销, 全算进容量 */
    heap_cursor = 0;
    for (u32 i = 1; i < HEAP_FRAMES; i++)
        pframe_alloc();               /* 把剩余 255 帧也占上 (占着不用也是占) */

    for (u32 i = 0; i < NCLASS; i++)
        free_list[i] = 0;
    used_bytes = 0;
    blocks = 0;
}

void *kmalloc(u32 size)
{
    if (heap_base == 0 || size == 0 || size > 2048)
        return 0;

    u32 cls = size_to_class(size);
    u32 csz = class_size[cls];
    u8 *blk;

    if (free_list[cls]) {
        blk = (u8 *)free_list[cls];               /* 从空闲链摘 */
        free_list[cls] = *(void **)blk;
    } else {
        if (heap_cursor + csz > heap_capacity)
            return 0;                             /* 堆区切满了 */
        blk = heap_base + heap_cursor;
        heap_cursor += csz;
    }

    blk[0] = (u8)(cls | 0x80);                    /* 块头: bit7=在用 + 级号 */
    blocks++;
    used_bytes += csz;
    return blk + 4;                               /* 跳过 4 字节头 */
}

void kfree(void *p)
{
    if (p == 0)
        return;
    u8 *blk = (u8 *)p - 4;
    u32 cls = blk[0] & 0x7F;                      /* bit7 是在用位 */
    if (cls >= NCLASS)
        return;                                   /* 野指针, 无视 */

    *(void **)blk = free_list[cls];               /* 头插回空闲链 */
    free_list[cls] = blk;
    blocks--;
    used_bytes -= class_size[cls];
}

u32 heap_total_bytes(void) { return heap_capacity; }
u32 heap_used_bytes(void)   { return used_bytes; }
u32 heap_blocks(void)       { return blocks; }
