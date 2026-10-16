"""只生成独占草稿；不把修复写回主树，也不启动编译或客体。"""
from __future__ import annotations

import difflib
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXPECTED = {
    "user/studio2/studio_build.c": "69249a23196d36ac5420e336bc605b37025d279d4721a2b75471fbf81dbfd4a0",
    "user/studio2/studio_io.c": "df36f817150447f5b48e609a575aa8d99597e96bf3c71e1550897faa9fb5d587",
    "user/studio2/STUDIO2.JSON": "eed94567f1d2e7db7434cf4e6e6161be57ac9b65229b7b85cdd1f0b3fa615bbb",
}

OLD_NAMESPACE = '''static ScStatus namespace_pin(StPin *p)
{
    ScPathInfo info;
    rec(&info,sizeof(info));
    ScStatus s=ScPathQuery("/SYS/INC",&info);
    if(s<0)return s;
    ScLeaseOpenDesc d;
    rec(&d,sizeof(d));
    d.path="/SYS/INC";
    d.object_kind=SC32_PATH_DIRECTORY;
    d.lease_kind=SC32_LEASE_NAMESPACE;
    d.expected_revision=info.generation;
    d.flags=SC32_LEASE_READ_ONLY_NAMESPACE|SC32_LEASE_METADATA_ONLY;
    rec(&p->info,sizeof(p->info));
    s=ScFileLeaseOpenPinned(&d,&p->info);
    if(s>=0)p->live=1;
    return s;
}'''

NEW_NAMESPACE = '''static ScStatus namespace_pin(StPin *p)
{
    if(p->live)return SC32_E_BUSY;
    ScPathInfo path_info;
    rec(&path_info,sizeof(path_info));
    ScStatus s=ScPathQuery("/SYS/INC",&path_info);
    if(s<0)return s;
    if(path_info.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
    ScDirectory directory=0;
    s=ScDirectoryOpen("/SYS/INC",&directory);
    if(s<0)return s;
    /* 先交给构建清理状态机持有。后续失败不能丢掉还未关闭的目录，
     * 尤其不能把一次返回BUSY的Close当成已回收。 */
    rec(&p->info,sizeof(p->info));
    p->info.directory=directory;
    p->live=1;
    ScDirectoryEntry row;
    ScDirectoryPage page;
    rec(&page,sizeof(page));
    s=ScDirectoryNext(directory,&row,1,&page);
    if(s<0)return s;
    /* revision与全局目录epoch是两种身份。只读Acquire固定实际子树，
     * 仍允许编译器独立读头文件；META的OpenPinned不能代替这份合同。 */
    ScLeaseAcquireDesc d;
    ScLeaseInfo acquired;
    rec(&d,sizeof(d));
    rec(&acquired,sizeof(acquired));
    d.kind=SC32_LEASE_NAMESPACE;
    d.directory=directory;
    d.expected_revision=path_info.generation;
    d.expected_epoch=page.generation;
    d.flags=SC32_LEASE_READ_ONLY_NAMESPACE;
    s=ScFileLeaseAcquire(&d,&acquired);
    if(s>=0)p->info=acquired;
    return s;
}'''

OLD_CLOSE = '''static ScStatus pin_close(StPin *p)
{
    if(!p->live)return 0;
    if(p->info.file){
        ScStatus s=ScFileClose(p->info.file);
        if(s==SC32_E_BUSY||s==SC32_E_AGAIN)return SC32_PENDING;
        p->info.file=0;
    }
    ScLeaseControl c;
    ScLeaseInfo info;
    pin_control(p,&c);
    rec(&info,sizeof(info));
    ScStatus s=ScFileLeaseRelease(&c,&info);
    if(info.lease==p->info.lease)p->info=info;
    if(s==0||s==SC32_E_STALE){
        memset(p,0,sizeof(*p));
        return 0;
    }
    return s;
}'''

NEW_CLOSE = '''static ScStatus pin_close(StPin *p)
{
    if(!p->live)return 0;
    if(p->info.file){
        ScStatus s=ScFileClose(p->info.file);
        if(s==SC32_PENDING||s==SC32_E_BUSY||s==SC32_E_AGAIN)return SC32_PENDING;
        if(s<0&&s!=SC32_E_STALE)return s;
        p->info.file=0;
    }
    if(p->info.directory){
        ScStatus s=ScDirectoryClose(p->info.directory);
        if(s==SC32_PENDING||s==SC32_E_BUSY||s==SC32_E_AGAIN)return SC32_PENDING;
        if(s<0&&s!=SC32_E_STALE)return s;
        p->info.directory=0;
    }
    /* Acquire失败时也持有待关目录，但没有lease；Close成功才可清空。
     * 正常目录的Release仅解除绑定，不会替应用关闭既有目录句柄。 */
    if(!p->info.lease){
        memset(p,0,sizeof(*p));
        return 0;
    }
    ScLeaseControl c;
    ScLeaseInfo info;
    pin_control(p,&c);
    rec(&info,sizeof(info));
    ScStatus s=ScFileLeaseRelease(&c,&info);
    if(info.lease==p->info.lease)p->info=info;
    if(s==0||s==SC32_E_STALE){
        memset(p,0,sizeof(*p));
        return 0;
    }
    return s;
}'''

