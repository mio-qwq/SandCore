/* =====================================================================
 *  SandCore 分页子系统 (kernel/paging.c)
 *  ---------------------------------------------------------------------
 *  【v1 模型】两级页表 (4KB 页)。内核页目录恒等映射全部物理内存
 *  (内核代码/栈/堆/显存/BSS 全在其中, 分页开启瞬间运行环境不变)。
 *  每个用户任务一份页目录: 拷贝内核的共享表项, 仅"用户区"表项
 *  (PT#1, 覆盖 0x400000-0x7FFFFF) 指向任务私有页表 —— 这样:
 *    * 所有程序都用 org 0x400000 链接, 装载时映射到各自物理帧;
 *    * 换任务 = 换 CR3 (私有区域随之切换);
 *    * 内核访问用户内存走恒等映射 (物理地址直读)。
 *  v1 信任模型: 用户物理帧同时被恒等映射, 应用之间没有硬隔离 —— M7 收紧。
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
    u32 *t = (u32 *)p;
    for (u32 i = 0; i < 1024; i++)
        t[i] = 0;
    return p;
}

void paging_init(void)
{
    /* 内核页目录: 全物理内存恒等映射 (按 E820 管理上限),
     * 共享页表放在 3MB-4MB 区间 (内核区, 恒等可达) */
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
    __asm__ __volatile__("mov %%cr0, %0" :: "r"(cr0));
    paging_on = 1;
}

u32 paging_kernel_pd(void)
{
    return (u32)kernel_pd;
}

void paging_switch(u32 pd)
{
    __asm__ __volatile__("mov %%cr3, %0" :: "r"(pd));
}

u32 paging_new_task_dir(void)
{
    u32 pd = pt_alloc();                   /* 页目录 (新页, 全零) */
    u32 user_pt = pt_alloc();              /* 私有用户区页表 */
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
