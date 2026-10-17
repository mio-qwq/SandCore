from pathlib import Path
import hashlib,json

ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
src=ROOT/'drafts/m10a2-p6-followup-refresh-v11'
out=ROOT/'drafts/m10a2-p6-followup-refresh-v12'
previous='build/m10a2-p6-target-refresh-12/p6-generation.json'
witness='build/m10a2-p6-refresh-vm-12/refresh-vm.json'
host='build/m10a2-main-product-fix-01/p6-unified-host-23.json'
n,v,h=[json.loads((ROOT/p).read_bytes()) for p in (previous,witness,host)]
assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged']
assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['host_report_sha256']==sha(ROOT/host)
assert h['exit_code']==0
out.mkdir(exist_ok=False)
s=(src/'followup.py').read_text(encoding='utf-8')
old=json.loads((ROOT/'build/m10a2-p6-target-refresh-11/p6-generation.json').read_bytes())
changes={
 "path=ROOT/'drafts/m10a2-p6-followup-refresh-v10/followup.py'":"path=ROOT/'drafts/m10a2-p6-followup-refresh-v11/followup.py'",
 'build/m10a2-p6-target-refresh-11/p6-generation.json':previous,
 '66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0':sha(ROOT/previous),
 'build/m10a2-p6-refresh-vm-11/refresh-vm.json':witness,
 'build/m10a2-main-session-return-01/p6-unified-host-21.json':host,
 'v4.v4.v4.v4.v6.v4.v4.prepared_header_map':'v4.v4.v4.v4.v4.v6.v4.v4.prepared_header_map',
 'sha(ROOT/v4.v4.v4.v4.PREVIOUS)==v4.v4.v4.v4.PREVIOUS_SHA':'sha(ROOT/v4.v4.v4.v4.v4.PREVIOUS)==v4.v4.v4.v4.v4.PREVIOUS_SHA',
}
for a,b in changes.items():
    assert a in s,a
    s=s.replace(a,b)
(out/'followup.py').write_text(s,encoding='utf-8')
s=(src/'driver.py').read_text(encoding='utf-8').replace('HOST23','HOST24').replace('p6-unified-host-23.json','p6-unified-host-24.json')
(out/'driver.py').write_text(s,encoding='utf-8')
pins=json.loads((src/'PINS.json').read_bytes())
for p in src.iterdir():
    if p.suffix in ('.py','.json'):pins['files'][p.relative_to(ROOT).as_posix()]=dict(sha256=sha(p),bytes=p.stat().st_size)
for p in (previous,witness,host):pins['files'][p]=dict(sha256=sha(ROOT/p),bytes=(ROOT/p).stat().st_size)
(out/'PINS.json').write_text(json.dumps(pins,indent=2)+'\n',encoding='utf-8')
print('STUDIO_ONLY_REFRESH_PREPARED')
