"""只提升实际SCO13两目标导入清单，不把宿主编译称为客体生成。"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
source=ROOT/'build/m10a2-p6-sco-13/report.json'
report=json.loads(source.read_bytes())
assert report['status']=='PASS' and {r['name'] for r in report['cases']}=={'SETTINGS2','INSTALLER2'}
rows=[]
for row in report['cases']:
    assert row['compile_exit']==row['link_exit']==0 and row['status']=='PASS'
    for name,expected in row['actual_compiler_input_sha256'].items():
        assert sha((ROOT/name).read_bytes())==expected,name
    name=row['name']+'.NATIVE.JSON'
    origin=source.parent/name;target=ROOT/'user/settings2'/name
    body=origin.read_bytes()
    assert sha(body)==row['manifest_sha256']
    assert sha((source.parent/(row['name']+'.SCO')).read_bytes())==row['object_sha256']
    manifest=json.loads(body);formal=json.loads(target.with_name(row['name']+'.json').read_bytes())
    for key in ('kind','name','exports','init','fini'):
        assert manifest.get(key)==formal.get(key)
    rows.append((target,body,dict(label=row['name'],path=target.relative_to(ROOT).as_posix(),
        before_sha256=sha(target.read_bytes()),after_sha256=sha(body),object_sha256=row['object_sha256'])))
for path,body,_ in rows:path.write_bytes(body)
(HERE/'native-manifest-promotion.json').write_text(json.dumps(dict(
    status='REAL_SCCC_HOST_IO_MANIFESTS_PROMOTED_NOT_GUEST_GENERATED',
    report_sha256=sha(source.read_bytes()),modified=[r for _,_,r in rows]),indent=2)+'\n')
print('Promoted SETTINGS2,INSTALLER2')
