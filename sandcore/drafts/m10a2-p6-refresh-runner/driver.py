"""只刷新真实变化的三个目标；旧原生运行底座与新生成产物分别留证。"""
from pathlib import Path
import argparse
import json
import sys
import traceback

sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
for folder in (ROOT/'tools',HERE.parent/'m10a2-p6-target-refresh',HERE.parent/'m10a2-gui-owner-review'):
    sys.path.insert(0,str(folder))
import refresh as target
import stage_m10a2_sdk as sdk
import verify_m10a2_ui_platform as first
from refresh_m10a2_ui_native import RefreshHarness
from verify_m10a2_scl import prepare
from verify_m10a2_ui_gui import GuiGuest,sessions
from scserial import QemuSession,sha
from m10_guest_boot import unique_symbols,wait_first_desktop
from owner_snapshot import PRIVATE_SOURCE_PINS,desktop_binding,wait_original_host

OLD=ROOT/'build/m10a2-p6-guest-generation-run-03/p6-generation.json'
OLD_SHA='f5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc'

class CoherentHarness(RefreshHarness):
    owner_arguments=None
    owner_resources=None
    observing=False

    def quiet(self):
        if self.owner_arguments is None or self.observing:return super().quiet()
        self.observing=True
        try:ready=wait_original_host(self,*self.owner_arguments)
        finally:self.observing=False
        self.report.setdefault('coherent_owner_observations',[]).append(ready)
        self.persist()
        if ready['status']!='ACTUAL_ORIGINAL_HOST_READY':raise ValueError('原生原桌面或一致快照尚未就绪')
        snap=ready['snapshot'];self.owner_resources=snap['resources']
        return snap['physical_pages'],ready['observations']

    def resources(self):
        if not self.observing and self.owner_resources is not None:
            value=self.owner_resources;self.owner_resources=None;return value
        return super().resources()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host-report',type=Path,required=True)
    p.add_argument('--vm-out',type=Path,required=True)
    p.add_argument('--bundle-out',type=Path,required=True)
    p.add_argument('--execute',action='store_true')
    a=p.parse_args()
    vm_out=a.vm_out.resolve();bundle=a.bundle_out.resolve()
    for path,prefix in ((vm_out,'m10a2-p6-refresh-vm'),(bundle,'m10a2-p6-target-refresh')):
        if not path.is_relative_to(ROOT/'build') or not path.name.startswith(prefix) or path.exists():raise ValueError('需要build内本批前缀的新输出')
    planned,payload,pins,old=target.preparation(OLD,OLD_SHA,['SCUI','SCSHELL','STUDIO2'])
    host_inputs=sdk.Inputs();sdk.verified_build(a.host_report.resolve(),host_inputs)
    pins.update(host_inputs.pins)
    core=ROOT/'build/m10a2-p6-final-01/core'
    data=ROOT/'build/m10a1/sanddata.img'
    if sha(data)!=old['source_data_sha256']:raise ValueError('原盘不同')
    pins[data]=sha(data)
    manifest_path=ROOT/'build/m10a2-p6-final-01/sdk/MANIFEST.json'
    prior=json.loads(manifest_path.read_text(encoding='utf-8'));pins[manifest_path]=sha(manifest_path)
    bootstrap={}
    def add(name,path,expected):
        path=Path(path).resolve(strict=True)
        if not path.is_relative_to(ROOT) or sha(path)!=expected:raise ValueError('引导原生来源改变: '+str(path))
        pins[path]=expected;bootstrap[name]=path.read_bytes()
    for name in old['native_libraries']:
        row=old['native_libraries'][name];add('SYS/LIB/'+name,row['path'],row['sha256'])
    codec='SYS/LIB/SCSOUND.SCL';row=prior['files'][codec]
    add(codec,manifest_path.parent/codec,row['sha256'])
    for name,dest in (('LOGIN2.SCX','SYS/CORE/LOGIN2.SCX'),('SCSHELL.SCX','SYS/SHELL/SCSHELL.SCX'),('SCFILEWORK.SCX','SYS/SHELL/SCFILEWORK.SCX'),('SCCC1.SCX','SYS/BIN/SCCC.SCX'),('SCLD.SCX','SYS/BIN/SCLD.SCX')):
        row=old['native_artifacts'][name];add(dest,row['path'],row['sha256'])
    uax='SYS/UI/UAX15_1.SCU';add(uax,manifest_path.parent/uax,prior['files'][uax]['sha256'])
    host=json.loads(a.host_report.read_text(encoding='utf-8'))
    image=next(v for v in host['outputs'] if v['path'].endswith('/fs/SYS/CORE/IMAGE.SCX'))
    add('SYS/CORE/IMAGE.SCX',ROOT/image['path'],image['sha256'])
    for name in ('sandcore.img','core.elf','core.sym','core.bin','fs/SYS/CORE/CORE.SKM'):
        path=core/name;pins[path]=sha(path)
    receipt=json.loads((ROOT/'drafts/m10a2-p6-native-core-receipt/core-build-receipt.json').read_text(encoding='utf-8'))
    # 这份旧CORE只服务原生生成引导；五库和两个角色程序保持同一旧字节。
    # 新SCUI/SCSHELL不会覆盖SYS映射，待新发行摘要及后续GUI真正执行。
    if sha(core/'fs/SYS/CORE/CORE.SKM')!='6f697fd6c09d38e409504ad987d3de945d47593f565678fde799ea4944989811':raise ValueError('旧原生引导CORE改变')
    # 观察旧运行底座时只用它真正编译过的布局来源；新源码另表固定，
    # 避免把待生成头文件的新摘要冒充旧 SCUI 的编译输入。
    layout=old['preflight']['task_layout']
    bootstrap_source_pins={}
    for relative,wanted in dict(layout['headers'],**PRIVATE_SOURCE_PINS).items():
        current=ROOT/relative
        archived=manifest_path.parent/'SYS/SDK/SRC'/relative
        path=next((item for item in (current,archived) if item.is_file() and sha(item)==wanted),None)
        if path is None:raise ValueError('缺少旧运行底座的精确布局源：'+relative)
        path=path.resolve();pins[path]=wanted;bootstrap_source_pins[str(path)]=wanted
    for path in HERE.glob('*.py'):pins[path]=sha(path)
    for path in (HERE.parent/'m10a2-gui-owner-review').glob('*.py'):pins[path]=sha(path)
    if not a.execute:
        print(json.dumps(dict(status='REFRESH_PREFLIGHT_ONLY',changed=planned['changed_labels'],bootstrap='PRIOR_NATIVE_PINNED_NOT_REFRESH_OUTPUT',source_disk_unchanged=True)))
        return
    report=dict(status='RUNNING',scope='THREE_TARGET_NATIVE_REFRESH_WITH_PRIOR_NATIVE_BOOTSTRAP',cases=[],screenshots=[],warm_observations=[],
        source_disk_sha256=sha(data),changed_labels=planned['changed_labels'],full_M10a2=False,
        bootstrap_origin='PRIOR_NATIVE_PINNED_NOT_REFRESH_OUTPUT',host_report=str(a.host_report.resolve()),
        core_binding=dict(elf_sha256=sha(core/'core.elf'),core_sha256=sha(core/'fs/SYS/CORE/CORE.SKM')),
        input_sha256=bootstrap_source_pins,current_input_sha256={str(path):value for path,value in pins.items()})
    def persist():(vm_out/'refresh-vm.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    first.BUILD=core
    with first.harness_configuration():
        with QemuSession(core/'sandcore.img',data,vm_out,'tcg',True,audio=False,memory=256,
                         data_prepare=lambda image:prepare(image,core,dict(payload,**bootstrap),runtime_fixtures=False)) as vm:
            guest=GuiGuest(vm);h=None;report['qemu_pid']=vm.process.pid;persist()
            try:
                symbols=unique_symbols(core/'core.sym')
                h=CoherentHarness(vm,guest,report,symbols,persist,first.local_elf_symbols(core/'core.elf'))
                wait_first_desktop(guest,symbols,report);guest.connect()
                for name,command in (('command','true'),('task','ls /HOME'),('background','echo P6_REFRESH_WARM & wait')):h.run('refresh-warm-'+name,command)
                h.warm_desktop()
                selected=sessions(h.run('refresh-original-sessions','sessionctl list')['output'].encode())
                sid=next(r['id'] for r in selected if r['active'])
                desktop=old['native_artifacts']['SCSHELL.SCX'];layout=old['preflight']['task_layout']
                binding=desktop_binding(Path(desktop['path']),desktop['sha256'],Path(desktop['path']+'.sdbg'),desktop['debug']['sha256'],layout)
                h.owner_arguments=(core/'core.elf',layout,binding,sid)
                result=target.execute(h,planned,payload,pins,old,bundle,host_report=a.host_report.resolve())
                report.update(status='THREE_TARGET_REFRESH_GENERATED',bundle_status=result['status'],bundle_report=str(bundle/'p6-generation.json'))
                h.screen('p6-refresh-prior-native-bootstrap-desktop')
            except BaseException as e:
                report.update(status='FAIL_OR_INTERRUPTED',error=dict(message=str(e),traceback=traceback.format_exc()))
                if h is not None:
                    try:h.screen('p6-target-refresh-failure')
                    except Exception:pass
                raise
            finally:
                try:guest.close()
                finally:
                    changed=[str(path) for path,wanted in pins.items() if not path.is_file() or sha(path)!=wanted]
                    report.update(changed_input_output_paths=changed,current_inputs_unchanged=not changed,source_disk_unchanged=sha(data)==old['source_data_sha256'])
                    if changed or not report['source_disk_unchanged']:report['status']='FAIL_OR_INTERRUPTED'
                    persist()

if __name__=='__main__':main()
