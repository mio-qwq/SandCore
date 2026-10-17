"""仅接native09真实终态，再准备同HOST16来源的CORE/严格SDK签发器。"""
from pathlib import Path
import ast,hashlib,json

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
HOST='build/m10a2-main-appfix-01/p6-unified-host-16.json'
HOST_SHA='a38c9b326d9bb89d8daab3761fd95bac3976d504fe3d424c016e5ff96c137e29'

def main():
    native=ROOT/'build/m10a2-p6-target-refresh-09/p6-generation.json'
    outer=ROOT/'build/m10a2-p6-refresh-vm-09/refresh-vm.json'
    n=json.loads(native.read_bytes());v=json.loads(outer.read_bytes());h=json.loads((ROOT/HOST).read_bytes())
    assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED'
    assert n['current_inputs_unchanged'] and n['source_disk_unchanged'] and not n['changed_input_output_paths']
    assert v['current_inputs_unchanged'] and v['source_disk_unchanged'] and not v['changed_input_output_paths']
    assert n['strict_reader_audit']['checks_not_weakened'] is True
    assert sha(ROOT/HOST)==HOST_SHA and v['host_report_sha256']==HOST_SHA and h['exit_code']==0
    assert (len(n['objects']),len(n['native_libraries']),len(n['native_artifacts']))==(27,8,21)
    assert {x['path']:x['sha256'] for x in h['inputs']}.items()>=n['source_input_sha256'].items()
    out=ROOT/'drafts/m10a2-p6-final-10-receipt';out.mkdir(exist_ok=False)
    original=ROOT/'drafts/m10a2-p6-final-08-receipt';sources={}
    for name in ('bindings.py','finalize.py','inspect_final.py'):
        source=original/name;s=source.read_text(encoding='utf-8');sources[str(source.relative_to(ROOT))]=sha(source)
        for before,after in (
            ('final-08','final-10'),('final08','final10'),
            ('build/m10a2-p6-target-refresh-08','build/m10a2-p6-target-refresh-09'),
            ('build/m10a2-p6-refresh-vm-08','build/m10a2-p6-refresh-vm-09'),
            ('build/m10a2-main-studio-01/p6-unified-host-14.json',HOST),
            ('d054c6115d97e75ac26245b602188b477d83f8cd792c43d202f721ab93495ff6',HOST_SHA),
            ('eeec17054e33fe9acefc6470d85b8e032fb52166522716c8a52261c44e925dc8',sha(native)),
            ('e7995034e0940fd4669dee99ee04a81fe15fbfb6515768dcfcd2aac83cde8412',sha(outer)),
            ('HOST14','HOST16'),('native08','native09'),('outer08','outer09'),('refresh08','refresh09')):
            s=s.replace(before,after)
        if name=='finalize.py':
            anchor='    OUT.mkdir(parents=True,exist_ok=False)';assert s.count(anchor)==1
            s=s.replace(anchor,anchor+'''
    # CORE所嵌LOGIN/桌面/五库和内核未变；保留旧目录，只复用编译产物，
    # 新路径依赖与发行头仍由本次make/只读检查核实。
    import shutil
    shutil.copytree(ROOT/'build/m10a2-p6-final-09/core',OUT/'core')
    for dependency in (OUT/'core').rglob('*.d'):
        body=dependency.read_bytes()
        dependency.write_bytes(body.replace(b'build/m10a2-p6-final-09/core',b'build/m10a2-p6-final-10/core'))
''')
        ast.parse(s);(out/name).write_bytes(s.encode('utf-8'))
    (out/'PREPARATION.json').write_text(json.dumps(dict(status='NATIVE09_HOST16_FINAL10_PREPARED_NOT_EXECUTED',original_helpers=sources,native_report_sha256=sha(native),outer_report_sha256=sha(outer),host_report_sha256=HOST_SHA,core_reuses_final09_objects=True,user_targets_changed=['SETTINGS2','TERMINAL2'],core_build_started=False),indent=2)+'\n',encoding='utf-8')
    print('FINAL10_BINDING_PREPARED_NOT_EXECUTED')

if __name__=='__main__':main()
