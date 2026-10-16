"""生成精确接线补丁与独立检查副本；绝不改写主树文件。"""
from pathlib import Path
import difflib
import hashlib
import json

DRAFT = Path(__file__).resolve().parent
ROOT = DRAFT.parents[1]
OUT = ROOT / "build/m10a2-debug2-agent-check"
STAGE = OUT / "integration"
STAGE.mkdir(parents=True, exist_ok=True)
changes = {}
original = {}

def read(name):
    if name not in changes:
        original[name] = (ROOT / name).read_text(encoding="utf-8")
        changes[name] = original[name]
    return changes[name]

def replace(name, before, after, count=1):
    text = read(name)
    if text.count(before) != count:
        raise ValueError(f"接线锚点不唯一或已变化: {name}: {before[:90]!r}")
    changes[name] = text.replace(before, after)

replace("kernel/process.c", '#include "audio.h"', '#include "audio.h"\n#include "sc32_debug.h"')
replace("kernel/process.c", "void process_reset(int pid)\n{", "void process_reset(int pid)\n{\n    sc32_debug_reset(pid);")
replace("kernel/process.c", "    if(!task_owned(pid) || !bytes)return;", "    if(!task_owned(pid) || !bytes)return;\n    sc32_debug_unmap(pid,base,bytes);")
replace("kernel/process.c", "    if(vector==1 && p->debug_owner>0) {", "    int modern=sc32_debug_exception(pid,vector,frame);\n    if(modern==1)return;\n    if(modern==2){pause_fault(pid,frame,cr2);return;}\n    if(vector==1 && p->debug_owner>0) {")
replace("kernel/process.c", "    if(command==DBG_BREAK) {", "    if(command==DBG_BREAK) {\n        if(sc32_debug_legacy_conflict(pid,address))return -1;")
replace("kernel/process.c", "    return PROCESS(owner).debug_child<0;", "    return PROCESS(owner).debug_child<0 && sc32_debug_stop_step(owner);")
replace("kernel/process.c", "void process_detach_owner(int owner)", """int process_debug_authorize(int owner,int pid,u32 generation)
{
    return pid>0 && task_owned(pid) && task_owned(owner)
        && task_generation(pid)==generation && PROCESS(pid).debug_owner==owner
        && task_generation(owner)==PROCESS(pid).debug_owner_generation
        && (TASK(pid).state==1 || TASK(pid).state==3);
}
int process_debug_legacy_break(int pid,u32 address)
{
    if(!task_owned(pid))return 0;
    for(int i=0;i<BREAKPOINTS;i++)if(PROCESS(pid).breaks[i].used && PROCESS(pid).breaks[i].address==address)return 1;
    return 0;
}
int process_debug_legacy_pending(int pid)
{return task_owned(pid) && PROCESS(pid).pending>=0;}
void process_debug_original(int pid,u32 address,u8 *bytes,u32 length)
{
    if(!task_owned(pid))return;
    for(int i=0;i<BREAKPOINTS;i++){
        breakpoint_t *b=&PROCESS(pid).breaks[i];
        if(b->used && b->address>=address && b->address-address<length)bytes[b->address-address]=b->original;
    }
}
void process_detach_owner(int owner)""")
replace("kernel/process.h", "/* M10a1：仅内核任务存储器使用，资源按真实任务申请。 */", """/* 新调试gate复用真实owner旁表；这些是内核私有入口，旧SCAPI不变。 */
int process_debug_authorize(int owner,int pid,u32 generation);
int process_debug_legacy_break(int pid,u32 address);
int process_debug_legacy_pending(int pid);
void process_debug_original(int pid,u32 address,u8 *bytes,u32 length);
/* M10a1：仅内核任务存储器使用，资源按真实任务申请。 */""")
replace("kernel/task.c", '#include "sc32_core.h"', '#include "sc32_core.h"\n#include "sc32_debug.h"')
replace("kernel/task.c", "        frame[11]=(u32)paging_module_protect(TASK(current).pd,frame[8],frame[10],frame[9]);break;", "        frame[11]=(u32)paging_module_protect(TASK(current).pd,frame[8],frame[10],frame[9]);\n        if(!frame[11])sc32_debug_mapping_changed(current,frame[8],frame[10]);\n        break;")
replace("kernel/task.c", "        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);", "        else if(frame[8]>=0x900u && frame[8]<0x920u)\n            frame[11]=(u32)sc32_debug_call(current,frame[8],frame[10],frame[9]);\n        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);")
replace("kernel/task.c", "        userspace_executable(pid,filename);", "        userspace_executable(pid,filename);\n        if(sc32_debug_main_image(pid,filename,load_size,bss_size)){task_stop(pid);return -4;}")
replace("kernel/scl_boot.c", '#include "scl_boot.h"', '#include "scl_boot.h"\n#include "sc32_debug.h"')
replace("kernel/scl_boot.c", "    error=root_copy(pd,base,file,metadata_size,allocation_bytes);", "    error=root_copy(pd,base,file,metadata_size,allocation_bytes);\n    if(!error)error=sc32_debug_bootstrap_image(pid,base,file,stat[1],allocation_bytes);")
replace("kernel/scl_boot.c", "    if(error){(void)paging_module_free(pd,base);return error;}", "    if(error){sc32_debug_unmap(pid,base,allocation_bytes);(void)paging_module_free(pd,base);return error;}")
replace("kernel/sc32_core.c", '#include "sc32_core.h"', '#include "sc32_core.h"\n#include "../user/SC32DEBUG.H"')
replace("kernel/sc32_core.c", "    result.capabilities_hi|=SC32_CAP_HOST_HI;", "    result.capabilities_hi|=SC32_CAP_HOST_HI|SC32_DEBUG_CAP_HI;")
replace("Makefile", "sc32_host.o sc32_lease.o", "sc32_host.o sc32_lease.o sc32_debug.o")
replace("user/runtime/scldr.c", "    void *application_stack_candidate;", "    void *application_stack_candidate;\n#ifdef SCL_BOOT_LIBRARY\n    ScU32 debug_module,debug_generation;\n#endif")
replace("user/runtime/scldr.c", "static void app_module_releasing(ScRuntime *runtime,ScLoaded *module);", "static void app_module_releasing(ScRuntime *runtime,ScLoaded *module);\nstatic int scl_debug_register(ScRuntime *,ScLoaded *);\nstatic void scl_debug_ready(ScLoaded *);\nstatic void scl_debug_unregister(ScLoaded *);")
replace("user/runtime/scldr.c", "static void release_storage(ScRuntime *r, ScLoaded *m)\n{", "static void release_storage(ScRuntime *r, ScLoaded *m)\n{\n#ifdef SCL_BOOT_LIBRARY\n    scl_debug_unregister(m);\n#endif")
replace("user/runtime/scldr.c", "    ScU32 entry = word(m->metadata, 96);", "#ifdef SCL_BOOT_LIBRARY\n    int published=scl_debug_register(r,m);if(published)return published;\n#endif\n    ScU32 entry = word(m->metadata, 96);")
replace("user/runtime/scldr.c", "    m->ready = 1;", "    m->ready = 1;\n#ifdef SCL_BOOT_LIBRARY\n    scl_debug_ready(m);\n#endif")
replace("user/runtime/scldr.c", "    m->id = r->next_id++; m->borrowed = m->pinned = m->ready = m->initialized = 1;", "    m->id = r->next_id++; m->borrowed = m->pinned = m->ready = m->initialized = 1;\n#ifdef SCL_BOOT_LIBRARY\n    int published=scl_debug_register(r,m);\n    if(published){r->platform.release(r->platform.context,m);return published;}\n    scl_debug_ready(m);\n#endif")
replace("user/runtime/SCLROOT.C", '#include "scl_platform.c"', '#include "scl_platform.c"\n#include "scl_debug_registry.c"')
replace("user/sc32/SC32API.C", '#include "../leaseapi/leaseapi.c"', '#include "../leaseapi/leaseapi.c"\n#include "../debugapi/sc32_debug.c"')
replace("user/SC32API.H", "#define SC32_ABI_MINOR 3u", "#define SC32_ABI_MINOR 4u")
replace("user/SC32API.H", "#define SC32_LIBRARY_VERSION 5u", "#define SC32_LIBRARY_VERSION 6u")
replace("tools/stage_m10a2_sdk.py", "    'SCUI.H': 'user/SCUI.H', 'SCLIB.H': 'user/sclib/SCLIB.H',", "    'SC32DEBUG.H': 'user/SC32DEBUG.H', 'SCDBGFILE.H': 'user/SCDBGFILE.H',\n    'SCUI.H': 'user/SCUI.H', 'SCLIB.H': 'user/sclib/SCLIB.H',")
replace("tools/stage_m10a2_sdk.py", "'user/dropapi/')),", "'user/dropapi/', 'user/debugapi/')),")
replace("tools/stage_m10a2_sdk.py", "'SCCC-SCL.md','SCCC-AGGREGATE-ABI.md','THIRD-PARTY.md')", "'SCCC-SCL.md','SCCC-AGGREGATE-ABI.md','THIRD-PARTY.md','SC32-DEBUG.md')")
replace("tools/stage_m10a2_sdk.py", "    if name == 'SCLDR.SCL':\n        return ('user/runtime/SCLDR.H',)", "    if name == 'SCLDR.SCL':\n        return ('user/runtime/SCLDR.H','user/debugapi/SC32DEBUGWIRE.H','user/SC32DEBUG.H','user/SC32API.H')")
manifest = json.loads(read("user/sc32/SC32API.json"))
new_exports = ("ScDebugLaunch", "ScDebugQuery", "ScDebugControl", "ScDebugRead", "ScDebugBreakpointAdd",
               "ScDebugBreakpointRemove", "ScDebugBreakpoints", "ScDebugModules", "ScDebugSegments", "ScDebugSymbols")
