"""精确核前像/交付像，保留未改行原字节并合并独立的TEXT1顶部函数。"""
from pathlib import Path
import difflib
import hashlib
import json
import sys
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
DROP=ROOT/'drafts/m10a2-drop-source-refresh'
TEXT=ROOT/'drafts/m10a2-shell-text-clipboard'
sha=lambda b:hashlib.sha256(b).hexdigest()
norm=lambda b:b.replace(b'\r\n',b'\n')
pins=json.loads((DROP/'PINS.json').read_text(encoding='utf-8'))
rows=[]

def preserve(old,new):
    old_lines=old.splitlines(keepends=True)
    a=[norm(v) for v in old_lines];b=norm(new).splitlines(keepends=True)
    ending=b'\r\n' if old.count(b'\r\n')>old.count(b'\n')//2 else b'\n'
    output=[]
    for kind,i,j,k,l in difflib.SequenceMatcher(None,a,b,autojunk=False).get_opcodes():
        if kind=='equal':output.extend(old_lines[i:j])
        else:output.extend(v.replace(b'\n',ending) for v in b[k:l])
    body=b''.join(output)
    assert norm(body)==norm(new)
    return body

for path,row in list(pins['modified'].items())+list(pins['added'].items()):
    target=ROOT/path;origin=DROP/'overlay'/path
    assert target.resolve().is_relative_to(ROOT)
    new=origin.read_bytes();assert sha(new)==row['result_sha256'],str(origin)
    if path in pins['modified']:
        old=target.read_bytes();assert sha(old)==row['before_sha256'],str(target)
        body=preserve(old,new)
    else:
        assert not target.exists(),str(target)
        old=b'';body=new
    rows.append([target,body,dict(path=path,before_sha256=sha(old) if old else None,
                                carrier_sha256=sha(new),after_sha256=sha(body),
                                untouched_lines_preserved=True)])

tp=json.loads((TEXT/'PINS.json').read_text(encoding='utf-8'))
before=(TEXT/tp['before']['file']).read_bytes();after=(TEXT/tp['after']['file']).read_bytes()
assert sha(before)==tp['before']['sha256'] and sha(after)==tp['after']['sha256']
clip=next(row for row in rows if row[2]['path']==tp['target'])
combined=clip[1]
for name,row in tp['functions'].items():
    marker=('ScStatus '+name+'(').encode()
    a=before.index(marker);b=after.index(marker)
    old_function=before[a:a+row['before_bytes']]
    new_function=after[b:b+row['after_bytes']]
    assert sha(old_function)==row['before_sha256'] and sha(new_function)==row['after_sha256']
    assert combined.count(old_function)==1,name
    combined=combined.replace(old_function,new_function,1)
clip[1]=combined;clip[2].update(after_sha256=sha(combined),TEXT1_functions=list(tp['functions']))
result=dict(status='DROP_AND_TEXT_PREFLIGHT_ONLY',modified=[row for _,_,row in rows],
            old_public_headers_byte_preserved=True,build_started=False,guest_validation_started=False)
for path in ('user/SCAPI.H','user/SCUI.H','user/SCSHELL.H'):
    assert path not in pins['modified'] and path not in pins['added']
if '--execute' in sys.argv:
    for target,body,row in rows:
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(body)
        assert sha(target.read_bytes())==row['after_sha256']
    result['status']='DROP_AND_TEXT_SOURCE_INTEGRATED_NOT_BUILT'
    (HERE/'drop-text-integration.json').write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode())
print(json.dumps(dict(status=result['status'],files=len(rows)),ensure_ascii=False))
