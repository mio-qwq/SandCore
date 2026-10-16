"""只做AST/来源摘要交接；不导入安装器、不读取/复制数据盘。"""
from pathlib import Path
import ast
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
REFERENCES=(
 'tools/stage_m10a2_sdk.py','tools/m10a2_sdk_final.py','tools/make_m10a2_codec_proof.py',
 'tools/mk_login_issue.py','tools/mk_shell_issue.py','tools/mkcore.py','tools/mkfs_m9.py','tools/mkfs_m10.py',
 'tools/upgrade_m10a2_defaults.py','tools/m10a2_defaults.json','tools/verify_m10a2_scl.py',
 'tools/scserial.py','tools/verify_m10a2_ui_gui.py','tools/verify_m10a2_shell_runtime.py',
 'm10a2.mk','m10a2-session.mk','m10a2-sound.mk','m10.mk',
 'kernel/login_role.inc','kernel/login_issue.inc','kernel/shell_issue.inc',
 'user/sound2/sound_jobs.c','docs/M10A2-DEFAULTS.md',
 'drafts/m10a2-p6-guest-generation/driver.py','drafts/m10a2-p6-guest-generation/model.py',
)
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    ast.parse((HERE/'prepare_candidate.py').read_text(encoding='utf-8'))
    refs={name:sha((ROOT/name).read_bytes()) for name in REFERENCES}
    (HERE/'SOURCE-PINS.json').write_text(json.dumps(dict(status='READ_ONLY_PREPARATION_NOT_EXECUTED',files=refs,
        expected_prepare_metadata='fold(rootless_path) -> (uid,gid,mode); old default behavior unchanged',
        execution_requires='Root metadata hook after generation, complete native PASS, current HOST archive report, SDK stage and native-bound CORE',
        source_data_sha256='3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64',
        vm_started=False,build_executed=False,installer_executed=False),indent=2)+'\n')
    files=[]
    for path in sorted(HERE.iterdir()):
        if path.is_file() and path.name!='DELIVERY.json':
            data=path.read_bytes();files.append(dict(path=path.name,bytes=len(data),sha256=sha(data)))
    result=dict(status='FROZEN_FINAL_FLOW_PREPARATION_NOT_EXECUTED',files=files,
        native_generation_current_status='Partial run03 progress only; actual final PASS required by helper',
        main_modified=False,git_used=False,vm_started=False,build_executed=False,data_copied=False)
    (HERE/'DELIVERY.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(files=len(files),source_references=len(refs),delivery_sha256=sha((HERE/'DELIVERY.json').read_bytes()))))
if __name__=='__main__':main()
