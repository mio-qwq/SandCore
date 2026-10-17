"""主核PIO改变，原生用户态仍是native08；两份HOST来源明确分列。"""
from pathlib import Path
import argparse,ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
HOST14='build/m10a2-main-studio-01/p6-unified-host-14.json'
HOST15='build/m10a2-main-pio-01/p6-unified-host-15.json'
SHA14='d054c6115d97e75ac26245b602188b477d83f8cd792c43d202f721ab93495ff6'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host-sha256',required=True);a=p.parse_args()
assert sha(ROOT/HOST14)==SHA14 and sha(ROOT/HOST15)==a.host_sha256
old=json.loads((ROOT/HOST14).read_bytes());new=json.loads((ROOT/HOST15).read_bytes())
assert new['exit_code']==0 and len(new['outputs'])==41 and not new['source_files_modified_during_build'] and not new['missing_outputs']
before={r['path']:r['sha256'] for r in old['inputs']};after={r['path']:r['sha256'] for r in new['inputs']}
changed={k for k in before.keys()&after.keys() if before[k]!=after[k]}
assert changed=={'kernel/ata.c','kernel/ata.h'},changed
native=json.loads((ROOT/'build/m10a2-p6-target-refresh-08/p6-generation.json').read_bytes())
assert native['status']=='DECLARED_P6_NATIVE_GENERATION_PASS'
assert all(after.get(name)==value for name,value in native['source_input_sha256'].items())
out=ROOT/'drafts/m10a2-p6-final-09-receipt';out.mkdir(exist_ok=False)
original=ROOT/'drafts/m10a2-p6-final-08-receipt';sources={}
def replace(s,old,new,count=1):
    assert s.count(old)==count,(old,s.count(old));return s.replace(old,new)
for name in ('bindings.py','finalize.py','inspect_final.py'):
    source=original/name;s=source.read_text(encoding='utf-8');sources[str(source.relative_to(ROOT))]=sha(source)
    s=s.replace('final-08','final-09').replace('final08','final09')
    s=s.replace(HOST14,HOST15).replace(SHA14,a.host_sha256).replace('HOST14','HOST15')
    if name=='bindings.py':
        s=replace(s,'GENERATION_HOST=HOST',"GENERATION_HOST=ROOT/'"+HOST14+"'")
        s=replace(s,'GENERATION_HOST_SHA=HOST_SHA',"GENERATION_HOST_SHA='"+SHA14+"'")
        s=replace(s,"Path(v['host_report']).resolve() == HOST.resolve()","Path(v['host_report']).resolve() == GENERATION_HOST.resolve()")
        s=replace(s,"if Path(name).resolve() == HOST.resolve()]","if Path(name).resolve() == GENERATION_HOST.resolve()]")
        s=replace(s,"require(host_pins == [HOST_SHA]","require(host_pins == [GENERATION_HOST_SHA]")
        s=replace(s,"self.runtime_metadata=dict(host_report=pin(HOST), producer_and_runtime_same_host=True)",
            "self.runtime_metadata=dict(host_report=pin(HOST), producer_and_runtime_same_host=False, generation_host_report=pin(GENERATION_HOST), reason='KERNEL_ATA_PIO_ONLY_USER_SNAPSHOT_IDENTICAL')")
        anchor="        self.native = self.checked.json(NATIVE / 'p6-generation.json', native_sha)"
        extra="""        producer=self.checked.json(GENERATION_HOST,GENERATION_HOST_SHA)
        require(producer['exit_code']==0 and not producer['source_files_modified_during_build'], '实际原生producer HOST14不再有效')
        producer_inputs={row['path']:row['sha256'] for row in producer['inputs']}
"""
        s=replace(s,anchor,extra+anchor)
        s=replace(s,"        self.snapshot = n['source_input_sha256']","        self.snapshot = n['source_input_sha256']\n        require(all(producer_inputs.get(k)==v for k,v in self.snapshot.items()), 'native08不是实际HOST14同一用户源码')")
        s=s.replace("reason='IMPLEMENTATION_FIXES_WITH_IDENTICAL_FIVE_STRUCTURE_HEADERS'","reason='KERNEL_ATA_PIO_WITH_IDENTICAL_USER_SOURCE_AND_FIVE_STRUCTURE_HEADERS'")
    if name=='finalize.py':
        # 只复用同一native08的旧CORE编译中间物；新ata.c/h仍由make依赖实际重编译。
        anchor='    OUT.mkdir(parents=True,exist_ok=False)'
        extra="""
    import shutil
    old_core=ROOT/'build/m10a2-p6-final-08/core'
    shutil.copytree(old_core,OUT/'core')
    for dependency in (OUT/'core').rglob('*.d'):
        text=dependency.read_text(encoding='utf-8')
        dependency.write_bytes(text.replace('build/m10a2-p6-final-08/core','build/m10a2-p6-final-09/core').encode())
"""
        s=replace(s,anchor,anchor+extra)
    ast.parse(s);(out/name).write_bytes(s.encode())
(out/'PREPARATION.json').write_bytes((json.dumps(dict(status='LAYERED_NATIVE08_HOST14_PRODUCER_HOST15_KERNEL_PREPARED_NOT_EXECUTED',
    original_helpers=sources,host14=dict(path=HOST14,sha256=SHA14),host15=dict(path=HOST15,sha256=a.host_sha256),
    changed_original_input_paths=sorted(changed),user_snapshot_identical=True,user_native_regeneration=False,
    core_reuses_unchanged_objects_with_rebased_dependencies=True,core_build_started=False),indent=2)+'\n').encode())
print('FINAL09_LAYERED_BINDING_PREPARED_NOT_EXECUTED')
