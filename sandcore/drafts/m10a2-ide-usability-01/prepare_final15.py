from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
native=ROOT/'build/m10a2-p6-target-refresh-15/p6-generation.json'
outer=ROOT/'build/m10a2-p6-refresh-vm-15/refresh-vm.json'
host=ROOT/'build/m10a2-main-product-fix-04/p6-unified-host-26.json'
n,v,h=[json.loads(p.read_bytes()) for p in (native,outer,host)]
assert n['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and n['current_inputs_unchanged']
assert v['status']=='EXPLICIT_LABEL_REFRESH_GENERATED' and v['host_report_sha256']==sha(host)
assert h['exit_code']==0 and not h['source_files_modified_during_build']
src=ROOT/'drafts/m10a2-p6-final-13-receipt';out=ROOT/'drafts/m10a2-p6-final-15-receipt';out.mkdir(exist_ok=False)
changes={
 'build/m10a2-p6-target-refresh-13':'build/m10a2-p6-target-refresh-15',
 'build/m10a2-p6-refresh-vm-13':'build/m10a2-p6-refresh-vm-15',
 'build/m10a2-main-product-fix-02/p6-unified-host-24.json':host.relative_to(ROOT).as_posix(),
 sha(ROOT/'build/m10a2-p6-target-refresh-13/p6-generation.json'):sha(native),
 sha(ROOT/'build/m10a2-p6-refresh-vm-13/refresh-vm.json'):sha(outer),
 sha(ROOT/'build/m10a2-main-product-fix-02/p6-unified-host-24.json'):sha(host),
}
for name in ('bindings.py','finalize.py','inspect_final.py'):
    s=(src/name).read_text(encoding='utf-8').replace('m10a2-p6-final-12','PREVIOUS_CORE_PLACEHOLDER')
    s=s.replace('m10a2-p6-final-13','m10a2-p6-final-15').replace('PREVIOUS_CORE_PLACEHOLDER','m10a2-p6-final-13')
    for a,b in changes.items():s=s.replace(a,b)
    s=s.replace('HOST24','HOST26').replace('native13','native15').replace('outer13','outer15').replace('refresh13','refresh15').replace('final13','final15')
    ast.parse(s);(out/name).write_text(s,encoding='utf-8')
print('FINAL15_NATIVE15_BINDING_PREPARED')
