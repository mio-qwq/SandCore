"""记录本任务最终源码及已完成只读检查，不运行最终预检或VM。"""
import ast
import json
import model


def main():
    for path in model.HERE.glob("*.py"):ast.parse(path.read_text(encoding="utf-8"),filename=str(path))
    path=model.ROOT/"build/m10a2-p6-guest-generation-agent-check-02/report.json"
    checked=model.read_json(path)
    value=dict(status="DRIVER_IMPLEMENTED_PENDING_ROOT_FINAL_PINS_AND_GUEST_GENERATION",
        qemu_started=False,guest_generation_run=False,final_runtime_preflight_run=False,
        new_ordinary_libraries=8,targets=27,
        source_only_and_parser_report=str(path.relative_to(model.ROOT)),report_sha256=model.sha(path),
        checks=[{k:v for k,v in entry.items() if k!="source_input_sha256"} for entry in checked["checks"]],
        final_python_syntax="PASS",source_scope="OWN_DRAFT_AND_BUILD_ONLY_MAIN_NOT_EDITED",
        runtime_inputs_needed="README schema=1 final HOST build/report, actual per-case NATIVE.JSON reports, original data pin, bootstrap HOST/issuer/resources, current layout",
        limits=["Windows QEMU/serial/PID barrier/owner ledger not executed this task", "Final HOST build/input pins not yet supplied", "P3 standard-library behavior matrix reused externally, not rerun", "Native LOGIN2/five-libraries issuer/CORE must be rebuilt for following execution VM"],
        files={p.name:model.sha(p) for p in sorted(model.HERE.iterdir()) if p.is_file() and p.name!="completion.json"})
    (model.HERE/"completion.json").write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(value["status"],"FINAL_SYNTAX_PASS","QEMU_NOT_STARTED")


if __name__=="__main__":main()
