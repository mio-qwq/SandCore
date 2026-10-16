"""原生刷新04结束后，只合入已审稳定读兼容操作与对应规范。"""
from pathlib import Path
import difflib,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
BASE=ROOT/'drafts/m10a2-stable-read-sharing'
sha=lambda b:hashlib.sha256(b).hexdigest()
norm=lambda b:b.replace(b'\r\n',b'\n')
refresh=json.loads((ROOT/'build/m10a2-p6-refresh-vm-04/refresh-vm.json').read_text(encoding='utf-8'))
assert refresh['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and refresh['current_inputs_unchanged'] and refresh['source_disk_unchanged']
pins=json.loads((BASE/'PINS.json').read_text(encoding='utf-8'))
rows=[]
for path,row in pins['modified'].items():
    target=ROOT/path;old=target.read_bytes();carrier=(BASE/'overlay'/path).read_bytes()
    assert sha(old)==row['before_sha256'] and sha(carrier)==row['result_sha256'],path
    old_lines=old.splitlines(keepends=True);a=[norm(v) for v in old_lines];b=norm(carrier).splitlines(keepends=True)
    ending=b'\r\n' if old.count(b'\r\n')>old.count(b'\n')//2 else b'\n';out=[]
    for kind,i,j,k,l in difflib.SequenceMatcher(None,a,b,autojunk=False).get_opcodes():
        out.extend(old_lines[i:j] if kind=='equal' else [v.replace(b'\n',ending) for v in b[k:l]])
    body=b''.join(out);assert norm(body)==norm(carrier)
    rows.append((target,body,dict(path=path,before_sha256=sha(old),carrier_sha256=sha(carrier),after_sha256=sha(body))))
for target,body,row in rows:target.write_bytes(body)
result=dict(status='STABLE_READ_SOURCE_INTEGRATED_NOT_BUILT',modified=[r for _,_,r in rows],
            native04_recompile_required=False,reason='KERNEL_PRIVATE_CONFLICT_OPERATION_ONLY_NO_USER_SOURCE_OR_LAYOUT_CHANGE',
            public_ABI_changed=False,guest_runtime_verified=False)
(HERE/'stable-read-integration.json').write_bytes((json.dumps(result,indent=2)+'\n').encode())
print('Stable read compatibility integrated:',len(rows),'files')
