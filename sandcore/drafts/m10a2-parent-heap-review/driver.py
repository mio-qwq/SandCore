"""复用失败07盘的临时写入层观察父桌面，不重新编译五目标。"""
from pathlib import Path
import argparse,json,os,sys,traceback
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
for p in (ROOT/'tools',ROOT/'drafts/m10a2-p6-followup-refresh-v6',ROOT/'drafts/m10a2-gui-owner-review'):
    sys.path.insert(0,str(p))
from followup import Basis,CORE,sha
import owner_snapshot as owner
import survey
import scserial,qemu_config

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or not out.name.startswith('m10a2-parent-heap-observe-'):raise ValueError('new workspace diagnostic output required')
    if out.exists():raise FileExistsError(out)
    basis=Basis();legacy=basis.legacy();core=ROOT/CORE
    source=ROOT/'build/m10a2-p6-refresh-vm-07/sanddata.img';boot=core/'sandcore.img'
    previous=json.loads((source.parent/'refresh-vm.json').read_bytes())
    assert previous['status']=='FAIL_OR_INTERRUPTED' and previous['current_inputs_unchanged'] and previous['source_disk_unchanged']
    proof=survey.layout(ROOT,basis);pins={source:sha(source),boot:sha(boot)}
    report=dict(status='RUNNING',scope='READ_ONLY_PARENT_HEAP_DIAGNOSIS_NOT_NATIVE_REGENERATION',
                cases=[],screenshots=[],warm_observations=[],surveys=[],
                core_binding=previous['core_binding'],input_sha256=previous['input_sha256'],full_M10a2=False)
    def persist():
        if out.exists():(out/'heap-review.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode())
    # 只在构造本VM的两盘入口时建同卷hardlink；QEMU全程-snapshot，
    # 写入进入QEMU临时覆盖层，不写failed07盘，也不新增256MiB副本。
    original_copy=scserial.shutil.copy2;original_prefix=qemu_config.prefix
    def link_copy(origin,destination,*args,**kwargs):
        origin=Path(origin).resolve();destination=Path(destination).resolve()
        if origin not in pins or destination.parent!=out.resolve():raise ValueError('unexpected diagnostic disk copy')
        os.link(origin,destination);return str(destination)
    def prefix(*args,**kwargs):
        command,selected=original_prefix(*args,**kwargs);return command+['-snapshot'],selected
    legacy.first.BUILD=core
    with legacy.first.harness_configuration():
        try:
            scserial.shutil.copy2=link_copy;qemu_config.prefix=prefix
            vm=legacy.QemuSession(boot,source,out,'tcg',True,audio=False,memory=256)
        finally:
            scserial.shutil.copy2=original_copy;qemu_config.prefix=original_prefix
        with vm:
            report['qemu_pid']=vm.process.pid;persist();guest=legacy.GuiGuest(vm)
            try:
                symbols=legacy.unique_symbols(core/'core.sym')
                h=legacy.CoherentHarness(vm,guest,report,symbols,persist,legacy.first.local_elf_symbols(core/'core.elf'))
                legacy.wait_first_desktop(guest,symbols,report);guest.connect()
                h.run('warm-command','true');h.warm_desktop()
                rows=legacy.sessions(h.run('original-session','sessionctl list')['output'].encode())
                sid=next(r['id'] for r in rows if r['active'])
                desktop=basis.runtime_native['native_artifacts']['SCSHELL.SCX']
                binding=legacy.desktop_binding(Path(desktop['path']),desktop['sha256'],Path(desktop['path']+'.sdbg'),desktop['debug']['sha256'],basis.layout)
                h.owner_arguments=(core/'core.elf',basis.layout,binding,sid)
                def take(label):
                    h.quiet();ready=report['coherent_owner_observations'][-1]
                    value=survey.take(h,owner,core,proof,basis,ready);value['label']=label
                    report['surveys'].append(value);persist()
                    print(json.dumps(dict(label=label,arenas=value['arena_count'],arena_pages=value['arena_pages'],
                        warm=value['warm'],glyphs=value['glyph_cache']['count'])),flush=True)
                take('startup')
                print(json.dumps(dict(ready=True,pid=vm.process.pid,report=str(out/'heap-review.json'))),flush=True)
                for line in sys.stdin:
                    request=json.loads(line)
                    if request['op']=='survey':take(request['label'])
                    elif request['op']=='command':
                        allowed={'list':'ls /HOME/P6FOLLOWUP5/OUT','compiler-help':'/SYS/BIN/SCCC.SCX',
                                 'linker-help':'/SYS/BIN/SCLD.SCX'}
                        command=allowed[request['name']]
                        h.run('diagnostic-'+request['name'],command,expected=request.get('expected',0))
                    elif request['op']=='finish':break
                    else:raise ValueError('unsupported read-only diagnostic operation')
                report['status']='DIAGNOSTIC_CLOSED_NOT_ORIGINAL_FAILURE_RESOLUTION'
            except BaseException as error:
                report.update(status='DIAGNOSTIC_FAILED',error=dict(message=str(error),traceback=traceback.format_exc()));raise
            finally:
                guest.close();report['source_disks_unchanged']=all(sha(p)==s for p,s in pins.items());persist()
    report['source_disks_unchanged']=all(sha(p)==s for p,s in pins.items());persist()

if __name__=='__main__':main()
