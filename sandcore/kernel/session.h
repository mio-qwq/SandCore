#ifndef SANDCORE_SESSION_H
#define SANDCORE_SESSION_H
#include "io.h"

#define SESSION_NORMAL 0u
#define SESSION_SYSTEM 1u
#define SESSION_LOCKED 2u
/* 桌面会话独立于UID和串口授权票据。同一UID的两次登录也有不同
 * 窗口/输入空间；此编号不能用来取得任何零环管理权限。 */
typedef struct {
    u32 id,kind,closing,references,epoch,retained;
    int uid,gid,task_head;
    char name[32],home[64];
} session_t;
void session_init(int uid,int gid,const char *name,const char *home);
u32 session_create(int uid,int gid,u32 kind,const char *name,const char *home);
void session_discard(u32 id);
session_t *session_get(u32 id);
u32 session_active(void);
u32 session_current(void); /* 任务0使用可见桌面，三环使用自己的会话。 */
u32 session_task(int pid);
u32 session_system(void);
int session_bind(int pid,u32 id);
void session_unbind(int pid);
int session_switch_trusted(u32 id);
int session_control(int pid,u32 id,u32 action);
int session_page(int pid,u32 *out,u32 words,u32 after);
int session_config_path(const char *leaf,char out[64]);
int session_config_path_for(u32 id,const char *leaf,char out[64]);
int session_config_prepare(int pid); /* 仅显式保存时创建本人目录，不覆盖既有默认值。 */
int session_show_login(void); /* 仅内核输入/注销路径调用，不能由编号取得权限。 */
#endif