OLD_CLEAN = '''    if(b->phase==S_BUILD_CLEAN){
        if(b->next<b->input_count){
            ScStatus s=pin_close(&b->inputs[b->next].pin);
            if(s==SC32_PENDING)return;
            b->next++;
            return;
        }
        ScStatus s=pin_close(&b->manifest);
        if(s==SC32_PENDING)return;
        s=pin_close(&b->system_headers);
        if(s==SC32_PENDING)return;
        b->phase=0;'''

NEW_CLEAN = '''    if(b->phase==S_BUILD_CLEAN){
        /* 只有已确认关闭才推进。失败仍保留pin，下个安全点重试，
         * 不让下一次构建memset吞掉尚活的目录或lease引用。 */
        if(b->next<b->input_count){
            ScStatus s=pin_close(&b->inputs[b->next].pin);
            if(s){if(s<0&&!b->error)b->error=s;return;}
            b->next++;
            return;
        }
        ScStatus s=pin_close(&b->manifest);
        if(s){if(s<0&&!b->error)b->error=s;return;}
        s=pin_close(&b->system_headers);
        if(s){if(s<0&&!b->error)b->error=s;return;}
        b->phase=0;'''

ADDED_IMPORTS = ["ScDirectoryClose", "ScDirectoryNext", "ScDirectoryOpen", "ScFileLeaseAcquire"]


def sha(body: bytes) -> str:
    return hashlib.sha256(body).hexdigest()


def changed_once(body: str, before: str, after: str) -> str:
    if body.count(before) != 1:
        raise ValueError("固定原文不是唯一匹配")
    return body.replace(before, after, 1)


def transform(relative: str, body: str) -> str:
    if relative.endswith("studio_build.c"):
        return changed_once(changed_once(body, OLD_NAMESPACE, NEW_NAMESPACE), OLD_CLEAN, NEW_CLEAN)
    if relative.endswith("studio_io.c"):
        return changed_once(body, OLD_CLOSE, NEW_CLOSE)
    desc = json.loads(body)
    names = {r["name"] for r in desc["imports"]}
    if names.intersection(ADDED_IMPORTS):
        raise ValueError("原模板意外已含新增导入")
    for name in ADDED_IMPORTS:
        desc["imports"].append(dict(name=name, dependency="SC32API.SCL", kind="function", size=1))
    desc["imports"].sort(key=lambda r: r["name"])
    return json.dumps(desc, ensure_ascii=False, indent=2) + "\n"


