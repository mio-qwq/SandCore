"""只生成可审阅的接线补丁；不修改当前 Phase 4 构建输入。"""
from pathlib import Path
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
changes = {}

def replace(path, old, new):
    before = changes.get(path, (ROOT / path).read_text(encoding="utf-8"))
    if before.count(old) != 1:
        raise RuntimeError(f"不唯一或输入已漂移: {path}: {old[:70]!r}")
    changes[path] = before.replace(old, new, 1)

replace("kernel/task.h", "int  task_exec_scx(const char *path);", "int task_exec_scx_parts(const char *filename,const char *arguments);\nint  task_exec_scx(const char *path);")
task = (ROOT / "kernel/task.c").read_text(encoding="utf-8")
start = task.index("int task_exec_scx(const char *path)\n{")
body = task.index("    /* M9首轮链接已证明大暂存", start)
old = task[start:body]
new = '''int task_exec_scx(const char *path)
{
    /* 旧 EXEC 仍接受首个空格分隔的路径与至多127B参数，不改历史语义。 */
    char filename[64];int len=0;
    while(*path==' ')path++;
    while(path[len] && path[len]!=' ' && len<63){filename[len]=path[len];len++;}
    if(path[len] && path[len]!=' ')return -1;
    filename[len]=0;
    if(!auth_launch_access(filename,FS_ACCESS_READ))return -5;
    const char *arguments=path+len;
    while(*arguments==' ')arguments++;
    int arglen=0;while(arguments[arglen] && arglen<127)arglen++;
    if(arguments[arglen])return -1;
    return task_exec_scx_parts(filename,arguments);
}

int task_exec_scx_parts(const char *filename,const char *arguments)
{
    /* 新 typed launch 把路径与参数分开；旧wrapper仍负责自己的旧长度
     * 边界。装载检查/页分配/动态SCX引导只有这一份正文，防止两条路径漂移。 */
    if(!filename || !arguments)return -1;
    u32 filelen=0;while(filename[filelen] && filelen<63)filelen++;
    if(!filelen || filename[filelen])return -1;
    u32 arglen=0;while(arguments[arglen] && arglen<1023)arglen++;
    if(arguments[arglen])return -1;
    if(!auth_launch_access(filename,FS_ACCESS_READ))return -5;
'''
replace("kernel/task.c", old, new)
replace("kernel/task.c", "        for (int i = 0; i <= arglen; i++) TASK(pid).args[i] = arguments[i];", "        /* 168B旧任务前缀仍只容纳127B；新版ARGUMENTS2由streams保存完整参数。 */\n        u32 legacy_args=arglen<127?arglen:127;\n        for(u32 i=0;i<legacy_args;i++)TASK(pid).args[i]=arguments[i];\n        TASK(pid).args[legacy_args]=0;")
replace("kernel/task.c", '#include "sc32_core.h"', '#include "sc32_core.h"\n#include "sc32_host.h"')
replace("kernel/task.c", "int task_exec_scx(const char *path)\n{", '#include "shell_issue.inc"\n\nint task_exec_scx(const char *path)\n{')
replace("kernel/task.c", "    if(fs_read_at(filename,scx_stage+36,load_size,36)!=(int)load_size)return -1;", "    if(fs_read_at(filename,scx_stage+36,load_size,36)!=(int)load_size)return -1;\n    if(auth_launch_issued_shell() && (icon_length || shell_issue_verify(scx_stage,36u+load_size)))return -5;")
replace("kernel/task.c", "    task_state_set(pid,2);          /* 先改变状态，避免关闭调试子进程时重复进入 */", "    task_state_set(pid,2);          /* 先改变状态，避免关闭调试子进程时重复进入 */\n    sc32_host_owner_stopped(pid);  /* 立即撤桌面角色，真实资源沿原owner阶段回收。 */")
replace("kernel/task.c", "        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);", "        else if(frame[8]>=0x800u && frame[8]<0x820u)\n            frame[11]=(u32)sc32_host_call(current,frame[8],frame[10],frame[9]);\n        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);")
replace("kernel/streams.c", '#include "auth.h"', '#include "auth.h"\n#include "session.h"')
replace("kernel/streams.c", "int streams_wait(int parent,u32 token,u32 out[8])", '#include "streams_launch.inc"\n\nint streams_wait(int parent,u32 token,u32 out[8])')
replace("kernel/streams.h", "int streams_exec(int parent,const char *command,const i32 descriptors[3],u32 flags);", "int streams_exec(int parent,const char *command,const i32 descriptors[3],u32 flags);\nint streams_launch_session(int parent,const char *path,const char *arguments);")
replace("kernel/auth.h", "int auth_login_screen_exec(u32 desktop);", "int auth_session_shell_exec(u32 desktop); /* 可信内核固定路径，以普通会话主体启动。 */\nint auth_login_screen_exec(u32 desktop);")
replace("kernel/auth.h", "int auth_launch_access(const char *path,u32 access);", "int auth_launch_access(const char *path,u32 access);\nint auth_launch_issued_shell(void); /* 仅固定host启动事务，普通EXEC不能设置。 */")
replace("kernel/auth.c", "int auth_exec_ticket(int pid,u32 token,const char *command,int activate)", '''static int shell_launch;
int auth_launch_issued_shell(void){return launch_set && shell_launch;}
int auth_session_shell_exec(u32 desktop)
{
    session_t *s=session_get(desktop);
    if(task_pid()!=0 || !s || s->kind!=SESSION_NORMAL || s->closing || s->uid<0 || s->gid<0)return -5;
    /* 内核只能启动固定发行host，且启动身份是该会话普通主体；不是
     * AUTH_SERVICE/SYSTEM，不能因此取得MOD、跨SID、硬件或其它文件权。 */
    shell_launch=1;
    int child=execute("SYS/SHELL/SCSHELL.SCX",s->uid,s->gid,AUTH_NORMAL,0,s->uid,s->gid,desktop);
    shell_launch=0;return child;
}
int auth_exec_ticket(int pid,u32 token,const char *command,int activate)''')
replace("kernel/session.c", '#include "desktop.h"', '#include "desktop.h"\n#include "sc32_host.h"')
replace("kernel/session.c", "    u32 id=s->id;sc32_drop_session_discard(id);", "    u32 id=s->id;sc32_host_session_discard(id);sc32_drop_session_discard(id);")
replace("kernel/session.c", "copy(s->home,home,64);serial=id;return id;", "copy(s->home,home,64);serial=id;sc32_host_session_created(id);return id;")
replace("kernel/main.c", '#include "sc32_drop.h"', '#include "sc32_drop.h"\n#include "sc32_host.h"')
replace("kernel/main.c", "        sc32_drop_poll();", "        sc32_host_poll();           /* 仅推进真正待启动SID，固定路径普通身份。 */\n        sc32_drop_poll();")
replace("kernel/fs.c", '    || same(name,"SYS/LIB") || child(name,"SYS/LIB"); }', '    || same(name,"SYS/LIB") || child(name,"SYS/LIB")\n    || same(name,"SYS/SHELL") || child(name,"SYS/SHELL"); }')
replace("kernel/sc32_broker.h", "int sc32_broker_available(void);", "int sc32_broker_available(void);\nint sc32_broker_kernel_close(int pid,u32 handle);")
replace("kernel/sc32_broker.c", "static int br_close(int pid,u32 request,u32 output,u32 kind)", '#include "broker_shell.inc"\n\nstatic int br_close(int pid,u32 request,u32 output,u32 kind)')
replace("user/sc32/SC32API.C", '#include "../dropapi/sc32_drop.c"', '#include "../dropapi/sc32_drop.c"\n#include "../hostapi/sc32_launch.c"')
replace("user/scui/SCUIPRIVATE.H", "static ScStatus ui_paint_root(UiNode *);", "static int ui_root_can_paint(const UiRoot *);\nstatic ScStatus ui_paint_root(UiNode *);")
replace("user/SCUI.H", "ScStatus ScUiPainterFill(ScUiPainter *,const ScUiRect *,ScU32 color_role);", "ScStatus ScUiPainterFill(ScUiPainter *,const ScUiRect *,ScU32 color_role);\nScStatus ScUiPainterClear(ScUiPainter *,const ScUiRect *);\nScStatus ScUiPainterTextColor(ScUiPainter *,const ScUiRect *,const ScUiText *,ScU32 font_role,ScU32 flags,ScU32 color_role);\nScStatus ScUiPainterBounds(ScUiPainter *,ScUiRect *out);")
replace("user/SCUI.H", "SC_UI_ABI_MAJOR=1,SC_UI_ABI_MINOR=0,SC_UI_VERSION=1,", "SC_UI_ABI_MAJOR=1,SC_UI_ABI_MINOR=1,SC_UI_VERSION=2,")
replace("user/SCUI.H", "ScStatus ScUiContextStep(ScUiContext *,ScU32 timeout_ticks);", "ScStatus ScUiContextStep(ScUiContext *,ScU32 timeout_ticks);\nScStatus ScUiContextWaitTimeout(ScUiContext *,ScU32 maximum_ticks,ScU32 *out);")
replace("user/SCUI.H", "    SC_UI_WINDOW=1,", "    SC_UI_TEXT_WRAP=1,SC_UI_TEXT_ELLIPSIS=2,SC_UI_TEXT_CENTER=4,SC_UI_TEXT_RIGHT=8,\n    SC_UI_TEXT_UNDERLINE=16,SC_UI_TEXT_STRIKE=32,\n    SC_UI_WINDOW=1,")
ui_manifest = json.loads((ROOT / "user/scui/SCUI.json").read_text(encoding="utf-8"))
ui_manifest["abi_minor"] = 1
ui_manifest["library_version"] = 2
if any(row["name"] == "ScUiPainterClear" for row in ui_manifest["exports"]):
    raise RuntimeError("PainterClear已存在，须人工合并新底座版本")
