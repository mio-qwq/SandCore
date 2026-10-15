#!/usr/bin/env python3
"""P5首次原生运行：可信桌面、普通客户端HELLO/catalog及退出实账。"""
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
from verify_m10a2_ui_gui import GuiGuest, sessions
from verify_m10a2_scl import prepare
from scserial import QemuSession, sha
from m10_guest_boot import unique_symbols, wait_first_desktop
from scl_format import validate
from scd_format import validate_scx

ROOT = first.ROOT


def native_path(value):
    path = Path(value)
    if os.name == 'nt' and value.startswith('/mnt/c/'):
        path = Path('C:/' + value[7:])
    return path.resolve(strict=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'generation-report', 'core-report', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    parser.add_argument('--reuse-probe', type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('仅Windows QEMU')
    stage, out = args.stage.resolve(), args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('需要build内全新输出目录')
    generated, core = refresh.read_json(args.generation_report), refresh.read_json(args.core_report)
    if (generated.get('status') != 'P5_NATIVE_GENERATION_PASS' or not generated.get('current_inputs_unchanged')
            or core.get('exit_code') != 0 or core.get('changed_source_files') != []
            or any(core['inputs_sha256'].get(name) != value for name,value in generated['source_input_sha256'].items())
            or any(not name.startswith('kernel/') for name in set(core['inputs_sha256'])|set(generated['source_snapshot'])
                if core['inputs_sha256'].get(name) != generated['source_snapshot'].get(name))):
        raise ValueError('需要同一冻结候选的实际生成与绑定CORE')
    pins = {stage / name: value for name, value in core['inputs_sha256'].items()}
    for item in core['outputs']:
        pins[stage / item['path']] = item['sha256']
    build = stage / 'build/p5-unified-check'
    for name in ('sandcore.img', 'core.elf', 'core.sym', 'core.bin'):
        pins[build / name] = sha(build / name)
    libraries, metadata = {}, {}
    for name, item in generated['native_libraries'].items():
        actual = native_path(item['path'])
        pins[actual] = item['sha256'];libraries[name] = actual.read_bytes();metadata[name] = item
        source = {'SC32API.SCL':'sc32/SC32API.json','SCUI.SCL':'scui/SCUI.json','SCSHELL.SCL':'scshell/SCSHELL.json'}[name]
        stdlib.source_contract(validate(libraries[name]), refresh.read_json(stage / 'user' / source))
    old = refresh.read_json(ROOT / 'build/m10a2-ui-platform-guest-05/ui-platform.json')
    p3 = refresh.read_json(ROOT / 'evidence/m10a2-stdlib/native.json')
    p3_directory = ROOT / 'evidence/m10a2-stdlib/native-libraries'
    p3_provenance = refresh.read_json(p3_directory / 'provenance.json')
    if (p3.get('status') != 'DECLARED_NATIVE_LIBRARY_STDLIB_CASES_PASS'
            or p3.get('current_inputs_unchanged') is not True or p3.get('source_disk_unchanged') is not True
            or p3_provenance.get('guest_sccc_compiled') is not True):
        raise ValueError('P3基础库缺实际生成/执行来源')
    # 只复用正文和完整私有头均未改变的ROOT/UTIL。P5的新HOST/LEASE
    # 声明不假称这些旧库重新编译；旧公开记录和调用ABI继续保留。
    for name, value in p3_provenance['source_input_sha256'].items():
        if name.startswith(('user/runtime/', 'user/sclib/')):
            pins[stage / name] = value
    for name in ('SCLDR.SCL', 'SCCLIB.SCL', 'SCLIB.SCL'):
        item = old['native_libraries'][name] if name == 'SCCLIB.SCL' else p3_provenance['libraries'][name]
        actual = native_path(item['path']) if name == 'SCCLIB.SCL' else (p3_directory / item['path']).resolve(strict=True)
        if name != 'SCCLIB.SCL' and p3['installed_libraries'][name]['sha256'] != item['sha256']:
            raise ValueError('复用的P3原生产物与真实执行字节不同')
        if not actual.is_file():
            raise ValueError('旧原生产物未保留')
        pins[actual] = item['sha256'];libraries[name] = actual.read_bytes();metadata[name] = dict(item,
            path=str(actual), source='REUSED_PRIOR_ACTUAL_NATIVE_FOUNDATION_NOT_GENERATED_THIS_VM')
    extra = {'SYS/LIB/' + name: body for name, body in libraries.items()}
    for name, item in generated['native_artifacts'].items():
        actual = native_path(item['path']);pins[actual] = item['sha256']
        validate_scx(actual.read_bytes());extra['SYS/SHELL/' + name] = actual.read_bytes()
    issued = core['issued_native_host']
    host = generated['native_artifacts']['SCSHELL.SCX']
    if (issued.get('sha256') != host['sha256'] or issued.get('bytes') != host['bytes']
            or issued.get('generation_report_sha256') != sha(args.generation_report)
            or (build / 'fs/SYS/CORE/CORE.SKM').read_bytes()[128:] != (build / 'core.bin').read_bytes()):
        raise ValueError('CORE必须绑定本轮安装的同一原生host')
    prefix, package = '/TMP/P5RT-' + secrets.token_hex(3), '/HOME/M10SHELL'
    for name in list(first.PUBLIC_HEADERS) + ['SC32HOST.H', 'SC32LEASE.H', 'SCSHELL.H']:
        extra['SYS/INC/' + name] = (stage / 'user' / name).read_bytes()
    for name in first.STANDARD_HEADERS:
        extra['SYS/INC/' + name] = (stage / 'user/stdlib' / name).read_bytes()
    for name in ('sclib/SCLIB.H', 'runtime/SCLDR.H'):
        extra['SYS/INC/' + Path(name).name] = (stage / 'user' / name).read_bytes()
    for suffix in ('.C', '.json'):
        path = ROOT / 'tests/m10a2' / ('SHELLPROBE' + suffix)
        extra[prefix[1:] + '/SHELLPROBE' + suffix] = path.read_bytes();pins[path] = sha(path)
    compiler_item = old['native_artifacts']['SCCC1.SCX']
    compiler_file = native_path(compiler_item['path']);pins[compiler_file] = refresh.PUBLICATION_G1_SHA
    compiler = prefix + '/ACTUALG1.SCX';extra[compiler[1:]] = compiler_file.read_bytes()
    probe_reuse = None
    if args.reuse_probe:
        previous = refresh.read_json(args.reuse_probe)
        case = next(item for item in previous['cases'] if item['name'] == 'actual-G1-SHELLPROBE')
        artifact = args.reuse_probe.parent / 'native-artifacts/SHELLPROBE.SCX'
        if (case.get('status') != 'PASS' or case.get('exit_code') != 0
                or sha(artifact) != case.get('generated_sha256')
                or any(previous['input_sha256'].get(str(path)) != pins[path]
                    for path in pins if path.name in ('SHELLPROBE.C', 'SHELLPROBE.json'))
                or any(previous['native_libraries'][name]['sha256'] != item['sha256'] for name, item in metadata.items())):
            raise ValueError('Probe复用必须保留实际生成字节、源码和全部库字节')
        pins[artifact] = sha(artifact)
        extra[package[1:] + '/GENERATED/SHELLPROBE.SCX'] = artifact.read_bytes()
        probe_reuse = dict(report=str(args.reuse_probe.resolve()), report_sha256=sha(args.reuse_probe),
            artifact_sha256=sha(artifact), generated_this_vm=False, original_compile_case=case)
    for name in ('GENERATED', 'HOST'):
        extra[package[1:] + '/' + name + '/.KEEP'] = b''
    extra[package[1:] + '/WARM.DAT'] = b''
    for path in (build / 'fs/SYS/UI').rglob('*'):
        if path.is_file():
            extra['SYS/UI/' + path.relative_to(build / 'fs/SYS/UI').as_posix()] = path.read_bytes();pins[path] = sha(path)
    for path, expected in pins.items():
        if sha(path) != expected:
            raise ValueError('输入与来源不同：' + str(path))
    original = sha(args.data)
    if original != generated['source_data_sha256']:
        raise ValueError('原盘来源改变')
    report = dict(status='RUNNING', scope='P5_FIRST_NATIVE_HOST_AND_CLIENT_RUNTIME', cases=[], screenshots=[],
        source_data_sha256=original, native_libraries=metadata, native_artifacts=generated['native_artifacts'],
        core_report_sha256=sha(args.core_report), generation_report_sha256=sha(args.generation_report),
        core_sha256=sha(build / 'fs/SYS/CORE/CORE.SKM'), compiler=dict(sha256=refresh.PUBLICATION_G1_SHA),
        input_sha256={str(path): value for path, value in pins.items()}, full_M10a2=False)

    def persist():
        (out / 'shell-runtime.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    first.BUILD, first.PACKAGE = build, package
    symbols = unique_symbols(build / 'core.sym')
    with first.harness_configuration():
        with QemuSession(build / 'sandcore.img', args.data, out, args.accel, True,
                audio=False, memory=256, data_prepare=lambda image: prepare(image, build, extra, runtime_fixtures=False)) as vm:
            report['qemu_pid'] = vm.process.pid
            (out / 'native-artifacts').mkdir()
            guest = GuiGuest(vm)
            try:
                harness = refresh.RefreshHarness(vm, guest, report, symbols, persist, first.local_elf_symbols(build / 'core.elf'))
                wait_first_desktop(guest, symbols, report);guest.connect();harness.warm()
                target = package + '/GENERATED/SHELLPROBE.SCX'
                if probe_reuse:
                    report['probe_compile_reuse'] = probe_reuse
                else:
                    harness.compile('actual-G1-SHELLPROBE', compiler, prefix + '/SHELLPROBE.C', target,
                        prefix + '/SHELLPROBE.json', refresh.read_json(ROOT / 'tests/m10a2/SHELLPROBE.json'),
                        False, out / 'native-artifacts/SHELLPROBE.SCX', compiler_sha256=refresh.PUBLICATION_G1_SHA)
                before, observed_before = harness.quiet();resources_before = harness.resources()
                original_rows = sessions(harness.run('sessions-before-native-host', 'sessionctl list')['output'].encode())
                previous = next(row['id'] for row in original_rows if row['active'])
                harness.login_cooldown()
                marker = '__P5_RESULT_' + secrets.token_hex(8)
                inner = target + '; _p5=$?; echo ' + marker + ':$_p5; exit $_p5'
                item = harness.run('ordinary-native-host-and-real-catalog', "printf '%s\\n' '' '" + inner + "' | su mio",
                    contains=(b'SHELLPROBE PASS ordinary_native_host_and_real_catalog_IPC', (marker + ':0').encode()),
                    forbidden=(b'SHELLPROBE FAIL', b'[AUTH] login failed'), timeout=90)
                sid = re.search(r'SHELLPROBE HOST state=3 sid=(\d+) pid=(\d+)/(\d+) epoch=(\d+)', item['output'])
                if not sid:
                    raise ValueError('缺少公开ACTIVE host身份')
                report['actual_host_identity'] = dict(session=int(sid[1]), pid=int(sid[2]), generation=int(sid[3]), epoch=int(sid[4]))
                harness.run('show-actual-native-desktop', 'sessionctl show ' + sid[1])
                harness.screen('p5-first-actual-native-desktop')
                vm.hmp('sendkey lwin');guest.stop.wait(.4)
                harness.screen('p5-first-actual-native-start-menu')
                vm.hmp('sendkey esc')
                harness.run('restore-original-desktop', 'sessionctl show ' + str(previous))
                harness.run('logout-native-desktop-session', 'sessionctl logout ' + sid[1])
                after, observed_after = harness.quiet()
                ledger, okay = harness.ledger(before, after, resources_before, harness.resources(), True)
                harness.append(dict(name='native-host-client-session-final-owner-page-account', **ledger,
                    before_observations=observed_before, after_observations=observed_after), okay)
                report['status'] = 'P5_FIRST_NATIVE_HOST_CLIENT_PASS'
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                report['failure_console_tail'] = bytes(guest.console[-16000:]).decode('utf-8', 'replace')
                report['failure_debug_tail'] = bytes(guest.debug[-16000:]).decode('utf-8', 'replace')
                try:
                    report['failure_registers'] = vm.hmp('info registers')
                    report['failure_instructions'] = vm.hmp('x /16i $eip')
                    report['failure_stack'] = vm.hmp('x /32wx $esp')
                except Exception as read_error:
                    report['failure_register_error'] = repr(read_error)
                try:harness.screen('p5-first-native-runtime-failure')
                except Exception:pass
                raise
            finally:
                # 串口心跳失联不能遮掉原故障，更不能让最后持久化停在RUNNING。
                try:
                    guest.close()
                except Exception as close_error:
                    report['serial_close_error'] = repr(close_error)
                    report['status'] = 'FAIL_OR_INTERRUPTED'
                changed = [str(path) for path, value in pins.items() if sha(path) != value]
                report.update(changed_input_output_paths=changed, current_inputs_unchanged=not changed,
                    source_disk_unchanged=sha(args.data) == original)
                if changed or not report['source_disk_unchanged']:report['status'] = 'FAIL_OR_INTERRUPTED'
                persist()
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
