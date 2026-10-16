"""只提升本次真实SCCC/SCLD成功的三份原生清单。"""
from pathlib import Path
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
report_path=ROOT/'build/m10a2-p6-sco-12/report.json'
report=json.loads(report_path.read_bytes())
assert report['status']=='PASS'
targets={'SCUI':'scui/SCUI','STUDIO2':'studio2/STUDIO2','SETTINGS2':'settings2/SETTINGS2'}
assert {c['name'] for c in report['cases']}==set(targets)
rows=[]
for case in report['cases']:
    assert case['compile_exit']==case['link_exit']==0 and case['status']=='PASS'
    for path, expected in case['actual_compiler_input_sha256'].items():
        assert sha((ROOT/path).read_bytes())==expected,path
    origin=report_path.parent/(case['name']+'.NATIVE.JSON')
    body=origin.read_bytes()
    assert sha(body)==case['manifest_sha256']
    assert sha((report_path.parent/(case['name']+'.SCO')).read_bytes())==case['object_sha256']
    target=ROOT/'user'/(targets[case['name']]+'.NATIVE.JSON')
    template=target.with_name(target.name.replace('.NATIVE.JSON','.json' if case['name']=='SCUI' else '.JSON' if case['name']=='STUDIO2' else '.json'))
    actual=json.loads(body);published=json.loads(template.read_bytes())
    for key in ('name','abi_major','abi_minor','library_version','exports','init','fini'):
        assert actual.get(key)==published.get(key),(case['name'],key)
    rows.append((target,body,dict(label=case['name'],path=target.relative_to(ROOT).as_posix(),
        before_sha256=sha(target.read_bytes()),after_sha256=sha(body),
        object_sha256=case['object_sha256'])))
for target,body,row in rows:target.write_bytes(body)
(HERE/'native-manifest-promotion.json').write_text(json.dumps(dict(
    status='REAL_SCCC_HOST_IO_MANIFESTS_PROMOTED_NOT_GUEST_GENERATED',
    report_sha256=sha(report_path.read_bytes()),modified=[r for _,_,r in rows]),indent=2)+'\n')
print('Promoted:',','.join(targets))
