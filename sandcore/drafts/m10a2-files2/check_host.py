#!/usr/bin/env python3
"""仅构建Files 2的i386对象/动态SCX并核导入，不启动客体或改安装盘。"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / "build/m10a2-files2-agent-host"


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    with (OUT / "host.log").open("a", encoding="utf-8") as stream:
        stream.write("$ " + " ".join(map(str, command)) + "\n")
        stream.write(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "host.log").write_text("", encoding="utf-8")
    started = time.monotonic()
    obj = OUT / "files2.o"
    flags = ["-m32", "-std=gnu99", "-ffreestanding", "-nostdlib", "-fno-builtin",
             "-fno-stack-protector", "-fno-pie", "-fno-asynchronous-unwind-tables",
             "-msoft-float", "-mno-sse", "-mno-mmx", "-mincoming-stack-boundary=2",
             "-mpreferred-stack-boundary=4", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Iuser/stdlib", "-Iuser/sclib", "-Iuser"]
    run(["gcc", *flags, "-x", "c", "-c", str(HERE / "FILES2.C"), "-o", str(obj)])
    undefined = sorted(line.split()[-1] for line in run(["nm", "-u", str(obj)]).splitlines() if line.strip())
    libraries = {
        "SC32API.SCL": ("user/sc32/SC32API.json", 3, 5),
        "SCCLIB.SCL": ("user/stdlib/SCCLIB.json", 0, 1),
        "SCLIB.SCL": ("user/sclib/SCLIB.json", 0, 1),
        "SCUI.SCL": ("user/scui/SCUI.json", 1, 2),
        "SCSHELL.SCL": ("user/scshell/SCSHELL.json", 0, 1),
    }
    exports = {}
    for library, (source, _, _) in libraries.items():
        manifest = json.loads((ROOT / source).read_text(encoding="utf-8"))
        for symbol in manifest["exports"]:
            name = symbol["name"]
            if name in exports:
                raise ValueError("ambiguous export " + name)
            exports[name] = library
    missing = [symbol for symbol in undefined if symbol not in exports]
    if missing:
        raise ValueError("no public export: " + ", ".join(missing))
    manifest = dict(kind="application", name="FILES2.SCX", dependencies=[
        dict(name=library, abi_major=1, min_minor=minor, min_version=version)
        for library, (_, minor, version) in libraries.items()
        if library in {exports[symbol] for symbol in undefined}
    ], imports=[dict(name=symbol, dependency=exports[symbol], kind="function", size=1)
                for symbol in undefined])
    (HERE / "FILES2.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    run([sys.executable, "tools/mkdscx.py", str(obj), str(OUT / "FILES2.SCX"),
         "--manifest", str(HERE / "FILES2.json")])
    run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fno-builtin",
         "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-Iuser/sclib", "-Iuser", "-x", "c", str(HERE / "MODEL_HOST.C"),
         "-Wl,--gc-sections", "-o", str(OUT / "model-host")])
    model_result = run([str(OUT / "model-host")]).strip()
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    inputs = set(path for path in HERE.iterdir() if path.is_file())
    inputs.update(ROOT / value[0] for value in libraries.values())
    inputs.update(ROOT / path for path in ("user/SC32API.H", "user/SC32PATH.H", "user/SC32LEASE.H",
                  "user/SC32BROKER.H", "user/SC32HOST.H", "user/SC32GUI.H", "user/SC32DROP.H",
                  "user/SCUI.H", "user/SCSHELL.H", "user/SCTYPES.H", "user/sclib/SCLIB.H",
                  "tools/mkdscx.py", "tools/scd_format.py", "tools/mkscl.py", "tools/scl_format.py"))
    inputs.update((ROOT / "user/stdlib").glob("*.h"))
    report = dict(scope="FILES2_I386_HOST_OBJECT_AND_DYNAMIC_SCX_ONLY", guest_run=False,
                  wall_seconds=time.monotonic()-started, imports=len(undefined),
                  model_host=dict(pointer_bits=64, sanitizers=["address", "undefined"], result=model_result,
                                  executes_ui_or_io=False),
                  dependencies=manifest["dependencies"],
                  inputs=[dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p))
                          for p in sorted(inputs)],
                  outputs=[dict(path=p.relative_to(ROOT).as_posix(), bytes=p.stat().st_size,
                                sha256=digest(p)) for p in (obj, OUT / "FILES2.SCX")])
    (OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("scope", "guest_run", "imports", "outputs")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
