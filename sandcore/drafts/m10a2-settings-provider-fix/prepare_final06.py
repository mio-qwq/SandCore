"""从已审final05执行器建立独立06批次；布局原件与历史来源不改。"""
from pathlib import Path
import argparse,ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-sha256',required=True)
p.add_argument('--refresh-sha256',required=True)
p.add_argument('--host-sha256',required=True)
a=p.parse_args()
for name,value,status in (
    ('build/m10a2-p6-target-refresh-06/p6-generation.json',a.native_sha256,'DECLARED_P6_NATIVE_GENERATION_PASS'),
    ('build/m10a2-p6-refresh-vm-06/refresh-vm.json',a.refresh_sha256,'EXPLICIT_LABEL_REFRESH_GENERATED')):
    path=ROOT/name;report=json.loads(path.read_bytes())
    assert sha(path)==value and report['status']==status
    assert report['current_inputs_unchanged'] and report['source_disk_unchanged'] and not report['changed_input_output_paths']
assert sha(ROOT/'build/m10a2-main-studio-01/p6-unified-host-12.json')==a.host_sha256
out=ROOT/'drafts/m10a2-p6-final-06-receipt'
out.mkdir(exist_ok=False)
old=ROOT/'drafts/m10a2-p6-final-05-receipt'
sources={}
for name in ('bindings.py','finalize.py','inspect_final.py'):
    source=old/name;s=source.read_text(encoding='utf-8');sources[str(source.relative_to(ROOT))]=sha(source)
    for before,after in (
        ('final-05','final-06'),('final05','final06'),('native05','native06'),('outer05','outer06'),
        ('refresh05','refresh06'),('target-refresh-05','target-refresh-06'),('refresh-vm-05','refresh-vm-06'),
        ('HOST11','HOST12'),('p6-unified-host-11.json','p6-unified-host-12.json'),
        ('15ae392845d5307dd3617778ee461ba53f2e87bc7533b31d29857db40b2b3785',a.host_sha256),
        ('9a21978c82563b1d9c31ce36d0384482c1f44dee56cb0ffcda33937e9a26257e',a.native_sha256),
        ('80a8a2fa91ee170272a1ac02d6bfc218c7297da0bd1c3460f8c597d6baffafbf',a.refresh_sha256)):
        s=s.replace(before,after)
    ast.parse(s)
    (out/name).write_text(s,encoding='utf-8')
(out/'PREPARATION.json').write_text(json.dumps(dict(status='FINAL06_WIRING_PREPARED_NOT_EXECUTED',
    original_helpers=sources,helpers={p.name:sha(p) for p in out.glob('*.py')},
    native_sha256=a.native_sha256,refresh_sha256=a.refresh_sha256,host_sha256=a.host_sha256,
    layout_recompiled=False,original_layout_proof_modified=False,core_built=False,sdk_staged=False),indent=2)+'\n',encoding='utf-8')
print('FINAL06_WIRING_PREPARED_NOT_EXECUTED')
