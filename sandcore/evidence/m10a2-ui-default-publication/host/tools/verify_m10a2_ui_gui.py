#!/usr/bin/env python3
"""第二轮Windows双盘：安装真实G1的SC32/SCUI，再走普通会话GUI工作流。

源码收齐后显式运行。本脚本不编译P3库、不装GCC库冒充原生、
不合成ScMessage、不写HMP内存；生成、实际执行和复用证据分别记账。
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import struct
import time
import traceback

import verify_m10a2_ui_platform as first
import verify_m10a2_stdlib as stdlib
from scserial import QemuSession, sha
from verify_m9 import Guest, process_cpu
from m10_guest_boot import physical_word, unique_symbols, wait_first_desktop
from scui_input import ScuiInput, parse_snapshot, screen_point
from scui_workflow import work
from scl_format import validate
from scd_format import validate_scx

ROOT, BUILD, PACKAGE = first.ROOT, first.BUILD, first.PACKAGE
P3 = ROOT / 'evidence/m10a2-stdlib'
REUSED_LIBRARIES = ('SCLDR.SCL', 'SCCLIB.SCL', 'SCLIB.SCL')
NEW_LIBRARIES = ('SC32API.SCL', 'SCUI.SCL')
DEFAULT_REPAIR_LIBRARIES = ('SC32API.SCL', 'SCCLIB.SCL', 'SCUI.SCL')
HELPERS = ('tools/verify_m10a2_ui_platform.py', 'tools/verify_m10a2_stdlib.py',
           'tools/verify_m10a2_scl.py', 'tools/verify_m9.py', 'tools/scserial.py',
           'tools/m10_guest_boot.py', 'tools/scui_input.py', 'tools/scui_workflow.py',
           'tools/scl_format.py', 'tools/scd_format.py', 'tools/mkcore.py')
SAVED = PACKAGE + '/GUI.SAVED.TXT'


def digest(body):
    return hashlib.sha256(body).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def pin_path(path, directory, expected, size=None):
    path = first.inside(path, directory)
    if not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise ValueError('来源记录缺少精确SHA256：' + str(path))
    if sha(path) != expected or (size is not None and path.stat().st_size != size):
        raise ValueError('当前文件与来源字节不同：' + str(path))
    return path


def validate_refresh_origin(report, path, build_report, proof_hashes, historical_build=None):
    """只放行本次单ui_data修复；旧生成与当前准备snapshot分开核。"""
    reuse = report.get('source_reuse')
    if reuse is None:
        return None
    import refresh_m10a2_ui_native as refresh
    if reuse.get('schema') != 1 or reuse.get('kind') != 'ONE_SCUI_SOURCE_REFRESH':
        raise ValueError('不支持未审定的原生复用范围')
    origin_path = pin_path(Path(reuse['previous_report']), ROOT, reuse['previous_report_sha256'])
    previous, _, old_groups, old_cases, old_sha = refresh.previous_round(
        origin_path, report['source_data_sha256'], {})
    if historical_build is None:
        proof, hashes, _ = first.verified_build(build_report)
    else:
        # 仅已审定的Core路由切片可以引用历史构建；当前运行Core另行完整核验。
        proof, hashes = historical_build
    if hashes != proof_hashes:
        raise ValueError('刷新读取的当前构建与GUI固定输入不同')
    delta = refresh.validate_build_delta(previous, old_groups, proof)
    _, _, closures = refresh.obtain_artifacts(previous, origin_path, old_cases,
        report['input_sha256'], {})
    if (reuse.get('previous_build_report') != previous['build_report']
            or reuse.get('previous_build_report_sha256') != previous['build_report_sha256']
            or reuse.get('previous_status') != previous['status']
            or reuse.get('previous_source_input_sha256') != refresh.source_snapshot(previous['input_sha256'])
            or reuse.get('current_source_input_sha256') != refresh.source_snapshot(report['input_sha256'])
            or reuse.get('build_delta') != delta
            or reuse.get('unchanged_artifact_compile_closures') != closures
            or reuse.get('fresh_scui_compile_closure') != refresh.include_closure(['user/scui/SCUI.C', 'user/scui/SCUI.json'])):
        raise ValueError('刷新origin/单项差异/实际编译闭包与真实文件不同')
    runtime = dict(core_sha256=previous['core_sha256'], installed_libraries=previous['installed_libraries'],
        scope='OLD_FIRST_VM_EXECUTION_LIBRARIES_NOT_REFRESHED_SCUI')
    old_callers, callers = previous['caller_executions'], report.get('caller_executions', [])
    if len(callers) != len(old_callers):
        raise ValueError('刷新改变了已复用caller执行范围')
    for old, item in zip(old_callers, callers):
        if (any(item.get(key) != value for key, value in old.items())
                or item.get('execution_class') != 'REUSED' or item.get('executed_this_vm') is not False
                or item.get('source_report_sha256') != old_sha
                or item.get('runtime_dependencies_origin') != runtime):
            raise ValueError('旧caller的运行库/实际来源被改写为当前执行')
    old_aggregate, aggregate = previous['aggregate_abi'], report.get('aggregate_abi', {})
    if any(aggregate.get(key) != value for key, value in old_aggregate.items() if key != 'executions'):
        raise ValueError('复用aggregate artifact或原始要求被改写')
    old_executions, executions = old_aggregate['executions'], aggregate.get('executions', [])
    if len(executions) != len(old_executions):
        raise ValueError('刷新改变aggregate实际执行方向')
    for old, item in zip(old_executions, executions):
        if (any(item.get(key) != value for key, value in old.items())
                or item.get('execution_class') != 'REUSED' or item.get('executed_in_this_vm') is not False
                or item.get('source_report_sha256') != old_sha):
            raise ValueError('旧aggregate运行被混记为本VM执行')
    for name, item in dict(report['native_artifacts'], **report['native_libraries']).items():
        generation = item.get('generation_origin', {})
        if name == 'SCUI.SCL':
            if (item.get('execution_class') != 'FRESH' or item.get('generated_this_vm') is not True
                    or generation.get('mode') != 'FRESH' or generation.get('report') != str(path)
                    or generation.get('qemu_pid') != report.get('qemu_pid')
                    or generation.get('source_input_sha256') != refresh.source_snapshot(report['input_sha256'])
                    or generation.get('include_manifest_closure') != reuse['fresh_scui_compile_closure']):
                raise ValueError('新SCUI缺本VM实际生成来源')
        else:
            old = previous['native_libraries' if name.endswith('.SCL') else 'native_artifacts'][name]
            expected = refresh.generation_origin(old, origin_path, old_sha,
                previous['input_sha256'], closures[name])
            if (item.get('execution_class') != 'REUSED' or item.get('generated_this_vm') is not False
                    or item.get('origin_report_sha256') != old_sha or generation != expected
                    or item.get('sha256') != old['sha256'] or item.get('bytes') != old['bytes']):
                raise ValueError('复用产物缺原实际生成byte/闭包：' + name)
    return dict(report=previous, path=origin_path, sha256=old_sha, cases=old_cases)


def native_first_round(path, build_report, proof_hashes, historical_build=None):
    path = path.resolve(strict=True)
    directory = path.parent
    report = read_json(path)
    if report.get('status') != 'DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS':
        raise ValueError('必须使用完整已通过首轮，不接受失败或仅生成记录')
    if report.get('scope') != 'M10A2_UI_PLATFORM_FIRST_WINDOWS_GUEST_ONLY':
        raise ValueError('首轮不是声明的平台/文本/G1范围')
    repair_flag = report.get('compiler_default_repair')
    if repair_flag is not None and type(repair_flag) is not bool:
        raise ValueError('Compiler修复模式必须是明确布尔值')
    repaired = repair_flag is True
    if report.get('source_reuse', {}).get('kind') == 'POST_ACTIVATE_PUBLICATION_REPAIR':
        if not repaired or historical_build is not None:
            raise ValueError('发布修复必须保本次正确G1资格且不混Core路由切片')
        import refresh_m10a2_ui_native as refresh
        # 同一严格producer/consumer合同验证新SCUI与原G1、STD、SC32、
        # 四dynamic App和legacy机器回归；旧执行仍带旧库来源。
        return refresh.validated_refresh(path, build_report, proof_hashes)
    if repaired and (historical_build is not None or report.get('source_reuse') is not None):
        raise ValueError('default修复必须为新G1实际首轮，不能借旧Core或刷新复用链')
    expected_libraries = DEFAULT_REPAIR_LIBRARIES if repaired else NEW_LIBRARIES
    if set(report.get('native_libraries', {})) != set(expected_libraries):
        raise ValueError('实际首轮生成库集合与Compiler模式不同')
    if (report.get('current_inputs_unchanged') is not True
            or report.get('source_disk_unchanged') is not True
            or report.get('changed_input_output_paths') != []):
        raise ValueError('首轮输入/原盘保持条件没有成立')
    if report.get('build_report_sha256') != sha(build_report):
        raise ValueError('首轮不是指定统一构建的输入与CORE')
    if report.get('pinned_build_input_output_sha256') != proof_hashes:
        raise ValueError('首轮固定的input/output清单不是当前指定构建')
    refresh_origin = validate_refresh_origin(report, path, build_report, proof_hashes, historical_build)
    if report.get('generated_system_libraries_execution') is not False:
        raise ValueError('第一轮生成系统库执行边界记录不明确')
    aggregate = report.get('aggregate_abi', {})
    executions = aggregate.get('executions', [])
    if ({item.get('name') for item in executions} != {
            'G1-aggregate-caller-GCC-callee', 'GCC-aggregate-caller-G1-callee'}
            or len(executions) != 2
            or any(item.get('executed') is not True or item.get('check_count') != 55
                   or item.get('ordinary_uid') != 1000
                   or item.get('actual_caller_stack_balance_checked') is not True for item in executions)
            or report.get('generated_library_execution') is not True):
        raise ValueError('缺少实际两个方向aggregate ABI执行/精确栈平衡')
    inputs = report.get('input_sha256', {})
    if not inputs:
        raise ValueError('首轮缺完整源输入摘要')
    for name, expected in inputs.items():
        pin_path(ROOT / name, ROOT, expected)
    # first的平台helper在本次会话只借配置，不改任何旧脚本正文。
    for name in HELPERS[:6]:
        if inputs.get(name) != sha(ROOT / name):
            raise ValueError('首轮helper已经改变：' + name)
    cases = {item['name']: item for item in report['cases']}
    if len(cases) != len(report['cases']):
        raise ValueError('首轮用例名称重复，不能确定真正编译来源')

    def valid_compile(case):
        if not case:
            return False
        if case.get('status') == 'PASS':
            return not refresh_origin or case.get('execution_class') == 'FRESH'
        if not refresh_origin or case.get('status') != 'REUSED_PASS':
            return False
        original = refresh_origin['cases'].get(case.get('source_case'))
        if (not original or original.get('status') != 'PASS'
                or case.get('original_status') != 'PASS' or case.get('execution_class') != 'REUSED'
                or case.get('executed_in_this_vm') is not False
                or case.get('source_report_sha256') != refresh_origin['sha256']):
            return False
        return all(case.get(key) == original.get(key) for key in
            ('command', 'exit_code', 'generated_sha256', 'generated_bytes', 'compiler_sha256', 'guest_sccc_compiled'))
    g1 = report['native_artifacts']['SCCC1.SCX']
    g1_path = pin_path(Path(g1['path']), directory, g1['sha256'], g1['bytes'])
    raw = g1_path.read_bytes()
    if len(raw) < 36 or raw[:8] != b'SCX1MIO\0':
        raise ValueError('G1不是原SCX容器')
    entry, load, bss, stack, flags, base, author = struct.unpack_from('<7I', raw, 8)
    if (entry >= load or len(raw) != 36 + load or not 0 < load <= 262108
            or load + bss > 0x3d0000 or stack != 131072 or flags
            or base != 0x400000 or author != 0x004f494d
            or g1.get('guest_sccc_compiled') is not True
            or g1.get('executed_this_generated_file') is not True):
        raise ValueError('实际执行过的G1容器/来源合同不成立')
    g1_case = cases.get(g1.get('compile_case'))
    g1_contract = dict(load_bytes=load, bss_bytes=bss, stack_bytes=stack, entry_rva=entry,
        load_budget=262108, mapped_budget=0x3d0000, exact_file_bytes=len(raw),
        old_container_not_widened=True)
    if (not valid_compile(g1_case) or g1_case.get('exit_code') != 0
            or g1_case.get('generated_sha256') != g1['sha256']
            or g1_case.get('generated_bytes') != len(raw)
            or g1.get('format_contract') != g1_contract):
        raise ValueError('G1缺本次源码真实自举输出的完整记录')
    if repaired:
        machine = report['native_artifacts'].get('SWDEFAULT.SCX', {})
        actual = pin_path(Path(machine['path']), directory, machine['sha256'], machine['bytes'])
        body = actual.read_bytes()
        if len(body) < 36 or body[:8] != b'SCX1MIO\0':
            raise ValueError('default机器回归不是原legacy SCX')
        entry, load, bss, stack, flags, base, author = struct.unpack_from('<7I', body, 8)
        compile_case = cases.get(machine.get('compile_case'), {})
        executed = cases.get('actual-G1-default-switch-machine-flow-actual-mio', {})
        reclaimed = cases.get('actual-G1-default-switch-machine-flow-actual-mio-actual-owner-page-account', {})
        if (entry >= load or len(body) != load + 36 or not 0 < load <= 262108
                or load + bss > 0x3d0000 or stack != 131072 or flags
                or base != 0x400000 or author != 0x004f494d
                or machine.get('purpose') != 'SIX_DEFAULT_EXPLICIT_NESTED_SWITCH_MACHINE_MAIN_CHECKS'
                or machine.get('compiler_sha256') != g1['sha256']
                or machine.get('guest_sccc_compiled') is not True
                or machine.get('executed_this_generated_file') is not True
                or not valid_compile(compile_case) or compile_case.get('exit_code') != 0
                or compile_case.get('compiler_sha256') != g1['sha256']
                or compile_case.get('generated_sha256') != machine['sha256']
                or executed.get('status') != 'PASS' or executed.get('ordinary_exit_code') != 0
                or executed.get('complete_owner_and_gc_observed') is not True
                or reclaimed.get('status') != 'PASS' or reclaimed.get('complete_owner_and_gc_observed') is not True):
            raise ValueError('缺真正新G1的六项default机器main/普通退出与回收')

    def obtain(name, library):
        item = report['native_libraries' if library else 'native_artifacts'][name]
        actual = pin_path(Path(item['path']), directory, item['sha256'], item['bytes'])
        case = cases.get(item.get('compile_case'))
        if (not valid_compile(case) or case.get('exit_code') != 0
                or case.get('generated_sha256') != item['sha256']
                or case.get('guest_sccc_compiled') is not True
                or case.get('compiler_sha256') != g1['sha256']
                or item.get('guest_sccc_compiled') is not True
                or item.get('compiler_sha256') != g1['sha256']):
            raise ValueError('缺少同一真实G1成功编译记录：' + name)
        if library:
            manifest = read_json(ROOT / 'user' / first.LIBRARIES[name][1])
            contract = stdlib.source_contract(validate(actual.read_bytes()), manifest)
            if item.get('executed_this_generated_file') is not False or item.get('installed_in_this_vm') is not False:
                raise ValueError('首轮生成库被混记为已安装/执行：' + name)
        else:
            manifest = read_json(ROOT / 'tests/m10a2' / (name[:-4] + '.json'))
            contract = stdlib.source_contract(validate_scx(actual.read_bytes()), manifest, True)
            if item.get('executed_this_generated_file') is not (name in ('PLATFORMPROBE.SCX', 'SCUITEXT.SCX')):
                raise ValueError('首轮原生App的生成/执行边界不同：' + name)
        if item.get('format_contract') != contract:
            raise ValueError('首轮记录与当前manifest完整合同不同：' + name)
        return actual.read_bytes(), dict(item, path=str(actual), source='ACTUAL_FIRST_ROUND_G1',
            first_round_report=str(path), first_round_report_sha256=sha(path),
            generated_this_vm=False, installed_this_vm=False, executed_this_vm=False,
            current_contract=contract)

    libraries, apps, metadata = {}, {}, {}
    for name in expected_libraries:
        libraries[name], metadata[name] = obtain(name, True)
    for name in first.APPLICATIONS:
        apps[name], metadata[name + '.SCX'] = obtain(name + '.SCX', False)
    if repaired:
        apps['DEFAULTAPI'], metadata['DEFAULTAPI.SCX'] = obtain('DEFAULTAPI.SCX', False)
        if report['native_artifacts']['DEFAULTAPI.SCX'].get('intended_next_round_execution') != 'ACTUAL_NEW_SC32_SCCLIB_UNKNOWN_ERROR_DEFAULT_BRANCHES':
            raise ValueError('DEFAULTAPI首轮不是仅生成/下轮真实新库执行的明确范围')
    # 两个来源目录都必须与实际报告相互绑定，不能捡同名文件。
    for folder in ('native-libraries', 'native-artifacts'):
        provenance_path = first.inside(directory / folder / 'provenance.json', directory)
        provenance = read_json(provenance_path)
        origin = first.inside(provenance_path.parent / provenance['report'], directory)
        if (origin != path or provenance.get('schema') != 1
                or provenance.get('guest_sccc_compiled') is not True
                or provenance.get('compiler_sha256') != g1['sha256']
                or provenance.get('current_status') != report['status']
                or provenance.get('generated_library_executed') is not False
                or provenance.get('libraries') != report['native_libraries']
                or provenance.get('applications') != report['native_artifacts']):
            raise ValueError('首轮get回字节的provenance不匹配：' + folder)
        expected_sources = {name: value for name, value in inputs.items()
                            if name.startswith(('user/', 'tests/m10a2/'))}
        if provenance.get('source_input_sha256') != expected_sources:
            raise ValueError('首轮provenance源摘要遗漏或不同：' + folder)
        if refresh_origin:
            expected_generation = {name: item['generation_origin'] for name, item in
                dict(report['native_artifacts'], **report['native_libraries']).items()}
            if (provenance.get('source_snapshot_scope') != 'CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN'
                    or provenance.get('source_reuse') != report['source_reuse']
                    or provenance.get('artifact_generation') != expected_generation):
                raise ValueError('刷新provenance混淆当前准备与旧生成来源：' + folder)
    return report, libraries, apps, metadata


def reused_p3(proof_hashes, compiler_repaired=False):
    directory = first.inside(P3 / 'native-libraries', P3)
    provenance_path = first.inside(directory / 'provenance.json', P3)
    provenance = read_json(provenance_path)
    origin_path = first.inside(directory / provenance['report'], P3)
    executed_path = first.inside(P3 / 'native.json', P3)
    origin, executed = read_json(origin_path), read_json(executed_path)
    if (provenance.get('schema') != 1 or provenance.get('guest_sccc_compiled') is not True
            or origin.get('status') != 'DECLARED_HOST_AND_NATIVE_CALLER_STDLIB_CASES_PASS'
            or executed.get('status') != 'DECLARED_NATIVE_LIBRARY_STDLIB_CASES_PASS'
            or executed.get('current_inputs_unchanged') is not True
            or executed.get('source_disk_unchanged') is not True
            or origin.get('current_inputs_unchanged') is not True
            or origin.get('source_disk_unchanged') is not True):
        raise ValueError('P3真实生成/另一VM执行的证据不完整')
    execution = executed.get('installed_library_execution', {})
    reused = executed.get('native_install_provenance', {})
    if (execution.get('complete_declared_modes') is not True
            or reused.get('provenance_sha256') != sha(provenance_path)
            or reused.get('report_sha256') != sha(origin_path)):
        raise ValueError('P3执行记录没有绑定这份原生生成来源')
    folders = ('runtime', 'sclib') if compiler_repaired else ('runtime', 'stdlib', 'sclib')
    selected = {name: value for name, value in provenance['source_input_sha256'].items()
                if name.startswith(tuple('user/' + folder + '/' for folder in folders))
                or name in ('user/SCTYPES.H', 'user/SC32GUI.H')}
    current = set()
    for folder in folders:
        current.update(p.relative_to(ROOT).as_posix() for p in (ROOT / 'user' / folder).rglob('*')
            if p.is_file() and p.suffix.lower() in ('.c', '.h', '.inc', '.asm', '.json'))
    current.update(('user/SCTYPES.H', 'user/SC32GUI.H'))
    if set(selected) != current:
        raise ValueError('P3复用源码目录增加/缺失，不是同一完整实现')
    for name, expected in selected.items():
        pin_path(ROOT / name, ROOT, expected)
        if proof_hashes.get(name) != expected:
            raise ValueError('P3复用源不在当前统一构建固定输入中：' + name)
    cases = {item['name']: item for item in origin['cases']}
    libraries, metadata = {}, {}
    reused_names = ('SCLDR.SCL', 'SCLIB.SCL') if compiler_repaired else REUSED_LIBRARIES
    for name in reused_names:
        item = provenance['libraries'][name]
        path = pin_path(directory / item['path'], directory, item['sha256'], item['bytes'])
        case = cases.get(item['compile_case'])
        if (not case or case.get('status') != 'PASS' or case.get('exit_code') != 0
                or case.get('generated_sha256') != item['sha256']
                or item.get('guest_sccc_compiled') is not True):
            raise ValueError('P3库缺实际成功生成记录：' + name)
        old = executed['installed_libraries'][name]
        if (old.get('source') != 'GUEST_SCCC_PRIOR_VM' or old.get('sha256') != item['sha256']
                or execution.get('source', {}).get(name) != old):
            raise ValueError('P3安装并执行的不是该字节：' + name)
        libraries[name] = path.read_bytes()
        contract = stdlib.source_contract(validate(libraries[name]),
            read_json(ROOT / 'user' / first.LIBRARIES[name][1]))
        metadata[name] = dict(path=str(path), bytes=len(libraries[name]), sha256=sha(path),
            source='REUSED_P3_GUEST_SCCC_NATIVE_ALREADY_EXECUTED',
            provenance_sha256=sha(provenance_path), generation_report=str(origin_path),
            generation_report_sha256=sha(origin_path), execution_report=str(executed_path),
            execution_report_sha256=sha(executed_path), format_contract=contract,
            generated_this_vm=False, installed_this_vm=False, executed_this_vm=False)
    return libraries, metadata, selected


def sessions(blob):
    found = []
    for match in re.finditer(rb'^([0-9]+)\s+(-?[0-9]+)\s+(-?[0-9]+)\s+(SYSTEM|LOCKED|user)\s+(yes|no)\s+([0-9]+)\s+(.*)$', blob, re.M):
        found.append(dict(id=int(match[1]), uid=int(match[2]), gid=int(match[3]),
            type=match[4].decode(), active=match[5] == b'yes', tasks=int(match[6]),
            name=match[7].strip().decode('utf-8', 'strict')))
    if not found or len({item['id'] for item in found}) != len(found):
        raise ValueError('实际SESSIONPAGE输出缺失或重复ID')
    return found


def counter(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]+:[0-9]+', value):
        raise ValueError('应用双字计数不是完整hi:lo')
    high, low = map(int, value.split(':'))
    if high > 0xffffffff or low > 0xffffffff:
        raise ValueError('应用双字计数越界')
    return (high << 32) | low


class GuiHarness(first.PlatformHarness):
    def ledger(self, before, after, resources_before, resources_after, cold_font):
        ledger, okay = super().ledger(before, after, resources_before, resources_after, cold_font)
        old = resources_before['finite_wait_heap']
        new = resources_after['finite_wait_heap']
        # task.c::wait_reserve初始32个pid只占一页；wait_remove刻意保留
        # 这一个空容量。复用PLATFORM后，首次有限等待可在TEXT/GUI发生，
        # 它与字体是否冷态没有因果关系，必须按真实地址/PF单独归属。
        first_page = (old['count'] == old['capacity'] == old['pages'] == old['heap'] == 0
            and new['count'] == 0 and new['capacity'] == 32 and new['pages'] == 1
            and new['heap'] > 0 and new['heap'] % 4096 == 0
            and new['heap_aligned_managed'] and new['heap_page_allocated']
            and resources_before.get('wait_pages') == 0 and resources_after.get('wait_pages') == 1)
        if first_page and ledger['actual_first_finite_wait_cache_delta'] == 0:
            specific = {'finite wait heap changed outside measured first-page cache',
                        'persistent scalar changed: wait_pages'}
            ledger['unexpected'] = [item for item in ledger['unexpected'] if item not in specific]
            ledger['actual_first_finite_wait_cache_delta'] = 1
            ledger['attributed_physical_delta'] += 1
            ledger['unexplained_delta'] = ledger['physical_delta'] - ledger['attributed_physical_delta']
            ledger['finite_wait_attribution_policy'] = 'ACTUAL_INITIAL_32_EMPTY_ONE_PAGE_INDEPENDENT_OF_FONT'
            okay = not ledger['unexpected'] and ledger['unexplained_delta'] == 0
        return ledger, okay

    def input_record(self, item):
        self.report['physical_input'].append(item)
        self.persist()

    def snapshot(self, inputs, label):
        result = inputs.snapshot()
        result['label'] = label
        self.report['snapshots'].append(result)
        self.persist()
        return result

    def pit_window(self, label, ticks=200, timeout=25):
        begin, cpu = time.monotonic(), process_cpu(self.vm.process)
        start = physical_word(self.vm, self.symbols['sc_ticks'])
        observations, last = [], None
        while True:
            now = physical_word(self.vm, self.symbols['sc_ticks'])
            frames = physical_word(self.vm, self.symbols['wm_frames'])
            if (now, frames) != last:
                observations.append(dict(tick=now, frames=frames))
                last = (now, frames)
            if (now - start) & 0xffffffff >= ticks:
                break
            if self.guest.errors or self.vm.process.poll() is not None:
                raise RuntimeError('真实计量期间串口/QEMU退出')
            if time.monotonic() - begin > timeout:
                raise TimeoutError('PIT未在有界时间内推进')
            self.guest.stop.wait(.08)
        item = dict(name=label, actual_pit_ticks=(now - start) & 0xffffffff,
            wall_seconds=time.monotonic() - begin,
            qemu_one_core_cpu_seconds=process_cpu(self.vm.process) - cpu,
            first_compositor_frame=observations[0]['frames'], last_compositor_frame=frames,
            observations=observations, hundred_hz_not_fps=True)
        self.report['performance'].append(item)
        self.persist()
        return item


def named_record(snapshot, kind, role=None):
    records = [item for item in snapshot['records'] if item['kind'] == kind
               and (role is None or item.get('role') == role)]
    if len(records) != 1:
        raise ValueError('F12缺唯一正式查询记录：%s/%s' % (kind, role))
    return records[0]


def node_text(node):
    size, body = node.get('textBytes'), node.get('textHex')
    if not isinstance(size, int) or size < 0 or not isinstance(body, str):
        raise ValueError('F12缺完整公开名字/文字十六进制')
    if body == '-':
        body = ''
    if len(body) != size * 2 or not re.fullmatch(r'[0-9a-fA-F]*', body):
        raise ValueError('公开查询文字长度/十六进制不一致')
    return bytes.fromhex(body).decode('utf-8', 'strict')


def choose_node(snapshot, root, kind, labels=(), predicate=None):
    found = [item for item in snapshot['nodes'].values()
             if item['root'] == root and item.get('kind') == kind
             and (not labels or node_text(item) in labels)
             and (predicate is None or predicate(item))]
    if len(found) != 1:
        raise ValueError('实际节点不是唯一目标 root=%s kind=%s labels=%s' % (root, kind, labels))
    return found[0]


def point_for_node(snapshot, node):
    names = [role for role, value in snapshot['nodes'].items() if value is node]
    if len(names) != 1:
        raise ValueError('没有实际node角色可定位')
    return screen_point(snapshot, names[0])


def focus_main(inputs, snapshot):
    window = snapshot['windows']['main']
    border = (window['w'] - window['cw']) // 2
    title = window['h'] - window['ch'] - border
    if title < 2 or window['w'] < 80:
        raise ValueError('实际主窗标题几何不够安全聚焦')
    inputs.click(window['x'] + border + 60, window['y'] + max(2, title // 2))


def modal_windows(snapshot, owner):
    return [item for item in snapshot['windows'].values()
            if item['state'] & 16 and item['state'] & 1 and item['pid'] == owner]


def wait_snapshot(harness, inputs, label, predicate, timeout=20):
    begin, seen = time.monotonic(), []
    while time.monotonic() - begin < timeout:
        value = harness.snapshot(inputs, label)
        seen.append(value['count'])
        if predicate(value):
            return value
        harness.guest.stop.wait(.12)
    raise TimeoutError('真实F12条件未成立：%s snapshots=%s' % (label, seen))


def command_key(harness, inputs, snapshot, key, command, label):
    focus_main(inputs, snapshot)
    start = len(inputs.guest.console)
    inputs.key(key)
    match = inputs.guest.wait(rb'(?:\r?\n)SCUI COMMAND id=' + str(command).encode()
        + rb' result=(-?[0-9]+)\r?\n', start, 20)
    harness.append(dict(name=label + '-actual-command', command=command,
        input=key, result=int(match[1]), console_offset=start), int(match[1]) >= 0)
    return harness.snapshot(inputs, label)


def menu_keys(harness, inputs, snapshot, menu, steps, command, label):
    focus_main(inputs, snapshot)
    start = len(inputs.guest.console)
    inputs.key('alt-' + menu)
    inputs.key('home')
    for _ in range(steps):
        inputs.key('down')
    inputs.key('ret')
    match = inputs.guest.wait(rb'(?:\r?\n)SCUI COMMAND id=' + str(command).encode()
        + rb' result=(-?[0-9]+)\r?\n', start, 20)
    harness.append(dict(name=label + '-actual-menu-command', command=command,
        menu=menu, down_steps=steps, result=int(match[1])), int(match[1]) >= 0)
    return harness.snapshot(inputs, label)


def replace_text(inputs, snapshot, node, text):
    if any(ord(char) < 32 or ord(char) > 126 for char in text):
        raise ValueError('本次HMP文字只输入明确ASCII，UTF8来自真实fixture/应用')
    inputs.click(*point_for_node(snapshot, node))
    inputs.key('ctrl-a')
    inputs.key('backspace')
    mapping = {'/': 'slash', '.': 'dot', '-': 'minus', '_': 'shift-minus',
               ' ': 'spc', ':': 'shift-semicolon', '\\': 'backslash'}
    for char in text:
        inputs.key(mapping.get(char, 'shift-' + char.lower() if char.isupper() else char))


def dialog_snapshot(harness, inputs, label, pid, count=1):
    result = wait_snapshot(harness, inputs, label,
        lambda value: len(modal_windows(value, pid)) == count)
    for window in modal_windows(result, pid):
        if not any(node['root'] == window['root'] and 'kind' in node for node in result['nodes'].values()):
            raise ValueError('活modal缺真正publicNodeQuery子树')
    return result


def file_workflow(harness, inputs, initial, pid):
    # Ctrl+O/S是应用正式命令；所有内部field/button由F12活root查询确定。
    before = named_record(initial, 'DOCUMENT')
    value = command_key(harness, inputs, initial, 'ctrl-o', 101, 'file-open')
    value = dialog_snapshot(harness, inputs, 'file-open-modal', pid)
    harness.screen('scui-native-file-open')
    inputs.key('esc')
    value = wait_snapshot(harness, inputs, 'file-open-cancel', lambda s: not modal_windows(s, pid))
    harness.append(dict(name='file-cancel-preserves-document', before=before,
        after=named_record(value, 'DOCUMENT')), named_record(value, 'DOCUMENT')['bytes'] == before['bytes']
        and named_record(value, 'DOCUMENT')['revision'] == before['revision'])

    value = command_key(harness, inputs, value, 'ctrl-s', 102, 'file-save')
    value = dialog_snapshot(harness, inputs, 'file-save-modal', pid)
    window = modal_windows(value, pid)[0]
    path = choose_node(value, window['root'], 8, predicate=lambda n: node_text(n).startswith('/'))
    replace_text(inputs, value, path, PACKAGE)
    inputs.key('ret')
    value = harness.snapshot(inputs, 'file-save-directory')
    filename = choose_node(value, window['root'], 8, predicate=lambda n: not node_text(n).startswith('/'))
    replace_text(inputs, value, filename, 'GUI.SAVED.TXT')
    start = len(inputs.guest.console)
    okay = choose_node(value, window['root'], 3, labels=('确定',))
    inputs.click(*point_for_node(value, okay))
    # 预置空文件会真实触发内部overwrite确认，不以另一次Save冒充它。
    overwrite = dialog_snapshot(harness, inputs, 'file-overwrite-confirmation', pid, 2)
    new_roots = [w for w in modal_windows(overwrite, pid) if w['root'] != window['root']]
    if len(new_roots) != 1:
        raise ValueError('覆盖确认没有唯一新的实际modal root')
    harness.screen('scui-native-overwrite')
    confirm = choose_node(overwrite, new_roots[0]['root'], 3, labels=('覆盖', '确定', '是'))
    inputs.click(*point_for_node(overwrite, confirm))
    saved = inputs.guest.wait(rb'(?:\r?\n)SCUI SAVED bytes=([0-9]+) path=' + re.escape(SAVED.encode()) + rb'\r?\n', start, 30)
    value = wait_snapshot(harness, inputs, 'file-save-complete', lambda s: not modal_windows(s, pid))
    actual = harness.file(SAVED, 'native-GUI-saved-document')
    harness.append(dict(name='actual-native-stdio-save-and-savepoint', bytes=len(actual),
        declared_bytes=int(saved[1]), sha256=digest(actual), document=named_record(value, 'DOCUMENT')),
        len(actual) == int(saved[1]) == named_record(value, 'DOCUMENT')['bytes']
        and not named_record(value, 'DOCUMENT')['flags'] & 2)

    # 权限/错误路径留在同一FileDialog，不写系统配置也不把错误删掉。
    value = command_key(harness, inputs, value, 'ctrl-o', 101, 'file-error-dialog')
    value = dialog_snapshot(harness, inputs, 'file-error-modal', pid)
    root = modal_windows(value, pid)[0]['root']
    path = choose_node(value, root, 8, predicate=lambda n: node_text(n).startswith('/'))
    old_doc = named_record(value, 'DOCUMENT')
    # MOD对普通用户允许读；AUTH的真实目录mode才会拒绝本普通会话的读取。
    replace_text(inputs, value, path, '/SYS/AUTH')
    inputs.key('ret')
    error = wait_snapshot(harness, inputs, 'file-real-permission-error',
        lambda s: any(item['kind'] == 'DIALOG' and item.get('root') == root and item.get('error', 0) < 0 for item in s['records']))
    harness.screen('scui-native-file-error')
    harness.append(dict(name='file-error-keeps-modal-and-old-document', dialog=named_record(error, 'DIALOG'),
        before=old_doc, after=named_record(error, 'DOCUMENT')),
        any(w['root'] == root for w in modal_windows(error, pid))
        and named_record(error, 'DOCUMENT')['revision'] == old_doc['revision'])
    inputs.key('esc')
    return wait_snapshot(harness, inputs, 'file-error-cancel', lambda s: not modal_windows(s, pid))


def visual_workflow(harness, inputs, initial):
    value = initial
    # View首两个项为Classic/Aurora；DPI真实菜单值由冻结Demo同批提供。
    for style, steps, command, name in ((1, 0, 114, 'classic'), (0, 1, 115, 'aurora')):
        value = menu_keys(harness, inputs, value, 'v', steps, command, name)
        theme = named_record(value, 'THEME')
        harness.append(dict(name=name + '-actual-theme', query=theme), theme['style'] == style)
        value = command_key(harness, inputs, value, 'f11', 126, name + '-resources')
        harness.screen('scui-native-' + name + '-resources')
    for percent, command in ((100, 128), (150, 129), (200, 130)):
        # Freeze协议要求实际View命令，不由管理者直接改内核字段。
        value = menu_keys(harness, inputs, value, 'v', 9 + (percent - 100) // 50,
            command, 'dpi-' + str(percent))
        metrics = named_record(value, 'METRICS', 'main')
        scale, pixels, face = metrics['scalePercent'], metrics['fontPixels'], metrics['fontFace']
        harness.append(dict(name='native-DPI-%d-font-and-layout' % percent, metrics=metrics,
            windows=value['windows'], nodes=value['nodes']), scale == percent and pixels == 16 * percent // 100
            and face == 16 and value['nodes']['menu']['h'] >= 24 * percent // 100)
        harness.screen('scui-native-%d-percent' % percent)
    value = menu_keys(harness, inputs, value, 'v', 9, 128, 'dpi-restore-100')
    metrics = named_record(value, 'METRICS', 'main')
    harness.append(dict(name='DPI-restored-before-geometry-and-reclaim', metrics=metrics),
        metrics['scalePercent'] == 100 and metrics['fontPixels'] == 16)
    return value


def property_workflow(harness, inputs, initial, pid):
    value = menu_keys(harness, inputs, initial, 't', 0, 109, 'property-apply')
    value = dialog_snapshot(harness, inputs, 'property-apply-modal', pid)
    root = modal_windows(value, pid)[0]['root']
    field = choose_node(value, root, 8,
        predicate=lambda n: (n['state'] & 3) == 3 and not n['state'] & 256)
    apply = choose_node(value, root, 3, labels=('应用',))
    replace_text(inputs, value, field, '')
    inputs.click(*point_for_node(value, apply))
    rejected = wait_snapshot(harness, inputs, 'property-required-field-rejected',
        lambda s: named_record(s, 'DIALOG')['error'] < 0)
    current = next(n for n in rejected['nodes'].values() if n['id'] == field['id'])
    harness.append(dict(name='property-validation-keeps-modal-and-invalid-field',
        dialog=named_record(rejected, 'DIALOG'), node=current),
        current['state'] & 512 and any(w['root'] == root for w in modal_windows(rejected, pid)))
    harness.screen('scui-native-property-validation')
    replace_text(inputs, rejected, current, 'SandCore verified')
    start = len(inputs.guest.console)
    apply = choose_node(rejected, root, 3, labels=('应用',))
    inputs.click(*point_for_node(rejected, apply))
    inputs.guest.wait(rb'(?:\r?\n)SCUI APPLY accepted=1\r?\n', start, 20)
    accepted = wait_snapshot(harness, inputs, 'property-actual-apply',
        lambda s: named_record(s, 'DIALOG')['error'] == 0)
    current = next(n for n in accepted['nodes'].values() if n['id'] == field['id'])
    harness.append(dict(name='property-typed-validate-then-apply',
        dialog=named_record(accepted, 'DIALOG'), node=current,
        console=bytes(inputs.guest.console[start:]).decode('utf-8', 'replace')),
        not current['state'] & 512 and b'SCUI VALIDATE name_bytes=17' in bytes(inputs.guest.console[start:])
        and any(w['root'] == root for w in modal_windows(accepted, pid)))
    harness.screen('scui-native-property-applied')
    inputs.key('esc')
    return wait_snapshot(harness, inputs, 'property-applied-cancel', lambda s: not modal_windows(s, pid))


def geometry_workflow(harness, inputs, initial):
    focus_main(inputs, initial)
    value = harness.snapshot(inputs, 'geometry-before')
    old = value['windows']['main']
    old_context = named_record(value, 'CONTEXT')
    screen = named_record(value, 'DISPLAY')
    if old['x'] + old['w'] + 64 >= screen['w'] or old['y'] + old['h'] + 48 >= screen['h']:
        raise ValueError('当前真实窗口/屏幕没有定向尺寸增长所需空间')
    sx, sy = old['x'] + old['w'] - 3, old['y'] + old['h'] - 3
    border = (old['w'] - old['cw']) // 2
    inputs.move(sx, sy)
    inputs.button('left', True)
    inputs.move(sx + 64 + 1 - border, sy + 48)
    inputs.button('left', False)
    grown = wait_snapshot(harness, inputs, 'geometry-grown',
        lambda s: s['windows']['main']['cw'] > old['cw'] and s['windows']['main']['ch'] > old['ch']
        and s['nodes']['menu']['w'] > value['nodes']['menu']['w']
        and named_record(s, 'CONTEXT')['surface'] - old_context['surface']
        == (s['windows']['main']['cw'] * s['windows']['main']['ch'] - old['cw'] * old['ch']) * 4)
    new = grown['windows']['main']
    current = named_record(grown, 'CONTEXT')
    expected_surface_delta = (new['cw'] * new['ch'] - old['cw'] * old['ch']) * 4
    harness.append(dict(name='real-resize-replaces-surface-and-layout', before=old, after=new,
        context_before=old_context, context_after=current, expected_surface_delta=expected_surface_delta),
        (new['window'], new['generation'], new['pidgen']) == (old['window'], old['generation'], old['pidgen'])
        and new['geometry'] != old['geometry']
        and current['surface'] - old_context['surface'] == expected_surface_delta
        and grown['nodes']['menu']['w'] > value['nodes']['menu']['w'])
    harness.screen('scui-native-resized')
    # 原尺寸来自公开WININFO；真实抓取偏移按已固定wm_native正文计，
    # 不调用Place、不写kernel字段、不用假SIZE消息绕过输入。
    sx, sy = new['x'] + new['w'] - 3, new['y'] + new['h'] - 3
    border = (new['w'] - new['cw']) // 2
    inputs.move(sx, sy)
    inputs.button('left', True)
    inputs.move(sx + old['w'] - new['w'] + 1 - border, sy + old['h'] - new['h'])
    inputs.button('left', False)
    restored = wait_snapshot(harness, inputs, 'geometry-restored',
        lambda s: (s['windows']['main']['cw'], s['windows']['main']['ch']) == (old['cw'], old['ch'])
        and named_record(s, 'CONTEXT')['surface'] == old_context['surface']
        and s['nodes']['menu']['w'] == value['nodes']['menu']['w'])
    restored_window, restored_context = restored['windows']['main'], named_record(restored, 'CONTEXT')
    names = ('menu', 'toolbar', 'tabs', 'status')
    rectangles = lambda s: {name: tuple(s['nodes'][name][k] for k in ('x', 'y', 'w', 'h')) for name in names}
    harness.append(dict(name='real-geometry-restore-reclaims-old-surface', before=old,
        after=restored_window, context_before=old_context, context_after=restored_context,
        before_rectangles=rectangles(value), after_rectangles=rectangles(restored)),
        (restored_window['x'], restored_window['y'], restored_window['w'], restored_window['h'])
        == (old['x'], old['y'], old['w'], old['h']) and restored_window['geometry'] != new['geometry']
        and restored_context['surface'] == old_context['surface']
        and restored_context['nodes'] == old_context['nodes'] and rectangles(value) == rectangles(restored))
    harness.screen('scui-native-geometry-restored')
    return restored


def paint_values(snapshot):
    context = named_record(snapshot, 'CONTEXT')
    return dict(paints=counter(context['paints']), submitted=counter(context['submitted']),
        messages=counter(context['messages']), surface=context['surface'], cache=context['cache'],
        visible=context['visible'], nodes=context['nodes'], timers=context['timers'])


def damage_workflow(harness, inputs, initial):
    # Resources没有caret/indeterminate动画，才可要求静止零绘图。
    value = command_key(harness, inputs, initial, 'f11', 126, 'idle-static-resources')
    inputs.move(1, 1)
    harness.pit_window('settle-static-resources', ticks=8)
    before = harness.snapshot(inputs, 'idle-before')
    timing = harness.pit_window('static-native-two-window-200-PIT', ticks=200)
    after = harness.snapshot(inputs, 'idle-after')
    a, b = paint_values(before), paint_values(after)
    harness.append(dict(name='static-roots-do-not-submit-unchanged-frames', before=a, after=b,
        actual_pit=timing, source_resolution=named_record(after, 'DISPLAY')),
        a['paints'] == b['paints'] and a['submitted'] == b['submitted']
        and a['surface'] == b['surface'] and a['nodes'] == b['nodes'])

    value = command_key(harness, inputs, after, 'f6', 121, 'local-damage-controls')
    inputs.move(*screen_point(value, 'check', dx=10))
    harness.pit_window('settle-checkbox-hover', ticks=8)
    before = harness.snapshot(inputs, 'same-hover-before')
    point = screen_point(before, 'check', dx=10)
    for offset in (1, 2, 3, 2, 1, 0):
        inputs.move(point[0] + offset, point[1])
    after = harness.snapshot(inputs, 'same-hover-after')
    a, b = paint_values(before), paint_values(after)
    harness.append(dict(name='same-control-mouse-moves-do-not-repaint', before=a, after=b,
        node=after['nodes']['check']), a['paints'] == b['paints'] and a['submitted'] == b['submitted'])
    state = after['nodes']['check']['state'] & (32 | 64)
    tick = physical_word(harness.vm, harness.symbols['sc_ticks'])
    cpu, began = process_cpu(harness.vm.process), time.monotonic()
    inputs.click(*screen_point(after, 'check', dx=10))
    changed = wait_snapshot(harness, inputs, 'one-control-changed',
        lambda s: s['nodes']['check']['state'] & (32 | 64) != state
        and paint_values(s)['submitted'] > b['submitted'])
    end = paint_values(changed)
    full = changed['windows']['main']['cw'] * changed['windows']['main']['ch'] * 4
    delta = end['submitted'] - b['submitted']
    latency = dict(actual_pit_ticks=(physical_word(harness.vm, harness.symbols['sc_ticks']) - tick) & 0xffffffff,
        wall_seconds=time.monotonic() - began, qemu_one_core_cpu_seconds=process_cpu(harness.vm.process) - cpu,
        method='PHYSICAL_BUTTON_EDGE_TO_F12_PUBLIC_COMMIT_AND_SUBMIT_OBSERVATION',
        includes_F12_and_serial_observation=True)
    harness.append(dict(name='one-control-change-submits-local-damage', before=b, after=end,
        submitted_delta=delta, full_main_surface_bytes=full, latency=latency,
        state_before=state, state_after=changed['nodes']['check']['state'] & (32 | 64)),
        0 < delta < full and end['surface'] == b['surface'] and end['nodes'] == b['nodes'])
    harness.report['performance'].append(dict(name='checkbox-actual-observed-response', **latency))
    harness.screen('scui-native-local-damage')
    return changed


def hidden_workflow(harness, inputs, initial, sid, original_sid):
    # 隐藏期不能发F12到该桌面；只接收Demo有限后台业务timer的公开计量。
    focus_main(inputs, initial)
    before = harness.snapshot(inputs, 'before-session-hidden')
    start = len(inputs.guest.console)
    harness.run('show-original-desktop-hide-ordinary-UI', 'sessionctl show ' + str(original_sid))
    pattern = rb'(?:\r?\n)SCUI HIDDEN sample=([12]) round=([0-9]+) tick=([0-9]+) tickHi=([0-9]+) visible=([0-9]+) paints=([0-9]+:[0-9]+) submitted=([0-9]+:[0-9]+)\r?\n'
    inputs.guest.wait(pattern, start, 30)
    began, samples, pairs = time.monotonic(), [], []
    while time.monotonic() - began < 35:
        samples = [dict(sample=int(m[1]), round=int(m[2]), tick=int(m[3]), tick_hi=int(m[4]),
            visible=int(m[5]), paints=counter(m[6].decode()), submitted=counter(m[7].decode()))
            for m in re.finditer(pattern, bytes(inputs.guest.console[start:]))]
        pairs = [(a, b) for a in samples for b in samples
                 if a['sample'] == 1 and b['sample'] == 2 and a['round'] == b['round']]
        if pairs:
            break
        if inputs.guest.errors or harness.vm.process.poll() is not None:
            raise RuntimeError('真实隐藏计量期间会话终止')
        inputs.guest.stop.wait(.08)
    if len(pairs) != 1:
        raise ValueError('没有唯一同round完整隐藏样本：' + repr(samples))
    a, b = pairs[0]
    elapsed = ((b['tick_hi'] << 32) | b['tick']) - ((a['tick_hi'] << 32) | a['tick'])
    harness.append(dict(name='hidden-session-zero-paint-and-submit-over-200-PIT', before=a, after=b,
        actual_monotonic_ticks=elapsed, session=sid,
        observation='PUBLIC_CONTEXT_QUERY_FROM_TWO_FINITE_BACKGROUND_TIMERS'),
        a['visible'] == b['visible'] == 0 and elapsed >= 200
        and a['paints'] == b['paints'] and a['submitted'] == b['submitted'])
    harness.screen('original-desktop-while-ordinary-UI-hidden')
    harness.run('restore-actual-ordinary-desktop', 'sessionctl show ' + str(sid))
    # 恢复必须真的提交损伤，不能因为hidden停止把整会话永久停住。
    restored = wait_snapshot(harness, inputs, 'session-restored-real-paint',
        lambda s: paint_values(s)['visible'] == 2 and paint_values(s)['paints'] > b['paints']
        and paint_values(s)['submitted'] > b['submitted'])
    harness.append(dict(name='restored-session-repaints-same-live-windows',
        before=before['windows'], after=restored['windows'], after_counters=paint_values(restored)),
        all(restored['windows'][role]['state'] & 1
            and restored['windows'][role]['session'] == sid
            and tuple(restored['windows'][role][key] for key in ('window', 'generation', 'pidgen', 'cw', 'ch'))
            == tuple(before['windows'][role][key] for key in ('window', 'generation', 'pidgen', 'cw', 'ch'))
            for role in ('main', 'second')))
    harness.screen('scui-native-restored-session')
    return restored


def session_list(harness, name):
    item = harness.run(name, 'sessionctl list')
    result = sessions(item['output'].encode('utf-8'))
    item['actual_sessions'] = result
    harness.persist()
    return result


def launch_demo(harness, inputs, original_sid):
    cooldown = harness.login_cooldown()
    marker, job_marker = '__SCUI_RESULT_' + secrets.token_hex(8), '__SCUI_JOB_' + secrets.token_hex(8)
    app = PACKAGE + '/NATIVE_APPS/SCUIDEMO.SCX'
    inner = 'id; ' + app + '; _gui=$?; echo ' + marker + ':$_gui; exit $_gui'
    command = "printf '%s\\n' '' '" + inner + "' | su mio & echo " + job_marker + ':$!'
    start = len(inputs.guest.console)
    item = harness.run('ordinary-login-Shell-background-native-demo', command,
        contains=(job_marker.encode(),), forbidden=(b'[AUTH] login failed',), timeout=60)
    jobs = re.findall(r'(?:^|\r?\n)' + job_marker + r':([0-9]+)\r?\n', item['output'])
    if len(jobs) != 1 or int(jobs[0]) < 1:
        raise ValueError('未取得真实Shell background job PID')
    ready_pattern = rb'(?:^|\r?\n)(?:SYSTEM [^\r\n]* \[SYSTEM\] # )?SCUI READY roots=([0-9]+) nodes=([0-9]+) virtual=([0-9]+) font=([0-9]+)/([0-9]+) scale=([0-9]+)\r?\n'
    failed_pattern = (rb'(?:^|\r?\n)(?:SYSTEM [^\r\n]* \[SYSTEM\] # )?(?:SCUI ERROR[^\r\n]*|'
                      + re.escape(marker.encode()) + rb':-?[0-9]+)\r?\n')
    # 已报告真实启动失败就立即留证，不把它延后伪装成缺少READY的超时。
    ready = inputs.guest.wait(rb'(?:' + ready_pattern + rb'|' + failed_pattern + rb')', start, 90)
    if ready[1] is None:
        raise RuntimeError('原生Demo启动实际失败：' + ready[0].decode('utf-8', 'replace').strip())
    end = inputs.guest.wait(rb'(?:\r?\n)SCUI SNAPSHOT END count=([0-9]+)\r?\n', ready.end(), 25)
    initial = parse_snapshot(bytes(inputs.guest.console[ready.end():end.end()]))
    # READY之前是完整su内层id。window身份来自真正ScWindowQuery，
    # 与真实SESSIONPAGE/PROCESSPAGE交叉核对，不借SYSTEM窗替普通桌面。
    # READY匹配含前导换行；保留这一个真实UID行的尾换行再核身份。
    ids = re.findall(rb'(?:^|\r?\n)uid=1000 gid=([0-9]+) realm=0\r?\n', bytes(inputs.guest.console[start:ready.end()]))
    windows = [initial['windows'][role] for role in ('main', 'second')]
    sid, pid, generation = windows[0]['session'], windows[0]['pid'], windows[0].get('pidgen')
    okay = len(ids) == 1 and int(ready[1]) == 2 and int(ready[3]) == 100000
    okay = okay and sid > 0 and sid != original_sid and pid > 0 and isinstance(generation, int) and generation > 0
    okay = okay and all((w['session'], w['pid'], w.get('pidgen')) == (sid, pid, generation) for w in windows)
    identities = session_list(harness, 'actual-ordinary-login-desktop-before-show')
    row = [value for value in identities if value['id'] == sid]
    okay = okay and len(row) == 1 and row[0]['uid'] == 1000 and row[0]['type'] == 'user'
    processes = harness.run('actual-native-demo-PROCESSPAGE-identity', 'ps')
    live = re.findall(r'^' + str(pid) + r'\s+1000\s+([0-9]+)\s+([0-9]+)\s+(.*)$', processes['output'], re.M)
    okay = okay and len(live) == 1 and len(ids) == 1
    if okay:
        okay = int(live[0][0]) == int(ids[0])
    item.update(actual_inner_result_marker=marker, actual_background_job_pid=int(jobs[0]),
        ordinary_uid=1000, ordinary_realm=0, actual_session=sid, actual_window_owner_pid=pid,
        actual_window_owner_generation=generation, authentication_cooldown=cooldown,
        ready=ready.group().decode('utf-8'), first_query=initial, sessionpage=row,
        processpage_rows=live, launch_console_offset=start)
    harness.persist()
    harness.append(dict(name='true-ordinary-login-native-window-identity', session=sid, pid=pid,
        generation=generation, real_login_gid_lines=[x.decode() for x in ids], sessionpage=row,
        processpage_rows=live, actual_windows=windows), okay)
    harness.last_login_tick = physical_word(harness.vm, harness.symbols['sc_ticks'])
    harness.run('show-actual-independent-ordinary-desktop', 'sessionctl show ' + str(sid))
    shown = session_list(harness, 'verify-selected-ordinary-desktop')
    active = [row for row in shown if row['active']]
    harness.append(dict(name='ordinary-desktop-selected-by-real-sessionctl', actual=active),
        len(active) == 1 and active[0]['id'] == sid and active[0]['uid'] == 1000 and active[0]['type'] == 'user')
    item['ordinary_desktop_shown'] = True
    harness.report['caller_executions'].append(dict(name='SCUIDEMO.SCX', source='ACTUAL_FIRST_ROUND_G1',
        path=app, sha256=harness.report['native_artifacts']['SCUIDEMO.SCX']['sha256'],
        started_this_vm=True, completed_this_vm=False, ordinary_uid=1000, session=sid,
        pid=pid, generation=generation, libraries='SYS_LIB_ACTUAL_NATIVE_FIVE'))
    harness.persist()
    return dict(session=sid, pid=pid, generation=generation, job=int(jobs[0]), marker=marker, start=start)


def exit_and_reclaim(harness, inputs, final, launch, original_sid, baseline, baseline_resources, observations):
    inputs.click(*screen_point(final, 'exit'))
    exit_line = inputs.guest.wait(rb'(?:\r?\n)SCUI EXIT commands=([0-9]+) changes=([0-9]+) drops=([0-9]+) failures=([0-9]+) status=(-?[0-9]+) code=(-?[0-9]+)\r?\n', launch['start'], 45)
    result = inputs.guest.wait(rb'(?:\r?\n)' + launch['marker'].encode() + rb':([0-9]+)\r?\n', exit_line.end() - 1, 45)
    outcome = dict(zip(('commands', 'changes', 'drops', 'failures', 'status_code', 'code'),
                       (int(exit_line[index]) for index in range(1, 7))))
    harness.append(dict(name='native-demo-normal-return-and-real-inner-exit', **outcome,
        actual_inner_exit=int(result[1])), outcome['failures'] == 0 and outcome['status_code'] >= 0
        and outcome['code'] == 0 and int(result[1]) == 0 and outcome['drops'] > 0)
    # wait PID实际经过oldJob generation链和正常fini，不用“串口出现EXIT”
    # 代替应用回收；随后注销被show保留的临时普通会话。
    harness.run('wait-actual-ordinary-Shell-background-job', 'wait ' + str(launch['job']))
    harness.run('return-to-original-selected-desktop', 'sessionctl show ' + str(original_sid))
    harness.run('logout-actual-temporary-ordinary-session', 'sessionctl logout ' + str(launch['session']))
    listed = session_list(harness, 'verify-real-session-and-task-cleanup')
    harness.append(dict(name='temporary-session-is-actually-gone-original-restored', actual_sessions=listed),
        all(row['id'] != launch['session'] for row in listed)
        and [row['id'] for row in listed if row['active']] == [original_sid])
    processes = harness.run('verify-native-demo-owner-no-live-task', 'ps')
    harness.append(dict(name='native-demo-owner-task-gone', pid=launch['pid'], generation=launch['generation'],
        actual_processpage=processes['output']), not re.search(r'^' + str(launch['pid']) + r'\s', processes['output'], re.M))
    # 异步壁纸归旧桌面。完成其真实reader后，再核全部应用/WM/broker/drop
    # owner与PF；新字体栅格的global冷缓存独立列账，不给未知差额豁免。
    harness.warm_desktop()
    after, after_observed = harness.quiet()
    resources_after = harness.resources()
    ledger, okay = harness.ledger(baseline, after, baseline_resources, resources_after, cold_font=True)
    harness.report['GUI_owner_recovery'] = dict(ledger, before_observations=observations,
        after_observations=after_observed, resources_before=baseline_resources,
        resources_after=resources_after, font_cache_is_global_not_application_owned=True,
        allocation_history_is_actual_keep_idle_pool_only=True,
        temporary_session_logout_completed=True, callback_fini_completed_by_true_job_wait=True)
    harness.append(dict(name='native-GUI-windows-broker-drop-task-and-private-PF-reclaimed', **ledger), okay)
    stdout = bytes(inputs.guest.console[launch['start']:])
    path = harness.vm.out / 'SCUIDEMO.stdout.bin'
    path.write_bytes(stdout)
    harness.report['SCUIDEMO_stdout'] = dict(path=str(path), bytes=len(stdout), sha256=digest(stdout))
    execution = next(item for item in harness.report['caller_executions'] if item['name'] == 'SCUIDEMO.SCX')
    execution.update(completed_this_vm=True, actual_exit_code=int(result[1]), failures=outcome['failures'])
    for item in harness.report['native_libraries'].values():
        item['executed_this_vm'] = True
        item['GUI_execution_completed_this_vm'] = True
    harness.report['native_library_execution']['new_GUI_execution_this_vm'] = True
    harness.report['native_library_execution']['new_GUI_execution_identity'] = dict(
        pid=launch['pid'], generation=launch['generation'], session=launch['session'])
    harness.report['native_library_execution']['new_GUI_execution_core_sha256'] = harness.report['core_sha256']
    harness.report['native_artifacts']['SCUIDEMO.SCX']['executed_this_vm'] = True
    harness.report['native_artifacts']['SCUIDEMO.SCX']['completed_this_vm'] = True
    harness.screen('scui-native-normal-exit-original-desktop')
    harness.persist()


def verified_core_route_reuse(build_report, proof, platform_report, pinned_external):
    """固定build12→13的现代F12路由修复；不允许任意新Core借此复用。"""
    import refresh_m10a2_ui_native as refresh
    expected_build = '8f4beaa353863edf0520db784c8afef3cec44f306143939d62cb2e6b7295c5fc'
    old_wm = 'cee84f57f5ba68b89c193ef40bff909e3436f627eaf5f064be24948a2ef874ce'
    new_wm = 'f22357f64be1b7a1c1fab492981080ef58aa2d25c329be5b18cc4590c7c72bca'
    if sha(build_report) != expected_build:
        raise ValueError('Core路由复用只接受已冻结ui-build-13')
    first_report = read_json(platform_report)
    old_path = first.inside(Path(first_report['build_report']), BUILD)
    old_proof, old_groups, ledger = refresh.recorded_build(old_path, first_report['build_report_sha256'])
    groups = {category: {item['path']: item for item in proof[category]}
              for category in ('inputs', 'outputs')}
    inputs = refresh.changed_entries(old_groups['inputs'], groups['inputs'])
    outputs = refresh.changed_entries(old_groups['outputs'], groups['outputs'], output=True)
    changed_outputs = {(BUILD / name).relative_to(ROOT).as_posix()
                       for name in ('fs/SYS/CORE/CORE.SKM', 'core.sym', 'core.elf', 'core.bin')}
    if (set(inputs) != {'kernel/wm.c'} or set(outputs) != changed_outputs
            or inputs['kernel/wm.c']['before']['sha256'] != old_wm
            or inputs['kernel/wm.c']['after']['sha256'] != new_wm
            or sha(ROOT / 'kernel/wm.c') != new_wm):
        raise ValueError('Core路由复用超出真实单WM输入/四Core产物变化')
    core_name = (BUILD / 'fs/SYS/CORE/CORE.SKM').relative_to(ROOT).as_posix()
    if first_report['core_sha256'] != old_groups['outputs'][core_name]['sha256']:
        raise ValueError('旧生成实际Core与build12凭证不同')
    archive = ROOT / 'evidence/m10a2-ui-refresh'
    archived = {}
    for name, saved in (('fs/SYS/CORE/CORE.SKM', 'CORE-build12.SKM'),
                        ('core.elf', 'core-build12.elf'), ('core.sym', 'core-build12.sym')):
        record = old_groups['outputs'][(BUILD / name).relative_to(ROOT).as_posix()]
        path = pin_path(archive / saved, archive, record['sha256'], record['bytes'])
        pinned_external[path] = record['sha256']
        archived[name] = dict(path=str(path), bytes=record['bytes'], sha256=record['sha256'])
    old_core = Path(archived['fs/SYS/CORE/CORE.SKM']['path']).read_bytes()
    payload_record = old_groups['outputs'][(BUILD / 'core.bin').relative_to(ROOT).as_posix()]
    if len(old_core) - 128 != payload_record['bytes'] or digest(old_core[128:]) != payload_record['sha256']:
        raise ValueError('归档旧Core128B头后的精确payload与旧core.bin不同')
    saved_proof = pin_path(archive / 'ui-build-12.json', archive, first_report['build_report_sha256'])
    pinned_external[saved_proof] = first_report['build_report_sha256']
    pinned_external[old_path] = first_report['build_report_sha256']
    evidence = dict(kind='CORE_ROUTE_ONLY_REUSE', schema=1,
        source_commit_before='0d78cd4de7816859bb5f50e80f87b37f13cb47d8',
        scope='FOCUSED_MODERN_GUI_F12_RAW_KEY_ROUTE_ONLY',
        generation_build_report=str(old_path), generation_build_report_sha256=sha(old_path),
        runtime_build_report=str(build_report), runtime_build_report_sha256=sha(build_report),
        generation_build_input_output_sha256=ledger,
        input_changes=inputs, output_changes=outputs, archived_generation_core=archived,
        generation_core_sha256=first_report['core_sha256'],
        runtime_core_sha256=groups['outputs'][core_name]['sha256'],
        user_sources_G1_five_libraries_three_apps_and_boot_bytes_unchanged=True,
        previous_checks_keep_original_core_origin=True,
        current_core_route_requires_actual_GUI_execution=True)
    return old_path, (old_proof, ledger), evidence


def reuse_publication_checks(path, report, libraries, metadata, hashes, pinned_external):
    """只复用实际不加载SCUI的两App；新SCUI的文本与GUI仍真跑。"""
    import refresh_m10a2_ui_native as refresh
    path = first.inside(path, ROOT / 'build')
    old = read_json(path)
    if old.get('native_checks_reuse'):
        return reuse_publication_completed_checks(path, report, libraries, metadata, hashes, pinned_external)
    publication = report['SCUI_publication_repair']
    if (publication.get('kind') != refresh.PUBLICATION_MODE
            or old.get('status') != 'FAIL_OR_INTERRUPTED' or old.get('GUI_behavior_verified') is not False
            or old.get('compiler_default_repair') is not True or old.get('native_checks_reuse')
            or old.get('source_disk_unchanged') is not True or old.get('current_inputs_unchanged') is not True
            or old.get('changed_input_output_paths') != [] or old.get('changed_provenance_artifact_paths') != []):
        raise ValueError('发布切片必须引用原完整新五库执行且GUI未通过的保持报告')
    _, _, old_ledger = refresh.recorded_build(Path(publication['previous_build_report']),
        publication['previous_build_report_sha256'])
    if old.get('pinned_build_input_output_sha256') != old_ledger:
        raise ValueError('旧两App不是发布修复前的精确build14运行')
    for key in ('scope', 'source_data_sha256', 'core_sha256', 'core_sym_sha256', 'core_elf_sha256',
                'pinned_P3_runtime_stdlib_sclib_and_old_layout_sha256'):
        if old.get(key) != report.get(key):
            raise ValueError('发布复用的真实内核/原盘/ROOT来源变化：' + key)
    before_inputs = old['input_sha256']
    if set(before_inputs) != set(hashes):
        raise ValueError('发布复用的完整payload路径集合不同')
    changes = {}
    for name, before in before_inputs.items():
        if hashes[name] == before:
            continue
        if name in refresh.PUBLICATION_SOURCES:
            a, b, _ = refresh.PUBLICATION_SOURCES[name]
            if (before, hashes[name]) != (a, b):
                raise ValueError('发布源并非冻结的精确新旧字节')
            changes[name] = dict(before=before, after=hashes[name], kind='IRRELEVANT_SCUI_SOURCE_FOR_THESE_APPS')
        elif name in ('tools/verify_m10a2_ui_gui.py', 'tools/scui_input.py', 'tools/scui_workflow.py'):
            changes[name] = dict(before=before, after=hashes[name], kind='HOST_GUI_DRIVER_ONLY')
        else:
            raise ValueError('两App执行依赖发生未审定变化：' + name)
    old_libraries = old['native_libraries']
    if set(old_libraries) != set(libraries):
        raise ValueError('旧安装五库集合不完整')
    changed_libraries = {name: dict(before=old_libraries[name]['sha256'], after=digest(body))
                         for name, body in libraries.items() if old_libraries[name]['sha256'] != digest(body)}
    if set(changed_libraries) != {'SCUI.SCL'}:
        raise ValueError('发布修复只允许SCUI真实库字节变化')
    report_sha = sha(path)
    pinned_external[path] = report_sha
    cases = {case['name']: case for case in old['cases']}
    if len(cases) != len(old['cases']):
        raise ValueError('旧发布报告case重复')
    app_cases = {'PLATFORMPROBE.SCX': 'native-five-platform-actual-mio-session',
                 'DEFAULTAPI.SCX': 'native-new-default-api-actual-mio-session'}
    proofs = {}
    for app, case_name in app_cases.items():
        current = metadata[app]
        previous = old['native_artifacts'][app]
        if (previous['sha256'] != current['sha256'] or previous['bytes'] != current['bytes']
                or previous['format_contract'] != current['format_contract']):
            raise ValueError('复用App机器字节或manifest变化：' + app)
        actual = Path(current['path']).read_bytes()
        pending = {'SCLDR.SCL'} | {item['name'] for item in validate_scx(actual)['dependencies']}
        resolved = {}
        while pending:
            name = pending.pop()
            if name in resolved:
                continue
            if name not in libraries or name == 'SCUI.SCL':
                raise ValueError('复用App实际加载了变化的SCUI或未知库：' + app)
            value = digest(libraries[name])
            if old_libraries[name]['sha256'] != value:
                raise ValueError('复用App的实际传递依赖变了：' + name)
            resolved[name] = value
            pending.update(item['name'] for item in validate(libraries[name])['dependencies'])
        origin = dict(core_sha256=old['core_sha256'], loaded_library_sha256=resolved,
            old_installed_five_library_sha256={name: item['sha256'] for name, item in old_libraries.items()},
            current_loaded_dependencies_exactly_unchanged=True, SCUI_not_loaded=True,
            changed_installed_library_irrelevant=changed_libraries,
            source_report=str(path), source_report_sha256=report_sha)
        for name in (case_name, case_name + '-actual-owner-page-account'):
            source = cases.get(name, {})
            if (source.get('status') != 'PASS' or source.get('complete_owner_and_gc_observed') is not True
                    or (name == case_name and (source.get('exit_code') != 0 or source.get('ordinary_exit_code') != 0))):
                raise ValueError('旧两App缺实际退出/owner PASS：' + name)
            if name == case_name:
                output = pin_path(Path(source['stdout_path']), path.parent, source['stdout_sha256'], source['stdout_bytes'])
                pinned_external[output] = source['stdout_sha256']
            item = copy.deepcopy(source)
            item.update(status='REUSED_PASS', original_status='PASS', execution_class='REUSED',
                executed_in_this_vm=False, source_report=str(path), source_report_sha256=report_sha,
                source_case=name, runtime_dependencies_origin=origin)
            report['cases'].append(item)
        callers = [item for item in old['caller_executions'] if item.get('name') == app]
        if len(callers) != 1 or callers[0].get('completed_this_vm') is not True or callers[0].get('ordinary_uid') != 1000:
            raise ValueError('旧caller缺真实普通执行来源：' + app)
        item = copy.deepcopy(callers[0])
        item.update(execution_class='REUSED', completed_this_vm=False, executed_this_vm=False,
            completed_in_origin_vm=True, runtime_dependencies_origin=origin)
        report['caller_executions'].append(item)
        report['native_artifacts'][app].update(executed_this_vm=False, completed_this_vm=False,
            execution_origin_report=str(path), execution_origin_report_sha256=report_sha)
        proofs[app] = origin
    report['native_checks_reuse'] = dict(kind='SCUI_PUBLICATION_UNLOADED_DEPENDENCIES_ONLY',
        source_report=str(path), source_report_sha256=report_sha, source_overall_status=old['status'],
        app_runtime_closures=proofs, current_input_changes=changes,
        old_samples_not_current_VM_samples=True, installed_five_libraries_same_bytes=False,
        new_SCUI_text_and_GUI_require_actual_execution=True)


def reuse_publication_completed_checks(path, report, libraries, metadata, hashes, pinned_external):
    """只接受09的已完成文本账；两项旧平台证据仍回到07独立核验。"""
    previous = read_json(path)
    report_sha = sha(path)
    if (report_sha != '449756cee63a9ae451f30933e5a0c3d64ace049465a77fac4929a698d6c19be8'
            or previous.get('status') != 'FAIL_OR_INTERRUPTED'
            or previous.get('GUI_behavior_verified') is not False
            or previous.get('source_disk_unchanged') is not True
            or previous.get('current_inputs_unchanged') is not True
            or previous.get('changed_input_output_paths') != []
            or previous.get('changed_provenance_artifact_paths') != []
            or previous.get('native_checks_reuse', {}).get('kind') != 'SCUI_PUBLICATION_UNLOADED_DEPENDENCIES_ONLY'):
        raise ValueError('本分支只接受已冻结09原始失败报告中的完成证据')
    for key in ('scope', 'source_data_sha256', 'core_sha256', 'core_sym_sha256', 'core_elf_sha256',
                'build_report_sha256', 'platform_report_sha256', 'pinned_build_input_output_sha256',
                'pinned_P3_runtime_stdlib_sclib_and_old_layout_sha256', 'SCUI_publication_repair',
                'compiler_default_repair'):
        if previous.get(key) != report.get(key):
            raise ValueError('09文本的实际编译与运行闭包已变化：' + key)
    before_inputs = previous['input_sha256']
    if set(before_inputs) != set(hashes):
        raise ValueError('09与本次输入路径集合不同')
    changes = {}
    for name, before in before_inputs.items():
        if before != hashes[name]:
            if name not in ('tools/verify_m10a2_ui_gui.py', 'tools/scui_input.py', 'tools/scui_workflow.py'):
                raise ValueError('09文本运行相关输入已变化：' + name)
            changes[name] = dict(before=before, after=hashes[name], kind='HOST_GUI_DRIVER_ONLY')
    library_sha = {name: digest(body) for name, body in libraries.items()}
    runtime = previous.get('native_library_execution', {})
    if (runtime.get('installed_five_library_sha256') != library_sha
            or runtime.get('platform_and_official_text_completed') is not True
            or runtime.get('actual_ordinary_uid') != 1000
            or runtime.get('GCC_library_execution_this_vm') is not False
            or runtime.get('official_text_execution_class') != 'FRESH'):
        raise ValueError('09缺少当前五原生库的真实普通会话文本执行')
    for key, current in (('native_libraries', report['native_libraries']),
                         ('native_artifacts', report['native_artifacts'])):
        if set(previous.get(key, {})) != set(current):
            raise ValueError('09库或App集合变化：' + key)
        for name, item in current.items():
            old = previous[key][name]
            if any(old.get(field) != item.get(field) for field in ('sha256', 'bytes', 'format_contract')):
                raise ValueError('09库或App字节/manifest变化：' + name)
    origin = previous['native_checks_reuse']
    source = pin_path(Path(origin['source_report']), ROOT / 'build', origin['source_report_sha256'])
    # 仍由原严格分支验证07真实PASS、stdout、加载依赖与14→16四源差。
    reuse_publication_checks(source, report, libraries, metadata, hashes, pinned_external)
    original_unloaded = copy.deepcopy(report['native_checks_reuse'])
    cases = {item['name']: item for item in previous['cases']}
    if len(cases) != len(previous['cases']):
        raise ValueError('09重复case不能作为证据')
    for item in report['cases']:
        if item.get('status') == 'REUSED_PASS' and cases.get(item['name']) != item:
            raise ValueError('09引用的平台证据与07原始来源不同：' + item['name'])
    case_name = 'native-five-official-3013-text-corpus'
    completed = cases.get(case_name, {})
    account = cases.get(case_name + '-actual-owner-page-account', {})
    if (completed.get('status') != 'PASS' or completed.get('exit_code') != 0
            or completed.get('ordinary_exit_code') != 0
            or completed.get('complete_owner_and_gc_observed') is not True
            or account.get('status') != 'PASS' or account.get('complete_owner_and_gc_observed') is not True):
        raise ValueError('09文本没有实际普通退出和owner回收PASS')
    output = pin_path(Path(completed['stdout_path']), path.parent,
        completed['stdout_sha256'], completed['stdout_bytes'])
    pinned_external[path] = report_sha
    pinned_external[output] = completed['stdout_sha256']
    execution = dict(core_sha256=previous['core_sha256'], loaded_library_sha256=library_sha,
        build_report_sha256=previous['build_report_sha256'],
        platform_report_sha256=previous['platform_report_sha256'],
        source_input_sha256={name: value for name, value in before_inputs.items()
            if name.startswith(('kernel/', 'user/', 'tests/m10a2/', 'assets/'))},
        source_report=str(path), source_report_sha256=report_sha,
        current_compile_and_runtime_dependencies_exactly_unchanged=True)
    for original in (completed, account):
        value = copy.deepcopy(original)
        value.update(status='REUSED_PASS', original_status='PASS', execution_class='REUSED',
            executed_in_this_vm=False, source_report=str(path), source_report_sha256=report_sha,
            source_case=original['name'],
            source_case_sha256=digest(json.dumps(original, ensure_ascii=False, sort_keys=True).encode('utf-8')),
            runtime_dependencies_origin=execution)
        report['cases'].append(value)
    callers = [item for item in previous['caller_executions'] if item.get('name') == 'SCUITEXT.SCX']
    if (len(callers) != 1 or callers[0].get('completed_this_vm') is not True
            or callers[0].get('ordinary_uid') != 1000
            or callers[0].get('sha256') != metadata['SCUITEXT.SCX']['sha256']):
        raise ValueError('09文本caller没有真实完成或字节不同')
    caller = copy.deepcopy(callers[0])
    caller.update(execution_class='REUSED', completed_this_vm=False, executed_this_vm=False,
        completed_in_origin_vm=True, runtime_dependencies_origin=execution)
    report['caller_executions'].append(caller)
    report['native_artifacts']['SCUITEXT.SCX'].update(executed_this_vm=False, completed_this_vm=False,
        execution_origin_report=str(path), execution_origin_report_sha256=report_sha)
    report['native_checks_reuse'] = dict(kind='PUBLICATION_09_TEXT_PLUS_07_UNLOADED_APPS',
        source_report=str(path), source_report_sha256=report_sha, source_overall_status=previous['status'],
        original_unloaded_app_checks=original_unloaded, text_runtime_dependencies_origin=execution,
        host_driver_changes=changes, old_samples_not_current_VM_samples=True,
        no_platform_or_text_rerun=True, new_SCUI_text_and_GUI_require_actual_execution=False,
        GUI_requires_actual_execution=True)
    report['native_library_execution'] = dict(runtime, execution_class='REUSED', executed_this_vm=False,
        official_text_execution_class='REUSED', source_report=str(path), source_report_sha256=report_sha,
        new_GUI_execution_this_vm=False, old_samples_not_current_VM_samples=True)


def reuse_native_checks(path, report, libraries, metadata, hashes, pinned_external):
    """只复用原GUI会话已完成的两组检查，不把整体FAIL改为PASS。"""
    path = first.inside(path, ROOT / 'build')
    prior = read_json(path)
    if (prior.get('scope') != report['scope'] or prior.get('status') != 'FAIL_OR_INTERRUPTED'
            or prior.get('GUI_behavior_verified') is not False
            or prior.get('current_inputs_unchanged') is not True
            or prior.get('source_disk_unchanged') is not True
            or prior.get('changed_input_output_paths') != []
            or prior.get('changed_provenance_artifact_paths') != []
            or prior.get('native_checks_reuse')):
        raise ValueError('复用必须为源盘/输入保持且GUI未通过的原独立运行报告')
    route = report.get('core_route_reuse')
    historical = {}
    if route:
        archived = route['archived_generation_core']
        historical = dict(core_sha256=route['generation_core_sha256'],
            core_sym_sha256=archived['core.sym']['sha256'], core_elf_sha256=archived['core.elf']['sha256'],
            build_report_sha256=route['generation_build_report_sha256'],
            pinned_build_input_output_sha256=route['generation_build_input_output_sha256'])
    for key in ('source_data_sha256', 'core_sha256', 'core_sym_sha256', 'core_elf_sha256',
                'build_report_sha256', 'platform_report_sha256',
                'pinned_build_input_output_sha256', 'pinned_P3_runtime_stdlib_sclib_and_old_layout_sha256'):
        if prior.get(key) != historical.get(key, report.get(key)):
            raise ValueError('两组native检查的真实运行/构建依赖变化：' + key)
    old_inputs = prior.get('input_sha256', {})
    if set(old_inputs) != set(hashes):
        raise ValueError('复用报告与当前完整输入路径集合不同')
    tool_changes = {}
    for name, before in old_inputs.items():
        after = hashes[name]
        if before != after:
            if name not in ('tools/verify_m10a2_ui_gui.py', 'tools/scui_input.py', 'tools/scui_workflow.py'):
                raise ValueError('仅允许宿主GUI接收/快照解析主控改变：' + name)
            tool_changes[name] = dict(before=before, after=after, kind='HOST_GUI_DRIVER_ONLY')
    library_sha = {name: digest(body) for name, body in libraries.items()}
    runtime = prior.get('native_library_execution', {})
    if (runtime.get('platform_and_official_text_completed') is not True
            or runtime.get('installed_five_library_sha256') != library_sha
            or runtime.get('actual_ordinary_uid') != 1000
            or runtime.get('GCC_library_execution_this_vm') is not False):
        raise ValueError('旧检查并非这五份实际native库的真实普通会话执行')
    if set(prior.get('native_libraries', {})) != set(libraries):
        raise ValueError('旧运行没有完整五native来源')
    for name, body in libraries.items():
        old = prior['native_libraries'][name]
        current = report['native_libraries'][name]
        if (old.get('sha256') != digest(body) or old.get('bytes') != len(body)
                or old.get('format_contract') != current.get('format_contract')):
            raise ValueError('native库字节/manifest契约不同：' + name)
    if set(prior.get('native_artifacts', {})) != set(report['native_artifacts']):
        raise ValueError('复用的三个app集合不同')
    for name, item in report['native_artifacts'].items():
        old = prior['native_artifacts'][name]
        if (old.get('sha256') != item['sha256'] or old.get('bytes') != item['bytes']
                or old.get('format_contract') != item.get('format_contract')):
            raise ValueError('app字节/manifest契约不同：' + name)
    report_sha = sha(path)
    pinned_external[path] = report_sha
    cases = {item['name']: item for item in prior.get('cases', [])}
    if len(cases) != len(prior.get('cases', [])):
        raise ValueError('原GUI报告有重复case')
    required = ('native-five-platform-actual-mio-session', 'native-five-official-3013-text-corpus')
    if report.get('compiler_default_repair') is True:
        if prior.get('compiler_default_repair') is not True:
            raise ValueError('新default修复运行不能复用旧编译器五库结果')
        required += ('native-new-default-api-actual-mio-session',)
    origin_runtime = dict(core_sha256=prior['core_sha256'], library_sha256=library_sha,
        source_input_sha256={name: value for name, value in old_inputs.items()
                             if name.startswith(('kernel/', 'user/', 'tests/m10a2/', 'assets/'))},
        build_report_sha256=prior['build_report_sha256'],
        platform_report_sha256=prior['platform_report_sha256'],
        current_compile_and_runtime_dependencies_unchanged=route is None,
        current_user_compile_and_library_dependencies_unchanged=True,
        current_core_route_change=route)
    for name in required:
        case = cases.get(name, {})
        account = cases.get(name + '-actual-owner-page-account', {})
        if (case.get('status') != 'PASS' or case.get('exit_code') != 0
                or case.get('ordinary_exit_code') != 0
                or case.get('complete_owner_and_gc_observed') is not True
                or account.get('status') != 'PASS'
                or account.get('complete_owner_and_gc_observed') is not True):
            raise ValueError('原报告缺这组实际PASS/普通退出/owner账：' + name)
        output = pin_path(Path(case['stdout_path']), path.parent,
            case['stdout_sha256'], case['stdout_bytes'])
        pinned_external[output] = case['stdout_sha256']
        for original in (case, account):
            value = copy.deepcopy(original)
            value.update(status='REUSED_PASS', original_status='PASS', execution_class='REUSED',
                executed_in_this_vm=False, source_report=str(path), source_report_sha256=report_sha,
                source_case=original['name'],
                source_case_sha256=digest(json.dumps(original, ensure_ascii=False, sort_keys=True).encode('utf-8')),
                runtime_dependencies_origin=origin_runtime)
            report['cases'].append(value)
    names = ('PLATFORMPROBE.SCX', 'SCUITEXT.SCX')
    if report.get('compiler_default_repair') is True:
        names += ('DEFAULTAPI.SCX',)
    callers = [item for item in prior.get('caller_executions', []) if item.get('name') in names]
    if len(callers) != len(names) or {item['name'] for item in callers} != set(names):
        raise ValueError('原报告缺当前声明的实际caller执行来源')
    for item in callers:
        if (item.get('completed_this_vm') is not True or item.get('ordinary_uid') != 1000
                or item.get('sha256') != metadata[item['name']]['sha256']):
            raise ValueError('原caller未实际完成或来源不同：' + item['name'])
        value = copy.deepcopy(item)
        value.update(execution_class='REUSED', executed_this_vm=False, completed_this_vm=False,
            completed_in_origin_vm=True, source_report=str(path), source_report_sha256=report_sha,
            runtime_dependencies_origin=origin_runtime)
        report['caller_executions'].append(value)
    report['native_checks_reuse'] = dict(schema=1, source_report=str(path), source_report_sha256=report_sha,
        source_overall_status=prior['status'], source_GUI_behavior_verified=False,
        checks=list(required), runtime_dependencies_origin=origin_runtime,
        host_driver_changes=tool_changes, no_platform_or_text_rerun=True,
        old_samples_not_current_VM_samples=True)
    report['native_library_execution'] = dict(runtime, execution_class='REUSED', executed_this_vm=False,
        source_report=str(path), source_report_sha256=report_sha,
        new_GUI_execution_this_vm=False, old_samples_not_current_VM_samples=True)
    for name in names:
        report['native_artifacts'][name].update(executed_this_vm=False, completed_this_vm=False,
            execution_origin_report=str(path), execution_origin_report_sha256=report_sha)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--platform-report', type=Path, required=True)
    parser.add_argument('--reuse-native-checks', type=Path)
    parser.add_argument('--core-route-only-reuse', action='store_true')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    if args.core_route_only_reuse and not args.reuse_native_checks:
        parser.error('Core路由切片必须明确指定已实际通过的native-check来源')
    if os.name != 'nt':
        parser.error('仅Windows QEMU；不在WSL启动本主控')
    args.out = args.out.resolve()
    if args.out.exists():
        raise FileExistsError('拒绝覆盖已有VM、报告或失败证据')
    report = dict(status='PREFLIGHT', scope='M10A2_UI_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND',
        cases=[], caller_executions=[], screenshots=[], snapshots=[], physical_input=[], performance=[],
        native_libraries={}, native_artifacts={}, GUI_behavior_verified=False,
        full_M10a2=False, unrelated_prior_evidence='REUSED_NOT_RERUN',
        library_generation_this_vm=False, no_GCC_library_installed=True,
        source_disk_unchanged=None, current_inputs_unchanged=None,
        implementation_limits=['不包含IME、BiDi或复杂脚本整形', '不代替完整应用重制/SDK/组件验收',
            '本轮实际GUI不代表全部控件所有组合或跨进程拖放UI矩阵',
            'PIT和串口观察延迟不等于帧率或未观测的瞬时输入长尾'])
    guest = harness = vm = None
    pins, original = {}, None
    pinned_external = {}

    def persist():
        (args.out / 'ui-gui.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def preservation():
        changed = [name for name, expected in pins.items()
                   if not (ROOT / name).is_file() or sha(ROOT / name) != expected]
        external_changed = [str(path) for path, expected in pinned_external.items()
                            if not path.is_file() or sha(path) != expected]
        report.update(changed_input_output_paths=changed, changed_provenance_artifact_paths=external_changed,
            current_inputs_unchanged=(not changed and not external_changed) if pins else None)
        if original is not None:
            report['source_disk_unchanged'] = sha(args.data) == original
        if changed or external_changed or report['source_disk_unchanged'] is False:
            report['status'] = 'FAIL_OR_INTERRUPTED'
            report['input_preservation_error'] = '当前源码/构建/来源字节或原盘发生变化'

    try:
        args.data = args.data.resolve(strict=True)
        args.build_report = first.inside(args.build_report, ROOT)
        args.platform_report = first.inside(args.platform_report, ROOT)
        proof, proof_hashes, outputs = first.verified_build(args.build_report)
        if proof.get('source_files_modified_during_build') != []:
            raise ValueError('统一build必须显式source_modified=[]，不能以缺字段视为未修改')
        generation_build, generation_hashes, historical_build = args.build_report, proof_hashes, None
        if args.core_route_only_reuse:
            generation_build, historical_build, route = verified_core_route_reuse(
                args.build_report, proof, args.platform_report, pinned_external)
            generation_hashes = historical_build[1]
            report['core_route_reuse'] = route
        first_report, generated, apps, metadata = native_first_round(args.platform_report,
            generation_build, generation_hashes, historical_build)
        compiler_repaired = first_report.get('compiler_default_repair') is True
        reused, reused_metadata, p3_sources = reused_p3(proof_hashes, compiler_repaired)
        libraries = dict(reused, **generated)
        if set(libraries) != set(first.LIBRARIES):
            raise ValueError('真实原生五库不完整')
        native = dict(reused_metadata, **{name: metadata[name] for name in generated})
        original = sha(args.data)
        p3_native = read_json(P3 / 'native.json')
        if (first_report.get('source_data_sha256') != original
                or p3_native.get('source_data_sha256') != original):
            raise ValueError('首轮/P3/本轮不是同一保持的M10a1只读原盘')
        prefix = '/TMP/UI-GUI-' + secrets.token_hex(3)
        extra, hashes, excluded = first.payload(prefix, proof_hashes, compiler_repaired)
        for name in HELPERS:
            hashes[name] = sha(ROOT / name)
        hashes['tools/verify_m10a2_ui_gui.py'] = sha(Path(__file__))
        for name, expected in first_report['input_sha256'].items():
            if name in hashes and hashes[name] != expected:
                raise ValueError('当前payload/helper与完成首轮不同：' + name)
        pins = dict(proof_hashes, **hashes)
        report.update(status='RUNNING', input_sha256=hashes,
            pinned_build_input_output_sha256=proof_hashes,
            pinned_P3_runtime_stdlib_sclib_and_old_layout_sha256=p3_sources,
            source_data_path=str(args.data), source_data_sha256=original,
            build_report=str(args.build_report), build_report_sha256=sha(args.build_report),
            platform_report=str(args.platform_report), platform_report_sha256=sha(args.platform_report),
            native_libraries=native,
            native_artifacts={name + '.SCX': metadata[name + '.SCX'] for name in apps},
            host_build_wall_seconds=proof['wall_seconds'],
            core_sha256=sha(BUILD / 'fs/SYS/CORE/CORE.SKM'),
            core_sym_sha256=sha(BUILD / 'core.sym'), core_elf_sha256=sha(BUILD / 'core.elf'),
            new_library_source='FIRST_ROUND_ACTUAL_G1_GENERATED_BYTES_NOT_EXECUTED_IN_FIRST_VM',
            compiler_default_repair=compiler_repaired,
            SCUI_publication_repair=copy.deepcopy(first_report.get('source_reuse')) if first_report.get('source_reuse', {}).get('kind') == 'POST_ACTIVATE_PUBLICATION_REPAIR' else None,
            fresh_generated_libraries=sorted(generated), reused_P3_libraries=sorted(reused),
            reused_library_source='P3_ACTUAL_GUEST_GENERATED_AND_PRIOR_NATIVE_VM_EXECUTED_BYTES',
            excluded_source_drafts=excluded, ordinary_login_not_simulated=True,
            execution_scope='EXTERNAL_SYSTEM_CONTROL_PLUS_TRUE_AUTH_LOGIN_ORDINARY_DESKTOP_GUI')
        if args.reuse_native_checks:
            if report['SCUI_publication_repair']:
                reuse_publication_checks(args.reuse_native_checks, report, libraries, metadata, hashes, pinned_external)
            else:
                reuse_native_checks(args.reuse_native_checks, report, libraries, metadata, hashes, pinned_external)
        for name, item in native.items():
            extra['SYS/LIB/' + name] = libraries[name]
            item.update(installed_path='/SYS/LIB/' + name, planned_install=True, installed_this_vm=False)
            pinned_external[Path(item['path'])] = item['sha256']
        for name, body in apps.items():
            app_name = name + '.SCX'
            extra[PACKAGE[1:] + '/NATIVE_APPS/' + app_name] = body
            item = report['native_artifacts'][app_name]
            item.update(installed_path=PACKAGE + '/NATIVE_APPS/' + app_name,
                planned_install=True, installed_this_vm=False)
            pinned_external[Path(item['path'])] = item['sha256']
        # 保存目标预存在本VM副本中，只用普通权限真正Save/overwrite。
        extra[SAVED[1:]] = b'previous saved document\n'
        for path in (args.platform_report, P3 / 'native.json', P3 / 'stdlib.json',
                     P3 / 'native-libraries/provenance.json',
                     args.platform_report.parent / 'native-libraries/provenance.json',
                     args.platform_report.parent / 'native-artifacts/provenance.json'):
            pinned_external[path] = sha(path)
        oracle = read_json(ROOT / 'assets/ui/tests/PROVENANCE.json')
        expected_boundaries, oracle_info = 0, {}
        for name, kind, count in (('GRAPHEME.SCT', 1, 1187), ('WORD.SCT', 2, 1826)):
            value = first.corpus_info((ROOT / 'assets/ui/tests' / name).read_bytes(), kind, count)
            source = next(item for item in oracle['outputs'] if item['path'] == name)
            if (source['sha256'] != value['sha256'] or source['cases'] != count or source['kind'] != kind
                    or sha(ROOT / 'tests/m10a2/unicode15_1' / source['original']) != source['original_sha256']):
                raise ValueError('原始官方oracle及容器来源改变')
            expected_boundaries += value['boundaries']
            oracle_info[name] = dict(value, original_sha256=source['original_sha256'],
                expected_source=source['expected_boundaries_source'])
        report['official_corpus'] = oracle_info
        text_marker = ('SCUI_TEXT_PASS version=15.1 cases=3013 boundaries=%d' % expected_boundaries).encode()
        symbols = unique_symbols(BUILD / 'core.sym')
        for name in ('pf_used', 'sc_ticks', 'wm_frames', 'mx', 'my', 'cleanup_head', 'zombie_head',
                     'cleanup_revoke_pending', 'br_garbage_first', 'br_notify_first', 'gc_first', 'session_first',
                     'wait_count', 'wait_capacity', 'wait_heap', 'wait_pages', 'pf_frames', 'pf_bitmap'):
            if name not in symbols:
                raise ValueError('当前CORE缺唯一输入/时钟/真实owner账符号：' + name)
        scoped = first.local_elf_symbols(BUILD / 'core.elf')

        def prepared(image):
            result = first.prepare(image, BUILD, extra, runtime_fixtures=False)
            for item in native.values():
                item['installed_this_vm'] = True
            for item in report['native_artifacts'].values():
                item['installed_this_vm'] = True
            result.update(installed_libraries=native, installed_native_applications=report['native_artifacts'],
                complete_source_sha256=hashes, excluded_drafts=excluded,
                copied_license_files={name: digest(body) for name, body in extra.items() if name.startswith('SYS/LICENSE/')},
                public_sdk_files=sorted(name for name in extra if name.startswith('SYS/INC/')),
                host_bootstrap_libraries=False, generated_libraries_installed=True,
                new_SC32_SCUI_from_first_G1=True,
                new_SCCLIB_from_repaired_first_G1=compiler_repaired,
                remaining_three_from_prior_actual_native_P3=not compiler_repaired,
                reused_P3_library_names=sorted(reused))
            report['preparation'] = result
            return result

        with first.harness_configuration():
            with QemuSession(BUILD / 'sandcore.img', args.data, args.out, args.accel, True,
                             audio=False, memory=256, data_prepare=prepared) as vm:
                report['qemu_pid'] = vm.process.pid
                guest = Guest(vm)
                try:
                    harness = GuiHarness(vm, guest, report, symbols, persist, scoped)
                    inputs = ScuiInput(vm, guest, symbols, harness.input_record)
                    persist()
                    wait_first_desktop(guest, symbols, report)
                    guest.connect()
                    harness.warm()
                    for name, body in libraries.items():
                        harness.file('/SYS/LIB/' + name, 'initial-real-native-library-' + name, expected=body)
                    if not args.reuse_native_checks:
                        probe = PACKAGE + '/NATIVE_APPS/PLATFORMPROBE.SCX'
                        harness.ordinary('native-five-platform-actual-mio-session', probe, 'all ' + probe,
                        first.PLATFORM_MARKERS, cold_font=True, identity=True)
                        report['caller_executions'].append(dict(name='PLATFORMPROBE.SCX', path=probe,
                        sha256=metadata['PLATFORMPROBE.SCX']['sha256'], source='ACTUAL_FIRST_ROUND_G1',
                        completed_this_vm=True, libraries='SYS_LIB_ACTUAL_NATIVE_FIVE', ordinary_uid=1000))
                        report['native_artifacts']['PLATFORMPROBE.SCX']['executed_this_vm'] = True
                    if not args.reuse_native_checks or report.get('native_checks_reuse', {}).get('new_SCUI_text_and_GUI_require_actual_execution'):
                        text = PACKAGE + '/NATIVE_APPS/SCUITEXT.SCX'
                        harness.ordinary('native-five-official-3013-text-corpus', text, '', (text_marker,), timeout=600)
                        report['caller_executions'].append(dict(name='SCUITEXT.SCX', path=text,
                        sha256=metadata['SCUITEXT.SCX']['sha256'], source='ACTUAL_FIRST_ROUND_G1',
                        completed_this_vm=True, libraries='SYS_LIB_ACTUAL_NATIVE_FIVE', ordinary_uid=1000,
                        official_cases=3013, expected_bidirectional_boundaries=expected_boundaries))
                        report['native_artifacts']['SCUITEXT.SCX']['executed_this_vm'] = True
                        for item in native.values():
                            item['executed_this_vm'] = True
                        report['native_library_execution'] = dict(platform_and_official_text_completed=True,
                        installed_five_library_sha256={name: digest(body) for name, body in libraries.items()},
                        actual_ordinary_uid=1000, new_library_generation_not_repeated=True,
                        GCC_library_execution_this_vm=False,
                        platform_execution_class='REUSED' if args.reuse_native_checks else 'FRESH',
                        official_text_execution_class='FRESH',
                        platform_and_text_both_executed_this_vm=not args.reuse_native_checks)
                        if compiler_repaired and not args.reuse_native_checks:
                            target = PACKAGE + '/NATIVE_APPS/DEFAULTAPI.SCX'
                            harness.ordinary('native-new-default-api-actual-mio-session', target, '',
                                (b'DEFAULTAPI PASS unknown-system-and-standard-errors',), timeout=90)
                            report['caller_executions'].append(dict(name='DEFAULTAPI.SCX', path=target,
                                sha256=metadata['DEFAULTAPI.SCX']['sha256'], source='ACTUAL_REPAIRED_FIRST_ROUND_G1',
                                completed_this_vm=True, libraries='SYS_LIB_ACTUAL_NATIVE_FIVE', ordinary_uid=1000,
                                checked_real_new_SC32_UNKNOWN_and_new_SCCLIB_strerror=True))
                            report['native_artifacts']['DEFAULTAPI.SCX'].update(executed_this_vm=True, completed_this_vm=True)
                    rows = session_list(harness, 'original-selected-session-before-GUI')
                    active = [row for row in rows if row['active']]
                    if len(active) != 1:
                        raise ValueError('必须取得唯一原选中桌面，不能猜SYSTEM或ID1')
                    original_sid = active[0]['id']
                    report['original_selected_session'] = active[0]
                    baseline, observations = harness.quiet()
                    resources_before = harness.resources()
                    launch = launch_demo(harness, inputs, original_sid)
                    report['ordinary_GUI'] = launch
                    value = work(harness, inputs, report['snapshots'], launch['session'])
                    value = property_workflow(harness, inputs, value, launch['pid'])
                    value = file_workflow(harness, inputs, value, launch['pid'])
                    value = visual_workflow(harness, inputs, value)
                    value = geometry_workflow(harness, inputs, value)
                    value = damage_workflow(harness, inputs, value)
                    value = hidden_workflow(harness, inputs, value, launch['session'], original_sid)
                    exit_and_reclaim(harness, inputs, value, launch, original_sid,
                        baseline, resources_before, observations)
                    for name, body in libraries.items():
                        harness.file('/SYS/LIB/' + name, 'final-real-native-library-' + name, expected=body)
                    report.update(status='DECLARED_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND_PASS',
                        GUI_behavior_verified=True, declared_workflows_completed=True,
                        native_library_generation_this_vm=False, full_M10a2=False)
                    persist()
                except BaseException as error:
                    report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                        message=str(error), traceback=traceback.format_exc()))
                    report['failure_console_tail'] = bytes(guest.console[-16384:]).decode('utf-8', 'replace')
                    report['failure_debug_tail'] = bytes(guest.debug[-16384:]).decode('utf-8', 'replace')
                    path = vm.out / 'ui-gui-failure.stdout.bin'
                    path.write_bytes(bytes(guest.console))
                    report['failure_stdout'] = dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))
                    try:
                        report['failure_registers'] = vm.hmp('info registers')
                        if harness:
                            harness.screen('scui-native-GUI-failure')
                    except Exception as capture:
                        report['failure_capture_error'] = repr(capture)
                    raise
                finally:
                    try:
                        guest.close()
                    except BaseException as close_error:
                        report.setdefault('cleanup_errors', []).append(repr(close_error))
                        report['status'] = 'FAIL_OR_INTERRUPTED'
                        raise
                    finally:
                        preservation()
                        persist()
    except BaseException as error:
        args.out.mkdir(parents=True, exist_ok=True)
        report['status'] = 'FAIL_OR_INTERRUPTED'
        report.setdefault('error', dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc()))
        preservation()
        persist()
        raise
    if report['status'] != 'DECLARED_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND_PASS':
        raise RuntimeError('验证或输入保持失败，已保留真实证据')
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
