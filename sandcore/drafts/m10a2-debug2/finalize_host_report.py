"""把已观察的最后一次定向编译与未改输入的复用边界写入交接。"""
from pathlib import Path
import hashlib
import json

DRAFT = Path(__file__).resolve().parent
ROOT = DRAFT.parents[1]
OUT = ROOT / "build/m10a2-debug2-agent-check"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

full = json.loads((OUT / "report.json").read_text(encoding="utf-8"))
if not all(row["exit_code"] == 0 for row in full["checks"]):
    raise ValueError("完整宿主检查仍有失败，不能生成通过交接")
differences = []
for row in full["sources"]:
    relative = row["path"].replace("\\", "/")
    path = DRAFT / relative
    if not path.is_file() or sha(path) != row["sha256"]:
        differences.append(relative)
changed_code = [name for name in differences if name.startswith(("kernel/", "user/", "public/", "tests/"))]
if set(changed_code) - {"kernel/sc32_debug.c", "kernel/sc32_debug_modules.inc"}:
    raise ValueError("存在最后一次内核定向编译未覆盖的代码变动: " + repr(changed_code))
outputs = []
for name in ("DEBUG2.SCX", "SC32API.SCL", "SCLDR.SCL", "debug-kernel.o"):
    path = OUT / name
    outputs.append(dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=sha(path)))
record = dict(status="HOST_DRAFT_CHECKS_PASS_GUEST_UNVERIFIED", guest_verified=False,
              full_host_report="build/m10a2-debug2-agent-check/report.json",
              complete_checks=len(full["checks"]), changed_since_full_report=differences,
              supplemental_kernel_compile=dict(exit_code=0, observed_exec_chunk="817001",
                  command="wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore gcc -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -msoft-float -mno-sse -mno-mmx -O2 -Wall -Wextra -Werror -Ibuild/m10a2-debug2-agent-check/integration/kernel -Ikernel -Ibuild/m10a2-debug2-agent-check/integration/user -Iuser -Iuser/runtime -c build/m10a2-debug2-agent-check/integration/kernel/sc32_debug.c -o build/m10a2-debug2-agent-check/debug-kernel.o",
                  sources=[dict(path=name, sha256=sha(DRAFT/name)) for name in changed_code]),
              reused="Unchanged application, SC32, ROOT, decoder and adapter sources retain the successful full host checks; documentation and patch generation are not guest execution evidence.",
              outputs=outputs,
              drafts=[dict(path=p.relative_to(DRAFT).as_posix(), sha256=sha(p)) for p in sorted(DRAFT.rglob("*"))
                      if p.is_file() and p.name not in ("completion.json", "integration.patch")])
(DRAFT / "completion.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(dict(status=record["status"], full_checks=record["complete_checks"], changed_code=changed_code, outputs=outputs), ensure_ascii=False))
