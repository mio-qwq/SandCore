"""只读审计既有 GUI01 与实际 ELF/SCDB/map；不导入或启动任何 VM 框架。"""
import ast
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import owner_snapshot as observer

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    gui_path = ROOT / "build/m10a2-p6-gui-run-01/p6-gui.json"
    gui = json.loads(gui_path.read_text(encoding="utf-8"))
    core = ROOT / "build/m10a2-p6-final-01/core/core.elf"
    if sha(core) != gui["core_binding"]["elf_sha256"]:
        raise ValueError("必须审计 GUI01 实际安装的 core，不是 HOST bootstrap 同名 ELF")
    scoped = observer.elf_objects(core)
    waits = {k: v for k, v in scoped.items() if k.endswith("::wait_pages")}
    if len(waits) != 1 or set(waits) != {"task.c::wait_pages"}:
        raise ValueError("wait_pages 作用域不唯一")
    native_path = ROOT / "build/m10a2-p6-guest-generation-run-03/p6-generation.json"
    if sha(native_path) != gui["native_report_sha256"]:
        raise ValueError("原生来源报告改变")
    native = json.loads(native_path.read_text(encoding="utf-8"))
    row = native["native_artifacts"]["SCSHELL.SCX"]
    image = native_path.parent / "native-artifacts/SCSHELL.SCX"
    sidecar, mapfile = Path(str(image) + ".sdbg"), Path(str(image) + ".map")
    layout = native["preflight"]["task_layout"]
    sdbg = observer.desktop_binding(image, row["sha256"], sidecar, row["debug"]["sha256"], layout)
    maps = observer.desktop_binding(image, row["sha256"], mapfile, row["map_sha256"], layout)
    if sdbg["desk"] != maps["desk"] or sdbg["desk_size"] != maps["desk_size"]:
        raise ValueError("真实 SCDB 与 map 的 Desktop 身份不一致")
    for name in ("owner_snapshot.py", "audit.py"):
        ast.parse((HERE / name).read_text(encoding="utf-8"))
    case = next(r for r in gui["cases"] if r["name"] == "p6-gui-single-final-owner-account")
    if case["status"] != "FAIL" or case["physical_delta"] != 205:
        raise ValueError("不是本次要审阅的原始失败")
    base = gui["baseline"]["resources"]
    pinned = {}
    for name in observer.PRIVATE_SOURCE_PINS:
        vals = [v for k, v in gui["input_sha256"].items() if k.replace("\\", "/").endswith("/" + name)]
        if not vals or any(v != observer.PRIVATE_SOURCE_PINS[name] for v in vals):
            raise ValueError("私有结构源 pin 不一致：" + name)
        pinned[name] = vals[0]
    review = dict(status="OFFLINE_SOURCE_REVIEW_NOT_NEW_RUNTIME_PASS", original_report=str(gui_path),
                  original_report_sha256=sha(gui_path), original_status=gui["status"],
                  original_ledger=case, current_inputs_unchanged=gui["current_inputs_unchanged"],
                  source_disk_unchanged=gui["source_disk_unchanged"], core_elf_sha256=sha(core),
                  scoped_wait=waits, native_pools={k: v for k, v in scoped.items() if k.endswith("::native_pool")},
                  baseline_scalar_wait_pages=base["wait_pages"], baseline_finite_wait_heap=base["finite_wait_heap"],
                  scalar_and_finite_wait_disagree_in_baseline=base["wait_pages"] != base["finite_wait_heap"]["pages"],
                  physical_delta=205, known_fs_pool_delta=-6, remainder_after_known_fs_decrease=211,
                  cache_attribution="NOT_OBSERVED_NOT_EXEMPTED", final_full_resources_saved=False,
                  baseline_owner_page_tables_saved=False, scdb_binding=sdbg, map_binding=maps,
                  source_pins=pinned, helper_vm_path_executed=False, new_build=False, new_vm=False)
    (HERE / "REVIEW.json").write_text(json.dumps(review, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    delivery = {p.name: dict(sha256=sha(p), bytes=p.stat().st_size) for p in sorted(HERE.iterdir())
                if p.is_file() and p.name != "DELIVERY.json"}
    (HERE / "DELIVERY.json").write_text(json.dumps(dict(status=review["status"], files=delivery), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: review[k] for k in ("scoped_wait", "native_pools", "scalar_and_finite_wait_disagree_in_baseline", "scdb_binding", "cache_attribution")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