manifest["abi_minor"], manifest["library_version"] = 4, 6
for dependency in manifest["dependencies"]:
    if dependency["name"] == "SCLDR.SCL":
        dependency["min_minor"], dependency["min_version"] = 2, 3
manifest["exports"].extend(dict(name=name, kind="function") for name in new_exports)
manifest["exports"].sort(key=lambda item: item["name"])
changes["user/sc32/SC32API.json"] = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
root_manifest = json.loads(read("user/runtime/SCLROOT.json"))
root_manifest["abi_minor"], root_manifest["library_version"] = 2, 3
changes["user/runtime/SCLROOT.json"] = json.dumps(root_manifest, ensure_ascii=False, indent=2) + "\n"

rules = """
# Debug2只构建本轮新代码，不启动冻结游戏或复制整盘。
$(BUILD)/sc32_debug.o: kernel/sc32_debug.c kernel/sc32_debug.h kernel/sc32_debug_modules.inc user/SC32DEBUG.H user/debugapi/SC32DEBUGWIRE.H $(STAMP)
\t$(CC) $(CFLAGS) -Werror -Iuser -Iuser/runtime -Iuser/debugapi -c $< -o $@
$(BUILD)/sc32-library.o: user/debugapi/sc32_debug.c user/debugapi/SC32DEBUGWIRE.H user/SC32DEBUG.H
$(BUILD)/scl-root.o: user/runtime/scl_debug_registry.c user/debugapi/SC32DEBUGWIRE.H user/SC32DEBUG.H
$(BUILD)/debug2.o: user/debug2/DEBUG2.C $(wildcard user/debug2/*.c user/debug2/*.H) user/SC32DEBUG.H user/SCDBGFILE.H $(STAMP)
\t$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -Iuser/stdlib -Iuser/debug2 -c $< -o $@
$(BUILD)/DEBUG2.SCX: $(BUILD)/debug2.o user/debug2/DEBUG2.json tools/mkdscx.py
\t$(PY) tools/mkdscx.py $< $@ --manifest user/debug2/DEBUG2.json
.PHONY: m10a2-debug2-artifacts
m10a2-debug2-artifacts: $(BUILD)/DEBUG2.SCX
"""
changes["m10a2.mk"] = read("m10a2.mk") + rules
# ROOT/SC32API都经普通引导规则编译，私有头用明确相对路径，避免改全局INC。

