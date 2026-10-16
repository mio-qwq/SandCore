"""SCLD导出schema只有名称和种类；size属于导入合同，不能写到新导出。"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
rows=[]
for relative,names in (
    ('user/scui/SCUI.json',('ScUiDropDefer','ScUiDropQuery','ScUiDropReply','ScUiDropRelease')),
    ('user/scshell/SCSHELL.json',('ScShellDragBeginEx','ScShellDropFilesDeferred','ScShellDropReply'))):
    p=ROOT/relative;before=p.read_bytes();d=json.loads(before)
    for name in names:
        row=next(v for v in d['exports'] if v['name']==name)
        assert row.pop('size')==1
    ending='\r\n' if b'\r\n' in before else '\n'
    after=(json.dumps(d,ensure_ascii=False,indent=2)+'\n').replace('\n',ending).encode()
    p.write_bytes(after)
    rows.append(dict(path=relative,before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest(),corrected_exports=list(names)))
(HERE/'export-schema-fix.json').write_bytes((json.dumps(dict(status='SOURCE_MANIFEST_SCHEMA_CORRECTED_NOT_LINKED',modified=rows),indent=2)+'\n').encode())
print('Corrected seven new export records; import size fields preserved')
