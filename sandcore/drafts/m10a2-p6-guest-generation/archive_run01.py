"""修复前归档run-01实际冻结driver字节；不修改旧运行证据。"""
import json
from pathlib import Path
import model

OLD_DRIVER_SHA="d13353e6ef1127f3db94c9d0a961424c00f6fd7084217bf6e6508fd3e06cdb20"
RUN_REPORT_SHA="a21d1d7a6a8a099ba69f31ff3a5acbfecfffb312e10df0bfaf2086ba800dbe1c"


def main():
    report_path=model.ROOT/"build/m10a2-p6-guest-generation-run-01/p6-generation.json"
    report=model.read_json(report_path);driver=model.HERE/"driver.py"
    frozen=model.read_json(model.HERE/"completion.json")
    if model.sha(driver)!=OLD_DRIVER_SHA or frozen["files"]["driver.py"]!=OLD_DRIVER_SHA or model.sha(report_path)!=RUN_REPORT_SHA:
        raise ValueError("旧driver或run-01精确前像不同，不能误归档另一个版本")
    if report["current_inputs_unchanged"] is not True or report["changed_input_output_paths"]!=[]:
        raise ValueError("run-01缺冻结输入保持证明")
    target=model.HERE/"history/run-01"
    target.mkdir(parents=True,exist_ok=False)
    for name in ("driver.py","README.md","DESIGN.md","finalize.py","completion.json"):
        (target/name).write_bytes((model.HERE/name).read_bytes())
    active=report["active_case"]
    stdout=Path(active["stdout_path"])
    # 同一仓库在WSL只读检查时按已记录的run目录定位，不猜其它stdout。
    if not stdout.is_file():stdout=report_path.parent/active["stdout_path"].replace("\\","/").rsplit("/",1)[-1]
    if model.sha(stdout)!=active["stdout_sha256"] or stdout.stat().st_size!=active["stdout_bytes"]:
        raise ValueError("旧producer stdout不同")
    (target/"producer.stdout.bin").write_bytes(stdout.read_bytes())
    value=dict(scope="EXACT_RUN01_DRIVER_BEFORE_FOREGROUND_FIX",driver_sha256=OLD_DRIVER_SHA,
        original_report=report_path.relative_to(model.ROOT).as_posix(),report_sha256=RUN_REPORT_SHA,qemu_pid=report["qemu_pid"],
        current_inputs_unchanged=True,changed_input_output_paths=[],source_disk_unchanged=report["source_disk_unchanged"],
        failure="OPTIONAL_IDENTITY_OBSERVER_REJECTED_SH_SCX_BACKGROUND_WRAPPER_NOT_COMPILER_OS_FAILURE",
        actual_producer_pid_generation="NOT_OBSERVED",shell_wrapper_pid=1,
        active_case=active["name"],stdout_sha256=active["stdout_sha256"],stdout_bytes=active["stdout_bytes"],
        files={p.name:model.sha(p) for p in sorted(target.iterdir()) if p.is_file()})
    (target/"archive.json").write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("ARCHIVED_EXACT_RUN01_DRIVER",OLD_DRIVER_SHA)


if __name__=="__main__":main()
