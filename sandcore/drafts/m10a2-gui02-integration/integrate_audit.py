"""合并独立审阅三个精确锚点，保留此前source退役门和GCC修复。"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
base=ROOT/'drafts/m10a2-drop-independent-review'
p=json.loads((base/'PINS.json').read_text(encoding='utf-8'))
target=ROOT/p['target'];body=target.read_bytes();old=body
sha=lambda b:hashlib.sha256(b).hexdigest()
for row in p['anchors']:
    before=row['before'].encode();after=row['after'].encode()
    assert sha(before)==row['before_sha256'] and sha(after)==row['after_sha256']
    assert body.count(before)==1
    body=body.replace(before,after,1)
assert b'if(d->cancelled)return 1;' in body
assert b'ui_d2_source_retired' in body
assert b'notification_error=s;\n    return SC_UI_END;' in body
target.write_bytes(body)
result=dict(status='INDEPENDENT_REVIEW_FIXES_INTEGRATED_NOT_BUILT',target=p['target'],before_sha256=sha(old),after_sha256=sha(body),
            anchors=len(p['anchors']),source_cancel_gate_preserved=True,GCC_warning_fix_preserved=True)
(HERE/'audit-integration.json').write_bytes((json.dumps(result,indent=2)+'\n').encode())
print(result['status'])
