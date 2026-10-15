#!/usr/bin/env python3
"""P5单次Windows客体生成：原实际G1编译变化的库、worker和桌面。"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import traceback

import refresh_m10a2_ui_native as refresh
import verify_m10a2_ui_platform as first
import verify_m10a2_stdlib as stdlib
from verify_m10a2_scl import prepare
from scserial import QemuSession, sha
from verify_m9 import Guest
from m10_guest_boot import unique_symbols, wait_first_desktop
from scl_format import validate
from scd_format import validate_scx

ROOT = first.ROOT
CASES = [('SC32API', 'sc32/SC32API', True), ('SCUI', 'scui/SCUI', True),
    ('SCSHELL', 'scshell/SCSHELL', True), ('SCFILEWORK', 'shellworker/SCFILEWORK', False),
    ('DESKTOP', 'scshell/desktop/SCSHELL', False)]


def closure(stage):
    pending = [stage / 'user' / (source + suffix) for _, source, _ in CASES for suffix in ('.C', '.json')]
    roots = [stage / 'user' / folder for folder in ('', 'stdlib', 'sclib', 'runtime', 'textdata')]
    seen = {}
    while pending:
        path = pending.pop().resolve(strict=True)
        if not path.is_relative_to(stage / 'user'):
            raise ValueError('源码include越过用户态源码树')
        name = path.relative_to(stage).as_posix()
        if name in seen:
            continue
        seen[name] = sha(path)
        if path.suffix.lower() not in ('.c', '.h', '.inc'):
            continue
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(encoding='utf-8'), flags=re.S)
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\r\n]+)[>"]', text, re.M):
            name = match[1]
            target = next((base / name for base in [path.parent] + roots if (base / name).is_file()), None)
            if target is None:
                raise ValueError('缺少实际include：' + str(path) + ' -> ' + name)
            pending.append(target)
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'build-report', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('仅Windows QEMU')
    stage, out = args.stage.resolve(), args.out.resolve()
    if not stage.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('需要build内隔离候选及全新输出目录')
    proof = refresh.read_json(args.build_report)
    if proof.get('exit_code') != 0 or proof.get('changed_source_files') != []:
        raise ValueError('需要源码保持的整体P5构建')
    source = proof['inputs_sha256']
    for name, value in source.items():
        if sha(stage / name) != value:
            raise ValueError('候选源码改变：' + name)
    build = stage / 'build/p5-unified-check'
    for item in proof['outputs']:
        if sha(stage / item['path']) != item['sha256']:
            raise ValueError('候选宿主产物改变：' + item['path'])
    files = closure(stage)
    prefix = '/TMP/P5-' + secrets.token_hex(3)
    package = '/HOME/M10SHELL'
    extra = {prefix[1:] + '/SRC/' + name[5:]: (stage / name).read_bytes() for name in files}
    public = list(first.PUBLIC_HEADERS) + ['SC32HOST.H', 'SC32LEASE.H', 'SCSHELL.H']
    for name in public:
        extra['SYS/INC/' + name] = (stage / 'user' / name).read_bytes()
    for name in first.STANDARD_HEADERS:
        extra['SYS/INC/' + name] = (stage / 'user/stdlib' / name).read_bytes()
    for name in ('runtime/SCLDR.H', 'sclib/SCLIB.H'):
        extra['SYS/INC/' + Path(name).name] = (stage / 'user' / name).read_bytes()
    for name in ('SCLDR', 'SC32API', 'SCCLIB', 'SCLIB', 'SCUI', 'SCSHELL'):
        extra['SYS/LIB/' + name + '.SCL'] = (build / (name + '.SCL')).read_bytes()
    for name in ('SCSHELL', 'SCFILEWORK'):
        extra['SYS/SHELL/' + name + '.SCX'] = (build / (name + '.SCX')).read_bytes()
    for path in (build / 'fs/SYS/UI').rglob('*'):
        if path.is_file():
            extra['SYS/UI/' + path.relative_to(build / 'fs/SYS/UI').as_posix()] = path.read_bytes()
    compiler_report = ROOT / 'build/m10a2-ui-platform-guest-05/ui-platform.json'
    refresh.pin(compiler_report, ROOT, refresh.PUBLICATION_PREVIOUS_SHA)
    compiler_item = refresh.read_json(compiler_report)['native_artifacts']['SCCC1.SCX']
    compiler_file = refresh.pin(Path(compiler_item['path']), compiler_report.parent,
        refresh.PUBLICATION_G1_SHA, compiler_item['bytes'])
    compiler = prefix + '/ACTUALG1.SCX'
    extra[compiler[1:]] = compiler_file.read_bytes()
    extra[package[1:] + '/GENERATED/.KEEP'] = b''
    extra[package[1:] + '/HOST/.KEEP'] = b''
    extra[package[1:] + '/WARM.DAT'] = b''
    original = sha(args.data)
    if original != refresh.read_json(compiler_report)['source_data_sha256']:
        raise ValueError('生成会话不是已保持的同一原盘')
    runtime_pins = {build / name: sha(build / name) for name in
        ('sandcore.img', 'fs/SYS/CORE/CORE.SKM', 'core.sym', 'core.elf', 'core.bin')}
    if (build / 'fs/SYS/CORE/CORE.SKM').read_bytes()[128:] != (build / 'core.bin').read_bytes():
        raise ValueError('CORE真实载荷与本批符号来源不一致')
    report = dict(status='RUNNING', scope='P5_NATIVE_GENERATION_ONLY', cases=[], screenshots=[],
        compiler=dict(sha256=sha(compiler_file), source_report_sha256=sha(compiler_report)),
        build_report=str(args.build_report.resolve()), build_report_sha256=sha(args.build_report),
        source_data_sha256=original, native_libraries={}, native_artifacts={},
        source_input_sha256=files, source_snapshot=source, host_outputs=proof['outputs'],
        generated_libraries_installed=False, generated_applications_executed=False, full_M10a2=False)
    report['runtime_input_sha256'] = {str(path): value for path, value in runtime_pins.items()}
    driver = sha(Path(__file__))

    def persist():
        (out / 'shell-generation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    first.BUILD, first.PACKAGE = build, package
    symbols = unique_symbols(build / 'core.sym')
    with first.harness_configuration():
        with QemuSession(build / 'sandcore.img', args.data, out, args.accel, True,
                audio=False, memory=256,
                data_prepare=lambda image: prepare(image, build, extra, runtime_fixtures=False)) as vm:
            report['qemu_pid'] = vm.process.pid
            for folder in ('native-libraries', 'native-artifacts'):
                (out / folder).mkdir()
            guest = Guest(vm)
            try:
                harness = refresh.RefreshHarness(vm, guest, report, symbols, persist,
                    first.local_elf_symbols(build / 'core.elf'))
                wait_first_desktop(guest, symbols, report)
                guest.connect()
                harness.warm()
                for label, source_name, library in CASES:
                    before, observations = harness.quiet()
                    resources = harness.resources()
                    name = Path(source_name).name + ('.SCL' if library else '.SCX')
                    local = out / ('native-libraries' if library else 'native-artifacts') / name
                    manifest = refresh.read_json(stage / 'user' / (source_name + '.json'))
                    body, item = harness.compile('actual-G1-' + label, compiler,
                        prefix + '/SRC/' + source_name + '.C', package + '/GENERATED/' + name,
                        prefix + '/SRC/' + source_name + '.json', manifest, library, local,
                        compiler_sha256=sha(compiler_file))
                    contract = stdlib.source_contract(validate(body) if library else validate_scx(body), manifest, not library)
                    report['native_libraries' if library else 'native_artifacts'][name] = dict(item,
                        path=str(local), compiler_sha256=sha(compiler_file), format_contract=contract,
                        generated_this_vm=True, installed_this_vm=False, executed_this_generated_file=False)
                    after, after_observations = harness.quiet()
                    ledger, okay = harness.ledger(before, after, resources, harness.resources(), False)
                    harness.append(dict(name='actual-G1-' + label + '-compile-owner-page-account', **ledger,
                        before_observations=observations, after_observations=after_observations), okay)
                harness.screen('p5-generation-bootstrap-desktop')
                report['status'] = 'P5_NATIVE_GENERATION_PASS'
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                report['failure_console_tail'] = bytes(guest.console[-12000:]).decode('utf-8', 'replace')
                try:
                    harness.screen('p5-generation-failure')
                except Exception:
                    pass
                raise
            finally:
                guest.close()
                changed = [name for name, value in source.items() if sha(stage / name) != value]
                changed_runtime = [str(path) for path, value in runtime_pins.items() if sha(path) != value]
                report.update(changed_source_files=changed, source_disk_unchanged=sha(args.data) == original,
                    changed_runtime_paths=changed_runtime, current_inputs_unchanged=not changed and not changed_runtime,
                    driver_unchanged=sha(Path(__file__)) == driver)
                if changed or not report['source_disk_unchanged'] or not report['driver_unchanged']:
                    report['status'] = 'FAIL_OR_INTERRUPTED'
                persist()
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
