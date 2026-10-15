#!/usr/bin/env python3
"""只生成变化的 Demo；以逐项源闭包复用 refresh08 的原生库。"""
import argparse
import copy
import json
import os
from pathlib import Path
import secrets
import time
import traceback

import refresh_m10a2_ui_native as refresh
import verify_m10a2_ui_platform as first
import verify_m10a2_stdlib as stdlib
from scserial import QemuSession, sha
from verify_m9 import Guest
from m10_guest_boot import unique_symbols, wait_first_desktop
from scl_format import validate
from scd_format import validate_scx

ROOT, BUILD = first.ROOT, first.BUILD
KIND = 'DEMO_ONLY_NATIVE_REFRESH'
BASE_SHA = '654fc073b84c66ba9afc31f4198d8ca5404afca73fb500a5c78018f08c4645b9'
BASE_BUILD_SHA = '7fa626266741670e8ef15c8b09c24e800d89de441e8b990abf5af89319c9eefc'
DEMO = 'tests/m10a2/SCUIDEMO.C'


def seeds(name):
    if name == 'SCCC1.SCX':
        return ['user/sccc.c']
    if name == 'SWDEFAULT.SCX':
        return ['tests/m10a2/SWDEFAULT.C']
    if name.endswith('.SCL'):
        return ['user/' + value for value in first.LIBRARIES[name]]
    stem = name[:-4]
    return ['tests/m10a2/' + stem + suffix for suffix in ('.C', '.json')]


def base_artifacts(path, build_report, pins):
    refresh.pin(path, ROOT, BASE_SHA, pins=pins)
    old = refresh.read_json(path)
    if (old.get('status') != refresh.STATUS or old.get('current_inputs_unchanged') is not True
            or old.get('source_disk_unchanged') is not True
            or old.get('build_report_sha256') != BASE_BUILD_SHA):
        raise ValueError('必须复用已保持的 refresh08 实际产物')
    _, groups, _ = refresh.recorded_build(Path(old['build_report']), BASE_BUILD_SHA)
    proof, hashes, _ = first.verified_build(build_report)
    new = {key: {item['path']: item for item in proof[key]} for key in ('inputs', 'outputs')}
    changes = {key: refresh.changed_entries(groups[key], new[key], key == 'outputs')
               for key in ('inputs', 'outputs')}
    if (set(changes['inputs']) != {DEMO} or set(changes['outputs']) != {
            (BUILD / 'SCUIDEMO.SCX').relative_to(ROOT).as_posix()}):
        raise ValueError('此入口只接受 Demo 源码和对应宿主产物改变')
    libraries, apps, metadata, closures = {}, {}, {}, {}
    cases = {item['name']: item for item in old['cases']}
    for name, item in dict(old['native_libraries'], **old['native_artifacts']).items():
        if name == 'SCUIDEMO.SCX':
            continue
        actual = refresh.pin(Path(item['path']), path.parent, item['sha256'], item['bytes'], pins)
        closure = refresh.include_closure(seeds(name), compiler_cli=name == 'SCCC1.SCX')
        if any(old['input_sha256'].get(key) != value or hashes.get(key) != value
               for key, value in closure.items()):
            raise ValueError('被复用的实际编译闭包改变：' + name)
        case = cases.get(item.get('compile_case'), {})
        if (case.get('status') not in ('PASS', 'REUSED_PASS') or case.get('exit_code') != 0
                or case.get('generated_sha256') != item['sha256']
                or case.get('generated_bytes') != item['bytes']):
            raise ValueError('复用产物缺原实际成功编译记录：' + name)
        if name in ('SCCC1.SCX', 'SWDEFAULT.SCX'):
            refresh.legacy_g1(actual.read_bytes())
        else:
            spec = json.loads((ROOT / seeds(name)[1]).read_text(encoding='utf-8'))
            contract = stdlib.source_contract(validate(actual.read_bytes()) if name.endswith('.SCL')
                else validate_scx(actual.read_bytes()), spec, not name.endswith('.SCL'))
            if contract != item['format_contract']:
                raise ValueError('原生产物与公开清单不同：' + name)
            (libraries if name.endswith('.SCL') else apps)[name if name.endswith('.SCL') else name[:-4]] = actual.read_bytes()
        metadata[name] = dict(item, source='REUSED_REFRESH08_ACTUAL_NATIVE_BYTES',
            generated_this_vm=False, installed_this_vm=False, executed_this_vm=False,
            reuse_origin=dict(report=str(path), report_sha256=BASE_SHA, compile_closure=closure))
        closures[name] = closure
    return old, libraries, apps, metadata, closures, changes, proof, hashes