ui_manifest["exports"].append({"name": "ScUiPainterClear", "kind": "function"})
ui_manifest["exports"].append({"name": "ScUiPainterTextColor", "kind": "function"})
ui_manifest["exports"].append({"name": "ScUiPainterBounds", "kind": "function"})
ui_manifest["exports"].append({"name": "ScUiContextWaitTimeout", "kind": "function"})
ui_manifest["exports"].sort(key=lambda row: row["name"])
changes["user/scui/SCUI.json"] = json.dumps(ui_manifest, ensure_ascii=False, indent=2) + "\n"
replace("user/scui/ui_paint.c", "static ScStatus ui_root_surface(UiNode *n)", '#include "ui_shell_paint.inc"\n\nstatic ScStatus ui_root_surface(UiNode *n)')
replace("user/scui/ui_paint.c", "static ScStatus ui_root_surface(UiNode *n)", '''static int ui_root_can_paint(const UiRoot *w)
{
    /* PREPARING只来自内核已授角色的两surface，仍保持visible=0；这里只
     * 给真实首帧绘制资格，输入/动画timer继续沿普通实际可见性判断。 */
    return w && (w->visible || (w->information.state&SC32_WINDOW_PREPARING));
}
static ScStatus ui_root_surface(UiNode *n)''')
replace("user/scui/ui_paint.c", "    if(!w->paint_pending || !w->visible)return SC_UI_PENDING;", "    if(!w->paint_pending || !ui_root_can_paint(w))return SC_UI_PENDING;")
replace("user/scui/ui_paint.c", "    revision=c->revision;", "    /* 隐藏准备surface必须提交一次真正完整首帧，不能把几块局部损伤\n     * 的包围框当完整覆盖。WM以同geometry实际Submit核对，然后撤资格。 */\n    if(w->information.state&SC32_WINDOW_PREPARING){rectangles[0]=claimed;count=1;}\n    revision=c->revision;")
replace("user/scui/ui_paint.c", "    began=0;\n    w->paint_pending=0;", "    began=0;\n    if(w->information.state&SC32_WINDOW_PREPARING){s=ui_root_surface(root);if(s<0)goto failed;}\n    w->paint_pending=0;")
replace("user/scui/ui_dispatch.c", "    if(m->id==SC32_MSG_PAINT){ScContainerInfo info;", "    if(m->id==SC32_MSG_PAINT){ScContainerInfo info;\n        /* 隐藏host的准备资格只在真实PAINT后重新查事实，不轮询全部hidden root。 */\n        if(!w->visible){s=ui_root_surface(root);if(s<0)goto done;}")
replace("user/scui/ui_dispatch.c", "ScStatus ScUiContextStep(ScUiContext *c,ScU32 timeout)", '''ScStatus ScUiContextWaitTimeout(ScUiContext *c,ScU32 maximum,ScU32 *out)
{
    if(!out || (maximum>0x7fffffffu && maximum!=SC32_INFINITE))return SC_UI_E_INVALID;
    ScStatus s=ui_object_enter((UiObject *)c,UI_CONTEXT);if(s<0)return s;
    /* 混合GUI/IPC服务泵只借真实下一期限；不在宿主应用猜拖放的1tick
     * 或为所有空闲GUI加固定高频poll。没有改变计时/消息分派语义。 */
    ScU32 value=ui_drag_wait(c,ui_timer_wait(c,maximum));
    *out=value;ui_object_leave(&c->object);return SC_UI_OK;
}
ScStatus ScUiContextStep(ScUiContext *c,ScU32 timeout)''')
replace("user/scui/ui_dispatch.c", "        w->information.visibility_epoch=m->epoch;", "        w->information.visibility_epoch=m->epoch;\n        w->information.state&=~SC32_WINDOW_PREPARING;")
replace("user/scui/ui_dispatch.c", "if(r->window && r->window->visible && r->window->paint_pending)", "if(r->window && ui_root_can_paint(r->window) && r->window->paint_pending)")
replace("user/scui/ui_dispatch.c", "if(root->window && root->window->visible && root->window->paint_pending)", "if(root->window && ui_root_can_paint(root->window) && root->window->paint_pending)")
replace("Makefile", "sc32_font.o sc32_broker.o sc32_drop.o", "sc32_font.o sc32_broker.o sc32_drop.o sc32_host.o")
changes["m10a2.mk"] = (ROOT / "m10a2.mk").read_text(encoding="utf-8") + '''
# 会话寿命启动底座；与Shell角色接线同批开启，旧受管作业不变。
$(BUILD)/sc32_host.o: kernel/sc32_host.c kernel/sc32_host.h user/SC32HOST.H user/hostapi/SC32HOSTWIRE.H $(STAMP)
\t$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/streams.o: kernel/streams_launch.inc
$(BUILD)/sc32_broker.o: kernel/broker_shell.inc
$(BUILD)/wm.o: kernel/wm_shell.inc kernel/sc32_host.h
$(BUILD)/scui-library.o: user/scui/ui_shell_paint.inc
SHELL_HOST_IMAGE ?= $(BUILD)/SCSHELL.SCX
# 正式原生收尾显式指向已核来源的G1产物，不把HOST摘要授给不同字节。
$(BUILD)/shell_host_issue.h: $(SHELL_HOST_IMAGE) tools/mk_shell_issue.py
\t$(PY) tools/mk_shell_issue.py --host $(SHELL_HOST_IMAGE) --out $@
$(BUILD)/task.o: kernel/shell_issue.inc $(BUILD)/shell_host_issue.h
$(BUILD)/sc32-library.o: user/SC32HOST.H user/hostapi/SC32HOSTWIRE.H user/hostapi/sc32_launch.c
'''
patch = []
metadata = {"status": "ISOLATED_SOURCE_NOT_BUILT_NOT_RUN", "inputs": {}, "new_files": []}
for path, after in changes.items():
    raw = (ROOT / path).read_bytes()
    before = raw.decode("utf-8")
    # 一些历史C文件混用CRLF/LF。只给真实新增/替换行选择邻近换行，
    # 不借生成补丁把整个已验证文件换行重写，避免上千行无意义差异。
    old_lines = before.splitlines(True)
    old_normal = [s.replace("\r\n", "\n") for s in old_lines]
    new_lines = after.splitlines(True)
    matched = difflib.SequenceMatcher(a=old_normal, b=new_lines, autojunk=False)
    preserved = []
    for kind, a0, a1, b0, b1 in matched.get_opcodes():
        if kind == "equal":
            preserved.extend(old_lines[a0:a1])
        else:
            anchor = old_lines[min(a0, len(old_lines)-1)] if old_lines else "\n"
            ending = "\r\n" if anchor.endswith("\r\n") else "\n"
            preserved.extend(s[:-1] + ending if s.endswith("\n") else s for s in new_lines[b0:b1])
    after = "".join(preserved)
    metadata["inputs"][path] = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
    patch.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True), f"a/sandcore/{path}", f"b/sandcore/{path}"))
for tree in ("kernel", "user", "tools"):
    for file in sorted((HERE / tree).rglob("*")):
        if file.is_file():
            rel = file.relative_to(HERE).as_posix()
            after = file.read_text(encoding="utf-8")
            metadata["new_files"].append({"path": rel, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
            patch.extend(difflib.unified_diff([], after.splitlines(True), "/dev/null", f"b/sandcore/{rel}"))
(HERE / "integration.patch").write_text("".join(patch), encoding="utf-8", newline="\n")
(HERE / "source-inputs.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"status": metadata["status"], "changed_paths": len(changes), "new_files": len(metadata["new_files"])}, ensure_ascii=False))
