"""复用GUI12观察器，仅增加固定登录界面的真实PS/2输入范围。"""
from pathlib import Path
import ast,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    src=ROOT/'drafts/m10a2-p6-gui-12';out=ROOT/'drafts/m10a2-p6-gui-13';out.mkdir(exist_ok=False)
    for name in ('coherent_runtime.py','owner_snapshot.py'):shutil.copyfile(src/name,out/name)
    body=(src/'driver.py').read_text(encoding='utf-8')
    start=body.index('    # 仅增加独立公开SDK探针的源码/严格导入清单，不安装宿主SCX。')
    end=body.index('    owner_plan = ObserverPlan(candidate, args.layout_report)',start)
    body=body[:start]+body[end:]
    body=body.replace('    pins.update(probe_pins)\n','')
    body=body.replace("HERE.parent/'m10a2-p6-followup-refresh-v9'","HERE.parent/'m10a2-p6-followup-refresh-v10'")
    body=body.replace('from coherent_runtime import CoherentRuntime, ObserverPlan','from coherent_runtime import CoherentRuntime, ObserverPlan\nfrom login_runtime import LoginRuntime')
    body=body.replace('runtime = CoherentRuntime(flow, harness,','runtime = LoginRuntime(flow, harness,')
    (out/'driver.py').write_text(body,encoding='utf-8')
    shutil.copyfile(Path(__file__).with_name('login_runtime.py'),out/'login_runtime.py')
    for p in out.glob('*.py'):ast.parse(p.read_text(encoding='utf-8'))
    (out/'PREPARATION.json').write_text(json.dumps(dict(status='PREPARED_NO_VM_STARTED',scope='actual leave desktop then logout and local passwordless sign-in',
        auxiliary_drop_probe_not_installed=True,ordinary_guards_unchanged=True,owner_observer_unchanged=True,
        files={p.name:sha(p) for p in out.glob('*.py')}),indent=2)+'\n',encoding='utf-8')
    print('GUI13_LOGIN_SCOPE_PREPARED_NOT_STARTED')
if __name__=='__main__':main()