def main() -> None:
    pins = {}
    patches = []
    for relative, expected in EXPECTED.items():
        before_bytes = (ROOT / relative).read_bytes()
        if sha(before_bytes) != expected:
            raise ValueError("主树原像已改变：" + relative)
        newline = "\r\n" if b"\r\n" in before_bytes else "\n"
        before = before_bytes.decode("utf-8").replace("\r\n", "\n")
        after = transform(relative, before)
        after_bytes = after.replace("\n", newline).encode("utf-8")
        target = HERE / "overlay" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(after_bytes)
        pins[relative] = dict(before_sha256=expected, after_sha256=sha(after_bytes),
                              before_bytes=len(before_bytes), after_bytes=len(after_bytes))
        patches.extend(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                                           fromfile="a/" + relative, tofile="b/" + relative, n=3))

    scw = ROOT / "drafts/m10a2-scw-rename"
    scw_pins = json.loads((scw / "PINS.json").read_text(encoding="utf-8"))
    scw_renames = json.loads((scw / "RENAMES.json").read_text(encoding="utf-8"))
    io_relative = "user/studio2/studio_io.c"
    scw_bytes = (scw / "overlay" / io_relative).read_bytes()
    if sha(scw_bytes) != scw_pins["modified"][io_relative]["after_sha256"]:
        raise ValueError("SCW after-image 已改变")
    scw_newline = "\r\n" if b"\r\n" in scw_bytes else "\n"
    scw_text = scw_bytes.decode("utf-8").replace("\r\n", "\n")
    merged = changed_once(scw_text, OLD_CLOSE, NEW_CLOSE).replace("\n", scw_newline).encode("utf-8")
    if "user/studio2/studio_build.c" in scw_pins["modified"]:
        raise ValueError("SCW 意外修改了 namespace 所在文件")
    exported = {r["name"] for r in json.loads((ROOT / "user/sc32/SC32API.NATIVE.JSON").read_text())["exports"]}
    if not set(ADDED_IMPORTS).issubset(exported):
        raise ValueError("SC32API 缺少既有导出")
    patch = "".join(patches).encode("utf-8")
    (HERE / "studio-namespace.patch").write_bytes(patch)
    record = dict(status="SOURCE_PATCH_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED", modified=pins,
                  patch_sha256=sha(patch), added_host_imports=ADDED_IMPORTS,
                  native_manifest="ROOT_REGENERATE_FROM_ACTUAL_SCO_NOT_EDITED",
                  scw=dict(pins_sha256=sha((scw / "PINS.json").read_bytes()),
                           renames_sha256=sha((scw / "RENAMES.json").read_bytes()),
                           io_before_sha256=scw_pins["modified"][io_relative]["before_sha256"],
                           io_after_scw_sha256=sha(scw_bytes), io_after_both_sha256=sha(merged),
                           close_hunk_unique_after_scw=True, studio_build_not_modified_by_scw=True,
                           renames_read=len(scw_renames)))
    (HERE / "PINS.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    # 只核对已经存在的现场来源，不把本次静态修改记成新的客体执行。
    gui_path = ROOT / "build/m10a2-p6-gui-run-01/p6-gui.json"
    gui = json.loads(gui_path.read_text(encoding="utf-8"))
    native_path = ROOT / "build/m10a2-p6-guest-generation-run-03/p6-generation.json"
    native_bytes = native_path.read_bytes()
    if sha(native_bytes) != gui["native_report_sha256"]:
        raise ValueError("GUI引用的原生来源报告摘要不匹配")
    native = json.loads(native_bytes)
    source_paths = list(EXPECTED) + [
        "user/studio2/STUDIO2.H", "user/studio2/STUDIO2.NATIVE.JSON",
        "user/SC32LEASE.H", "user/sc32/sc32_file.c", "user/sc32/sc32_path.c",
        "user/sc32/sc32_support.c", "kernel/fs_lease_open.inc",
        "kernel/fs_lease_index.inc", "kernel/fs_lease_cleanup.inc",
        "kernel/sc32_file.c", "kernel/sc32_file_lease.inc", "kernel/fs.c",
        "examples/m10a2-sdk/researchdesk/RDSTATS.SCP",
    ]
    source_review = {}
    for relative in source_paths:
        actual = sha((ROOT / relative).read_bytes())
        observed = [digest for name, digest in gui["input_sha256"].items()
                    if name.replace("\\", "/").lower().endswith("/" + relative.lower())]
        if not observed or any(digest != actual for digest in observed):
            raise ValueError("当前源与GUI输入pin不一致：" + relative)
        source_review[relative] = dict(sha256=actual, gui_pin_match=True)
    for relative in EXPECTED:
        if native["preflight"]["source_closures"]["STUDIO2"][relative] != source_review[relative]["sha256"]:
            raise ValueError("当前源与原生STUDIO2闭包不一致：" + relative)
    shot = ROOT / "build/m10a2-p6-gui-run-01/p6-native-studio-plugin-build-result.png"
    shot_sha = sha(shot.read_bytes())
    matching_shots = [r for r in gui["screenshots"] if r["sha256"] == shot_sha]
    if len(matching_shots) != 1:
        raise ValueError("现场截图没有唯一报告记录")
    artifact = native["native_artifacts"]["STUDIO2.SCX"]
    old_image = native_path.parent / "native-artifacts/STUDIO2.SCX"
    if sha(old_image.read_bytes()) != artifact["sha256"]:
        raise ValueError("原生Studio2映像与报告不匹配")
    review = dict(status="DETERMINISTIC_SOURCE_BLOCKER_IDENTIFIED_PATCH_NOT_EXECUTED",
                  original_native_report=str(native_path), native_report_sha256=sha(native_bytes),
                  original_native_artifact=artifact,
                  screenshot=dict(path=str(shot), sha256=shot_sha, inspected=True, width=1920, height=1080),
                  source_review=source_review,
                  invalid_request=dict(path="/SYS/INC", object_kind=2, lease_kind=3,
                                       open_flags=0, requested_flags=3, open_pinned_allowed_flags=2,
                                       status=-101, stage="S_BUILD_SAVE_before_first_command"),
                  replacement=dict(namespace_kind=3, flags=1, next_capacity=1,
                                   expected_revision="ScPathInfo.generation",
                                   expected_epoch="ScDirectoryPage.generation"),
                  actual_new_compile_or_vm=False,
                  exact_runtime_call_pc_observed=False,
                  read_only_scope="Existing reports, sources and screenshot only; no guest input or process inspection")
    (HERE / "REVIEW.json").write_text(json.dumps(review, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    delivery = {str(p.relative_to(HERE)).replace("\\", "/"): dict(sha256=sha(p.read_bytes()), bytes=p.stat().st_size)
                for p in sorted(HERE.rglob("*")) if p.is_file() and p.name != "DELIVERY.json"}
    (HERE / "DELIVERY.json").write_text(json.dumps(dict(status=record["status"], files=delivery), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