def validated(path, build_report, proof_hashes):
    report = refresh.read_json(path)
    reuse = report.get('source_reuse', {})
    old, libraries, apps, metadata, closures, changes, _, hashes = base_artifacts(
        Path(reuse['previous_report']), build_report, {})
    if (report.get('status') != refresh.STATUS or reuse.get('kind') != KIND
            or report.get('current_inputs_unchanged') is not True
            or report.get('source_disk_unchanged') is not True
            or report.get('changed_input_output_paths') != []
            or hashes != proof_hashes or report.get('pinned_build_input_output_sha256') != hashes
            or report.get('build_report_sha256') != sha(build_report)
            or reuse.get('compile_closures') != closures or reuse.get('build_delta') != changes
            or report.get('source_data_sha256') != old['source_data_sha256']):
        raise ValueError('Demo 刷新来源、构建或保持条件不符')
    for name, expected in report['input_sha256'].items():
        refresh.pin(ROOT / name, ROOT, expected)
    for name, item in metadata.items():
        collection = report['native_libraries' if name.endswith('.SCL') else 'native_artifacts']
        if collection.get(name) != item:
            raise ValueError('复用库或应用来源被改写：' + name)
    item = report['native_artifacts']['SCUIDEMO.SCX']
    actual = refresh.pin(Path(item['path']), path.parent, item['sha256'], item['bytes'])
    closure = refresh.include_closure(seeds('SCUIDEMO.SCX'))
    case = next(value for value in report['cases'] if value['name'] == refresh.DEMO_CASE)
    contract = stdlib.source_contract(validate_scx(actual.read_bytes()),
        refresh.read_json(ROOT / 'tests/m10a2/SCUIDEMO.json'), True)
    if (item.get('compile_closure') != closure or item.get('generated_this_vm') is not True
            or item.get('format_contract') != contract or contract['imports'] != 133
            or item.get('compiler_sha256') != metadata['SCCC1.SCX']['sha256']
            or case.get('exit_code') != 0 or case.get('status') != 'PASS'
            or case.get('generated_sha256') != item['sha256']
            or case.get('generated_bytes') != item['bytes']
            or case.get('qemu_pid') != report.get('qemu_pid')
            or item.get('executed_this_generated_file') is not False):
        raise ValueError('Demo 缺实际 G1 生成证据')
    if any(value.get('status') != 'PASS' for value in report['cases']):
        raise ValueError('Demo 生成或回收检查未通过')
    apps['SCUIDEMO'], metadata['SCUIDEMO.SCX'] = actual.read_bytes(), item
    return report, libraries, apps, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('out', 'build-report', 'previous-report'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('仅 Windows QEMU')
    args.out = args.out.resolve()
    if args.out.exists() or not args.out.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('必须使用全新 build 子目录')
    pins = {}
    old, _, _, metadata, closures, changes, proof, hashes = base_artifacts(
        args.previous_report.resolve(), args.build_report.resolve(), pins)
    prefix = '/TMP/UI-DEMO-' + secrets.token_hex(3)
    extra, inputs, excluded = first.payload(prefix, hashes, True)
    for name in refresh.HELPERS + ('tools/refresh_m10a2_ui_demo.py',):
        inputs[name] = sha(ROOT / name)
    compiler = prefix + '/ACTUALG1.SCX'
    extra[compiler[1:]] = Path(metadata['SCCC1.SCX']['path']).read_bytes()
    for name in first.LIBRARIES:
        extra['SYS/LIB/' + name] = (BUILD / name).read_bytes()
    original = sha(args.data)
    if original != old['source_data_sha256']:
        raise ValueError('只读原盘改变')
    report = dict(status='RUNNING', scope=refresh.SCOPE, cases=[], screenshots=[],
        source_reuse=dict(kind=KIND, previous_report=str(args.previous_report.resolve()),
            previous_report_sha256=BASE_SHA, compile_closures=closures, build_delta=changes),
        input_sha256=inputs, pinned_build_input_output_sha256=hashes,
        build_report=str(args.build_report.resolve()), build_report_sha256=sha(args.build_report),
        source_data_sha256=original, compiler_default_repair=True, excluded_source_drafts=excluded,
        native_libraries={name: value for name, value in metadata.items() if name.endswith('.SCL')},
        native_artifacts={name: value for name, value in metadata.items() if not name.endswith('.SCL')},
        generated_system_libraries_execution=False, GUI_behavior_verified=False, full_M10a2=False)

    def persist():
        (args.out / 'ui-platform.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        for folder in ('native-libraries', 'native-artifacts'):
            directory = args.out / folder
            directory.mkdir(exist_ok=True)
            (directory / 'provenance.json').write_text(json.dumps(dict(schema=1,
                report='../ui-platform.json', source_reuse=report['source_reuse'],
                libraries=report['native_libraries'], applications=report['native_artifacts'],
                generation_this_vm='SCUIDEMO.SCX_ONLY', current_status=report['status']),
                ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    symbols = unique_symbols(BUILD / 'core.sym')
    with first.harness_configuration():
        with QemuSession(BUILD / 'sandcore.img', args.data, args.out, args.accel, True,
                audio=False, memory=256,
                data_prepare=lambda image: first.prepare(image, BUILD, extra, runtime_fixtures=False)) as vm:
            report['qemu_pid'] = vm.process.pid
            guest = Guest(vm)
            try:
                harness = refresh.RefreshHarness(vm, guest, report, symbols, persist,
                    first.local_elf_symbols(BUILD / 'core.elf'))
                wait_first_desktop(guest, symbols, report)
                guest.connect()
                harness.warm()
                before, observations = harness.quiet()
                resources = harness.resources()
                local = args.out / 'native-artifacts/SCUIDEMO.SCX'
                manifest = refresh.read_json(ROOT / 'tests/m10a2/SCUIDEMO.json')
                body, item = harness.compile(refresh.DEMO_CASE, compiler, prefix + '/SCUIDEMO.C',
                    first.PACKAGE + '/GENERATED/SCUIDEMO.SCX', prefix + '/SCUIDEMO.json',
                    manifest, False, local, compiler_sha256=metadata['SCCC1.SCX']['sha256'])
                report['native_artifacts']['SCUIDEMO.SCX'] = dict(item, path=str(local),
                    generated_this_vm=True, installed_this_vm=False, executed_this_generated_file=False,
                    compiler_sha256=metadata['SCCC1.SCX']['sha256'],
                    format_contract=stdlib.source_contract(validate_scx(body), manifest, True),
                    compile_closure=refresh.include_closure(seeds('SCUIDEMO.SCX')))
                after, after_observations = harness.quiet()
                ledger, okay = harness.ledger(before, after, resources, harness.resources(), False)
                harness.append(dict(name='refresh-DEMO-compile-actual-owner-page-account', **ledger,
                    before_observations=observations, after_observations=after_observations), okay)
                report['status'] = refresh.STATUS
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                raise
            finally:
                guest.close()
                changed = [name for name, value in dict(hashes, **inputs).items() if sha(ROOT / name) != value]
                changed += [str(path) for path, value in pins.items() if sha(path) != value]
                report.update(changed_input_output_paths=changed, current_inputs_unchanged=not changed,
                    source_disk_unchanged=sha(args.data) == original)
                if changed or not report['source_disk_unchanged']:
                    report['status'] = 'FAIL_OR_INTERRUPTED'
                persist()
    validated(args.out / 'ui-platform.json', args.build_report, hashes)
    print('DEMO_ONLY_NATIVE_REFRESH_PASS; unchanged native libraries reused', flush=True)


if __name__ == '__main__':
    main()
