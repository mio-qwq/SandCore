from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
src=ROOT/'drafts/m10a2-p6-followup-refresh-v13';out=ROOT/'drafts/m10a2-p6-followup-refresh-v14'
previous='build/m10a2-p6-target-refresh-14/p6-generation.json'
witness='build/m10a2-p6-refresh-vm-14/refresh-vm.json'
host='build/m10a2-main-product-fix-03/p6-unified-host-25.json'
n,v,h=[json.loads((ROOT/p).read_bytes()) for p in (previous,witness,host)]
assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged']
assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['host_report_sha256']==sha(ROOT/host)
assert h['exit_code']==0
out.mkdir(exist_ok=False)
s=(src/'followup.py').read_text(encoding='utf-8')
constants={node.targets[0].id:node.value.value for node in ast.parse(s).body if isinstance(node,ast.Assign) and isinstance(node.targets[0],ast.Name) and isinstance(node.value,ast.Constant) and isinstance(node.value.value,str)}
for key,new in dict(PREVIOUS=previous,PREVIOUS_SHA=sha(ROOT/previous),WITNESS=witness,GENERATION_HOST=host).items():
    s=s.replace(constants[key],new)
s=s.replace("path=ROOT/'drafts/m10a2-p6-followup-refresh-v12/followup.py'","path=ROOT/'drafts/m10a2-p6-followup-refresh-v13/followup.py'")
s=s.replace('v4.v4.v4.v4.v4.v4.v6.v4.v4.prepared_header_map','v4.v4.v4.v4.v4.v4.v4.v6.v4.v4.prepared_header_map')
s=s.replace('sha(ROOT/v4.v4.v4.v4.v4.v4.PREVIOUS)==v4.v4.v4.v4.v4.v4.PREVIOUS_SHA','sha(ROOT/v4.v4.v4.v4.v4.v4.v4.PREVIOUS)==v4.v4.v4.v4.v4.v4.v4.PREVIOUS_SHA')
ast.parse(s);(out/'followup.py').write_text(s,encoding='utf-8')
s=(src/'driver.py').read_text(encoding='utf-8').replace('HOST25','HOST26').replace('p6-unified-host-25.json','p6-unified-host-26.json')
(out/'driver.py').write_text(s,encoding='utf-8')
pins=json.loads((src/'PINS.json').read_bytes())
for p in src.iterdir():
    if p.suffix in ('.py','.json'):pins['files'][p.relative_to(ROOT).as_posix()]=dict(sha256=sha(p),bytes=p.stat().st_size)
for p in (previous,witness,host):pins['files'][p]=dict(sha256=sha(ROOT/p),bytes=(ROOT/p).stat().st_size)
(out/'PINS.json').write_text(json.dumps(pins,indent=2)+'\n',encoding='utf-8')
print('STUDIO_OUTPUT_LAYOUT_REFRESH_PREPARED')
