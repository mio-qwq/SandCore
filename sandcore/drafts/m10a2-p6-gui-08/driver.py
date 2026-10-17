"""主代理持有的一次真实GUI会话；JSON输入只接现有产品和只读证据。"""
from pathlib import Path
import argparse
import json
import os
import sys
import traceback

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for directory in (ROOT/'tools', HERE.parent/'m10a2-p6-gui-flow',
                  HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow',
                  HERE.parent/'m10a2-p6-gui-coherent'):
    sys.path.insert(0, str(directory))

from prepare_candidate import Candidate
from scserial import QemuSession, sha
from m10_guest_boot import unique_symbols, wait_first_desktop
from verify_m10a2_ui_gui import GuiGuest, sessions
import verify_m10a2_ui_platform as first
import refresh_m10a2_ui_native as refresh
from scui_input import ScuiInput
from flow_input import FlowInput
from runtime import serve
from coherent_runtime import CoherentRuntime, ObserverPlan
sys.path.insert(0,str(HERE.parent/'m10a2-p6-gui-followup'))
from heartbeat_guest import FollowupGuest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-report', 'host-report', 'sdk-directory', 'core-build', 'source-image', 'out'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--layout-report', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or not out.name.startswith('m10a2-p6-gui-') or out.exists():
        raise ValueError('必须使用build内新的m10a2-p6-gui-输出目录')
    candidate = Candidate(args.native_report, args.host_report, args.sdk_directory,
                          args.core_build, args.source_image)
    owner_plan = ObserverPlan(candidate, args.layout_report)
    # 原SID时钟的字形缓存按真实malloc块/PTE归属，普通GUI/font账仍沿旧规则。
    sys.path[:0]=[str(HERE.parent/'m10a2-parent-heap-review'),str(HERE.parent/'m10a2-p6-followup-refresh-v6')]
    import survey,cache_observer
    from followup import Basis
    import coherent_runtime
    cache_basis=Basis();cache_proof=survey.layout(ROOT,cache_basis)
    for label,name in (('SCCLIB','SCCLIB.SCL'),('SCLIB','SCLIB.SCL')):
        if candidate.native['native_libraries'][name]['sha256']!=cache_basis.runtime_native['native_libraries'][name]['sha256']:
            raise ValueError('实际运行allocator/cache不是固定私有源')
    coherent_runtime.owner.wait_original_host=cache_observer.configure(coherent_runtime.owner,cache_basis,cache_proof)
    base_harness=refresh.RefreshHarness
    class CacheHarness(base_harness):
        def ledger(self,before,after,rb,ra,cold_font):
            result,okay=super().ledger(before,after,rb,ra,cold_font)
            if okay:
                result['clock_cache_account_not_required']=True
                return result,okay
            return cache_observer.account(result,rb,ra)
    refresh.RefreshHarness=CacheHarness
    pins = dict(candidate.input_pins.pins)
    pins.update(owner_plan.pins)
    pins.update(cache_basis.pins)
    for path in (HERE.parent/'m10a2-parent-heap-review').glob('*.py'):pins[path.resolve()]=sha(path)
    pins[candidate.native_path] = sha(candidate.native_path)
    pins[candidate.source] = candidate.source_sha
    for directory in (candidate.sdk,):
        for path in directory.rglob('*'):
            if path.is_file():pins[path.resolve()] = sha(path)
    for name in ('sandcore.img', 'core.elf', 'core.bin', 'core.sym', 'fs/SYS/CORE/CORE.SKM',
                 'login_program_issue.h', 'login_resource_issue.h', 'shell_host_issue.h'):
        path = candidate.core/name
        pins[path] = sha(path)
    for directory in (HERE, HERE.parent/'m10a2-p6-gui-flow',
                      HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow',
                      HERE.parent/'m10a2-gui-owner-review', HERE.parent/'m10a2-p6-gui-coherent', HERE.parent/'m10a2-p6-gui-followup'):
        for path in directory.glob('*.py'):pins[path.resolve()] = sha(path)
    if not args.execute:
        print(json.dumps(dict(status='P6_GUI_PREFLIGHT_ONLY', payload_files=len(candidate.payload),
            payload_bytes=sum(map(len, candidate.payload.values())), core_binding=candidate.core_binding,
            qemu_started=False, layout_report_sha256=owner_plan.metadata['layout_report_sha256']), ensure_ascii=False))
        return
    if os.name != 'nt':parser.error('只使用Windows QEMU')
    report = dict(status='RUNNING', scope='P6_ORDINARY_GUI_AND_SDK_WORKFLOW', cases=[], screenshots=[],
        physical_input=[], warm_observations=[], full_M10a2=False, GUI_workflows_verified=False,
        native_report=str(candidate.native_path), native_report_sha256=sha(candidate.native_path),
        host_report=str(candidate.host_path), host_report_sha256=sha(candidate.host_path),
        core_binding=candidate.core_binding, input_sha256={str(p):v for p,v in pins.items()})
    def persist():
        (out/'p6-gui.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    first.BUILD = candidate.core
    # 只借用既有资源/owner观察，不套P4/P5旧main的状态或夹具。
    with first.harness_configuration():
        with QemuSession(candidate.core/'sandcore.img', candidate.source, out, args.accel, True,
                         audio=True, memory=256, data_prepare=candidate.install) as vm:
            report['qemu_pid'] = vm.process.pid
            persist()
            guest = FollowupGuest(vm)
            harness = None
            try:
                symbols = unique_symbols(candidate.core/'core.sym')
                harness = refresh.RefreshHarness(vm, guest, report, symbols, persist,
                    first.local_elf_symbols(candidate.core/'core.elf'))
                wait_first_desktop(guest, symbols, report)
                guest.connect()
                # 沿既有warm的父Shell/动态任务/后台表理由，只换已存在路径。
                # 不预建Notes、工程产物或文件复制夹具，不把CLI算GUI动作。
                for name, command in (
                    ('parent-command-cache', 'true'),
                    ('task-history', 'ls /HOME'),
                    ('parent-background-table', 'echo P6_GUI_WARM & wait'),
                ):
                    harness.run('warm-'+name, command)
                harness.warm_desktop()
                flow = FlowInput(ScuiInput(vm, guest, symbols, report['physical_input'].append), harness)
                # 本轮首次使用各应用字形；只沿已验证ledger逐页计入真实
                # TTF owned_pages及首次wait堆，不把未知页差额当作缓存。
                runtime = CoherentRuntime(flow, harness, owner_plan=owner_plan, session_parser=sessions, cold_font=True)
                # AC97保持；只结束启动段录音，防止后续长GUI流程写满静音。
                capture = runtime.request(dict(id='startup-audio-stop', op='audio_stop'))
                if 'error' in capture:raise ValueError('启动段录音未精确停止：'+str(capture['error']))
                original = sessions(harness.run('sessions-before-gui', 'sessionctl list')['output'].encode())
                report['original_sessions'] = original
                active = [row for row in original if row['active']]
                if len(active) != 1:raise ValueError('缺唯一真实原SID')
                report['original_active_session'] = active[0]['id']
                runtime.establish_baseline(active[0])
                harness.login_cooldown()
                login = harness.run('ordinary-gui-login',
                    "printf '%s\\n' '' 'id; exit' | su mio", forbidden=(b'[AUTH] login failed',), timeout=120)
                listed = harness.run('sessions-after-gui-login', 'sessionctl list')
                rows = sessions(listed['output'].encode())
                old_ids = {row['id'] for row in original}
                created = [row for row in rows if row['id'] not in old_ids and row['uid']==1000 and row['type']=='user']
                if len(created) != 1:raise ValueError('未取得唯一真实新UID1000普通SID')
                sid = created[0]['id']
                report['ordinary_login'] = dict(session=sid, row=created[0], login_case=login['name'], list_case=listed['name'])
                harness.run('show-ordinary-gui-session', 'sessionctl show '+str(sid))
                bound = runtime.request(dict(id='initial-ordinary-bind', op='bind', session=sid))
                if 'error' in bound:raise ValueError('真实普通桌面绑定失败')
                persist()
                print(json.dumps(dict(status='P6_GUI_INTERACTIVE_READY', session=sid, pid=vm.process.pid,
                    report=str(out/'p6-gui.json'), GUI_workflows_verified=False)), flush=True)
                serve(runtime)
                if not runtime.finished:raise EOFError('交互输入结束，但尚未完成实际退出和资源账')
                report['status'] = 'P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                report['failure_console_tail'] = bytes(guest.console[-16000:]).decode('utf-8', 'replace')
                if harness is not None:
                    try:harness.screen('p6-gui-failure')
                    except Exception as capture:report['screenshot_error'] = repr(capture)
                raise
            finally:
                try:guest.close()
                finally:
                    changed = [str(path) for path, expected in pins.items() if not path.is_file() or sha(path)!=expected]
                    report.update(changed_input_output_paths=changed, current_inputs_unchanged=not changed,
                        source_disk_unchanged=sha(candidate.source)==candidate.source_sha)
                    if changed or not report['source_disk_unchanged']:report['status']='FAIL_OR_INTERRUPTED'
                    persist()
    report['verified_session_disk_sha256'] = sha(out/'sanddata.img')
    persist()


if __name__ == '__main__':main()
