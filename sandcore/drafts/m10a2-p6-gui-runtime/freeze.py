"""只做Python语法和只读来源摘要；不构建/连接/运行VM或写主树。"""
import ast
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for path in HERE.glob("*.py"):ast.parse(path.read_text(encoding="utf-8"),filename=str(path))
    references=("drafts/m10a2-p6-gui-flow/GUI-FLOW.md","drafts/m10a2-p6-gui-flow/flow_input.py",
        "tools/scui_input.py","tools/verify_m10a2_ui_gui.py","tools/verify_m10a2_ui_platform.py",
        "tools/verify_m10a2_shell_runtime.py","tools/audit_m10a2_file_job_results.py",
        "tools/scserial.py","tools/verify_m9_sound.py","user/SCSHELL.H","user/SC32DEBUG.H",
        "user/shellworker/worker_recovery.c","user/scshell/SCSHELLWIRE.H",
        "user/studio2/studio_build.c","user/debug2/ui.c","user/debug2/model.c")
    value=dict(status="HELPER_AND_PLAN_IMPLEMENTED_NOT_EXECUTED",python_syntax="PASS",build_started=False,qemu_started=False,
        generation_files_written=False,main_written=False,gui_workflow_pass=False,
        entry="runtime.GuiRuntime + runtime.serve",outer_owns="QemuSession/GuiGuest/RefreshHarness and native/SDK preparation",
        operations=["run","bind","keys","type","click","drag","screen","get","audio_start","audio_stop","finish"],
        missing_runtime_inputs=["Root actual native execution VM/context","actual current screenshot points","ordinary selected SID and real product facts/ACKs/module observations"],
        files={p.name:sha(p) for p in sorted(HERE.iterdir()) if p.is_file() and p.name!="DELIVERY.json"},
        readonly_reference_sha256={name:sha(ROOT/name) for name in references})
    (HERE/"DELIVERY.json").write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(value["status"],"SYNTAX_PASS","NO_BUILD_NO_VM")


if __name__=="__main__":main()
