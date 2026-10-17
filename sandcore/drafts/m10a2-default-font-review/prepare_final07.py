"""读取已完成07实际PASS后准备新批次；原06及layout证据保持原字节。"""
from pathlib import Path
import argparse
import ast
import hashlib
import json

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-sha256',required=True)
p.add_argument('--refresh-sha256',required=True)
p.add_argument('--host-sha256',required=True)
a=p.parse_args()
for name,value,status in (
    ('build/m10a2-p6-target-refresh-07/p6-generation.json',a.native_sha256,'DECLARED_P6_NATIVE_GENERATION_PASS'),
    ('build/m10a2-p6-refresh-vm-07/refresh-vm.json',a.refresh_sha256,'EXPLICIT_LABEL_REFRESH_GENERATED')):
    path=ROOT/name;report=json.loads(path.read_bytes())
    assert sha(path)==value and report['status']==status
    assert report['current_inputs_unchanged'] and report['source_disk_unchanged'] and not report['changed_input_output_paths']
assert sha(ROOT/'build/m10a2-main-studio-01/p6-unified-host-13.json')==a.host_sha256
out=ROOT/'drafts/m10a2-p6-final-07-receipt'
out.mkdir(exist_ok=False)
old=ROOT/'drafts/m10a2-p6-final-06-receipt'
sources={}
for name in ('bindings.py','finalize.py','inspect_final.py'):
    source=old/name;body=source.read_text(encoding='utf-8');sources[str(source.relative_to(ROOT))]=sha(source)
    for before,after in (
        ('final-06','final-07'),('final06','final07'),('native06','native07'),('outer06','outer07'),
        ('refresh06','refresh07'),('target-refresh-06','target-refresh-07'),('refresh-vm-06','refresh-vm-07'),
        ('HOST12','HOST13'),('p6-unified-host-12.json','p6-unified-host-13.json'),
        ('7e29017e4c20095c0cf8ec2e5bbcd9ae90626fe974d12dfae4596d641548e750',a.host_sha256),
        ('f2f5d67a8946ebe07c12459e17d85f00cd6538a38f785298d11194fd4dd1c7ba',a.native_sha256),
        ('33ce11d3dd1ad4763d462fce27b94270a0d73fc9f7666f925e079feea8eb74ae',a.refresh_sha256)):
        body=body.replace(before,after)
    ast.parse(body);(out/name).write_bytes(body.encode())
(out/'PREPARATION.json').write_bytes((json.dumps(dict(status='FINAL07_WIRING_PREPARED_NOT_EXECUTED',
    original_helpers=sources,helpers={p.name:sha(p) for p in out.glob('*.py')},
    native_sha256=a.native_sha256,refresh_sha256=a.refresh_sha256,host_sha256=a.host_sha256,
    layout_recompiled=False,original_layout_proof_modified=False,core_built=False,sdk_staged=False),indent=2)+'\n').encode())
print('FINAL07_WIRING_PREPARED_NOT_EXECUTED')
