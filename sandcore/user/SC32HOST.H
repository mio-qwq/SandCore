#ifndef SANDCORE_SC32HOST_H
#define SANDCORE_SC32HOST_H
#include "SC32API.H"
#include "SC32GUI.H"
#include "SC32BROKER.H"

/* 独立的会话寿命启动入口；旧受管 ScProcessSpawn 的退出连带关系不改。
 * path 与 arguments 分开传入，路径内空格不再被命令解释器误切。 */
typedef struct {
    ScU32 size,version;
    const char *path;
    const char *arguments;
    ScU32 flags,reserved[3];
} ScLaunchDesc;

typedef struct {
    ScU32 size,version,pid,generation,session;
    ScI32 uid,gid;
    ScU32 realm,flags,reserved[7];
} ScLaunchInfo;

/* flags 必须为零；仅真实普通会话中的普通主体可调用。成功不创建 job，
 * 不继承任何标准流引用。环境/CWD/凭据按当前真实调用者快照继承。
 * 子进程到自行退出或本会话注销为止；发起者退出不终止子进程。 */
ScStatus ScProcessLaunch(const ScLaunchDesc *desc,ScLaunchInfo *out);

enum {
    SC32_CAP_HOST_HI=2u,SC32_SHELL_PREPARING=1u,
    SC32_SHELL_OFFLINE=0u,SC32_SHELL_STARTING=1u,
    SC32_SHELL_PREPARED=2u,SC32_SHELL_ACTIVE=3u,
    SC32_SHELL_RETRY_EXHAUSTED=4u,SC32_SHELL_STOPPING=5u,
    SC32_SHELL_WINDOWS_BEGIN=0u,SC32_SHELL_WINDOWS_FINISHED=1u,
    SC32_SHELL_ACTION_FOCUS=0u,SC32_SHELL_ACTION_MINIMIZE=1u,
    SC32_SHELL_ACTION_MAXIMIZE=2u,SC32_SHELL_ACTION_RESTORE=3u,
    SC32_SHELL_ACTION_PLACE=4u,SC32_SHELL_ACTION_CLOSE=5u
};
typedef struct {
    ScU32 size,version,session,session_generation,host_epoch,pid,generation;
    ScU32 endpoint,state,desktop,desktop_generation,taskbar,taskbar_generation;
    ScI32 work_x,work_y;
    ScU32 work_width,work_height,display_epoch,flags,reserved[5];
} ScShellHostInfo;

typedef struct {
    ScU32 size,version,host_epoch,endpoint,desktop,desktop_generation;
    ScU32 taskbar,taskbar_generation,taskbar_height,flags,reserved[2];
} ScShellHostDesc;

typedef struct {
    ScU32 size,version,channel;
    ScShellHostInfo host;
    ScBrokerIdentity peer;
    ScU32 reserved;
} ScShellConnectionInfo;

typedef struct {
    ScU32 size,version;
    ScWindowInfo window;
    char title[128],executable[68];
    ScU32 reserved[5];
} ScShellWindowInfo;

typedef struct {
    ScU32 size,version,cursor,count,epoch,examined,flags,reserved;
} ScShellWindowPage;

typedef struct {
    ScU32 size,version,host_epoch,window,generation,pid,pid_generation,action;
    ScI32 x,y;
    ScU32 width,height,flags,reserved[3];
} ScShellWindowAction;

typedef struct {
    ScU32 size,version,host_epoch;ScI32 x,y;ScU32 flags,reserved[2];
} ScShellHostPoint;

/* 角色由内核固定发行路径启动事务授予。Query不授角色；普通程序不能
 * 通过相同文件名、UID、IPC名字或窗口编号任命自己为桌面host。 */
ScStatus ScShellHostQuery(ScShellHostInfo *out);
ScStatus ScShellHostPrepare(const ScShellHostDesc *desc,ScShellHostInfo *out);
ScStatus ScShellHostActivate(ScU32 host_epoch,ScShellHostInfo *out);
ScStatus ScShellHostConnect(ScU32 expected_epoch,ScShellConnectionInfo *out);
ScStatus ScShellHostWorkspace(ScU32 host_epoch,ScU32 height,ScShellHostInfo *out);
ScStatus ScShellHostWindows(ScU32 host_epoch,ScU32 after,ScU32 expected_epoch,
    ScShellWindowInfo *rows,ScU32 capacity,ScShellWindowPage *out);
ScStatus ScShellHostWindowAction(const ScShellWindowAction *action,ScWindowInfo *out);
/* 只给当前可见同SID普通窗口的值；用于区分桌面布局拖动和跨窗FILES拖起。
 * 不铸Drop票、不授文件权；真正Drop仍必须另走SC32DROP验证。 */
ScStatus ScShellHostWindowAt(const ScShellHostPoint *point,ScWindowInfo *out);
ScStatus ScShellHostRestart(ScShellHostInfo *out);
/* 正常清理先主动撤角色，再销窗口/释放Context/return；不会中途Stop
 * 正在运行fini的本人。异常丢surface仍由内核停止并按有限策略恢复。 */
ScStatus ScShellHostDeactivate(ScU32 host_epoch,ScShellHostInfo *out);
#endif