replace("user/sc32/SC32API.C", '#include "../debugapi/sc32_debug.c"', '#include "../debugapi/sc32_debug.c"')

new_files = {}
for folder in ("kernel", "user", "docs"):
    for path in (DRAFT / folder).rglob("*"):
        if path.is_file():
            new_files[path.relative_to(DRAFT).as_posix()] = path.read_text(encoding="utf-8")
new_files["user/SC32DEBUG.H"] = (DRAFT / "public/SC32DEBUG.H").read_text(encoding="utf-8")
# 唯一SCDBG磁盘格式头属于原生工具链，按其确切字节复用。
debug_header = ROOT / "drafts/m10a2-object-toolchain/SCDBGFILE.H"
if not debug_header.is_file():
    raise FileNotFoundError("需要原生对象工具链的唯一SCDBGFILE.H")
new_files["user/SCDBGFILE.H"] = debug_header.read_text(encoding="utf-8")

patch = []
for name, after in sorted(changes.items()):
    patch.extend(difflib.unified_diff(original[name].splitlines(keepends=True), after.splitlines(keepends=True),
                                    fromfile="a/" + name, tofile="b/" + name))
for name, text in sorted(new_files.items()):
    patch.extend(difflib.unified_diff([], text.splitlines(keepends=True), fromfile="/dev/null", tofile="b/" + name))
for name, text in {**changes, **new_files}.items():
    target = STAGE / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")
(DRAFT / "integration.patch").write_text("".join(patch), encoding="utf-8", newline="\n")
status = dict(scope="DRAFT_ONLY_NO_MAIN_MUTATION", sc32_new_abi="1.4/v6", debug_header="SC32DEBUG.H",
              new_exports=list(new_exports), integration_patch="integration.patch",
              sources=[dict(source=p.relative_to(DRAFT).as_posix(), target=p.relative_to(DRAFT).as_posix())
                       for folder in ("user", "kernel") for p in (DRAFT / folder).rglob("*") if p.is_file()],
              old_sources=[dict(path=name, sha256=hashlib.sha256((ROOT / name).read_bytes()).hexdigest()) for name in original],
              preserved_legacy=[dict(path=name, sha256=hashlib.sha256((ROOT / name).read_bytes()).hexdigest())
                                for name in ("user/SCAPI.H", "user/debugger.c", "user/SCDBG.H", "user/SCDBG.inc", "user/m9/scdbg.c")])
(DRAFT / "integration.json").write_text(json.dumps(status, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(status="PATCH_AND_CHECK_COPIES_WRITTEN", modified_main_files=0, patches=len(changes), new_files=len(new_files)), ensure_ascii=False))
