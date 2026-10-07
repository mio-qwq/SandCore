#ifndef SANDCORE_TASK_H
#define SANDCORE_TASK_H

#include "io.h"

/* 任务/调度子系统 v2 (M6) —— 规范见 docs/MEM.md 与 docs/SYSCALL.md
 * v2: 分页上线, 每任务独立页目录 (用户区 0x400000 私有),
 * 多个用户程序可并发; 内核主循环 = 任务 0。 */

#define LEGACY_TASK_ROWS 8
#define LEGACY_TASK_ROWS2 32

typedef struct {
    u32 pd;                /* 页目录物理地址 */
    u32 kstack_top;        /* 内核栈顶 (TSS.esp0) */
    u32 saved_esp;         /* 上次切换的恢复点 */
    u32 entry;
    u32 user_esp;
    int state;             /* 0=空 1=可运行 2=僵尸 3=暂停（异常或调试） */
    u32 reserved;          /* 保留旧布局槽；用户指针现在按私有页表访问，不假定连续帧 */
    char name[12];
    char args[128];         /* EXEC 分开程序路径与参数，命令行在任务槽内持久化 */
} task_t;

/* 公共168B任务前缀保留；内部通过稳定对象访问，禁止重新引入固定数组。
 * NTASK现在仅是历史编号高水位，不能用它限定新任务创建或旧快照大小。
 * 热路径用就绪队列，需枚举活任务时用task_next，避免扫已回收历史槽。 */
u32 task_slot_count(void);
#define NTASK task_slot_count()
task_t *task_at(int pid);
#define TASK(pid) (*task_at(pid))
int task_next(int after);
int task_owned(int pid);
enum {
    TASK_DATA_AUTH,TASK_DATA_LOGIN,TASK_DATA_USERSPACE,TASK_DATA_PROCESS,
    TASK_DATA_STREAMS,TASK_DATA_SIMD,TASK_DATA_IMAGE,TASK_DATA_FAULT,TASK_DATA_COUNT
};
void *task_data(int pid,int kind);
void task_state_set(int pid,int state); /* 同步调度队列；调试器不能直接改state。 */
void task_kernel_wake(void); /* IRQ只置待处理标记，不在驱动内部换栈。 */
#define TASK_EVENT_INPUT 1u
#define TASK_EVENT_VISIBILITY 2u
#define TASK_EVENT_STREAM 4u
#define TASK_EVENT_JOB 8u
#define TASK_EVENT_NETWORK 16u
#define TASK_EVENT_AUDIO 32u
#define TASK_EVENT_IMAGE 64u
#define TASK_EVENT_AUTH 128u
#define TASK_EVENT_TIMEOUT 0x40000000u
void task_notify(int pid,u32 events); /* IF=0；同步拥有者事件。 */
void task_notify_generation(int pid,u32 generation,u32 events);

void task_init(void);                /* 分页 + GDT/TSS + 内核任务 0 */
int  task_spawn(const char *name, u32 pd, u32 entry, u32 user_esp);
int  task_exec_scx(const char *path);/* 读 SCX → 建页目录 → 建任务 (SYS_EXEC) */
void task_stop(int pid);            /* 关窗口并置僵尸，下次调度安全回收 */
void task_cleanup_poll(void);       /* 仅主循环IF=0：分阶段撤引用，再允许僵尸页回收。 */
int task_cleanup_pending(void);
int task_pid(void);                 /* 当前调用者；用户不能通过寄存器伪造身份 */
u32 task_generation(int pid);       /* 内部只读代数，跨异步票据防PID复用 */
/* mio：仅可信零环在IF=0调用。hold=1固定当前任务/CR3与共享画布
 * 生命周期，随后可STI收硬件IRQ；结束必须先CLI再hold=0。内部
 * 合成/整帧复制用此入口，三环不能直接调用；不是用户禁止抢占API。
 * 持有期间不能嵌套获取或主动YIELD，旧task_t/系统调用合同不改。 */
void task_render_hold(int hold);
/* mio：100Hz采样记录放在旁表，168B任务布局/旧MONITOR不改。
 * PIT在调度之前按CPU压入的CS记录真正被打断的上下文；task0只在
 * STI/HALT之前置idle，醒来马上清除。图形样本是kernel的子集。 */
