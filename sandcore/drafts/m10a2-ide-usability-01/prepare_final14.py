from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
native=ROOT/'build/m10a2-p6-target-refresh-14/p6-generation.json'
outer=ROOT/'build/m10a2-p6-refresh-vm-14/refresh-vm.json'
host=ROOT/'build/m10a2-main-product-fix-03/p6-unified-host-25.json'
n,v,h=[json.loads(p.read_bytes()) for p in (native,outer,host)]
assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged']
assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['host_report_sha256']==sha(host)
assert h['exit_code']==0 and not h['source_files_modified_during_build']
src=ROOT/'drafts/m10a2-p6-final-13-receipt';out=ROOT/'drafts/m10a2-p6-final-14-receipt';out.mkdir(exist_ok=False)
changes={
 'build/m10a2-p6-target-refresh-13':'build/m10a2-p6-target-refresh-14',
 'build/m10a2-p6-refresh-vm-13':'build/m10a2-p6-refresh-vm-14',
 'build/m10a2-main-product-fix-02/p6-unified-host-24.json':host.relative_to(ROOT).as_posix(),
 sha(ROOT/'build/m10a2-p6-target-refresh-13/p6-generation.json'):sha(native),
 sha(ROOT/'build/m10a2-p6-refresh-vm-13/refresh-vm.json'):sha(outer),
 sha(ROOT/'build/m10a2-main-product-fix-02/p6-unified-host-24.json'):sha(host),
}
for name in ('bindings.py','finalize.py','inspect_final.py'):
    s=(src/name).read_text(encoding='utf-8').replace('m10a2-p6-final-12','PREVIOUS_CORE_PLACEHOLDER')
    s=s.replace('m10a2-p6-final-13','m10a2-p6-final-14').replace('PREVIOUS_CORE_PLACEHOLDER','m10a2-p6-final-13')
    for a,b in changes.items():s=s.replace(a,b)
    s=s.replace('HOST24','HOST25').replace('native13','native14').replace('outer13','outer14').replace('refresh13','refresh14').replace('final13','final14')
    ast.parse(s);(out/name).write_text(s,encoding='utf-8')
print('FINAL14_NATIVE14_BINDING_PREPARED')
