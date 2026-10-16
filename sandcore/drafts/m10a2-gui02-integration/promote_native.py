"""只提升真实SCO/严格SCLD通过且来源仍相同的12份清单。"""
from pathlib import Path
import hashlib,importlib.util,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('actual_cases',ROOT/'tools/check_m10a2_p6_objects.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
sha=lambda b:hashlib.sha256(b).hexdigest()
labels=('SCUI','SCSHELL','FILES2','DESKTOP','NOTES2','STUDIO2','SETTINGS2','INSTALLER2','CALC2','PALETTE2','MONITOR2','SOUND2')
reports={n:json.loads((ROOT/('build/m10a2-p6-sco-'+n)/'report.json').read_text()) for n in ('09','10')}
rows=[]
for label in labels:
    number='10' if label in ('SCUI','SCSHELL') else '09'
    report=reports[number];case=next(row for row in report['cases'] if row['name']==label)
    assert case['status']=='PASS' and case['compile_exit']==case['link_exit']==0,label
    for path,expected in case['actual_compiler_input_sha256'].items():assert sha((ROOT/path).read_bytes())==expected,path
    directory=ROOT/('build/m10a2-p6-sco-'+number)
    origin=directory/(label+'.NATIVE.JSON');body=origin.read_bytes()
    assert sha(body)==case['manifest_sha256']
    assert sha((directory/(label+'.SCO')).read_bytes())==case['object_sha256']
    _,template,_=module.CASES[label];target=(ROOT/'user'/template).with_suffix('.NATIVE.JSON')
    data=json.loads(body);current=json.loads((ROOT/'user'/template).read_bytes())
    for key in ('name','abi_major','abi_minor','library_version','exports','init','fini'):
        assert data.get(key)==current.get(key),(label,key)
    rows.append((target,body,dict(label=label,path=str(target.relative_to(ROOT)),origin=str(origin.relative_to(ROOT)),
                                before_sha256=sha(target.read_bytes()),after_sha256=sha(body),object_sha256=case['object_sha256'],
                                backend_report_sha256=sha((directory/'report.json').read_bytes()))))
for target,body,row in rows:target.write_bytes(body)
(HERE/'native-manifest-promotion.json').write_bytes((json.dumps(dict(status='ACTUAL_SCO_HOST_BACKEND_MANIFESTS_PROMOTED_NOT_GUEST_COMPILE',modified=[row for _,_,row in rows]),indent=2)+'\n').encode())
print('Promoted actual native manifests:',len(rows))
