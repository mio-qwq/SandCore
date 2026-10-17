"""准备一轮快拖定向会话；只更新观察器支持的精确task分派差异。"""
from pathlib import Path
import ast,hashlib,json

ROOT=Path(__file__).resolve().parents[2]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    before=ROOT/'drafts/m10a2-release-drag-01/before/kernel/task.c'
    current=ROOT/'kernel/task.c'
    old_sha='40495fad1e36bdc1e83e2787e6e18a4da710eea1553959a8ba3ca6f543ece2ef'
    assert sha(before)==old_sha
    original=before.read_text(encoding='utf-8');changed=current.read_text(encoding='utf-8')
    old='frame[8]<=SC32_D_CANCEL';new='frame[8]<=SC32_D_AT_RELEASE'
    assert original.count(old)==1 and changed==original.replace(old,new)
    out=ROOT/'drafts/m10a2-p6-gui-12';out.mkdir(exist_ok=False)
    observer=ROOT/'drafts/m10a2-gui-owner-review/owner_snapshot.py'
    assert sha(observer)=='25d87a8f1b5cbac487ed740416a65031a1c456a9cbaf48505ba739c651f859dc'
    body=observer.read_text(encoding='utf-8');assert body.count(old_sha)==1
    (out/'owner_snapshot.py').write_bytes(body.replace(old_sha,sha(current)).encode('utf-8'))
    coherent=ROOT/'drafts/m10a2-p6-gui-coherent/coherent_runtime.py'
    body=coherent.read_text(encoding='utf-8')
    assert body.count("OBSERVER = HERE.parent / 'm10a2-gui-owner-review/owner_snapshot.py'")==1
    body=body.replace("OBSERVER = HERE.parent / 'm10a2-gui-owner-review/owner_snapshot.py'","OBSERVER = HERE / 'owner_snapshot.py'")
    body=body.replace(sha(observer),sha(out/'owner_snapshot.py'))
    (out/'coherent_runtime.py').write_bytes(body.encode('utf-8'))
    source=ROOT/'drafts/m10a2-p6-gui-11/driver.py'
    body=source.read_text(encoding='utf-8')
    body=body.replace("HERE.parent/'m10a2-p6-gui-coherent'",'HERE')
    body=body.replace("HERE.parent/'m10a2-p6-followup-refresh-v6'","HERE.parent/'m10a2-p6-followup-refresh-v9'")
    anchor='    owner_plan = ObserverPlan(candidate, args.layout_report)'
    assert body.count(anchor)==1
    body=body.replace(anchor,'''    # 仅增加独立公开SDK探针的源码/严格导入清单，不安装宿主SCX。
    probe_report_path=ROOT/'build/m10a2-drop-release-probe-03/report.json'
    probe_report=json.loads(probe_report_path.read_bytes())
    if probe_report['status']!='ONE_PROBE_SCCC_BACKEND_AND_STRICT_LINK_PASS_GUEST_PENDING':raise ValueError('探针尚未通过真实后端')
    probe_pins={probe_report_path.resolve():sha(probe_report_path)}
    for local,destination in (('drafts/m10a2-drop-release-probe/DROPUP.C','SYS/SDK/EXAMPLES/DROPUP/DROPUP.C'),
                               ('build/m10a2-drop-release-probe-03/UP.JSON','SYS/SDK/EXAMPLES/DROPUP/UP.JSON')):
        path=ROOT/local;row=probe_report['files'][str(Path(local))]
        if path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:raise ValueError('探针输入改变')
        candidate.add(destination,path.read_bytes(),'INDEPENDENT_PUBLIC_SDK_INPUT_NOT_HOST_EXECUTABLE')
        probe_pins[path.resolve()]=row['sha256']
''' + anchor)
    body=body.replace('    pins = dict(candidate.input_pins.pins)','    pins = dict(candidate.input_pins.pins)\n    pins.update(probe_pins)')
    body=body.replace("choices=(0,350),default=0", "choices=(0,),default=0")
    body=body.replace("drag_diagnostic=dict(hold_before_release_ms=args.drag_settle_ms,product_code_changed=False,reason='GUI10 ended drag before confirmation; physical hold is diagnostic, not a fix')", "drag_regression=dict(hold_before_release_ms=0,product_code_changed=True,reason='actual release receipt; original GUI10 failure preserved')")
    (out/'driver.py').write_bytes(body.encode('utf-8'))
    for path in out.glob('*.py'):ast.parse(path.read_text(encoding='utf-8'))
    report=dict(status='GUI12_PREPARED_NOT_STARTED',
        originals={str(p.relative_to(ROOT)):sha(p) for p in (source,observer,coherent,before)},
        task_source_sha256=sha(current),task_change='only syscall selector upper bound CANCEL -> AT_RELEASE',
        private_structure_fields_changed=False,observer_logic_changed=False,layout02_reused=True,
        checks_removed=False,old_owner205_exempted=False,hold_before_release_ms=0,
        new_helpers={p.name:sha(p) for p in out.glob('*.py')})
    (out/'PREPARATION.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('GUI12_PREPARED_NOT_STARTED')


if __name__=='__main__':main()
