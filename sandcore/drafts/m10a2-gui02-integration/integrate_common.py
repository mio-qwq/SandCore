"""按精确前后像合入共享对话框、音频快捷键及失败后状态修复。"""
from pathlib import Path
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
rows=[]
for directory in ('m10a2-systemtools-dialog-lifetime','m10a2-sound-status-refresh'):
    base=ROOT/'drafts'/directory
    pins=json.loads((base/'PINS.json').read_text(encoding='utf-8'))
    modified=pins.get('modified') or {pins['target']:dict(before_sha256=pins['before']['sha256'],result_sha256=pins['after']['sha256'])}
    for path,row in modified.items():
        target=ROOT/path; origin=base/'overlay'/path if 'modified' in pins else base/'sound_jobs.c.after'
        assert sha(target.read_bytes())==row['before_sha256'],str(target)
        body=origin.read_bytes()
        assert sha(body)==row['result_sha256'],str(origin)
        rows.append((target,body,dict(path=path,source=str(origin.relative_to(ROOT)),before_sha256=row['before_sha256'],after_sha256=sha(body))))
for target,body,row in rows:target.write_bytes(body)
(HERE/'common-integration.json').write_bytes((json.dumps(dict(status='SOURCE_INTEGRATED_NOT_BUILT',modified=[r for _,_,r in rows]),ensure_ascii=False,indent=2)+'\n').encode())
print('Shared fixes integrated:',len(rows))
