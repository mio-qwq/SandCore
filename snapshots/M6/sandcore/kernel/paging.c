/* =====================================================================
 *  SandCore 分页子系统 (kernel/paging.c)
 *  ---------------------------------------------------------------------
 *  【v1 模型】两级页表 (4KB 页)。内核页目录恒等映射完整 4GB 地址域
 *  (内核代码/栈/堆/显存/BSS 全在其中, 分页开启瞬间运行环境不变)。
 *  每个用户任务一份页目录: 拷贝内核的共享表项, 仅"用户区"表项
 *  (PT#1, 覆盖 0x400000-0x7FFFFF) 指向任务私有页表 —— 这样:
 *    * 所有程序都用 org 0x400000 链接, 装载时映射到各自物理帧;
 *    * 换任务 = 换 CR3 (私有区域随之切换);
 *    * 装载时经物理恒等映射写帧，系统调用则沿用当前 CR3 访问用户虚址。
 *  恒等映射表项不带 US：ring3 只能访问自己的 PT#1，不能借恒等别名
 *  读写内核或别的应用。内核仍能经物理地址访问任意用户帧。
 * ===================================================================== */
#include "io.h"
#include "paging.h"
#include "memory.h"

#define PT_PER_DIR 1024
#define PTE_P  0x001
#define PTE_RW 0x002
#define PTE_US 0x004

static u32 kernel_pd[PT_PER_DIR] __attribute__((aligned(4096)));
static int paging_on;

static u32 pt_alloc(void)         /* 从物理分配器拿一页做页表 (恒等可达) */
{
    u32 p = pframe_alloc();
    if (!p) return 0;
    u32 *t = (u32 *)p;
    for (u32 i = 0; i < 1024; i++)
        t[i] = 0;
    return p;
}

void paging_init(void)
{
    /* 1024 个目录项 × 1024 个 4KB 页 = 4GB，恒等映射同时覆盖 MMIO。
     * 映射存在不代表 RAM 存在；物理分配器仍只按 E820 可用项分配帧。
     * 1024 张共享页表占 3MB-7MB，main.c 已预留 3MB-8MB。 */
    u32 pt_region = 0x300000u;
    for (u32 i = 0; i < PT_PER_DIR; i++) {
        u32 pt = pt_region + i * PAGE_SIZE;
        kernel_pd[i] = pt | PTE_P | PTE_RW;
        u32 *t = (u32 *)pt;
        for (u32 e = 0; e < 1024; e++)
            t[e] = (i * 0x400000u + e * PAGE_SIZE) | PTE_P | PTE_RW;
    }
    paging_switch((u32)kernel_pd);

    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                    /* CR0.PG */
    __asm__ __volatile__("mov %0, %%cr0" :: "r"(cr0) : "memory");
    paging_on = 1;
}

u32 paging_kernel_pd(void)
{
    return (u32)kernel_pd;
}

void paging_switch(u32 pd)
{
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pd) : "memory");
}

u32 paging_new_task_dir(void)
{
    u32 pd = pt_alloc();                   /* 页目录 (新页, 全零) */
    if (!pd) return 0;
    u32 user_pt = pt_alloc();              /* 私有用户区页表 */
    if (!user_pt) { pframe_free(pd); return 0; }
    u32 *d = (u32 *)pd;
    for (u32 i = 0; i < PT_PER_DIR; i++)
        d[i] = kernel_pd[i];               /* 共享项照抄 (含恒等映射) */
    d[USER_BASE / 0x400000u] = user_pt | PTE_P | PTE_RW | PTE_US;
    return pd;
}

void paging_map_user(u32 pd, u32 vaddr, u32 paddr)
{
    u32 *d = (u32 *)pd;
    u32 pt = d[USER_BASE / 0x400000u] & ~0xFFFu;   /* 私有用户页表 */
    u32 idx = (vaddr - USER_BASE) / PAGE_SIZE;
    ((u32 *)pt)[idx] = paddr | PTE_P | PTE_RW | PTE_US;
}

int paging_user_range(u32 pd, u32 ptr, u32 size)
{
    /* 先用“剩余区间长度”比较 size，不能先求 ptr+size：恶意长度会使
     * 32 位加法回绕，从而把越界缓冲误判为合法。size=0 无需读任何页。
     * 后续首末页都算包含端点，确认所有跨越的 PTE 同时带 P 与 US。
     * syscall 在中断门内 IF=0，同任务不会在校验后突然卸掉这些页。 */
    if (ptr < USER_BASE || ptr >= USER_TOP || size > USER_TOP-ptr) return 0;
    if (!size) return 1;
    u32 *d = (u32 *)pd;
    u32 *pt = (u32 *)(d[1] & ~0xFFFu);
    for (u32 p = (ptr-USER_BASE)/PAGE_SIZE; p <= (ptr+size-1-USER_BASE)/PAGE_SIZE; p++)
        if ((pt[p] & (PTE_P|PTE_US)) != (PTE_P|PTE_US)) return 0;
    return 1;
}

void paging_free_task_dir(u32 pd)
{
    /* 共享内核 PT 的所有者是整个系统，绝不能跟着某个进程退出而释放。
     * 这里只遍历唯一私有的 PT#1：PTE 记录每个实际分配的物理帧，
     * 因而即使装载到一半失败，也能用同一个函数完整回滚已分配部分。
     * 调用方必须保证 pd 不再是当前执行地址空间；内核目录额外拒绝释放。 */
    if (!pd || pd == (u32)kernel_pd) return;
    u32 pt = ((u32 *)pd)[1] & ~0xFFFu;
    for (u32 i = 0; i < 1024; i++)
        if (((u32 *)pt)[i] & PTE_P) pframe_free(((u32 *)pt)[i] & ~0xFFFu);
    pframe_free(pt);
    pframe_free(pd);
}
