#!/usr/bin/env python3
"""定向构建三个Ring3动态应用及增量壁纸接线语法，不启动客体。"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "build/m10a2-system-tools-agent-host"
FLAGS = ["-m32", "-std=gnu99", "-ffreestanding", "-nostdlib", "-fno-builtin", "-fno-stack-protector",
         "-fno-pie", "-fno-asynchronous-unwind-tables", "-msoft-float", "-mno-sse", "-mno-mmx",
         "-mincoming-stack-boundary=2", "-mpreferred-stack-boundary=4", "-O2", "-Wall", "-Wextra",
         "-Werror", "-Wno-unused-function", "-Iuser/stdlib", "-Iuser/sclib", "-Iuser", "-I" + str(HERE)]


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    with (OUT / "host.log").open("a", encoding="utf-8") as stream:
        stream.write("$ " + " ".join(map(str, command)) + "\n" + result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "host.log").write_text("", encoding="utf-8")
    metadata = {"SC32API.SCL": "user/sc32/SC32API.json", "SCCLIB.SCL": "user/stdlib/SCCLIB.json",
                "SCLIB.SCL": "user/sclib/SCLIB.json", "SCUI.SCL": "user/scui/SCUI.json", "SCSHELL.SCL": "user/scshell/SCSHELL.json"}
    exports = {}
    for library, path in metadata.items():
        for export in json.loads((ROOT / path).read_text(encoding="utf-8"))["exports"]:
            exports[export["name"]] = library
    published_symbols = set(exports)
    for name in ("ScWallpaperSet", "ScWallpaperQuery"):
        exports[name] = "SC32API.SCL"
    report = dict(scope="SYSTEM_TOOLS_HOST_ONLY", guest_run=False, applications=[])
    for name in ("CALC2", "PALETTE2", "MONITOR2"):
        obj = OUT / (name + ".o")
        run(["gcc", *FLAGS, "-x", "c", "-c", str(HERE / (name + ".C")), "-o", str(obj)])
        symbols = sorted(line.split()[-1] for line in run(["nm", "-u", str(obj)]).splitlines() if line.strip())
        missing = [symbol for symbol in symbols if symbol not in exports]
        if missing:
            raise ValueError("missing public exports: " + ", ".join(missing))
        wallpaper_symbols = set(symbols) & {"ScWallpaperSet", "ScWallpaperQuery"}
        pending = sorted(wallpaper_symbols - published_symbols)
        dependencies = []
        for library in metadata:
            if library not in {exports[symbol] for symbol in symbols}:
                continue
            minor, version = (4, 6) if library == "SC32API.SCL" and wallpaper_symbols else (3, 5) if library == "SC32API.SCL" else (1, 2) if library == "SCUI.SCL" else (0, 1)
            dependencies.append(dict(name=library, abi_major=1, min_minor=minor, min_version=version))
        manifest = dict(kind="application", name=name + ".SCX", dependencies=dependencies,
                        imports=[dict(name=symbol, dependency=exports[symbol], kind="function", size=1) for symbol in symbols])
        path = HERE / (name + ".json")
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        run([sys.executable, "tools/mkdscx.py", str(obj), str(OUT / (name + ".SCX")), "--manifest", str(path)])
        scx = OUT / (name + ".SCX")
        report["applications"].append(dict(name=name, bytes=scx.stat().st_size,
            sha256=hashlib.sha256(scx.read_bytes()).hexdigest(), imports=len(symbols),
            pending_main_library_exports=pending, dependencies=dependencies))
    wrapper = OUT / "sc32-wall-combined.c"
    wrapper.write_text('#include "' + (ROOT / "user/sc32/SC32API.C").as_posix() + '"\n#include "' + (HERE / "wallapi/sc32_wall.c").as_posix() + '"\n', encoding="utf-8")
    run(["gcc", *FLAGS, "-Iuser/sc32", "-I" + str(HERE / "wallapi"), "-fsyntax-only", str(wrapper)])
    wm = OUT / "wm-wall-combined.c"
    wm.write_text((ROOT / "kernel/wm.c").read_text(encoding="utf-8") + '\n#include "' + (HERE / "wallapi/wm_wall_snapshot.inc").as_posix() + '"\n', encoding="utf-8")
    run(["gcc", *FLAGS, "-Wno-error=misleading-indentation", "-Wno-error=missing-field-initializers", "-Ikernel", "-Ibuild", "-fsyntax-only", str(wm)])
    core = OUT / "sc32-wall-query-combined.c"
    text = '#include "wm.h"\n#include "theme.h"\n#include "' + (HERE / "wallapi/SC32WALLWIRE.H").as_posix() + '"\n'
    text += 'int wm_wallpaper_snapshot(int pid,unsigned int *mode,unsigned int *session);\nstatic int wall_mode_query(int pid,unsigned int request,unsigned int output);\n'
    text += (ROOT / "kernel/sc32_core.c").read_text(encoding="utf-8").replace('case SC32_WIRE_PROBE:', 'case SC32_W_QUERY:return wall_mode_query(pid,request,output);\n    case SC32_WIRE_PROBE:')
    text += '\n#include "' + (HERE / "wallapi/sc32_wall_query.inc").as_posix() + '"\n'
    core.write_text(text, encoding="utf-8")
    run(["gcc", *FLAGS, "-Wno-error=misleading-indentation", "-Ikernel", "-Ibuild", "-fsyntax-only", str(core)])
    report["wallpaper_adapters_syntax"] = True
    report["kernel_syntax_warning_policy"] = "Legacy WM/core combined syntax keeps existing misleading-indentation and WM missing-field-initializers as warnings; application checks remain Werror except unused shared optional helpers."
    run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fno-builtin", "-ffunction-sections", "-fdata-sections",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-Iuser/sclib", "-Iuser", "-I" + str(HERE),
         "-x", "c", str(HERE / "MATH_HOST.C"), "-Wl,--gc-sections", "-o", str(OUT / "math-host")])
    report["math_host"] = dict(pointer_bits=64, sanitizers=["address", "undefined"], executes_ui_or_io=False,
                               result=run([str(OUT / "math-host")]).strip())
    pinned = [ROOT / path for path in ("kernel/task.c", "kernel/wm.c", "kernel/wm.h", "kernel/sc32_core.c", "kernel/sc32_core.h", "kernel/session.c", "kernel/session.h", "kernel/theme.c", "user/sc32/SC32API.C", "user/sc32/SC32PRIVATE.H", "user/SC32API.H")]
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    (HERE / "wallapi/pinned-sources.json").write_text(json.dumps([dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p)) for p in pinned], indent=2) + "\n", encoding="utf-8")
    public_headers = {ROOT / path for path in ("user/SCUI.H", "user/SCSHELL.H", "user/SCTYPES.H", "user/SC32PATH.H", "user/SC32BROKER.H",
                     "user/SC32GUI.H", "user/SC32DROP.H", "user/SC32HOST.H", "user/SC32LEASE.H", "user/SC32PALETTE.H", "user/sclib/SCLIB.H",
                     "tools/mkdscx.py", "tools/scd_format.py", "tools/mkscl.py", "tools/scl_format.py", "build/font8_data.h", "build/font16_data.h")}
    public_headers.update((ROOT / "user/stdlib").glob("*.h"))
    inputs = sorted({p for p in HERE.rglob("*") if p.is_file()} | {ROOT / path for path in metadata.values()} | set(pinned) | public_headers)
    report["inputs"] = [dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p)) for p in inputs]
    (OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("scope", "guest_run", "applications", "wallpaper_adapters_syntax")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
