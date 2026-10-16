#!/usr/bin/env python3
"""P5首次原生运行：可信桌面、普通客户端HELLO/catalog及退出实账。"""
import argparse
import json
import os
from pathlib import Path
import re
import secrets
import hashlib
import struct
import time
import traceback

import refresh_m10a2_ui_native as refresh
import verify_m10a2_ui_platform as first
import verify_m10a2_stdlib as stdlib
from verify_m10a2_ui_gui import GuiGuest, sessions
from verify_m10a2_scl import prepare
from scserial import QemuSession, sha
from m10_guest_boot import unique_symbols, wait_first_desktop, physical_word
from scui_input import ScuiInput
from scl_format import validate
from scd_format import validate_scx

ROOT = first.ROOT


class ObservedGuest(GuiGuest):
    prepared_observer=None

    def wait(self, pattern, start=0, timeout=60, debug=False):
        if not self.prepared_observer or debug:
            return super().wait(pattern,start,timeout,debug)
        deadline=time.monotonic()+timeout
        while True:
            self.prepared_observer()
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError('原等待期限到达：'+repr(pattern))
            try:return super().wait(pattern,start,min(.15,remaining),debug)
            except TimeoutError:
                if time.monotonic()>=deadline:raise


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
    parser.add_argument('--inspect-layout', type=Path)
    parser.add_argument('--file-jobs', action='store_true', help='定向验证真实typed查询及COPY/MOVE worker')
    parser.add_argument('--file-clipboard', action='store_true', help='真实不同进程发布FILE并退出后执行粘贴')
    parser.add_argument('--desktop-layout', action='store_true', help='真实PS/2单图标/框选/组拖动及持久文件核验')
    parser.add_argument('--resume-layout-report', type=Path, help='只接受先前通过流程关闭后的同一测试盘，验证冷启动恢复')
    parser.add_argument('--profile-icons', action='store_true', help='同一VM内用无窗口普通进程量冷图标加载分项')
    parser.add_argument('--icon-baseline-report',type=Path,help='只替换测试盘初始SCSHELL库，在同VM实际比较旧/新冷加载')
    parser.add_argument('--session-cycle',action='store_true',help='先退出一个真实隐藏PREPARED会话，再核新会话首帧/布局')
    args = parser.parse_args()
    if args.file_jobs and args.file_clipboard:parser.error('每次只运行一个定向文件工作流')
    args.file_jobs = args.file_jobs or args.file_clipboard
    if args.desktop_layout and (args.file_jobs or not args.inspect_layout):
        parser.error('桌面布局需当前只读布局文件，不能同时执行文件Probe')
    if args.resume_layout_report and not args.desktop_layout:parser.error('冷恢复仅用于桌面布局')
    if args.profile_icons and not args.desktop_layout:parser.error('图标分项与本轮实际桌面画面一起观察')
    if args.icon_baseline_report and not args.profile_icons:parser.error('前后比较需要图标分项')
    if args.session_cycle and (args.profile_icons or args.file_jobs):parser.error('会话生命周期只走桌面定向流程')
    probe_name = 'FILECLIPPROBE' if args.file_clipboard else 'FILEJOBPROBE' if args.file_jobs else 'SHELLPROBE'
    if args.file_jobs and (args.reuse_probe or args.inspect_layout):
        parser.error('文件事务使用本轮真实编译，不借用host专用Probe/观察器')
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
    for name in ('verify_m10a2_shell_runtime.py','verify_m10a2_scl.py','m10a2_shell_desktop_flow.py',
                 'm10a2_shell_inspect.py','m10a2_icon_comparison.py','scserial.py'):
        driver=ROOT/'tools'/name;pins[driver]=sha(driver)
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
    icon_baseline=None
    if args.icon_baseline_report:
        baseline=refresh.read_json(args.icon_baseline_report)
        if baseline.get('status')!='P5_NATIVE_GENERATION_PASS' or not baseline.get('current_inputs_unchanged'):
            raise ValueError('图标基线缺实际原生生成来源')
        baseline_item=baseline['native_libraries']['SCSHELL.SCL'];baseline_file=native_path(baseline_item['path'])
        if sha(baseline_file)!=baseline_item['sha256'] or baseline_item['sha256']==metadata['SCSHELL.SCL']['sha256']:
            raise ValueError('图标基线摘要错误或与新库相同')
        pins[baseline_file]=baseline_item['sha256'];pins[args.icon_baseline_report.resolve()]=sha(args.icon_baseline_report)
        icon_baseline=dict(report=str(args.icon_baseline_report.resolve()),report_sha256=sha(args.icon_baseline_report),
            baseline_library_sha256=baseline_item['sha256'],optimized_library_sha256=metadata['SCSHELL.SCL']['sha256'],
            initial_library_path=str(baseline_file),temporary_test_disk_only=True)
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
    icon_aliases=[]
    if args.profile_icons:
        for name,body in [('FAST.SCL',libraries['SCSHELL.SCL'])]+([('BASE.SCL',baseline_file.read_bytes())] if icon_baseline else []):
            table=validate(body);alias=bytearray(body)
            offset=table['strings_offset']+struct.unpack_from('<I',body,44)[0]
            old_name=table['name'].encode()+b'\0';new_name=name.encode()+b'\0'
            if len(new_name)>len(old_name):raise ValueError('测试库别名不能改变表偏移')
            alias[offset:offset+len(old_name)]=new_name+b'\0'*(len(old_name)-len(new_name))
            changed=validate(alias)
            if changed['name']!=name:raise ValueError('别名容器名字错误')
            payload=[]
            for segment in table['segments']:
                at=segment['file_offset'];size=segment['file_size']
                if body[at:at+size]!=alias[at:at+size]:raise ValueError('别名改变生产机器码或数据段')
                payload.append(dict(file_offset=at,file_size=size,sha256=hashlib.sha256(body[at:at+size]).hexdigest()))
            extra[package[1:]+'/GENERATED/'+name]=bytes(alias)
            icon_aliases.append(dict(name=name,production_sha256=hashlib.sha256(body).hexdigest(),
                alias_sha256=hashlib.sha256(alias).hexdigest(),only_changed_name_string=dict(offset=offset,bytes=len(old_name)),
                all_segment_payloads_unchanged=payload))
    for name in list(first.PUBLIC_HEADERS) + ['SC32HOST.H', 'SC32LEASE.H', 'SC32PATH.H', 'SCSHELL.H']:
        extra['SYS/INC/' + name] = (stage / 'user' / name).read_bytes()
    for name in first.STANDARD_HEADERS:
        extra['SYS/INC/' + name] = (stage / 'user/stdlib' / name).read_bytes()
    for name in ('sclib/SCLIB.H', 'runtime/SCLDR.H'):
        extra['SYS/INC/' + Path(name).name] = (stage / 'user' / name).read_bytes()
    for suffix in ('.C', '.json'):
        path = ROOT / 'tests/m10a2' / (probe_name + suffix)
        extra[prefix[1:] + '/' + probe_name + suffix] = path.read_bytes();pins[path] = sha(path)
        if args.profile_icons:
            path=ROOT/'tests/m10a2'/('ICONPROFILE'+suffix)
            extra[prefix[1:]+'/ICONPROFILE'+suffix]=path.read_bytes();pins[path]=sha(path)
    if args.file_jobs:
        extra[package[1:] + '/OP-SOURCE.BIN'] = bytes((i * 37 + 19) & 255 for i in range(131079))
    if args.file_clipboard:
        include = ROOT / 'tests/m10a2/FILEJOBPROBE.C'
        extra[prefix[1:] + '/FILEJOBPROBE.C'] = include.read_bytes();pins[include] = sha(include)
        extra[package[1:] + '/OP-MOVE.BIN'] = extra[package[1:] + '/OP-SOURCE.BIN']
        extra[package[1:] + '/CLIPDEST/.KEEP'] = b''
    compiler_item = old['native_artifacts']['SCCC1.SCX']
    compiler_file = native_path(compiler_item['path']);pins[compiler_file] = refresh.PUBLICATION_G1_SHA
    compiler = prefix + '/ACTUALG1.SCX';extra[compiler[1:]] = compiler_file.read_bytes()
    probe_reuse = None
    if args.reuse_probe:
        previous = refresh.read_json(args.reuse_probe)
        case = next(item for item in previous['cases'] if item['name'] == 'actual-G1-SHELLPROBE')
        artifact = args.reuse_probe.parent / 'native-artifacts/SHELLPROBE.SCX'
        headers = {str(path): value for path,value in pins.items()
            if path.is_relative_to(stage / 'user') and path.suffix.lower() == '.h'}
        if (case.get('status') != 'PASS' or case.get('exit_code') != 0
                or sha(artifact) != case.get('generated_sha256')
                or case.get('guest_sccc_compiled') is not True
                or case.get('compiler_sha256') != refresh.PUBLICATION_G1_SHA
                or any(previous['input_sha256'].get(str(path)) != pins[path]
                    for path in pins if path.name in ('SHELLPROBE.C', 'SHELLPROBE.json'))
                or any(previous['input_sha256'].get(name) != value for name,value in headers.items())):
            raise ValueError('Probe编译复用必须保留G1字节、源/manifest和全部候选头')
        # SCX只导入符号，库正文不进入编译。实现修复后仍实际加载并执行
        # 当前库，不能把“复用caller编译”扩为复用旧库的执行结果。
        stdlib.source_contract(validate_scx(artifact.read_bytes()),
            refresh.read_json(ROOT / 'tests/m10a2/SHELLPROBE.json'), True)
        pins[artifact] = sha(artifact)
        extra[package[1:] + '/GENERATED/SHELLPROBE.SCX'] = artifact.read_bytes()
        probe_reuse = dict(report=str(args.reuse_probe.resolve()), report_sha256=sha(args.reuse_probe),
            artifact_sha256=sha(artifact), generated_this_vm=False, original_compile_case=case,
            unchanged_headers=headers, old_execution_results_not_reused=True,
            runtime_library_changes={name:dict(before=previous['native_libraries'][name]['sha256'],after=item['sha256'])
                for name,item in metadata.items() if previous['native_libraries'][name]['sha256'] != item['sha256']})
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
    resumed = None
    if args.resume_layout_report:
        resumed=refresh.read_json(args.resume_layout_report)
        if (resumed.get('status')!='P5_NATIVE_DESKTOP_LAYOUT_PASS'
                or not resumed.get('current_inputs_unchanged') or not resumed.get('source_disk_unchanged')
                or resumed.get('final_test_data_sha256')!=original
                or args.data.resolve()!=(args.resume_layout_report.parent/'sanddata.img').resolve()
                or resumed.get('generation_report_sha256')!=sha(args.generation_report)
                or resumed.get('core_report_sha256')!=sha(args.core_report)):
            raise ValueError('冷恢复必须追溯同一已通过工作流的关闭后测试盘/原生库/CORE')
    elif original != generated['source_data_sha256']:
        raise ValueError('原盘来源改变')
    report = dict(status='RUNNING', scope='P5_FIRST_NATIVE_HOST_AND_CLIENT_RUNTIME', cases=[], screenshots=[],
        physical_input=[],
        source_data_sha256=original, native_libraries=metadata, native_artifacts=generated['native_artifacts'],
        core_report_sha256=sha(args.core_report), generation_report_sha256=sha(args.generation_report),
        core_sha256=sha(build / 'fs/SYS/CORE/CORE.SKM'), compiler=dict(sha256=refresh.PUBLICATION_G1_SHA),
        input_sha256={str(path): value for path, value in pins.items()}, full_M10a2=False)
    if args.file_jobs:
        report['scope'] = ('P5_REAL_CROSS_PROCESS_FILE_CLIPBOARD_AND_PUBLISHER_EXIT' if args.file_clipboard
            else 'P5_TYPED_PATH_QUERY_AND_REAL_COPY_MOVE_BINARY_WORKFLOW')
    if args.desktop_layout:
        report['scope']='P5_REAL_NATIVE_DESKTOP_LAYOUT_COLD_RESTORE' if resumed else 'P5_REAL_NATIVE_DESKTOP_DRAG_BOX_AND_PERSIST'
        if resumed:report['resume_layout_source']=dict(report=str(args.resume_layout_report.resolve()),
            report_sha256=sha(args.resume_layout_report),test_data_sha256=original)
    if icon_baseline:report['icon_comparison_sources']=icon_baseline
    if icon_aliases:report['icon_test_aliases']=icon_aliases

    def persist():
        (out / 'shell-runtime.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def prepared(image):
        value=prepare(image,build,extra,runtime_fixtures=False)
        value.update(host_bootstrap_libraries=False,generated_libraries_installed=True,
            installed_native_library_sha256={name:item['sha256'] for name,item in metadata.items()},
            installed_native_host_sha256=generated['native_artifacts']['SCSHELL.SCX']['sha256'])
        return value

    first.BUILD, first.PACKAGE = build, package
    symbols = unique_symbols(build / 'core.sym')
    with first.harness_configuration():
        with QemuSession(build / 'sandcore.img', args.data, out, args.accel, True,
                audio=False, memory=256, data_prepare=prepared) as vm:
            report['qemu_pid'] = vm.process.pid
            (out / 'native-artifacts').mkdir()
            guest = ObservedGuest(vm)
            try:
                harness = refresh.RefreshHarness(vm, guest, report, symbols, persist, first.local_elf_symbols(build / 'core.elf'))
                wait_first_desktop(guest, symbols, report);guest.connect();harness.warm()
                target = package + '/GENERATED/' + probe_name + '.SCX'
                if probe_reuse:
                    report['probe_compile_reuse'] = probe_reuse
                else:
                    harness.compile('actual-G1-' + probe_name, compiler, prefix + '/' + probe_name + '.C', target,
                        prefix + '/' + probe_name + '.json', refresh.read_json(ROOT / 'tests/m10a2' / (probe_name + '.json')),
                        False, out / 'native-artifacts' / (probe_name + '.SCX'), compiler_sha256=refresh.PUBLICATION_G1_SHA)
                before, observed_before = harness.quiet();resources_before = harness.resources()
                icon_target=package+'/GENERATED/ICONPROFILE.SCX'
                if args.profile_icons:
                    harness.compile('actual-G1-ICONPROFILE',compiler,prefix+'/ICONPROFILE.C',icon_target,
                        prefix+'/ICONPROFILE.json',refresh.read_json(ROOT/'tests/m10a2/ICONPROFILE.json'),False,
                        out/'native-artifacts/ICONPROFILE.SCX',compiler_sha256=refresh.PUBLICATION_G1_SHA)
                    # 编译与其历史/页缓存不计入后续普通任务执行/清理账。
                    before,observed_before=harness.quiet();resources_before=harness.resources()
                    if icon_baseline:
                        from m10a2_icon_comparison import run as compare_icons
                        compare_icons(vm,guest,harness,report,icon_target,out)
                        before,observed_before=harness.quiet();resources_before=harness.resources()
                original_rows = sessions(harness.run('sessions-before-native-host', 'sessionctl list')['output'].encode())
                previous = next(row['id'] for row in original_rows if row['active'])
                if args.session_cycle:
                    harness.login_cooldown()
                    warm=harness.run('ordinary-hidden-session-before-desktop',
                        "printf '%s\\n' '' '"+target+" prepare; exit' | su mio",
                        contains=(b'SHELLPROBE PREPARED_ONLY',),forbidden=(b'SHELLPROBE FAIL',),timeout=90)
                    identity=re.search(r'SHELLPROBE PREPARED_ONLY sid=(\d+) pid=(\d+)/(\d+) epoch=(\d+)',warm['output'])
                    if not identity:raise ValueError('缺少已结束的真实隐藏host身份')
                    report['prior_hidden_session']=dict(zip(('session','pid','generation','epoch'),map(int,identity.groups())))
                    harness.run('logout-prior-hidden-desktop','sessionctl logout '+identity[1])
                    harness.quiet()
                harness.login_cooldown()
                marker = '__P5_RESULT_' + secrets.token_hex(8)
                inner = target + '; _p5=$?; echo ' + marker + ':$_p5; exit $_p5'
                if args.profile_icons and not icon_baseline:inner=icon_target+'; '+inner
                if args.inspect_layout:
                    from m10a2_shell_inspect import inspect as inspect_host
                    layout=refresh.read_json(args.inspect_layout)
                    for name,value in layout['headers'].items():
                        if sha(stage/name)!=value:raise ValueError('只读观察布局不是当前候选头')
                    map_path=native_path(generated['native_artifacts']['SCSHELL.SCX']['path']).parent.parent/'file-actual-G1-DESKTOP-map'
                    watch=dict(start=len(guest.console),key=None,tick=None,done=False)

                    def observe_prepared():
                        if watch['done']:return
                        found=list(re.finditer(rb'SHELLPROBE HOST state=(\d+) sid=(\d+) pid=(\d+)/(\d+) epoch=(\d+)',
                            bytes(guest.console[watch['start']:])))
                        if not found:return
                        last=found[-1]
                        if int(last[1])==3:
                            watch['done']=True;report['prepared_inspection_skipped']='ALREADY_ACTIVE';return
                        if int(last[1])!=2:return
                        key=(int(last[3]),int(last[4]))
                        tick=physical_word(vm,symbols['sc_ticks'])
                        if watch['key']!=key:
                            watch.update(key=key,tick=tick);return
                        if (tick-watch['tick'])&0xffffffff<150:return
                        watch['done']=True
                        report['prepared_readonly_snapshot']=inspect_host(vm,symbols,layout,map_path,*key)
                        persist()

                    guest.prepared_observer=observe_prepared
                expected = (b'FILEJOB PASS typed_query_copy_move_and_actual_binary_content' if args.file_jobs
                    else b'SHELLPROBE PASS ordinary_native_host_and_real_catalog_IPC')
                if args.file_clipboard:expected = b'FILECLIP PASS cross_process_copy_cut_paste_and_publisher_exit'
                item = harness.run('ordinary-native-file-jobs' if args.file_jobs else 'ordinary-native-host-and-real-catalog',
                    "printf '%s\\n' '' '" + inner + "' | su mio",
                    contains=((b'ICONPROFILE PASS actual_native_cold_icon_load',) if args.profile_icons and not icon_baseline else ())+(expected, (marker + ':0').encode()),
                    forbidden=(b'FILEJOB FAIL', b'SHELLPROBE FAIL', b'ICONPROFILE FAIL', b'[AUTH] login failed'), timeout=150 if args.file_jobs else 90)
                if args.profile_icons and not icon_baseline:
                    measured=re.search(r'ICONPROFILE RESULT icons=(\d+) ticks=(\d+) hz=(\d+) pixels_bytes=(\d+) entries=(\d+) misses=(\d+) hits=(\d+)',item['output'])
                    if not measured:raise ValueError('缺少实际冷图标分项记录')
                    report['icon_load_profile']=dict(zip(('icons','ticks','hz','pixels_bytes','entries','misses','hits'),map(int,measured.groups())))
                    report['icon_load_profile'].update(scope='ORDINARY_NO_WINDOW_COLD_RESOURCE_CACHE_NOT_WHOLE_DESKTOP',
                        compiled_caller_sha256=sha(out/'native-artifacts/ICONPROFILE.SCX'))
                if args.file_jobs:
                    sid = re.search(r'FILEJOB ID uid=1000 sid=(\d+) generation=(\d+)', item['output'])
                    if not sid:raise ValueError('缺少普通用户文件事务身份')
                    report['ordinary_file_job_identity'] = dict(uid=1000, session=int(sid[1]), generation=int(sid[2]))
                    files = (('OP-SOURCE.BIN','CLIPDEST/OP-SOURCE.BIN','CLIPDEST/OP-MOVE.BIN','CP.LOG','MV.LOG')
                        if args.file_clipboard else ('OP-SOURCE.BIN','OP-MOVE.BIN','CP.LOG','MV.LOG'))
                    for name in files:
                        actual = out / name
                        actual.parent.mkdir(parents=True,exist_ok=True)
                        guest.client.get(package + '/' + name, actual)
                        matches = (actual.read_bytes() == extra[package[1:] + '/OP-SOURCE.BIN']) if name.endswith('.BIN') else None
                        report.setdefault('actual_file_results',[]).append(dict(name=name,bytes=actual.stat().st_size,
                            sha256=sha(actual),binary_content_matches=matches))
                        if matches is False:raise ValueError('独立取回的二进制内容不同：'+name)
                    harness.screen('p5-real-native-file-jobs-complete')
                else:
                    sid = re.search(r'SHELLPROBE HOST state=3 sid=(\d+) pid=(\d+)/(\d+) epoch=(\d+)', item['output'])
                if not sid:
                    raise ValueError('缺少公开ACTIVE host身份')
                if not args.file_jobs:
                    report['actual_host_identity'] = dict(session=int(sid[1]), pid=int(sid[2]), generation=int(sid[3]), epoch=int(sid[4]))
                    harness.run('show-actual-native-desktop', 'sessionctl show ' + sid[1])
                    if args.desktop_layout:
                        from m10a2_shell_desktop_flow import run as desktop_flow
                        guest.prepared_observer=None
                        desktop_flow(vm,guest,symbols,harness,report,refresh.read_json(args.inspect_layout),
                            native_path(host['path']).parent.parent/'file-actual-G1-DESKTOP-map',
                            report['actual_host_identity'],out,resumed)
                    else:
                        harness.screen('p5-first-actual-native-desktop')
                        inputs = ScuiInput(vm, guest, symbols, report['physical_input'].append)
                        # 自绘Start的真实本地范围为x<88*scale，窗口贴屏底。
                        # 点击其内部；不把没有实现的Win键当作菜单已打开证据。
                        inputs.click(24, physical_word(vm, symbols['gfx_height']) - 16)
                        guest.stop.wait(.4)
                        harness.screen('p5-first-actual-native-start-menu')
                        vm.hmp('sendkey esc')
                        guest.stop.wait(.2)
                        harness.screen('p5-first-actual-native-desktop-ready')
                harness.run('restore-original-desktop', 'sessionctl show ' + str(previous))
                harness.run('logout-native-desktop-session', 'sessionctl logout ' + sid[1])
                after, observed_after = harness.quiet()
                ledger, okay = harness.ledger(before, after, resources_before, harness.resources(), True)
                harness.append(dict(name='native-host-client-session-final-owner-page-account', **ledger,
                    before_observations=observed_before, after_observations=observed_after), okay)
                report['status'] = ('P5_REAL_CROSS_PROCESS_FILE_CLIPBOARD_PASS' if args.file_clipboard
                    else 'P5_TYPED_PATH_AND_REAL_FILE_JOBS_PASS' if args.file_jobs else 'P5_FIRST_NATIVE_HOST_CLIENT_PASS')
                if args.desktop_layout:
                    report['status']='P5_NATIVE_DESKTOP_COLD_LAYOUT_PASS' if resumed else 'P5_NATIVE_DESKTOP_LAYOUT_PASS'
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(message=str(error), traceback=traceback.format_exc()))
                report['failure_console_tail'] = bytes(guest.console[-16000:]).decode('utf-8', 'replace')
                report['failure_debug_tail'] = bytes(guest.debug[-16000:]).decode('utf-8', 'replace')
                if args.file_jobs:
                    try:
                        trace = out / 'WORK.TRACE'
                        guest.client.get('/HOME/M10SHELL/WORK.TRACE',trace)
                        report['actual_worker_trace'] = dict(path=str(trace),sha256=sha(trace),
                            text=trace.read_text(encoding='utf-8',errors='replace'))
                    except Exception as trace_error:
                        report['worker_trace_capture_error'] = repr(trace_error)
                try:
                    log = out / 'native-desktop-startup.log'
                    guest.client.get('/HOME/.DESKTOP/SHELL.LOG', log)
                    report['actual_startup_log'] = dict(path=str(log), sha256=sha(log),
                        bytes=log.stat().st_size, text=log.read_text(encoding='utf-8', errors='replace'))
                except Exception as log_error:
                    report['startup_log_capture_error'] = repr(log_error)
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
    report['final_test_data_sha256']=sha(out/'sanddata.img');persist()
    print(report['status'], flush=True)
    if report['status'] not in ('P5_FIRST_NATIVE_HOST_CLIENT_PASS','P5_TYPED_PATH_AND_REAL_FILE_JOBS_PASS',
            'P5_REAL_CROSS_PROCESS_FILE_CLIPBOARD_PASS','P5_NATIVE_DESKTOP_LAYOUT_PASS','P5_NATIVE_DESKTOP_COLD_LAYOUT_PASS'):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
