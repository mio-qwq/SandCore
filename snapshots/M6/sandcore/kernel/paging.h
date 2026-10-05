#ifndef SANDCORE_PAGING_H
#define SANDCORE_PAGING_H

#include "io.h"

/* 分页子系统 (M6) —— 规范见 docs/MEM.md §5.5
 *
 * 【v1 地址空间模型】
 *   0x000000-0x3FFFFF  内核区: 恒等映射, 所有任务共享 (PT#0)
 *   0x400000-0x7FFFFF  用户区: 每任务私有 (PT#1), 程序 org 0x400000
 *   0x800000-0xFFFFFFFF 恒等映射, 所有任务共享（含 RAM/MMIO 地址）
 *
 *  恒等映射为 supervisor；ring3 只能摸到本任务带 US 的私有页。
 *  换任务 = 换 CR3 (私有 PT#1)，内核公共映射保持不变；是否可分配
 *  RAM 仍以 E820 为准，不能因为某个 PTE 存在就分配不存在的物理内存。 */

#define PAGE_SIZE 4096u
#define USER_BASE 0x400000u
#define USER_TOP  0x800000u      /* 用户区末 (不含) */

void paging_init(void);          /* 建内核页目录, 开启 CR0.PG */
u32  paging_new_task_dir(void);  /* 建任务页目录: 拷共享项 + 私有用户 PT (全零) */
void paging_map_user(u32 pd, u32 vaddr, u32 paddr);   /* 用户区映射一页 (带 US|RW) */
void paging_switch(u32 pd);      /* 换 CR3 */
u32  paging_kernel_pd(void);     /* 内核页目录物理地址 (任务 0 用) */
void paging_free_task_dir(u32 pd); /* 只释放私有用户页与页表 */
int  paging_user_range(u32 pd, u32 ptr, u32 size); /* 确认每页都已映射 */

#endif /* SANDCORE_PAGING_H */
