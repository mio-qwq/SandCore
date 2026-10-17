from pathlib import Path
import hashlib,json,ast

ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

src=ROOT/'drafts/m10a2-p6-followup-refresh-v10'
out=ROOT/'drafts/m10a2-p6-followup-refresh-v11'
previous='build/m10a2-p6-target-refresh-11/p6-generation.json'
witness='build/m10a2-p6-refresh-vm-11/refresh-vm.json'
host='build/m10a2-main-session-return-01/p6-unified-host-21.json'
assert sha(ROOT/previous)=='66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0'
assert sha(ROOT/witness)=='fbb6db090cc822a624f94c5d79ecf9b32ddedabad40ff8bb6ae1ed374ac80d92'
assert sha(ROOT/host)=='738e41d51bc4cd026cdc658c0b8d484614c59e7952b4908be389eaa919a76749'
out.mkdir(exist_ok=False)
s=(src/'followup.py').read_text(encoding='utf-8')
changes={
 "path=ROOT/'drafts/m10a2-p6-followup-refresh-v9/followup.py'":"path=ROOT/'drafts/m10a2-p6-followup-refresh-v10/followup.py'",
 'build/m10a2-p6-target-refresh-10/p6-generation.json':previous,
 '1e39068eeec9de3027f1c04e666b0f24b60ec4795d5ef860a7f376994ffad740':sha(ROOT/previous),
 'build/m10a2-p6-refresh-vm-10/refresh-vm.json':witness,
 'build/m10a2-main-release-drag-01/p6-unified-host-20.json':host,
 'v4.v4.v4.v6.v4.v4.prepared_header_map':'v4.v4.v4.v4.v6.v4.v4.prepared_header_map',
 'sha(ROOT/v4.v4.v4.PREVIOUS)==v4.v4.v4.PREVIOUS_SHA':'sha(ROOT/v4.v4.v4.v4.PREVIOUS)==v4.v4.v4.v4.PREVIOUS_SHA',
}
for old,new in changes.items():
    assert old in s,old
    s=s.replace(old,new)
(out/'followup.py').write_text(s,encoding='utf-8')
s=(src/'driver.py').read_text(encoding='utf-8').replace('HOST21','HOST23').replace('p6-unified-host-21.json','p6-unified-host-23.json')
(out/'driver.py').write_text(s,encoding='utf-8')
pins=json.loads((src/'PINS.json').read_bytes())
for p in src.iterdir():
    if p.suffix in ('.py','.json'):pins['files'][p.relative_to(ROOT).as_posix()]=dict(sha256=sha(p),bytes=p.stat().st_size)
for name in (previous,witness,host):pins['files'][name]=dict(sha256=sha(ROOT/name),bytes=(ROOT/name).stat().st_size)
(out/'PINS.json').write_text(json.dumps(pins,indent=2)+'\n',encoding='utf-8')
for p in out.glob('*.py'):ast.parse(p.read_text(encoding='utf-8'))
print('CHANGED_TARGET_REFRESH_HELPER_PREPARED_NO_VM')
