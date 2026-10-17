"""只提升本批实际成功SCCC对象的导入表；失败产物原样保留。"""
from pathlib import Path
import hashlib
import json

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[('m10a2-p6-sco-14',('SCUI','DEBUG2','FILES2','SETTINGS2')),
         ('m10a2-p6-sco-15',('INSTALLER2',))]
folders=dict(SCUI='scui',DEBUG2='debug2',FILES2='files2',SETTINGS2='settings2',INSTALLER2='settings2')
pending=[]
for folder,labels in sources:
    report_path=ROOT/'build'/folder/'report.json'
    report=json.loads(report_path.read_bytes())
    for label in labels:
        case=next(row for row in report['cases'] if row['name']==label)
        assert case['status']=='PASS' and case['compile_exit']==case['link_exit']==0
        for name,expected in case['actual_compiler_input_sha256'].items():
            assert sha(ROOT/name)==expected,name
        name=label+'.NATIVE.JSON'
        origin=report_path.parent/name
        assert sha(origin)==case['manifest_sha256']
        assert sha(report_path.parent/(label+'.SCO'))==case['object_sha256']
        destination=ROOT/'user'/folders[label]/name
        native=json.loads(origin.read_bytes());formal=json.loads(destination.with_name(label+'.json').read_bytes())
        for key in ('kind','name','exports','init','fini'):assert native.get(key)==formal.get(key)
        pending.append((origin,destination,dict(label=label,report=str(report_path.relative_to(ROOT)),
            report_sha256=sha(report_path),before_sha256=sha(destination),after_sha256=sha(origin))))
for origin,destination,_ in pending:destination.write_bytes(origin.read_bytes())
receipt=dict(status='HOST_SCCC_IMPORT_TABLES_PROMOTED_NOT_GUEST_GENERATED',
             files=[row for _,_,row in pending])
Path(__file__).with_name('manifest-promotion.json').write_bytes((json.dumps(receipt,indent=2)+'\n').encode())
print('Promoted SCUI, DEBUG2, FILES2, SETTINGS2, INSTALLER2')
