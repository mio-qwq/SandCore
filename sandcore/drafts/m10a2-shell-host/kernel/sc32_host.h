#ifndef SANDCORE_SC32_HOST_H
#define SANDCORE_SC32_HOST_H
#include "io.h"
#include "../user/hostapi/SC32HOSTWIRE.H"
int sc32_host_call(int pid,u32 operation,u32 request,u32 output);
void sc32_host_session_created(u32 session);
void sc32_host_session_discard(u32 session);
void sc32_host_owner_stopped(int pid);
void sc32_host_poll(void);
int sc32_host_is_host(int pid,u32 session);
void sc32_host_windows_changed(u32 session);

/* WM与角色正文只交换可信值；清理入口不依赖已撤销的用户凭据。 */
int wm_shell_prepare(int pid,u32 desktop,u32 dgen,u32 bar,u32 bgen,u32 height);
int wm_shell_activate(int pid);
int wm_shell_workspace(int pid,u32 height);
void wm_shell_detach(int pid,u32 generation);
int wm_shell_active(u32 session);
void wm_shell_info(u32 session,ScShellHostInfo *out);
int wm_shell_window_next(int pid,u32 after,u32 budget,ScShellWindowInfo *row,
    u32 *cursor,u32 *epoch,u32 *examined,int *finished);
int wm_shell_window_action(int pid,const ScShellWindowAction *action,ScWindowInfo *out);
int wm_shell_window_at(int pid,int x,int y,ScWindowInfo *out);
#endif
