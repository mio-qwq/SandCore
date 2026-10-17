"""只资格审阅07实际生成字节；整轮owner失败不改，四个未变应用不重编译。"""
from pathlib import Path
import copy,hashlib,json,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-followup-refresh-v6')]
from followup import Basis
from scl_format import validate
from scd_format import validate_scx
from verify_m10a2_stdlib import source_contract
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
STATUS='DECLARED_P6_ARTIFACT_FACTS_AUDIT_PASS'
NATIVE='build/m10a2-p6-target-refresh-07/p6-generation.json'
OUTER='build/m10a2-p6-refresh-vm-07/refresh-vm.json'
HOST='build/m10a2-main-studio-01/p6-unified-host-13.json'
OUTPUT='build/m10a2-p6-artifact-qualification-07/qualified-artifacts.json'

def main():
    out=ROOT/OUTPUT;assert not out.exists()
    basis=Basis();basis.audit_sidecars();legacy=basis.legacy();contract=legacy.target.contract
    failed=json.loads((ROOT/NATIVE).read_bytes());outer=json.loads((ROOT/OUTER).read_bytes())
    assert failed['status']==outer['status']=='FAIL_OR_INTERRUPTED'
    assert outer['current_inputs_unchanged'] and outer['source_disk_unchanged'] and not outer['changed_input_output_paths']
    assert failed['qemu_pid']==outer['qemu_pid']
    account=[c for c in outer['cases'] if c['name']=='target-refresh-single-owner-account']
    assert len(account)==1 and account[0]['status']=='FAIL' and account[0]['physical_delta']==16 and account[0]['unexplained_delta']==16
    for name,value in failed['source_input_sha256'].items():
        assert outer['current_input_sha256'].get(str((ROOT/name).resolve()))==value,name
    assert (len(failed['objects']),len(failed['native_libraries']),len(failed['native_artifacts']))==(27,8,21)
    current,payload=contract.source_plan();labels=('FILES2','DEBUG2','SETTINGS2','INSTALLER2')
    pins={};audits={}
    def pin(path,expected,size):
        path=Path(path).resolve();assert path.is_relative_to(ROOT) and path.stat().st_size==size and sha(path)==expected
        pins[str(path)]=expected;return path.read_bytes()
    # 资格来自实际SCCC对象、SCLD容器、SCDB源/地址闭包、日志和真实get；
    # 不是只看文件存在，也不是把07最终FAIL换成生成整轮PASS。
    for label in labels:
        assert failed['preflight']['source_closures'][label]==current['source_closures'][label]
        obj=failed['objects'][label];app=failed['native_artifacts'][label+'.SCX']
        manifest=failed['preflight']['manifests'][label]['declared']
        assert contract.digest((ROOT/'user'/Path(current['cases'][label][1]).with_suffix('.NATIVE.JSON')).read_bytes())==failed['preflight']['manifests'][label]['sha256']
        info=contract.object_contract(pin(obj['path'],obj['sha256'],obj['bytes']),payload,manifest)
        image=pin(app['path'],app['sha256'],app['bytes']);table=validate_scx(image);source_contract(table,manifest,True)
        dbg=pin(app['path']+'.sdbg',app['debug']['sha256'],app['debug']['bytes'])
        contract.debug_contract(dbg,image,table,payload,[info])
        proofs=[]
        for row in (obj,app):
            cases=[c for c in failed['cases'] if c['name']==row['compile_case']]
            assert len(cases)==1;c=cases[0]
            assert c['status']=='PASS' and c['exit_code']==0 and c['generated_sha256']==row['sha256'] and c['generated_bytes']==row['bytes']
            pin(c['stdout_path'],c['stdout_sha256'],c['stdout_bytes'])
            proofs.append(dict(case=c['name'],exit_code=0,stdout_sha256=c['stdout_sha256']))
        for path in (obj['path'],obj['path']+'.log',obj['path']+'.map',app['path'],app['path']+'.map',app['path']+'.sdbg'):
            gets=[c for report in (failed,outer) for c in report['cases'] if c.get('path') and Path(c['path']).resolve()==Path(path).resolve() and all(k in c for k in ('guest','generation','qemu_pid'))]
            # 双报告若均保存相同get，只允许它们完全指向同一实际取回。
            unique={(c['guest'],c['generation'],c['qemu_pid'],c['sha256'],c['bytes']) for c in gets}
            assert len(unique)==1,(label,path,len(unique));c=gets[0]
            assert all(c['status']=='PASS' and c['generation']>0 and c['qemu_pid']==failed['qemu_pid'] for c in gets)
            pin(path,c['sha256'],c['bytes'])
        audits[label]=dict(actual_object_and_link=proofs,source_and_debug_contract_exact=True,actual_gets=6,
            current_compile_closure_unchanged=True,application_run_claimed=False)
    for collection in ('objects','native_libraries','native_artifacts'):
        for name,row in failed[collection].items():
            pin(row['path'],row['sha256'],row['bytes'])
            if 'debug' in row:pin(row['path']+'.sdbg',row['debug']['sha256'],row['debug']['bytes'])
    report=copy.deepcopy(failed)
    report.update(status=STATUS,scope='READ_ONLY_QUALIFICATION_OF_REUSABLE_PRODUCER_FACTS_NOT_ORIGINAL_LIFECYCLE_PASS',
        current_inputs_unchanged=True,source_disk_unchanged=True,changed_input_output_paths=[],
        original_generation_status='FAIL_OR_INTERRUPTED',original_owner_account_pass=False,
        generated_this_qualification=False,generated_applications_executed=False,full_M10a2=False,
        qualification=dict(original_native=dict(path=NATIVE,sha256=sha(ROOT/NATIVE)),
            original_outer=dict(path=OUTER,sha256=sha(ROOT/OUTER)),producer_host=dict(path=HOST,sha256=sha(ROOT/HOST)),
            original_error=report.pop('error'),qualified_application_labels=list(labels),audits=audits,
            existing_unchanged_artifacts_basis='PINNED_NATIVE06_SIDECAR_CHAIN',
            new_generation_still_requires_real_owner_account=True,original_report_rewritten=False),
        qualification_input_sha256=pins)
    out.parent.mkdir(exist_ok=False);out.write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode())
    print(json.dumps(dict(status=STATUS,path=OUTPUT,sha256=sha(out),qualified_apps=list(labels),original_owner_pass=False)))
if __name__=='__main__':main()
