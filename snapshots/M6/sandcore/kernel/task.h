#ifndef SANDCORE_TASK_H
#define SANDCORE_TASK_H

#include "io.h"

/* 任务/调度子系统 v2 (M6) —— 规范见 docs/MEM.md 与 docs/SYSCALL.md
 * v2: 分页上线, 每任务独立页目录 (用户区 0x400000 私有),
 * 多个用户程序可并发; 内核主循环 = 任务 0。 */

#define NTASK 8

typedef struct {
    u32 pd;                /* 页目录物理地址 */
    u32 kstack_top;        /* 内核栈顶 (TSS.esp0) */
    u32 saved_esp;         /* 上次切换的恢复点 */
    u32 entry;
    u32 user_esp;
    int state;             /* 0=空 1=可运行 2=僵尸 */
    u32 reserved;          /* 保留旧布局槽；用户指针现在按私有页表访问，不假定连续帧 */
    char name[12];
    char args[128];         /* EXEC 分开程序路径与参数，命令行在任务槽内持久化 */
} task_t;

extern task_t tasks[NTASK];

void task_init(void);                /* 分页 + GDT/TSS + 内核任务 0 */
int  task_spawn(const char *name, u32 pd, u32 entry, u32 user_esp);
int  task_exec_scx(const char *path);/* 读 SCX → 建页目录 → 建任务 (SYS_EXEC) */
void task_stop(int pid);            /* 关窗口并置僵尸，下次调度安全回收 */
u32  sched_pick(u32 cur_esp);        /* IRQ 尾部调: 轮转 + 换 CR3 */
u32  syscall_finish(u32 cur_esp);    /* 普通调用原路返回，EXIT 才立即切任务 */
void syscall_c_common(u32 vec, u32 *frame);   /* int 0x7C 分发 */

/* 系统调用功能号 (docs/SYSCALL.md) */
#define SYS_EXIT     0x00
#define SYS_PUTS     0x01
#define SYS_GETTICK  0x02
#define SYS_GETARGS  0x05
#define SYS_WINOPEN  0x10
#define SYS_WINCLOSE 0x11
#define SYS_TXT      0x12
#define SYS_FILL     0x13
#define SYS_GETKEY   0x14
#define SYS_MOUSE    0x15
#define SYS_WALLPAPER 0x18
#define SYS_WININFO  0x19
#define SYS_FSREAD   0x30
#define SYS_FSWRITE  0x31
#define SYS_FSLIST   0x32
#define SYS_EXEC     0x40

#endif /* SANDCORE_TASK_H */
