"""只刷新Root逐项显式指定且与真实变化完全相等的标签；默认不创建VM。"""
from pathlib import Path
import argparse
import json
import sys
import traceback

sys.dont_write_bytecode = True
from followup import Basis, CORE, GENERATION_HOST, HERE, RUNTIME_HOST, LAYOUT, PREVIOUS, PREVIOUS_SHA, ROOT, SDK, WITNESS, inside, read, sha, validated_labels, exact_changed_labels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen-audit', action='store_true', help='只读固定底座、layout和actual-get witness，不读HOST21、不构建、不接VM')
    parser.add_argument('--label', action='append', help='Root明确批准的完整实际CASES标签；可重复给多个标签')
    parser.add_argument('--host-report', type=Path)
    parser.add_argument('--host-sha256')
    parser.add_argument('--vm-out', type=Path)
    parser.add_argument('--bundle-out', type=Path)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    if args.frozen_audit and args.execute:
        parser.error('--frozen-audit不能和--execute同时使用')
    basis = Basis()
    witnesses = basis.audit_sidecars()
    if args.frozen_audit:
        print(json.dumps(dict(status='READ_ONLY_FROZEN_BASIS_AUDIT_PASS_NOT_REFRESHED',
            previous=PREVIOUS, previous_sha256=PREVIOUS_SHA,
            outer_witness=WITNESS, sidecars=witnesses, current_layout=LAYOUT,
            baseline_core=CORE, baseline_sdk=SDK, historical_task_layout_untouched=True,
            build_started=False, vm_started=False), ensure_ascii=False, indent=2))
        return
    if any(value is None for value in (args.host_report, args.host_sha256, args.vm_out, args.bundle_out)):
        parser.error('计划/执行均须显式给--label、HOST21完整报告SHA及两个全新输出路径')
    if len(args.host_sha256) != 64 or any(c not in '0123456789abcdefABCDEF' for c in args.host_sha256):
        raise ValueError('HOST21必须由Root显式给出完整64位SHA256')
    host_path = inside(args.host_report)
    if host_path.name != 'p6-unified-host-21.json':
        raise ValueError('本轮只接Root全部修复收齐后的HOST21完整报告')
    basis.pin(host_path, args.host_sha256.lower())
    vm_out, bundle = args.vm_out.resolve(), args.bundle_out.resolve()
    for path, prefix in ((vm_out, 'm10a2-p6-refresh-vm-'), (bundle, 'm10a2-p6-target-refresh-')):
        if not path.is_relative_to(ROOT / 'build') or not path.name.startswith(prefix) or path.exists():
            raise ValueError('需要build内对应前缀的两个全新输出目录')
    legacy = basis.legacy()
    target, sdk = legacy.target, legacy.sdk
    labels = validated_labels(args.label, target.contract.cases())
    planned, payload, pins, previous = target.preparation(ROOT / PREVIOUS, PREVIOUS_SHA, allowed=labels)
    if previous != basis.old:
        raise ValueError('preparation读取的previous与固定原报告不同')
    # Caller指定集合仍必须等于逐字节闭包比较的实际集合，不自动扩大范围。
    exact_changed_labels(labels, planned['changed_labels'])
    host_inputs = sdk.Inputs()
    _, recorded, _ = sdk.verified_build(host_path, host_inputs)
    for name, expected in planned['source_input_sha256'].items():
        if recorded.get(name) != expected:
            raise ValueError('HOST21与实际当前SCCC源快照不一致：' + name)
    for name, row in planned['manifests'].items():
        source = inside(row['source'])
        relative = source.relative_to(ROOT).as_posix()
        if recorded.get(relative) != row['sha256']:
            raise ValueError('HOST21未固定同一formal NATIVE.JSON：' + name)
    pins.update(host_inputs.pins)
    bootstrap = {}

    def add(destination, path, expected, size=None):
        path = basis.pin(path, expected, size)
        bootstrap[destination] = path.read_bytes()

    # 所有运行SCL保持final04 SDK的旧原生字节；本轮新库只取回，不覆盖SYS。
    for name, row in basis.runtime_native['native_libraries'].items():
        destination = 'SYS/LIB/' + name
        sdk_row = basis.manifest['files'][destination]
        if sdk_row['sha256'] != row['sha256'] or sdk_row['bytes'] != row['bytes']:
            raise ValueError('final04 SDK与previous八库不一致：' + name)
        basis.pin(ROOT / SDK / destination, row['sha256'], row['bytes'])
        add(destination, row['path'], row['sha256'], row['bytes'])
    for destination in ('SYS/LIB/SCSOUND.SCL', 'SYS/UI/UAX15_1.SCU'):
        row = basis.manifest['files'][destination]
        add(destination, ROOT / SDK / destination, row['sha256'], row['bytes'])
    artifacts = basis.runtime_native['native_artifacts']
    for name, destination in (('LOGIN2.SCX', 'SYS/CORE/LOGIN2.SCX'),
            ('SCSHELL.SCX', 'SYS/SHELL/SCSHELL.SCX'), ('SCFILEWORK.SCX', 'SYS/SHELL/SCFILEWORK.SCX'),
            ('SCCC1.SCX', 'SYS/BIN/SCCC.SCX'), ('SCLD.SCX', 'SYS/BIN/SCLD.SCX')):
        row = artifacts[name]
        add(destination, row['path'], row['sha256'], row['bytes'])
    for row in basis.core_receipt['actual_compiled_programs']:
        actual = artifacts[row['name']]
        if row['source']['sha256'] != actual['sha256'] or row['source']['bytes'] != actual['bytes']:
            raise ValueError('final04 CORE嵌入程序与旧运行底座不一致')
    old_host = read(ROOT / RUNTIME_HOST)
    image = next(row for row in old_host['outputs'] if row['path'].endswith('/fs/SYS/CORE/IMAGE.SCX'))
    add('SYS/CORE/IMAGE.SCX', image['path'], image['sha256'], image['bytes'])
    core, data = ROOT / CORE, ROOT / 'build/m10a1/sanddata.img'
    basis.pin(data, basis.old['source_data_sha256'])
    observer_pins = {row['path']: row['sha256'] for row in basis.layout_roles.values()}
    for name, expected in legacy.PRIVATE_SOURCE_PINS.items():
        if basis.core_inputs['non_generated_compile_sources'].get(name) != expected:
            raise ValueError('冻结owner helper私有源不属于实际final04 CORE闭包：' + name)
        observer_pins[str(basis.source(name, expected))] = expected
    for path in HERE.glob('*.py'):
        basis.pins[path] = sha(path)
    basis.pins[HERE / 'PINS.json'] = sha(HERE / 'PINS.json')
    pins.update(basis.pins)
    if host_inputs.unchanged():
        raise ValueError('只读准备期间HOST21输入发生变化')
    baseline = dict(origin='FINAL04_PRIOR_NATIVE_RUNTIME_NOT_FOLLOWUP_OUTPUT', requested_labels=labels,
        previous_report=str(ROOT / PREVIOUS), previous_report_sha256=PREVIOUS_SHA,
        runtime_native04_producer_host=dict(path=str(ROOT / basis.runtime_producer_host), sha256=sha(ROOT / basis.runtime_producer_host)), inherited_native09_producer_host=dict(path=str(ROOT / GENERATION_HOST), sha256=sha(ROOT / GENERATION_HOST)),
        runtime_host=dict(path=str(ROOT / RUNTIME_HOST), sha256=sha(ROOT / RUNTIME_HOST)),
        core=str(core), core_sha256=sha(core / 'fs/SYS/CORE/CORE.SKM'),
        sdk_manifest=str(ROOT / SDK / 'MANIFEST.json'), sdk_manifest_sha256=sha(ROOT / SDK / 'MANIFEST.json'),
        native_libraries={name: dict(bytes=row['bytes'], sha256=row['sha256'])
                          for name, row in basis.runtime_native['native_libraries'].items()},
        host_codec_exception='SCSOUND.SCL_ONLY_FIXED_FINAL04_SDK',
        current_layout_proof=str(ROOT / LAYOUT), current_layout_proof_sha256=sha(ROOT / LAYOUT),
        actual_layout_source_roles=basis.layout_roles,
        historical_preflight_task_layout_preserved=True,
        extra_actual_get_witness=dict(path=str(ROOT / WITNESS), sha256=sha(ROOT / WITNESS)),
        sidecar_audit=witnesses)
    if not args.execute:
        print(json.dumps(dict(status='FOLLOWUP_PREFLIGHT_ONLY_NOT_REFRESHED',
            changed=planned['changed_labels'], host_report=str(host_path), host_report_sha256=sha(host_path),
            bootstrap=baseline, build_started=False, vm_started=False), ensure_ascii=False, indent=2))
        return
    report = dict(status='RUNNING', scope='FOLLOWUP_EXPLICIT_LABEL_NATIVE_REFRESH_WITH_FINAL04_BOOTSTRAP',
        cases=[], screenshots=[], warm_observations=[], source_disk_sha256=sha(data),
        changed_labels=planned['changed_labels'], requested_labels=labels, full_M10a2=False,
        bootstrap_origin=baseline['origin'], bootstrap_binding=baseline,
        host_report=str(host_path), host_report_sha256=sha(host_path),
        core_binding=dict(elf_sha256=sha(core / 'core.elf'), core_sha256=sha(core / 'fs/SYS/CORE/CORE.SKM')),
        input_sha256=observer_pins, current_input_sha256={str(path): value for path, value in pins.items()})

    def persist():
        report['sidecar_actual_get_witnesses_used'] = basis.sidecar_audit
        (vm_out / 'refresh-vm.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    legacy.first.BUILD = core
    with basis.bind_generation(target, planned), legacy.first.harness_configuration():
        with legacy.QemuSession(core / 'sandcore.img', data, vm_out, 'tcg', True, audio=False, memory=256,
                data_prepare=lambda image: legacy.prepare(image, core, dict(payload, **bootstrap), runtime_fixtures=False)) as vm:
            guest, harness = legacy.GuiGuest(vm), None
            report['qemu_pid'] = vm.process.pid
            persist()
            try:
                symbols = legacy.unique_symbols(core / 'core.sym')
                harness = legacy.CoherentHarness(vm, guest, report, symbols, persist, legacy.first.local_elf_symbols(core / 'core.elf'))
                legacy.wait_first_desktop(guest, symbols, report)
                guest.connect()
                for name, command in (('command', 'true'), ('task', 'ls /HOME'), ('background', 'echo P6_REFRESH_WARM & wait')):
                    harness.run('refresh-warm-' + name, command)
                harness.warm_desktop()
                sessions = legacy.sessions(harness.run('refresh-original-sessions', 'sessionctl list')['output'].encode())
                session = next(row['id'] for row in sessions if row['active'])
                desktop = artifacts['SCSHELL.SCX']
                binding = legacy.desktop_binding(Path(desktop['path']), desktop['sha256'],
                    Path(desktop['path'] + '.sdbg'), desktop['debug']['sha256'], basis.layout)
                harness.owner_arguments = (core / 'core.elf', basis.layout, binding, session)
                result = target.execute(harness, planned, payload, pins, basis.old, bundle, host_report=host_path,
                                        work='/HOME/P6FOLLOWUP5')
                report.update(status='EXPLICIT_LABEL_REFRESH_GENERATED', bundle_status=result['status'],
                              bundle_report=str(bundle / 'p6-generation.json'))
                harness.screen('p6-followup-final04-bootstrap-desktop')
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                if harness is not None:
                    try:
                        harness.screen('p6-followup-refresh-failure')
                    except Exception:
                        pass
                raise
            finally:
                try:
                    guest.close()
                finally:
                    changed = [str(path) for path, expected in pins.items() if not path.is_file() or sha(path) != expected]
                    report.update(changed_input_output_paths=changed, current_inputs_unchanged=not changed,
                                  source_disk_unchanged=sha(data) == basis.old['source_data_sha256'])
                    if changed or not report['source_disk_unchanged']:
                        report['status'] = 'FAIL_OR_INTERRUPTED'
                    persist()


if __name__ == '__main__':
    main()
