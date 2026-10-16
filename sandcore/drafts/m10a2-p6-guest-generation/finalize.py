"""记录本任务最终源码及已完成只读检查，不运行最终预检或VM。"""
import ast
import json
import model


def main():
    for path in model.HERE.glob("*.py"):ast.parse(path.read_text(encoding="utf-8"),filename=str(path))
    path=model.ROOT/"build/m10a2-p6-guest-generation-agent-check-02/report.json"
    checked=model.read_json(path)
    archive=model.read_json(model.HERE/"history/run-01/archive.json")
    value=dict(status="FOREGROUND_DRIVER_FIX_IMPLEMENTED_PENDING_ROOT_RERUN",
        qemu_started=False,guest_generation_run=False,final_runtime_preflight_run=False,
        scope="THIS_AGENT_FIX_ONLY_NO_QEMU_RUN",producer_identity="NOT_OBSERVED",
        previous_run=dict(report=archive["original_report"],sha256=archive["report_sha256"],qemu_pid=archive["qemu_pid"],
            reached="ACTUAL_STARTUP_AND_WARM",failure=archive["failure"],old_driver_sha256=archive["driver_sha256"],
            archive="history/run-01/archive.json"),
        new_ordinary_libraries=8,targets=27,
        source_only_and_parser_report=str(path.relative_to(model.ROOT)),report_sha256=model.sha(path),
        checks=[{k:v for k,v in entry.items() if k!="source_input_sha256"} for entry in checked["checks"]],
        final_python_syntax="PASS",source_scope="OWN_DRAFT_AND_BUILD_ONLY_MAIN_NOT_EDITED",
        runtime_inputs_needed="Root updates actual Native07/current source config and reruns existing Windows workflow",
        limits=["Foreground producer fix not run in QEMU by this agent", "Producer PID/generation explicitly NOT_OBSERVED and not a gate", "P3 standard-library behavior matrix reused externally, not rerun", "Native LOGIN2/five-libraries issuer/CORE must be rebuilt for following execution VM"],
        files={p.name:model.sha(p) for p in sorted(model.HERE.iterdir()) if p.is_file() and p.name!="completion.json"})
    (model.HERE/"completion.json").write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(value["status"],"FINAL_SYNTAX_PASS","QEMU_NOT_STARTED")


if __name__=="__main__":main()
