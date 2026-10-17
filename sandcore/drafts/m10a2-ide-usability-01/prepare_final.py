from pathlib import Path
import ast,hashlib,json

ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

native=ROOT/'build/m10a2-p6-target-refresh-13/p6-generation.json'
outer=ROOT/'build/m10a2-p6-refresh-vm-13/refresh-vm.json'
host=ROOT/'build/m10a2-main-product-fix-02/p6-unified-host-24.json'
n,v,h=[json.loads(p.read_bytes()) for p in (native,outer,host)]
assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged'] and n['source_disk_unchanged']
assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['current_inputs_unchanged'] and v['source_disk_unchanged']
assert n['qemu_pid']==v['qemu_pid'] and v['host_report_sha256']==sha(host)
assert h['exit_code']==0 and not h['source_files_modified_during_build']
src=ROOT/'drafts/m10a2-p6-final-12-receipt'
out=ROOT/'drafts/m10a2-p6-final-13-receipt'
out.mkdir(exist_ok=False)
changes={
 'build/m10a2-p6-target-refresh-11':'build/m10a2-p6-target-refresh-13',
 'build/m10a2-p6-refresh-vm-11':'build/m10a2-p6-refresh-vm-13',
 'build/m10a2-main-session-return-01/p6-unified-host-21.json':host.relative_to(ROOT).as_posix(),
 '738e41d51bc4cd026cdc658c0b8d484614c59e7952b4908be389eaa919a76749':sha(host),
 '66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0':sha(native),
 'fbb6db090cc822a624f94c5d79ecf9b32ddedabad40ff8bb6ae1ed374ac80d92':sha(outer),
}
for name in ('bindings.py','finalize.py','inspect_final.py'):
    s=(src/name).read_text(encoding='utf-8')
    s=s.replace('m10a2-p6-final-11','PREVIOUS_CORE_PLACEHOLDER')
    s=s.replace('m10a2-p6-final-12','m10a2-p6-final-13')
    s=s.replace('PREVIOUS_CORE_PLACEHOLDER','m10a2-p6-final-12')
    for old,new in changes.items():s=s.replace(old,new)
    s=s.replace('HOST21','HOST24').replace('native11','native13').replace('outer11','outer13').replace('refresh11','refresh13').replace('final12','final13')
    ast.parse(s)
    (out/name).write_text(s,encoding='utf-8')
print('FINAL13_NATIVE12_BINDING_PREPARED')
