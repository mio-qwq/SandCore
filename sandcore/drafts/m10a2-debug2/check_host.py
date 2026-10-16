"""定向宿主编译/格式检查；不启动QEMU、不写主树或Git。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

DRAFT = Path(__file__).resolve().parent
ROOT = DRAFT.parents[1]
OUT = ROOT / "build/m10a2-debug2-agent-check"
STAGE = OUT / "integration"
OUT.mkdir(parents=True, exist_ok=True)
results = []

def run(name, args):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (name + ".log")).write_text(result.stdout, encoding="utf-8")
    results.append(dict(name=name, exit_code=result.returncode, log=str((OUT / (name + ".log")).relative_to(ROOT))))
    print(name, result.returncode, flush=True)
    return result

common = ["gcc", "-m32", "-ffreestanding", "-nostdlib", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
          "-fno-asynchronous-unwind-tables", "-msoft-float", "-mno-sse", "-mno-mmx", "-mincoming-stack-boundary=2",
          "-O2", "-Wall", "-Wextra", "-Werror", "-x", "c"]
includes = ["-I" + str(p) for p in (STAGE / "user", ROOT / "user/sc32", ROOT / "user/runtime", ROOT / "user",
            ROOT / "user/stdlib", ROOT / "user/sclib", STAGE / "kernel", ROOT / "kernel",
            DRAFT / "public", DRAFT / "user/debug2", ROOT / "drafts/m10a2-object-toolchain", ROOT / "build",
            ROOT / "build/m10a2-shell-integration-stage-01/build/p5-unified-check")]
run("prepare", [sys.executable, DRAFT / "prepare_integration.py"])
checks = (
    ("debug2", DRAFT / "user/debug2/DEBUG2.C"),
    ("debug-kernel", STAGE / "kernel/sc32_debug.c"),
    ("process-adapter", STAGE / "kernel/process.c"),
    ("task-adapter", STAGE / "kernel/task.c"),
    ("core-adapter", STAGE / "kernel/sc32_core.c"),
    ("bootstrap-adapter", STAGE / "kernel/scl_boot.c"),
    ("sc32-library", STAGE / "user/sc32/SC32API.C"),
    ("root-library", STAGE / "user/runtime/SCLROOT.C"),
)
for name, source in checks:
    # 旧process/task通用规则并未启用-Werror。其既有签名比较/排版
    # warning保留在日志；本次新gate、应用、ROOT/库仍逐个-Werror。
    flags = [x for x in common if x != "-Werror"] if name in ("process-adapter", "task-adapter") else common
    run(name, flags + includes + ["-c", source, "-o", OUT / (name + ".o")])
for object_name, manifest_path, output_name in (("sc32-library", STAGE / "user/sc32/SC32API.json", "SC32API.SCL"),
                                               ("root-library", STAGE / "user/runtime/SCLROOT.json", "SCLDR.SCL")):
    if next(r for r in results if r["name"] == object_name)["exit_code"] == 0:
        run("pack-" + object_name, [sys.executable, ROOT / "tools/mkscl.py", OUT / (object_name + ".o"), OUT / output_name,
                                   "--manifest", manifest_path])
if results[1]["exit_code"] == 0:
    undefined = subprocess.check_output(["nm", "-u", str(OUT / "debug2.o")], text=True)
    names = sorted(set(line.split()[-1] for line in undefined.splitlines() if line.strip()))
    libraries = {}
    for name, path in (("SC32API.SCL", STAGE / "user/sc32/SC32API.json"), ("SCCLIB.SCL", ROOT / "user/stdlib/SCCLIB.json"),
                       ("SCLIB.SCL", ROOT / "user/sclib/SCLIB.json"), ("SCUI.SCL", ROOT / "user/scui/SCUI.json")):
        data = json.loads(path.read_text(encoding="utf-8")); libraries[name] = data
    imports = []
    for symbol in names:
        matches = [(name, item) for name, data in libraries.items() for item in data["exports"] if item["name"] == symbol]
        if len(matches) != 1:
            raise ValueError(f"未解决或多重公共导出: {symbol}: {matches}")
        dependency, entry = matches[0]
        imports.append(dict(name=symbol, dependency=dependency, kind=entry["kind"], size=entry.get("size", 1)))
    used = sorted(set(item["dependency"] for item in imports))
    dependencies = [dict(name=name, abi_major=libraries[name]["abi_major"], min_minor=libraries[name]["abi_minor"],
                         min_version=libraries[name]["library_version"]) for name in used]
    manifest = dict(kind="application", name="DEBUG2.SCX", dependencies=dependencies, imports=imports)
    (DRAFT / "user/debug2/DEBUG2.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    run("dynamic-scx", [sys.executable, ROOT / "tools/mkdscx.py", OUT / "debug2.o", OUT / "DEBUG2.SCX", "--manifest", DRAFT / "user/debug2/DEBUG2.json"])
run("decoder-build", ["gcc", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror",
    "-I" + str(ROOT / "user"), "-I" + str(DRAFT / "user/debug2"), DRAFT / "user/debug2/decode.c", DRAFT / "tests/decode_test.c", "-o", OUT / "decoder-test"])
if results[-1]["exit_code"] == 0:
    run("decoder-run", [OUT / "decoder-test"])
run("prepare-final", [sys.executable, DRAFT / "prepare_integration.py"])
report = dict(scope="HOST_DRAFT_COMPILE_AND_DECODER_ONLY", guest_verified=False, checks=results,
              sources=[dict(path=str(p.relative_to(DRAFT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                       for p in sorted(DRAFT.rglob("*")) if p.is_file() and p.suffix in (".c", ".C", ".H", ".inc", ".py", ".json")])
(OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
sys.exit(0 if all(r["exit_code"] == 0 for r in results) else 1)
