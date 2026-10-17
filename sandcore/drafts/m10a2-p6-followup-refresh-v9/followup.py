"""native09作产物继承，final04仍作明确冻结的编译运行底座。"""
from contextlib import contextmanager
from pathlib import Path
import copy,importlib.util,json,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
path=ROOT/'drafts/m10a2-p6-followup-refresh-v8/followup.py'
import hashlib
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
expected=json.loads((HERE/'PINS.json').read_bytes())['files']
assert sha(path)==expected[path.relative_to(ROOT).as_posix()]['sha256']
spec=importlib.util.spec_from_file_location('frozen_refresh_v5_for_v6',path)
v4=importlib.util.module_from_spec(spec);spec.loader.exec_module(v4)
PREVIOUS='build/m10a2-p6-target-refresh-09/p6-generation.json'
PREVIOUS_SHA='57fa08a3e2ce36e350a0f88f6d56ab93a2418789ec22723ce2c762d10813f32b'
WITNESS='build/m10a2-p6-refresh-vm-09/refresh-vm.json'
GENERATION_HOST='build/m10a2-main-appfix-01/p6-unified-host-16.json'
CORE,RUNTIME_HOST,SDK,LAYOUT=v4.CORE,v4.RUNTIME_HOST,v4.SDK,v4.LAYOUT
inside,read=v4.inside,v4.read
validated_labels,exact_changed_labels=v4.validated_labels,v4.exact_changed_labels

class Basis(v4.Basis):
    def source(self,name,expected):
        # 编译VM仍运行冻结final04。当前task/WM为新接口改变时，观察器
        # 必须读真实保留的旧源码，不能把新源码配上旧CORE的摘要。
        archived=ROOT/'drafts/m10a2-release-drag-01/before'/name
        if archived.is_file() and sha(archived)==expected:
            return self.pin(archived,expected,archived.stat().st_size)
        return super().source(name,expected)

    def __init__(self):
        # 先完整审阅原04/03/G1实际链及layout02，不改写其生产来源。
        super().__init__()
        self.qualified_previous07=self.qualified_previous07
        # 父Basis已固定真实native04运行底座；本层只推进产物继承到08。
        for name,row in expected.items():self.pin(name,row['sha256'],row['bytes'])
        self.frozen['files'].update(expected)
        old,outer=read(ROOT/PREVIOUS),read(ROOT/WITNESS)
        assert sha(ROOT/PREVIOUS)==PREVIOUS_SHA
        assert (old['status']=='DECLARED_P6_NATIVE_GENERATION_PASS'
            and old['current_inputs_unchanged'] and old['source_disk_unchanged']
            and not old['changed_input_output_paths']
            and (len(old['native_libraries']),len(old['native_artifacts']),len(old['objects']))==(8,21,27))
        assert outer['status']=='EXPLICIT_LABEL_REFRESH_GENERATED'
        assert outer['bundle_status']==old['status'] and inside(outer['bundle_report'])==inside(PREVIOUS)
        assert outer['qemu_pid']==old['qemu_pid'] and outer['current_inputs_unchanged'] and outer['source_disk_unchanged']
        assert not outer['changed_input_output_paths']
        assert inside(outer['host_report'])==inside(GENERATION_HOST)
        assert outer['host_report_sha256']==expected[GENERATION_HOST]['sha256']
        # 实际编译器、链接器、桌面与CORE嵌入程序在06没有更换；运行04
        # 仅负责生成新候选文件，绝不把08的SCUI装进04再混称同一底座。
        for name in ('SCCC1.SCX','SCLD.SCX','SCSHELL.SCX','LOGIN2.SCX','SCFILEWORK.SCX'):
            assert old['native_artifacts'][name]['sha256']==self.runtime_native['native_artifacts'][name]['sha256']
        for name,value in self.layout['headers'].items():
            if name.startswith('user/'):
                label={'user/scui/SCUIPRIVATE.H':'SCUI','user/scshell/desktop/DESKTOP.H':'DESKTOP'}[name]
                assert old['preflight']['source_closures'][label][name]==value
        for report_name,report in ((PREVIOUS,old),(WITNESS,outer)):
            self.reports[report_name]=report
            for index,row in enumerate(report['cases']):
                if not all(k in row for k in ('guest','path','bytes','sha256','generation')):continue
                assert row['status']=='PASS' and row['generation']>0 and row['qemu_pid']==report['qemu_pid']
                self.gets.setdefault(inside(row['path']),[]).append((report_name,index,row))
        sidecars={}
        for collection in ('native_libraries','native_artifacts','objects'):
            for name,row in old[collection].items():
                image=self.pin(row['path'],row['sha256'],row['bytes'])
                if 'debug' in row:self.pin(str(image)+'.sdbg',row['debug']['sha256'],row['debug']['bytes'])
                suffixes=('.log','.map') if collection=='objects' else ('.map',)
                if collection!='objects' and 'debug' not in row and Path(str(image)+'.log').is_file():suffixes+=('.log',)
                for suffix in suffixes:
                    source=inside(str(image)+suffix)
                    assert source not in self.aliases
                    entry=(PREVIOUS,(row.get('inherited_sidecars') or {}).get(suffix))
                    self.aliases[source]=entry;sidecars[source]=entry
        self.sidecars=sidecars
        self.old,self.outer=old,outer

    @contextmanager
    def bind_generation(self,target,prepared):
        mapping=v4.v4.v6.v4.v4.prepared_header_map(prepared)
        before=copy.deepcopy(self.old['preflight']['public_header_sources'])
        original=target.inherit
        def inherit(current,old,out):
            assert current is prepared and old is self.old and v4.v4.v6.v4.v4.prepared_header_map(current)==mapping
            result=original(current,old,out)
            assert result['source_snapshot_scope']==target.SCOPE
            result['preflight']['public_header_sources']=dict(mapping)
            result['source_reuse']['current_public_header_sources']=dict(
                origin='ACTUAL_CURRENT_PREPARED_MAP',entries=len(mapping),
                previous_map_retained_in_previous_report=True,
                runtime_observer_layout_origin='FROZEN_FINAL04_LAYOUT02_EXPLICIT_REUSE')
            target.save(Path(out)/'p6-generation.json',result)
            return result
        with self.bind_sidecars(target):
            target.inherit=inherit
            try:yield
            finally:
                target.inherit=original
                assert self.old['preflight']['public_header_sources']==before

    def legacy(self):
        legacy=super().legacy()
        original=legacy.target.passed
        def passed(report):
            if report.get('status')=='DECLARED_P6_ARTIFACT_FACTS_AUDIT_PASS':
                # 这里只保留父层已固定的07副文件来源资格，不把它当整轮PASS。
                assert report==self.qualified_previous07
                assert sha(ROOT/v4.v4.PREVIOUS)==v4.v4.PREVIOUS_SHA
                assert report['original_owner_account_pass'] is False
                return
            return original(report)
        legacy.target.passed=passed
        return legacy
