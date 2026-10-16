"""只读语法与来源封装，不导入运行层/不构建/不启动VM。"""
from pathlib import Path
import ast
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for path in HERE.glob('*.py'):ast.parse(path.read_text(encoding='utf-8'),filename=str(path))
    refs=('tools/stage_m10a2_sdk.py','tools/m10a2_sdk_final.py','tools/refresh_m10a2_ui_native.py',
        'drafts/m10a2-p6-final-flow/prepare_candidate.py','drafts/m10a2-p6-guest-generation/model.py',
        'build/m10a2-p6-guest-generation-run-03/p6-generation.json')
    result=dict(status='IMPLEMENTED_UNEXECUTED_TARGET_REFRESH',syntax='PASS',build_started=False,qemu_started=False,
        main_written=False,prior_generation_written=False,gui_controller_written=False,candidate_patch_required=False,
        final_patch_and_runtime_inputs='PENDING_ROOT',current_assumption='SCSHELL plus any actually changed transitive label',
        files={p.name:sha(p) for p in sorted(HERE.iterdir()) if p.is_file() and p.name!='DELIVERY.json'},
        readonly_references={name:sha(ROOT/name) for name in refs})
    (HERE/'DELIVERY.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(result['status'],'SYNTAX_PASS','NO_BUILD_NO_VM')


if __name__=='__main__':main()
