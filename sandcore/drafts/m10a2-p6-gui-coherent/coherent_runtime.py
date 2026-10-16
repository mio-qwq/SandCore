"""继承原 JSONL 产品输入，只把基点和 finish 接到冻结的一致 owner 观察器。"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import time
from types import SimpleNamespace

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RUNTIME = HERE.parent / 'm10a2-p6-gui-runtime/runtime.py'
OBSERVER = HERE.parent / 'm10a2-gui-owner-review/owner_snapshot.py'
EXPECTED_RUNTIME = '20651143189d331c6adb1e66f0d4a7ac5ccf9bc5f9bf2359e604a8298110325a'
EXPECTED_OBSERVER = '25d87a8f1b5cbac487ed740416a65031a1c456a9cbaf48505ba739c651f859dc'
for directory in (RUNTIME.parent, OBSERVER.parent):
    sys.path.insert(0, str(directory))
import runtime as original_runtime
import owner_snapshot as owner


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pinned(path, expected):
    path = Path(path).resolve(strict=True)
    if not path.is_relative_to(ROOT) or sha(path) != expected:
        raise ValueError('实际来源路径/SHA不匹配：' + str(path))
    return path


def quiet_fields(value):
    return all(v == (0xffffffff if k in ('cleanup_head', 'zombie_head') else 0)
               for k, v in value['pending'].items())


def full_snapshot(value):
    return (value.get('status') == 'ACTUAL_COHERENT_READ_ONLY_SNAPSHOT'
            and value.get('owner_census_complete') is True and value.get('resume_confirmed') is True
            and value.get('host_ready') is True and quiet_fields(value)
            and not value['fs_native_files'] and all(not r['debug_loading'] for r in value['owners'])
            and value['resources']['wait_pages'] == value['resources']['finite_wait_heap']['pages'])


class ObserverPlan:
    def __init__(self, candidate, layout_report):
        if Path(original_runtime.__file__).resolve() != RUNTIME.resolve() or sha(RUNTIME) != EXPECTED_RUNTIME:
            raise ValueError('原runtime不是已冻结基线')
        if Path(owner.__file__).resolve() != OBSERVER.resolve() or sha(OBSERVER) != EXPECTED_OBSERVER:
            raise ValueError('owner helper不是已冻结基线')
        self.candidate, self.pins = candidate, {}
        proof_path = Path(layout_report).resolve(strict=True)
        proof = json.loads(proof_path.read_text(encoding='utf-8'))
        if (not proof_path.is_relative_to(ROOT/'build') or proof.get('schema') != 1
                or proof.get('status') != 'ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST'
                or proof.get('source_unchanged') is not True):
            raise ValueError('新GUI需要独立的实际当前i386布局凭证；不接受替换旧layout头SHA')
        self.pins[proof_path] = sha(proof_path)
        if set(proof['files']) != {'layout.json', 'layout.c', 'layout.o', 'layout.bin', 'build.log'}:
            raise ValueError('当前布局凭证缺少实际编译产物')
        for name, row in proof['files'].items():
            if Path(name).name != name:
                raise ValueError('布局产物必须为凭证目录内叶名')
            path = pinned(proof_path.parent/name, row['sha256'])
            if path.stat().st_size != row['bytes']:
                raise ValueError('布局产物长度不同')
            self.pins[path] = row['sha256']
        layout_path = proof_path.parent/'layout.json'
        self.layout = json.loads(layout_path.read_text(encoding='utf-8'))
        if self.layout.get('schema') != 1 or self.layout.get('method') != 'ACTUAL_I386_GCC_OFFSETOF_NO_GUEST_EXECUTION':
            raise ValueError('不是既有提取器实际生成的布局')
        extractor = ROOT/proof['extractor']['relative']
        self.pins[pinned(extractor, proof['extractor']['sha256'])] = proof['extractor']['sha256']
        if sha(proof_path.parent/'layout.c') != self.layout['source_sha256']:
            raise ValueError('实际布局TU与记录不一致')
        if (proof_path.parent/'layout.bin').read_bytes() != struct.pack('<'+'I'*len(self.layout['values']), *self.layout['values'].values()):
            raise ValueError('实际编译出的布局常量与JSON不一致')
        if proof['headers'] != self.layout['headers']:
            raise ValueError('布局凭证与实际头摘要不同')
        # 只把各真实运行组件的结构来源交给observer。全量当前输入另留原报告；
        # 归档里同名的历史头不与运行组件混成一套假路径。
        runtime_sources, roles = {}, {}
        native = candidate.native
        for relative, expected in self.layout['headers'].items():
            if relative == 'user/scui/SCUIPRIVATE.H':
                label, row = 'SCUI', native['native_libraries']['SCUI.SCL']
            elif relative == 'user/scshell/desktop/DESKTOP.H':
                label, row = 'DESKTOP', native['native_artifacts']['SCSHELL.SCX']
            elif relative.startswith('kernel/'):
                label, row = 'CORE', None
            else:
                raise ValueError('当前布局含未声明运行归属的头：' + relative)
            if row is not None:
                closure = row.get('generation_origin', {}).get('include_manifest_closure',
                          native['preflight']['source_closures'][label])
                if closure.get(relative) != expected:
                    raise ValueError('布局头不是实际运行native组件的编译来源：' + relative)
                role = dict(component=label, image_sha256=row['sha256'], generation_origin=row.get('generation_origin'),
                            native_report=str(candidate.native_path), native_report_sha256=sha(candidate.native_path))
            else:
                if candidate.recorded.get(relative) != expected:
                    raise ValueError('布局头不是实际CORE编译输入：' + relative)
                role = dict(component='CORE', core_binding=candidate.core_binding,
                            host_report=str(candidate.host_path), host_report_sha256=sha(candidate.host_path))
            path = pinned(ROOT/relative, expected)
            if candidate.recorded.get(relative) != expected:
                raise ValueError('当前HOST/source与运行结构来源不一致：' + relative)
            self.pins[path] = expected
            runtime_sources[str(path)] = expected
            roles[relative] = dict(path=str(path), sha256=expected, **role)
        for relative, expected in owner.PRIVATE_SOURCE_PINS.items():
            if candidate.recorded.get(relative) != expected:
                raise ValueError('当前CORE私有结构不是冻结observer支持的源：' + relative)
            path = pinned(ROOT/relative, expected)
            self.pins[path] = expected
            runtime_sources[str(path)] = expected
            roles[relative] = dict(path=str(path), sha256=expected, component='CORE',
                                  host_report=str(candidate.host_path), host_report_sha256=sha(candidate.host_path))
        self.runtime_sources = runtime_sources
        row = native['native_artifacts']['SCSHELL.SCX']
        image = pinned(row['path'], row['sha256'])
        debug = pinned(str(image)+'.sdbg', row['debug']['sha256'])
        self.pins.update({image: row['sha256'], debug: row['debug']['sha256']})
        self.binding = owner.desktop_binding(image, row['sha256'], debug, row['debug']['sha256'], self.layout)
        self.core = pinned(candidate.core/'core.elf', candidate.core_binding['elf_sha256'])
        self.pins[self.core] = candidate.core_binding['elf_sha256']
        history_path = ROOT/'build/m10a2-p6-gui-run-01/p6-gui.json'
        history = json.loads(history_path.read_text(encoding='utf-8'))
        failed = next(r for r in history['cases'] if r['name'] == 'p6-gui-single-final-owner-account')
        if history['status'] != 'FAIL_OR_INTERRUPTED' or failed['status'] != 'FAIL' or failed['physical_delta'] != 205:
            raise ValueError('GUI01原失败记录不匹配；不能覆盖或重新归属历史205页')
        self.pins[history_path] = sha(history_path)
        self.metadata = dict(layout_report=str(proof_path), layout_report_sha256=sha(proof_path),
            layout_sha256=sha(layout_path), runtime_source_roles=roles, observer_input_sha256=runtime_sources,
            core_elf=str(self.core), core_elf_sha256=sha(self.core), native_desktop=self.binding,
            helper_path=str(OBSERVER), helper_sha256=EXPECTED_OBSERVER,
            previous_owner_failure=dict(report=str(history_path), sha256=sha(history_path),
                case=failed['name'], status='FAIL', physical_delta=205,
                attributed_physical_delta=failed['attributed_physical_delta'],
                unexplained_delta=failed['unexplained_delta'], unexpected=failed['unexpected'],
                remains_unexplained=True, exempted=False))

    def view(self, harness):
        return SimpleNamespace(vm=harness.vm, guest=harness.guest, symbols=harness.symbols,
            resources=harness.resources,
            report=dict(core_binding=harness.report['core_binding'], input_sha256=dict(self.runtime_sources)))


class CoherentRuntime(original_runtime.GuiRuntime):
    def __init__(self, flow, harness, *, owner_plan, session_parser, cold_font=False):
        super().__init__(flow, harness, session_parser=session_parser, cold_font=cold_font)
        self.owner_plan, self.original = owner_plan, None
        self.observer = owner_plan.view(harness)
        harness.report['current_input_sha256'] = dict(harness.report['input_sha256'])
        harness.report['coherent_owner'] = dict(status='BASELINE_PENDING', source_binding=owner_plan.metadata,
                                               original_205_pages_exempted=False)

    def establish_baseline(self, row):
        if row['uid'] != 1000 or row['type'] != 'user' or not row['active']:
            raise ValueError('原SID不是实际选中的普通UID1000桌面')
        plan = self.owner_plan
        ready = owner.wait_original_host(self.observer, plan.core, plan.layout, plan.binding, row['id'])
        record = self.h.report['coherent_owner']
        record['baseline_observation'] = ready
        self._persist()
        if ready['status'] != 'ACTUAL_ORIGINAL_HOST_READY' or not full_snapshot(ready.get('snapshot', {})):
            raise ValueError('原native host未取得真实ready及完整一致基点')
        shot = ready['snapshot']
        self.original = dict(session=row['id'], pid=shot['host']['pid'], generation=shot['host']['generation'],
                             image_sha256=plan.binding['image_sha256'])
        self.baseline = (shot['physical_pages'], ready['observations'], shot['resources'])
        self.h.report['baseline'] = dict(pages=shot['physical_pages'], observations=ready['observations'],
                                        resources=shot['resources'], coherent_snapshot=shot, original_host=self.original)
        record.update(status='BASELINE_CAPTURED_NOT_WORKFLOW_PASS', original_host=self.original)
        self._persist()

    def request(self, request):
        if not isinstance(request, dict) or request.get('op') != 'finish':
            return super().request(request)
        # 去重、记录、错误及成功状态沿原runtime；只替换finish的采样来源。
        if not isinstance(request.get('id'), str) or not request['id']:
            raise ValueError('每次输入需要唯一id')
        ident = request['id']
        fingerprint = original_runtime.digest(json.dumps(request, sort_keys=True, ensure_ascii=False).encode())
        prior = self.state['actions'].get(ident)
        if prior:
            if prior['request_sha256'] != fingerprint:
                raise ValueError('相同id携带不同请求')
            return dict(prior, replayed=False)
        record = dict(id=ident, op='finish', request_sha256=fingerprint, status='ATTEMPTING',
                      seconds=time.monotonic(), qemu_pid=self.h.vm.process.pid)
        self.state['actions'][ident] = record
        self._persist()
        try:
            if self.baseline is None or self.original is None:
                raise ValueError('缺创建测试SID之前的真实一致baseline')
            if self.h.vm.audio_capture_id is not None:
                record['final_audio_capture'] = self._audio_stop()
            test = self.state['test_session']
            if test is None:
                raise ValueError('从未绑定真实测试SID')
            listed = self.h.run('p6-final-sessions-'+ident, 'sessionctl list')
            rows = self.session_parser(listed['output'].encode())
            if any(r['id'] == test['session'] for r in rows):
                raise ValueError('测试SID仍在，先正常关闭/领完/切回/logout')
            original = [r for r in rows if r['id'] == self.original['session'] and r['uid'] == 1000
                        and r['type'] == 'user' and r['active']]
            if len(original) != 1:
                raise ValueError('尚未实际恢复原普通SID')
            plan = self.owner_plan
            ready = owner.wait_original_host(self.observer, plan.core, plan.layout, plan.binding, self.original['session'])
            evidence = self.h.report['coherent_owner']
            evidence['final_observation'] = ready
            self._persist()
            if ready['status'] != 'ACTUAL_ORIGINAL_HOST_READY' or not full_snapshot(ready.get('snapshot', {})):
                raise ValueError('原native host终态或一致终账快照未成立')
            shot = ready['snapshot']
            if (shot['host']['pid'], shot['host']['generation']) != (self.original['pid'], self.original['generation']):
                raise ValueError('原native host PID/代数已经改变，不能拿替代进程平账')
            before, observed_before, resources_before = self.baseline
            # 这一对页数和resources都来自同一次暂停；不调用旧quiet再另取resources。
            ledger, okay = self.h.ledger(before, shot['physical_pages'], resources_before, shot['resources'], self.cold_font)
            self.h.append(dict(name='p6-gui-single-final-owner-account', **ledger,
                before_observations=observed_before, after_observations=ready['observations'],
                coherent_pair=True, original_host=self.original, original_205_pages_exempted=False), okay)
            evidence['status'] = 'COHERENT_ACCOUNT_RECORDED_WORKFLOW_VERDICTS_EXTERNAL'
            result = dict(ledger=ledger, session_removed=True, coherent_pair=True,
                          original_205_pages_exempted=False, gui_workflows_still_require_evidence=True)
            self.finished = True
            self.state['status'] = 'INPUT_SESSION_FINISHED_WORKFLOW_VERDICTS_EXTERNAL'
            record.update(status='EXECUTED_EFFECT_REQUIRES_EVIDENCE', result=result)
        except BaseException as error:
            record.update(status='UNVERIFIED_OR_INPUT_UNCERTAIN', error=dict(type=type(error).__name__, message=str(error)))
            self.h.report['coherent_owner'].update(status='COHERENT_FINISH_UNVERIFIED_OR_FAILED',
                                                   last_error=dict(record['error']))
        self._persist()
        return record
