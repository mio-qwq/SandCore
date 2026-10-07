#ifndef SANDCORE_STREAMS_H
#define SANDCORE_STREAMS_H
#include "io.h"
#define STREAM_READ 1u
#define STREAM_WRITE 2u
#define STREAM_NULL 4u
#define STREAM_TERMINAL 8u
#define STREAM_AGAIN (-6)
void streams_init(void);
/* M9仅新增字节流ABI；旧PUTS/CLIRUN仍保留。所有操作短暂、非阻塞，
 * -6让出后重试，0为EOF；管道无读者写入=-7。公开owner永远取真实PID。 */
void streams_spawn(int pid,int parent);
void streams_stop(int pid,int code);
int streams_stop_step(int pid,int code);
int streams_open(int pid,const char *path,u32 mode,u32 capacity);
int streams_read(int pid,int fd,void *data,u32 length);
int streams_write(int pid,int fd,const void *data,u32 length);
int streams_close(int pid,int fd,int commit);
int streams_dup(int pid,int from,int to);
int streams_pipe(int pid,int out[2]);
int streams_memory(int pid,const void *data,u32 bytes);
int streams_seek(int pid,int fd,u32 offset);
int streams_tty(int pid,int window);
int streams_terminal_info(int pid,int fd,u32 out[8]);
int streams_terminal_clear(int pid,int fd);
int streams_event(int pid,int fd,u32 action);
int streams_exec(int parent,const char *command,const i32 descriptors[3],u32 flags);
int streams_pipe_descriptors(int parent,const i32 descriptors[3]);
int streams_exec_buffer(int parent,const void *script,u32 bytes,const i32 descriptors[3],const char *arguments);
int streams_wait(int parent,u32 ticket,u32 out[8]);
int streams_job_snapshot(int parent,u32 ticket,u32 out[8]);
int streams_kill(int caller,int target);
int streams_processes(int caller,u32 *out,u32 words);
int streams_arguments(int pid,char *out,u32 capacity);
void streams_set_arguments(int pid,const char *arguments);
void streams_login_terminal(int parent,int child);
/* 只有接收外部UART的内核管理器能把SERIAL终端绑定到新SYSTEM Shell。
 * 串口数据统一经管理帧，用户拿不到UART基址、端口IO或伪造接收入口。 */
int streams_serial_attach(int pid,u32 session);
int streams_serial_input(const void *data,u32 length);
void streams_serial_reset(void);
void streams_serial_writable(void);
/* M10a1：IRQ只记录输入，主循环预算内分发；订阅节点就在稳定任务旁表。 */
void streams_poll(void);
void streams_terminal_input(int owner,int handle);
void streams_terminal_closed(int owner,int handle);
int streams_ready(int pid,int fd,u32 out[8]);
u32 streams_task_bytes(void);
#endif
