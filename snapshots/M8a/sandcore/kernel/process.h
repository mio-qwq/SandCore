#ifndef SANDCORE_PROCESS_H
#define SANDCORE_PROCESS_H
#include "io.h"

/* mio：M7 现场、异常与调试合同。CPU 现场仍是 idt.asm 的 19 个双字，
 * task_t 的布局保持不动；扩展元数据独立存储，避免旧验收工具偏移失效。
 * 所有调用发生在 IF=0 的陷入/桌面临界区，暂停任务的 saved_esp 不会
 * 同时被抢占改写。debug_owner 是内核记录的真实调用者，不能传 pid 伪造。
 * 文档：MEM.md / INTR.md / SYSCALL.md / M7.md。 */
void process_reset(int pid);
void process_exit(int pid,int code);
int process_status(int pid);
void process_exception(u32 vector,u32 *frame);
int process_handler(int vector,u32 entry);
int process_exception_return(u32 *frame,u32 context);
int process_debug_bind(int pid,int owner);
int process_debug(int owner,int pid,int command,u32 address,void *buffer,u32 length);
void process_detach_owner(int owner);

/* 调试命令：INFO 输出 88 字节（19 双字现场 + state/CR2/event）。
 * READ 的长度上限 256B；BREAK/UNBREAK 参数 address；STEP/CONTINUE
 * 只接受已暂停的目标，KILL 对活目标都有效。软件断点使用真实 0xCC，
 * 命中后恢复原字节，下一条指令经 TF 执行后重新插入，而非跳过指令。 */
#define DBG_INFO 0
#define DBG_STEP 1
#define DBG_CONTINUE 2
#define DBG_READ 3
#define DBG_BREAK 4
#define DBG_UNBREAK 5
#define DBG_KILL 6
#define DBG_PAUSE 7
#endif
