#ifndef SANDCORE_AUTH_H
#define SANDCORE_AUTH_H
#include "io.h"
#define AUTH_ROOT 0
#define AUTH_SYSTEM (-1)
#define AUTH_DEFAULT 1000
#define AUTH_NORMAL 0u
#define AUTH_SERVICE 1u
#define AUTH_SERIAL 2u
#define AUTH_KERNEL 3u
#define AUTH_USER_MAX 32u
#define AUTH_PENDING 1

/* 凭据只在supervisor旁表。task_t历史168B不改，调用者不能指定PID
 * 来替换身份。login票据绑定创建者代数，成功后只能创建新身份进程。
 * 原进程保持原UID；绝不把su调用者或其它已存在进程重新标记。 */
void auth_init(void);
void auth_spawn(int pid,int parent);
void auth_stop(int pid);
void auth_poll(void);
int auth_uid(int pid);
int auth_gid(int pid);
int auth_subject_uid(int pid);
int auth_subject_gid(int pid);
const char *auth_name(int pid);
const char *auth_home(int pid);
int auth_can_manage(int pid);
int auth_can_mod(int pid);
int auth_info(int pid,u32 out[8]);
int auth_login(int pid,const char *name,const char *password);
int auth_status(int pid,u32 ticket,u32 out[8]);
int auth_exec_ticket(int pid,u32 ticket,const char *command,int activate);
int auth_cancel(int pid,u32 ticket);
int auth_user_add(int pid,const char *name,const char *home,int uid,int gid);
int auth_user_remove(int pid,const char *name);
int auth_password(int pid,const char *name,const char *password,u32 proof);
int auth_users(int pid,char *out,u32 capacity);
/* 以下仅可信内核调用，无对应raw三环入口。serial_begin只由外部
 * UART管理接收状态机调用；不是“自报session数字就取得SYSTEM”。 */
u32 auth_serial_begin(void);
void auth_serial_end(void);
int auth_serial_exec(u32 session,const char *command);
int auth_service_exec(const char *command,int delegate);
int auth_login_screen_exec(u32 desktop); /* 可信内核仅启动固定、受保护的登录程序。 */
int auth_launch_access(const char *path,u32 access);
int auth_network_exec(int pid,const char *command,const i32 descriptors[3]);
/* M10a1：仅内核任务存储器使用，资源按真实任务申请。 */
u32 auth_task_bytes(void);
u32 auth_login_bytes(void);
#endif
