/* =====================================================================
 *  SandCore 物理内存管理 (kernel/memory.c)
 *  ---------------------------------------------------------------------
 *  【内存版图】(详见 docs/MEM.md)
 *    内核/栈/显存/E820 表 全在低 1MB 的约定位置 (docs/BOOT.md);
 *    位图分配器只管 [2MB, min(最大可用上界, 256MB)) 这段,
 *    2MB 以下全部视为"内核保留", 一律不分配 —— 简单且足够 M2 玩。
 *
 *  【分配器: PF-Bitmap v1】
 *    每 4KB 物理页 = 位图里的 1 位, 0 空闲 / 1 已用;
 *    分配 = 从头找第一个 0 位 (first-fit), 释放 = 清位。
 *    位图本体放在内核 .bss (8KB, 支持到 256MB)。
 *    设计取舍: 慢于伙伴系统但代码 100 行内, M5 再谈进化。
 * ===================================================================== */
#include "io.h"
#include "memory.h"

#define E820_COUNT_ADDR (*(volatile u32 *)0x7E00)
#define E820_ENTRIES    ((volatile e820_entry_t *)0x7E04)
#define E820_MAX        32

/* 分配器管辖范围 */
#define PF_BASE   0x200000u              /* 2MB 起步, 避开内核全部家当 */
#define PF_CAP    0x10000000u            /* 管理上限 256MB */
#define PAGE_SIZE 4096u

#define PF_MAX_FRAMES (PF_CAP / PAGE_SIZE)     /* 65536 帧 */
static u8 pf_bitmap[PF_MAX_FRAMES / 8];        /* 8KB, .bss 清零即全空闲 */

static u32 pf_frames;                          /* 实际管辖帧数 */
static u32 pf_used;                            /* 已分配帧数 */

/* ---------------- E820 ---------------- */
u32 e820_count(void)            { return E820_COUNT_ADDR; }
e820_entry_t *e820_entry(u32 i) { return (e820_entry_t *)&E820_ENTRIES[i]; }

/* 条目是否可用且完全在 4GB 内 (M2 只做 32 位世界的账) */
static int entry_usable_32(e820_entry_t *e)
{
    return e->type_hi == 0 && e->type_lo == E820_TYPE_USABLE
        && e->base_hi == 0 && e->len_hi == 0;
}

void memory_init(void)
{
    /* 找到包含 2MB 的那段可用 RAM, 取它的上界当管辖终点。
     * QEMU 的 E820 在 1MB 以上就是一大块连续可用区, 这个
     * 简化假设在我们的世界里成立 (docs/MEM.md 有论证)。 */
    u32 top = 0;
    for (u32 i = 0; i < e820_count(); i++) {
        e820_entry_t *e = e820_entry(i);
        if (!entry_usable_32(e))
            continue;
        u32 lo = e->base_lo;
        u32 hi = e->base_lo + e->len_lo;      /* 可能回绕, 但 4GB 内的可用区不会 */
        if (lo <= PF_BASE && hi > top)
            top = hi;
    }
    if (top > PF_CAP)
        top = PF_CAP;
    pf_frames = (top > PF_BASE) ? (top - PF_BASE) / PAGE_SIZE : 0;

    for (u32 i = 0; i < PF_MAX_FRAMES / 8; i++)
        pf_bitmap[i] = 0;                     /* .bss 已清零, 这里是自我说明 */
    pf_used = 0;
}

/* ---------------- 位图物理页分配 ---------------- */
u32 pframe_alloc(void)
{
    for (u32 i = 0; i < pf_frames; i++) {
        u8 mask = pf_bitmap[i >> 3];
        if (mask == 0xFF)
            continue;                         /* 整字节全忙, 快速跳过 */
        u32 bit = i & 7;
        if (mask & (1u << bit))
            continue;
        pf_bitmap[i >> 3] = mask | (1u << bit);
        pf_used++;
        return PF_BASE + i * PAGE_SIZE;
    }
    return 0;                                 /* 没页了 */
}

void pframe_free(u32 addr)
{
    if (addr < PF_BASE || addr >= PF_BASE + pf_frames * PAGE_SIZE)
        return;                               /* 越界帧不归我管, 无视 */
    u32 i = (addr - PF_BASE) / PAGE_SIZE;
    if(pf_bitmap[i>>3] & (1u<<(i&7))) {
        pf_bitmap[i >> 3] &= ~(1u << (i & 7));
        pf_used--;
    }
}

u32 pframe_alloc_run(u32 pages)
{
    if(!pages || pages>pf_frames) return 0;
    u32 run=0;
    for(u32 i=0;i<pf_frames;i++) {
        if(pf_bitmap[i>>3]&(1u<<(i&7))) run=0;
        else if(++run==pages) {
            u32 first=i+1-pages;
            /* 同一内核临界区内分配；扫描期间不提前占位，因而 OOM
             * 不会泄漏未完成的窗口。每页仍计入统一 pf_used 统计。 */
            for(u32 j=first;j<=i;j++) pf_bitmap[j>>3]|=(1u<<(j&7));
            pf_used+=pages;
            return PF_BASE+first*PAGE_SIZE;
        }
    }
    return 0;
}
void pframe_free_run(u32 addr,u32 pages)
{
    if((addr&4095) || addr<PF_BASE || addr>=PF_BASE+pf_frames*4096
       || pages>(PF_BASE+pf_frames*4096-addr)/4096) return;
    for(u32 i=0;i<pages;i++) pframe_free(addr+i*4096);
}

void pf_reserve(u32 addr, u32 size)   /* 预留区段: 位图标记已用 (任务槽等) */
{
    if (addr < PF_BASE)
        return;
    u32 start = (addr - PF_BASE) / PAGE_SIZE;
    u32 end = start + (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (end > pf_frames)
        end = pf_frames;
    for (u32 i = start; i < end; i++) {
        if (!(pf_bitmap[i >> 3] & (1u << (i & 7)))) {
            pf_bitmap[i >> 3] |= (1u << (i & 7));
            pf_used++;
        }
    }
}

/* ---------------- 统计 ---------------- */
u32 memory_managed_bytes(void) { return pf_frames * PAGE_SIZE; }
u32 memory_used_bytes(void)    { return pf_used * PAGE_SIZE; }
u32 memory_free_bytes(void)    { return (pf_frames - pf_used) * PAGE_SIZE; }