void task_idle(int idle);
/* mio：仅任务0在IF=0、完成本轮事件/合成且无render_hold时调用。
 * 有本tick尚未主动YIELD的可运行三环任务时，经原汇编出口换栈，
 * 恢复后返回1，主循环应立即重新检查事件；无此任务返回0，才可
 * STI/HLT。只改内部待机策略，不给旧YIELD添加参数/返回语义。 */
int task_idle_yield(void);
void task_cpu_tick(u32 interrupted_cs);
void task_cpu_info(u32 *out);         /* 固定64个u32，详见CPU.md与SCAPI.H */
u32  sched_pick(u32 cur_esp);        /* IRQ 尾部调: 轮转 + 换 CR3 */
u32  syscall_finish(u32 cur_esp);    /* 普通调用原路返回，EXIT 才立即切任务 */
void syscall_c_common(u32 vec, u32 *frame);   /* int 0x7C 分发 */

/* 系统调用功能号 (docs/SYSCALL.md) */
#define SYS_EXIT     0x00
#define SYS_PUTS     0x01
#define SYS_GETTICK  0x02
#define SYS_GETARGS  0x05
#define SYS_STATUS   0x06
#define SYS_YIELD    0x07
#define SYS_EXCRETURN 0x08
#define SYS_EXCHANDLER 0x09
#define SYS_WINOPEN  0x10
#define SYS_WINCLOSE 0x11
#define SYS_TXT      0x12
#define SYS_FILL     0x13
#define SYS_GETKEY   0x14
#define SYS_MOUSE    0x15
#define SYS_FRAME    0x16
#define SYS_KEYDOWN  0x17
#define SYS_WALLPAPER 0x18
#define SYS_WININFO  0x19
#define SYS_FONT8    0x1A
#define SYS_GLYPH16  0x1B
#define SYS_UTF8TEXT 0x1C
#define SYS_FONTINFO 0x1D
#define SYS_DISPLAYINFO 0x20
#define SYS_DISPLAYAPPLY 0x21
#define SYS_POINTER 0x22
#define SYS_WINOPEN2 0x23
#define SYS_DESKTOPRELOAD 0x24
#define SYS_WINCONTROL 0x25
#define SYS_MONITOR 0x26
#define SYS_PALETTEINFO 0x27
#define SYS_WINRGB 0x28
#define SYS_FRAME32 0x29
#define SYS_FILLRGB 0x2A
#define SYS_TEXTRGB 0x2B
#define SYS_THEMEINFO 0x2C
#define SYS_THEMELOAD 0x2D
#define SYS_THEMEPATH 0x2E
#define SYS_USERALLOC 0x100
#define SYS_USERFREE 0x101
#define SYS_FSREAD   0x30
#define SYS_FSWRITE  0x31
#define SYS_FSLIST   0x32
#define SYS_FSDIR    0x33
#define SYS_MKDIR    0x34
#define SYS_REMOVE   0x35
#define SYS_RENAME   0x36
#define SYS_STAT     0x37
#define SYS_FSREADAT 0x38
#define SYS_EXEC     0x40
#define SYS_DBGEXEC  0x50
/* 私有名字避免与lwIP的SYS_DEBUG日志开关冲突；旧调用号仍是0x51。 */
#define SC_SYS_DEBUG 0x51
#define SYS_USERQUERY 0x70
#define SYS_USERCONFIG 0x71
#define SYS_CHDIR 0x72
#define SYS_RESOLVE 0x73
#define SYS_CLIRUN 0x74
#define SYS_JOBSTATUS 0x75
#define SYS_TERMINAL 0x76
#define SYS_KEYPEEK 0x77
#define SYS_POINTERPEEK 0x78
#define SYS_CPUINFO 0x79
#define SYS_STORAGEINFO 0x7A
#define SYS_CONFIGCHECK 0x7B
#define SYS_IMAGEREQUEST 0x7C
#define SYS_IMAGERESULT 0x7D
#define SYS_IMAGECLAIM 0x7E
#define SYS_IMAGESUBMIT 0x7F
#define SYS_IMAGECANCEL 0x80
#define SYS_MOUSECAPTURE 0x81
#define SYS_MOUSERELATIVE 0x82
#define SYS_AUTHINFO 0x200
#define SYS_AUTHLOGIN 0x201
#define SYS_AUTHSTATUS 0x202
#define SYS_AUTHEXEC 0x203
#define SYS_AUTHUSERS 0x204
#define SYS_AUTHACCOUNT 0x205
#define SYS_AUTHCANCEL 0x206
#define SYS_AUTHPASSWORD 0x207
#define SYS_FSMETA 0x208
#define SYS_FSPERMISSIONS 0x209
#define SYS_STREAMOPEN 0x210
#define SYS_STREAMREAD 0x211
#define SYS_STREAMWRITE 0x212
#define SYS_STREAMCLOSE 0x213
#define SYS_STREAMDUP 0x214
#define SYS_STREAMPIPE 0x215
#define SYS_STREAMSEEK 0x216
#define SYS_STREAMTTY 0x217
#define SYS_SPAWN2 0x218
#define SYS_WAIT2 0x219
#define SYS_KILL2 0x21A
#define SYS_PROCESSINFO2 0x21B
#define SYS_ARGUMENTS2 0x21C
#define SYS_SIMDINFO 0x21D
#define SYS_MODULEEXEC 0x21E
#define SYS_MODULEINFO 0x21F
#define SYS_CAPTUREOPEN 0x220
#define SYS_CAPTUREREAD 0x221
#define SYS_CAPTURECLOSE 0x222
#define SYS_CAPTURELIST 0x223
#define SYS_ENVSET 0x224
#define SYS_ENVLIST 0x225
#define SYS_STREAMEVENT 0x226
#define SYS_STORAGEINFO2 0x227
#define SYS_DBGSPAWN2 0x228
#define SYS_STREAMMEMORY 0x229
#define SYS_SPAWNBUFFER2 0x22A
#define SYS_TERMINALINFO2 0x22B
#define SYS_TERMINALCLEAR 0x22C
#define SYS_PROCESSSELF 0x22D
#define SYS_KILLGEN2 0x22E
#define SYS_CLOCK2 0x22F
#define SYS_AUDIOINFO 0x230
#define SYS_AUDIOOPEN 0x231
#define SYS_AUDIOSUBMIT 0x232
#define SYS_AUDIOCONTROL 0x233
#define SYS_AUDIOSTATUS 0x234
#define SYS_AUDIOEFFECT 0x235
#define SYS_POWER2 0x236
#define SYS_CPUINFO2 0x237
#define SYS_STORAGEFLUSH 0x238
#define SYS_CREATEEX 0x239
#define SYS_JOBINFO2 0x23A
#define SYS_MODULELIST 0x23B
#define SYS_WINDOWPLACE 0x23C
#define SYS_PROCESSPAGE 0x240
#define SYS_EVENTWAIT 0x241
#define SYS_SESSIONPAGE 0x242
#define SYS_SESSIONCONTROL 0x243
#define SYS_VISIBILITY 0x244
#define SYS_WINDOWPAGE 0x245
#define SYS_SESSIONPATH 0x246
#define SYS_STREAMREADY 0x247
#define SYS_FONTINFO2 0x248
#define SYS_GLYPHBITMAP 0x249
#define SYS_NETINFO 0x250
#define SYS_NETCONTROL 0x251
#define SYS_SOCKET 0x252
#define SYS_SOCKETBIND 0x253
#define SYS_SOCKETCONNECT 0x254
#define SYS_SOCKETLISTEN 0x255
#define SYS_SOCKETACCEPT 0x256
#define SYS_SOCKETSEND 0x257
#define SYS_SOCKETRECEIVE 0x258
#define SYS_SOCKETSHUTDOWN 0x259
#define SYS_SOCKETCLOSE 0x25A
#define SYS_SOCKETSTATUS 0x25B
#define SYS_SOCKETOPTION 0x25C
#define SYS_SOCKETPAGE 0x25D
#define SYS_ROUTEPAGE 0x25E
#define SYS_ARPPAGE 0x25F
#define SYS_DNSSTART 0x260
#define SYS_DNSSTATUS 0x261
#define SYS_DNSCANCEL 0x262
#define SYS_NETEXEC 0x263

#endif /* SANDCORE_TASK_H */
