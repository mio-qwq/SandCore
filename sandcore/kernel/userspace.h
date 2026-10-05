#ifndef SANDCORE_USERSPACE_H
#define SANDCORE_USERSPACE_H
#include "io.h"

/* mio：M8单用户配置、任务目录和CLI作业的公共内核合同。
 * 元数据独立于168B task_t，旧调试器/验收脚本不必猜新增字段偏移。
 * 接口在IF=0的陷入内调用，pid由task_pid提供，不接受三环伪造身份。
 * 文本都复制给调用者；有效配置从不借出内核指针。
 *
 * USERQUERY(kind,name,out,capacity)：kind=0用户名/1家目录/2当前目录/
 * 3本任务完整执行路径/4指定环境变量；容量1..256，返回完整字节数。
 * 没找到环境变量=-2；容量不足=-1，不输出截断的路径或UTF-8。
 * USERCONFIG(kind,path,action)：kind=0用户/1环境；action=0仅验证候选，
 * 1验证后保存固定SYS配置再换快照，2重读固定配置忽略path。
 * -1参数/语法，-2读取，-3保存；失败不改变有效配置。
 * RESOLVE只规范化路径，不要求文件存在，输出最大64B包含NUL。
 * CLIRUN只将本人已启用终端授权给本次子任务，返回正31位作业票据。
 * JOBSTATUS只接受本父任务最近票据；0x40000000运行、0x40000001暂停，
 * 其余为退出码。下一次CLIRUN才覆盖已完成结果，pid复用不会覆盖它。
 */
void userspace_init(void);
void userspace_spawn(int pid,int parent);
void userspace_executable(int pid,const char *path);
void userspace_stop(int pid,int code);
void userspace_terminal_closed(int owner,int handle);
int userspace_query(int pid,int kind,const char *name,char *out,u32 capacity);
int userspace_config(int kind,const char *path,int action);
int userspace_resolve(int pid,const char *path,char *out,u32 capacity);
int userspace_chdir(int pid,const char *path);
int userspace_cli_run(int parent,const char *command,int terminal);
int userspace_job_status(int parent,u32 ticket);
int userspace_puts(int pid,const char *text);
int userspace_env_set(int pid,const char *name,const char *value,int remove);
int userspace_env_list(int pid,char *out,u32 capacity);
#endif
