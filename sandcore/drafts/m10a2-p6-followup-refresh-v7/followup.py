"""复用逐项资格审阅的07应用，只重新生成实际变化的SCUI。"""
from contextlib import contextmanager
from pathlib import Path
import copy,hashlib,importlib.util,json,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
expected=json.loads((HERE/'PINS.json').read_bytes())['files']
path=ROOT/'drafts/m10a2-p6-followup-refresh-v6/followup.py'
assert sha(path)==expected[path.relative_to(ROOT).as_posix()]['sha256']
spec=importlib.util.spec_from_file_location('frozen_v6_for_v7',path);v6=importlib.util.module_from_spec(spec);spec.loader.exec_module(v6)
PREVIOUS='build/m10a2-p6-artifact-qualification-07/qualified-artifacts.json'
PREVIOUS_SHA=expected[PREVIOUS]['sha256']
WITNESS='build/m10a2-p6-refresh-vm-07/refresh-vm.json'
GENERATION_HOST='build/m10a2-main-studio-01/p6-unified-host-13.json'
CORE,RUNTIME_HOST,SDK,LAYOUT=v6.CORE,v6.RUNTIME_HOST,v6.SDK,v6.LAYOUT
inside,read=v6.inside,v6.read
validated_labels,exact_changed_labels=v6.validated_labels,v6.exact_changed_labels

class Basis(v6.Basis):
    def __init__(self):
        super().__init__()
        self.audit_sidecars()
        for name,row in expected.items():self.pin(name,row['sha256'],row['bytes'])
        self.frozen['files'].update(expected)
        old=read(ROOT/PREVIOUS);original=read(ROOT/'build/m10a2-p6-target-refresh-07/p6-generation.json');outer=read(ROOT/WITNESS)
        assert old['status']=='DECLARED_P6_ARTIFACT_FACTS_AUDIT_PASS'
        assert old['original_generation_status']=='FAIL_OR_INTERRUPTED' and old['original_owner_account_pass'] is False
        assert old['qualification']['original_native']['sha256']==sha(ROOT/'build/m10a2-p6-target-refresh-07/p6-generation.json')
        assert old['qualification']['qualified_application_labels']==['FILES2','DEBUG2','SETTINGS2','INSTALLER2']
        assert all(row['current_compile_closure_unchanged'] and row['source_and_debug_contract_exact'] for row in old['qualification']['audits'].values())
        assert original['status']==outer['status']=='FAIL_OR_INTERRUPTED'
        assert old['cases']==original['cases'] and old['qemu_pid']==outer['qemu_pid']
        for group in ('objects','native_artifacts','native_libraries','preflight','input_sha256','source_input_sha256'):
            assert old[group]==original[group]
        for name in ('SCCC1.SCX','SCLD.SCX','SCSHELL.SCX','LOGIN2.SCX','SCFILEWORK.SCX'):
            assert old['native_artifacts'][name]['sha256']==self.runtime_native['native_artifacts'][name]['sha256']
        for report_name,report in ((WITNESS,outer),('build/m10a2-p6-target-refresh-07/p6-generation.json',original)):
            self.reports[report_name]=report
            for index,row in enumerate(report['cases']):
                if not all(k in row for k in ('guest','path','bytes','sha256','generation')):continue
                assert row['status']=='PASS' and row['qemu_pid']==original['qemu_pid'] and row['generation']>0
                source=inside(row['path'])
                if source not in self.gets:self.gets[source]=[(report_name,index,row)]
        self.sidecars={}
        for group in ('objects','native_artifacts','native_libraries'):
            for name,row in old[group].items():
                image=self.pin(row['path'],row['sha256'],row['bytes'])
                suffixes=('.log','.map') if group=='objects' else ('.map',)
                if group!='objects' and 'debug' not in row and Path(str(image)+'.log').is_file():suffixes+=('.log',)
                if 'debug' in row:self.pin(str(image)+'.sdbg',row['debug']['sha256'],row['debug']['bytes'])
                for suffix in suffixes:
                    source=inside(str(image)+suffix);entry=(PREVIOUS,(row.get('inherited_sidecars') or {}).get(suffix))
                    self.aliases[source]=entry;self.sidecars[source]=entry
        self.old,self.outer=old,outer
        self.reports[PREVIOUS]=old
        helper=ROOT/'drafts/m10a2-parent-heap-review'
        for p in helper.glob('*.py'):self.pin(p,sha(p),p.stat().st_size)
        for name in ('layout.bin','layout.o'):
            p=ROOT/'build/m10a2-parent-heap-layout-01'/name;self.pin(p,sha(p),p.stat().st_size)

    def legacy(self):
        legacy=super().legacy();target=legacy.target;original=target.passed
        def passed(report):
            if report.get('status')=='DECLARED_P6_ARTIFACT_FACTS_AUDIT_PASS':
                assert report==self.old and sha(ROOT/PREVIOUS)==PREVIOUS_SHA
                assert report['original_owner_account_pass'] is False
                return
            return original(report)
        target.passed=passed
        sys.path[:0]=[str(ROOT/'drafts/m10a2-parent-heap-review'),str(ROOT/'drafts/m10a2-gui-owner-review')]
        import survey,cache_observer,owner_snapshot
        proof=survey.layout(ROOT,self)
        legacy.wait_original_host=cache_observer.configure(owner_snapshot,self,proof)
        base=legacy.CoherentHarness
        class CacheHarness(base):
            def ledger(self,before,after,rb,ra,cold_font):
                old,_=super().ledger(before,after,rb,ra,cold_font)
                return cache_observer.account(old,rb,ra)
        legacy.CoherentHarness=CacheHarness
        return legacy

    @contextmanager
    def bind_generation(self,target,prepared):
        mapping=v6.v4.v4.prepared_header_map(prepared)
        original=target.inherit
        def inherit(current,old,out):
            assert current is prepared and old is self.old
            value=original(current,old,out)
            value['preflight']['public_header_sources']=dict(mapping)
            value['source_reuse']['qualified_prior_failure']=dict(path=PREVIOUS,sha256=PREVIOUS_SHA,
                original_native07_status='FAIL_OR_INTERRUPTED',owner_failure_reclassified=False,
                reused_applications=['FILES2','DEBUG2','SETTINGS2','INSTALLER2'])
            target.save(Path(out)/'p6-generation.json',value);return value
        with self.bind_sidecars(target):
            target.inherit=inherit
            try:yield
            finally:target.inherit=original
