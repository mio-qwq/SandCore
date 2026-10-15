#!/usr/bin/env python3
"""单次Windows VM以已验证G1重新编译SCUI，其余首轮证据按真实来源复用。

只接受已冻结的单ui_data或四私有源发布修复；不自举、不重跑
aggregate/platform/text，也不安装或执行本次新生成的SCUI系统库。
"""
import argparse
import copy
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
from verify_m9 import Guest
from m10_guest_boot import unique_symbols, wait_first_desktop
from scl_format import validate
from scd_format import validate_scx

ROOT, BUILD, PACKAGE = first.ROOT, first.BUILD, first.PACKAGE
STATUS = 'DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS'
SCOPE = 'M10A2_UI_PLATFORM_FIRST_WINDOWS_GUEST_ONLY'
CHANGED_SOURCE = 'user/scui/ui_data.c'
EXPECTED_SOURCE_SHA256 = 'd73dc2c8ef3ffafb2bcc493aace143bc7fd59c65fb0872d9dc28e4e3b252e1bd'
EXPECTED_SOURCE_BYTES = 110016
OLD_MODE = 'ONE_SCUI_SOURCE_REFRESH'
PUBLICATION_MODE = 'POST_ACTIVATE_PUBLICATION_REPAIR'
PUBLICATION_PREVIOUS_SHA = '5535d2fdb7c6736aa7321d250bf521564055253b319199b6bf85d7f594c7e993'
PUBLICATION_BUILD_SHA = 'a12e6e6511431a7955cae670cb296f66d7cf73257781d905d10b23b5011dfec5'
PUBLICATION_CURRENT_BUILD_SHA = '9c8a6193f310a814b30576d37535d29095a273bef2a1f2107ebeba7fca4697ba'
PUBLICATION_G1_SHA = 'dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138'
PUBLICATION_SOURCES = {
    'user/scui/ui_tree.c': ('500dc36bd845f2f65fd00d8a7c258e83cc80c95686af50763e91484ee55c61e7', '7d8297e6e9bbc65645e9874af8903a734bb86752fe3926fca86543794ffda226', 28107),
    'user/scui/ui_menu.c': ('e7253c037db1f415ee25b5bac338c2bf2346c76410ff70b4a0b957ba9b9f785a', 'd1d2ab42d501c4b4f00975fb313a4bf31c9d08ed01342e9e4692008ca1a01b95', 55162),
    'user/scui/ui_dispatch.c': ('8c88e5332bcfb0d854e21eed54f0a0088d3bc79fc7f230eb90e387e82a22af07', '40cfb74fab18cc05888684b53a41e14c303613252b80a6157fc7113c821e5e9e', 40700),
    'user/scui/SCUIPRIVATE.H': ('fb3eb69f44f7c69b3abba3bdb8920b797e2dd0e3ce1e42b6390c40312755f4a8', '58cfa650473ebce84e6e4a40c749bc058c494755184a0c3463094bc7963ec46f', 12454),
}
SCUI_CASE = 'actual-G1-library-SCUI.SCL'
HELPERS = ('tools/verify_m10a2_ui_platform.py', 'tools/verify_m10a2_stdlib.py',
           'tools/verify_m10a2_scl.py', 'tools/verify_m9.py', 'tools/scserial.py',
           'tools/m10_guest_boot.py', 'tools/scl_format.py', 'tools/scd_format.py', 'tools/mkcore.py')
SOURCE_ROOTS = (ROOT / 'user', ROOT / 'user/stdlib', ROOT / 'user/sclib',
                ROOT / 'user/runtime', ROOT / 'user/textdata', ROOT / 'tests/m10a2')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def source_snapshot(inputs):
    return {name: value for name, value in inputs.items()
            if name.startswith(('user/', 'tests/m10a2/'))}


def relative_path(name):
    # 来源JSON只提供路径数据，不允许任意外部文件或路径穿越当构建输入。
    if not isinstance(name, str) or '\\' in name or Path(name).is_absolute() or Path(name).drive:
        raise ValueError('非法仓库相对路径：' + repr(name))
    if any(part in ('', '.', '..') for part in name.split('/')):
        raise ValueError('来源路径含空/点/父组件：' + name)
    path = (ROOT / name).resolve()
    if not path.is_relative_to(ROOT.resolve()):
        raise ValueError('来源路径超工作区：' + name)
    return path


def recorded_build(path, expected_sha):
    """旧源码已经改变，只核旧凭证自身/清单，不拿当前树冒充旧树。"""
    path = first.inside(path, BUILD)
    if not re.fullmatch(r'ui-build-[a-z0-9-]+\.json', path.name) or sha(path) != expected_sha:
        raise ValueError('前轮统一build凭证路径/SHA不同')
    proof = read_json(path)
    if (proof.get('scope') != 'M10A2_SCUI_PLATFORM_COMPILER_UNIFIED_HOST_BUILD'
            or proof.get('exit_code') != 0 or proof.get('source_files_modified_during_build') != []):
        raise ValueError('旧build不是完整成功且源保持的本批构建')
    groups, hashes = {}, {}
    for category in ('inputs', 'outputs'):
        entries = proof.get(category)
        if not isinstance(entries, list) or not entries:
            raise ValueError('旧build缺实际' + category)
        values = {}
        for item in entries:
            name, value = item['path'], item['sha256']
            relative_path(name)
            if name in values or not re.fullmatch(r'[0-9a-f]{64}', value):
                raise ValueError('旧build重复路径/非法SHA：' + name)
            if category == 'outputs' and (not isinstance(item.get('bytes'), int) or item['bytes'] < 0):
                raise ValueError('旧build缺实际长度：' + name)
            if name in hashes and hashes[name] != value:
                raise ValueError('旧build input/output相互冲突：' + name)
            values[name] = dict(item)
            hashes[name] = value
        groups[category] = values
    hashes[path.relative_to(ROOT).as_posix()] = sha(path)
    return proof, groups, hashes


def pin(path, directory, expected, size=None, pins=None):
    path = first.inside(path, directory)
    if (not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected)
            or sha(path) != expected or (size is not None and path.stat().st_size != size)):
        raise ValueError('来源真实文件与SHA/长度不符：' + str(path))
    if pins is not None:
        pins[path] = expected
    return path


def changed_entries(old, new, output=False):
    if set(old) != set(new):
        raise ValueError('新旧build路径集合不同，超出只改SCUI的范围')
    return {name: dict(before=old[name], after=new[name]) for name in old
            if old[name]['sha256'] != new[name]['sha256']
            or (output and old[name]['bytes'] != new[name]['bytes'])}


def mode_sources(mode):
    if mode == OLD_MODE:
        return {CHANGED_SOURCE}
    if mode == PUBLICATION_MODE:
        return set(PUBLICATION_SOURCES)
    raise ValueError('未审定的SCUI刷新模式')


def artifact_names(mode, scui=False):
    mode_sources(mode)
    libraries = ('SC32API.SCL', 'SCCLIB.SCL') if mode == PUBLICATION_MODE else ('SC32API.SCL',)
    if scui:
        libraries += ('SCUI.SCL',)
    apps = tuple(value + '.SCX' for value in first.APPLICATIONS)
    if mode == PUBLICATION_MODE:
        apps += ('DEFAULTAPI.SCX', 'SWDEFAULT.SCX')
    return ('SCCC1.SCX',) + libraries + apps


def validate_build_delta(previous, old_groups, current, mode=OLD_MODE):
    new_groups = {kind: {item['path']: item for item in current[kind]}
                  for kind in ('inputs', 'outputs')}
    inputs = changed_entries(old_groups['inputs'], new_groups['inputs'])
    outputs = changed_entries(old_groups['outputs'], new_groups['outputs'], output=True)
    scui_output = (BUILD / 'SCUI.SCL').relative_to(ROOT).as_posix()
    allowed = mode_sources(mode)
    if set(inputs) != allowed or set(outputs) != {scui_output}:
        raise ValueError('build input必须精确等于当前冻结源集合，output只允许SCUI.SCL变化')
    current_bytes = relative_path(CHANGED_SOURCE).stat().st_size
    if mode == OLD_MODE:
        source = inputs[CHANGED_SOURCE]['after']
        if source['sha256'] != EXPECTED_SOURCE_SHA256 or current_bytes != EXPECTED_SOURCE_BYTES:
            raise ValueError('当前ui_data不是已冻结同SID/subject ACL修复')
    else:
        if previous.get('build_report_sha256') != PUBLICATION_BUILD_SHA or previous.get('compiler_default_repair') is not True:
            raise ValueError('发布修复必须从完整build14/新default G1首轮开始')
        for name, (before, after, size) in PUBLICATION_SOURCES.items():
            if (inputs[name]['before']['sha256'] != before or inputs[name]['after']['sha256'] != after
                    or relative_path(name).stat().st_size != size):
                raise ValueError('发布修复源不是已审定的精确新旧字节：' + name)
    if previous.get('core_sha256') != new_groups['outputs'][
            (BUILD / 'fs/SYS/CORE/CORE.SKM').relative_to(ROOT).as_posix()]['sha256']:
        raise ValueError('CORE已经改变，不能复用首轮运行')
    result = dict(allowed_input_changes=sorted(allowed), allowed_output_changes=[scui_output],
                  actual_input_changes=inputs, actual_output_changes=outputs,
                  all_other_recorded_build_inputs_outputs_identical=True)
    if mode == OLD_MODE:
        result['current_changed_source_bytes'] = current_bytes
    else:
        result['current_changed_sources_bytes'] = {name: relative_path(name).stat().st_size for name in sorted(allowed)}
    return result


def include_closure(seeds, compiler_cli=False):
    """只读literal闭包；G1只按已固定SCCC_CLI=1选择真正CLI界面。"""
    pending, seen = [relative_path(name) for name in seeds], {}
    include = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)[ \t]*$')
    directives = re.compile(r'(?m)^[ \t]*#[ \t]*include\b[^\n]*')
    while pending:
        path = first.inside(pending.pop(), ROOT)
        name = path.relative_to(ROOT).as_posix()
        if name in seen:
            continue
        seen[name] = sha(path)
        if path.suffix.lower() not in ('.c', '.h', '.inc'):
            continue
        text = path.read_text(encoding='utf-8')
        text = text.replace('\\\n', '').replace('\\\r\n', '')
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        if compiler_cli and name == 'user/sccc.c':
            if not re.search(r'(?m)^#define[ \t]+SCCC_CLI[ \t]+1[ \t]*$', text):
                raise ValueError('G1入口不再明确SCCC_CLI=1')
        if compiler_cli and name == 'user/s3c.c':
            interface = re.compile(r'#ifdef[ \t]+SCCC_CLI\s*#include[ \t]+"COMPILECLI.inc"\s*'
                                   r'#else\s*#include[ \t]+"COMPILEUI.inc"\s*#endif')
            if len(interface.findall(text)) != 1:
                raise ValueError('G1的CLI/GUI条件界面结构已改变')
            text = interface.sub('#include "COMPILECLI.inc"\n', text)
        parsed = list(include.finditer(text))
        if len(parsed) != len(directives.findall(text)):
            raise ValueError('无法静态闭合非literal include：' + name)
        for item in parsed:
            target = item.group(1) or item.group(2)
            candidates = [path.parent / target] + [folder / target for folder in SOURCE_ROOTS]
            found = next((value for value in candidates if value.is_file()), None)
            if found is None:
                raise ValueError('include闭包缺实际文件：' + name + ' -> ' + target)
            pending.append(found)
    return dict(sorted(seen.items()))


def unchanged_closure(seeds, previous_inputs, current_hashes, compiler_cli=False, mode=OLD_MODE):
    values = include_closure(seeds, compiler_cli=compiler_cli)
    for name, value in values.items():
        if previous_inputs.get(name) != value or current_hashes.get(name) != value:
            raise ValueError('复用artifact的实际include/manifest闭包改变：' + name)
    if mode_sources(mode).intersection(values):
        raise ValueError('不能复用含本次修复源的生成闭包')
    return dict(method='CLI_INTERFACE_EXACT_SELECTION_OTHER_LITERAL_BRANCHES_CONSERVATIVE' if compiler_cli
                else 'CONSERVATIVE_LITERAL_INCLUDE_ALL_BRANCHES_PLUS_MANIFEST',
                selected_SCCC_CLI=1 if compiler_cli else None,
                seeds=seeds, unchanged=True, source_input_sha256=values)


def legacy_g1(blob):
    if len(blob) < 36 or blob[:8] != b'SCX1MIO\0':
        raise ValueError('旧实际G1不是SCX1MIO')
    entry, load, bss, stack, flags, base, author = struct.unpack_from('<7I', blob, 8)
    if (entry >= load or not 0 < load <= 262108 or load + bss > 0x3d0000
            or stack != 131072 or flags or base != 0x400000 or author != 0x004f494d
            or len(blob) != 36 + load):
        raise ValueError('G1旧load/BSS/stack容器合同不符')
    return dict(load_bytes=load, bss_bytes=bss, stack_bytes=stack, entry_rva=entry,
                load_budget=262108, mapped_budget=0x3d0000, exact_file_bytes=len(blob),
                old_container_not_widened=True)


def previous_round(path, data_sha, pins, mode=OLD_MODE):
    path = first.inside(path, ROOT)
    report = read_json(path)
    if (report.get('status') != STATUS or report.get('scope') != SCOPE
            or report.get('current_inputs_unchanged') is not True
            or report.get('source_disk_unchanged') is not True
            or report.get('changed_input_output_paths') != []
            or report.get('source_data_sha256') != data_sha
            or report.get('generated_system_libraries_execution') is not False):
        raise ValueError('旧首轮不是完整PASS/源盘保持/系统库未执行的真实记录')
    # 本工具只从实际原首轮刷新，不让再刷新链把旧来源逐层洗成新来源。
    if report.get('source_reuse') is not None:
        raise ValueError('previous-report必须为原guest首轮，不能是已刷新报告')
    digest = sha(path)
    if mode == PUBLICATION_MODE:
        if digest != PUBLICATION_PREVIOUS_SHA or report.get('compiler_default_repair') is not True:
            raise ValueError('发布修复仅接受完整first-guest05的原真实报告')
        if report.get('native_artifacts', {}).get('SCCC1.SCX', {}).get('sha256') != PUBLICATION_G1_SHA:
            raise ValueError('发布修复必须使用原实际已验证的新G1')
        if (set(report.get('native_libraries', {})) != {'SC32API.SCL', 'SCCLIB.SCL', 'SCUI.SCL'}
                or set(report.get('native_artifacts', {})) != {'SCCC1.SCX', 'SWDEFAULT.SCX', 'DEFAULTAPI.SCX'} | {name + '.SCX' for name in first.APPLICATIONS}):
            raise ValueError('发布修复前轮缺精确三库/新四dynamic App/legacy SWDEFAULT集合')
    mode_sources(mode)
    pins[path] = digest
    old_path = Path(report['build_report'])
    proof, groups, ledger = recorded_build(old_path, report['build_report_sha256'])
    pins[old_path.resolve()] = report['build_report_sha256']
    if report.get('pinned_build_input_output_sha256') != ledger:
        raise ValueError('旧首轮与旧build完整input/output凭证不同')
    cases = {item['name']: item for item in report['cases']}
    if len(cases) != len(report['cases']) or any(item.get('status') != 'PASS' for item in cases.values()):
        raise ValueError('旧首轮重复case或含非PASS结果')
    required = ('HOST-platform-actual-mio-session', 'HOST-official-3013-text-corpus',
                'G1-caller-PLATFORMPROBE-actual-mio-session', 'G1-caller-SCUITEXT-actual-mio-session',
                'G1-aggregate-caller-GCC-callee', 'GCC-aggregate-caller-G1-callee')
    for name in required:
        if cases.get(name, {}).get('exit_code') != 0 or name + '-actual-owner-page-account' not in cases:
            raise ValueError('旧首轮缺真正执行/owner账：' + name)
    if mode == PUBLICATION_MODE:
        machine = cases.get('actual-G1-default-switch-machine-flow-actual-mio', {})
        account = cases.get('actual-G1-default-switch-machine-flow-actual-mio-actual-owner-page-account', {})
        if (machine.get('status') != 'PASS' or machine.get('ordinary_exit_code') != 0
                or machine.get('complete_owner_and_gc_observed') is not True
                or account.get('status') != 'PASS' or account.get('complete_owner_and_gc_observed') is not True):
            raise ValueError('发布修复缺原G1六项default机器main的实际普通退出/回收')
    aggregate = report.get('aggregate_abi', {})
    executions = aggregate.get('executions', [])
    if (len(executions) != 2 or {item.get('name') for item in executions} != set(required[-2:])
            or report.get('generated_library_execution') is not True
            or any(item.get('executed') is not True or item.get('check_count') != 55
                   or item.get('ordinary_uid') != 1000
                   or item.get('actual_caller_stack_balance_checked') is not True for item in executions)):
        raise ValueError('旧首轮缺真实两方向55check/栈平衡')
    for item in cases.values():
        if item.get('stdout_path'):
            pin(Path(item['stdout_path']), path.parent, item['stdout_sha256'], item['stdout_bytes'], pins)
    inputs = report.get('input_sha256')
    if not isinstance(inputs, dict) or not source_snapshot(inputs):
        raise ValueError('旧首轮没有完整源snapshot')
    for name in inputs:
        relative_path(name)
    for folder in ('native-artifacts', 'native-libraries'):
        origin_path = first.inside(path.parent / folder / 'provenance.json', path.parent)
        value = read_json(origin_path)
        if (first.inside(origin_path.parent / value['report'], path.parent) != path
                or value.get('schema') != 1 or value.get('guest_sccc_compiled') is not True
                or value.get('current_status') != STATUS or value.get('generated_library_executed') is not False
                or value.get('libraries') != report['native_libraries']
                or value.get('applications') != report['native_artifacts']
                or value.get('source_input_sha256') != source_snapshot(inputs)
                or value.get('compiler_sha256') != report['native_artifacts']['SCCC1.SCX']['sha256']):
            raise ValueError('旧native provenance与报告/原snapshot不一致')
        pins[origin_path] = sha(origin_path)
    for item in aggregate['artifacts'].values():
        pin(Path(item['path']), path.parent, item['sha256'], item['bytes'], pins)
    return report, proof, groups, cases, digest


def obtain_artifacts(previous, previous_path, cases, hashes, pins, mode=OLD_MODE):
    compiler = previous['native_artifacts']['SCCC1.SCX']
    path = pin(Path(compiler['path']), previous_path.parent, compiler['sha256'], compiler['bytes'], pins)
    contract = legacy_g1(path.read_bytes())
    case = cases.get(compiler.get('compile_case'), {})
    if (compiler.get('format_contract') != contract or compiler.get('guest_sccc_compiled') is not True
            or compiler.get('executed_this_generated_file') is not True
            or case.get('exit_code') != 0 or case.get('generated_sha256') != compiler['sha256']
            or case.get('generated_bytes') != compiler['bytes']):
        raise ValueError('G1缺原真实自举/执行来源')
    blobs, contracts, closures = {'SCCC1.SCX': path.read_bytes()}, {'SCCC1.SCX': contract}, {}
    closures['SCCC1.SCX'] = unchanged_closure(['user/sccc.c'], previous['input_sha256'], hashes, compiler_cli=True, mode=mode)
    for name in artifact_names(mode, scui=True)[1:]:
        library = name.endswith('.SCL')
        item = previous['native_libraries' if library else 'native_artifacts'][name]
        actual = pin(Path(item['path']), previous_path.parent, item['sha256'], item['bytes'], pins)
        case = cases.get(item.get('compile_case'), {})
        if (case.get('exit_code') != 0 or case.get('generated_sha256') != item['sha256']
                or case.get('guest_sccc_compiled') is not True or item.get('guest_sccc_compiled') is not True
                or case.get('compiler_sha256') != compiler['sha256']
                or item.get('compiler_sha256') != compiler['sha256']):
            raise ValueError('artifact缺同一原真实G1成功编译记录：' + name)
        if name == 'SWDEFAULT.SCX':
            contracts[name] = legacy_g1(actual.read_bytes())
            if (item.get('executed_this_generated_file') is not True
                    or item.get('purpose') != 'SIX_DEFAULT_EXPLICIT_NESTED_SWITCH_MACHINE_MAIN_CHECKS'):
                raise ValueError('SWDEFAULT原legacy生成/机器执行边界不成立')
            seeds = ['tests/m10a2/SWDEFAULT.C']
        elif library:
            source, manifest_name = first.LIBRARIES[name]
            declared = read_json(ROOT / 'user' / manifest_name)
            contracts[name] = stdlib.source_contract(validate(actual.read_bytes()), declared)
            if item.get('executed_this_generated_file') is not False or item.get('installed_in_this_vm') is not False:
                raise ValueError('旧首轮库生成/执行混记：' + name)
            seeds = ['user/' + source, 'user/' + manifest_name]
        else:
            stem = name[:-4]
            declared = read_json(ROOT / 'tests/m10a2' / (stem + '.json'))
            contracts[name] = stdlib.source_contract(validate_scx(actual.read_bytes()), declared, True)
            if item.get('executed_this_generated_file') is not (stem in ('PLATFORMPROBE', 'SCUITEXT')):
                raise ValueError('旧app执行边界不同：' + name)
            seeds = ['tests/m10a2/' + stem + '.C', 'tests/m10a2/' + stem + '.json']
        if name != 'SWDEFAULT.SCX' and item.get('format_contract') != contracts[name]:
            raise ValueError('真实artifact与未变manifest不符：' + name)
        blobs[name] = actual.read_bytes()
        if name != 'SCUI.SCL':
            closures[name] = unchanged_closure(seeds, previous['input_sha256'], hashes, mode=mode)
    return blobs, contracts, closures


def reused_case(item, previous_path, previous_sha, old_inputs, runtime):
    value = copy.deepcopy(item)
    value.update(status='REUSED_PASS', original_status='PASS', execution_class='REUSED',
                 executed_in_this_vm=False, source_report=str(previous_path),
                 source_report_sha256=previous_sha, source_case=item['name'],
                 source_input_sha256=old_inputs, runtime_dependencies_origin=runtime)
    # 原stdout、命令、旧VM样本仍是真实历史；不能给它换本次PID/秒数。
    return value


def generation_origin(item, previous_path, previous_sha, old_inputs, closure):
    return dict(mode='REUSED', report=str(previous_path), report_sha256=previous_sha,
                compile_case=item['compile_case'], source_input_sha256=source_snapshot(old_inputs),
                include_manifest_closure=closure, compiler_sha256=item.get('compiler_sha256'),
                generated_this_vm=False)


def write_provenance(report, directory):
    directory.mkdir(parents=True, exist_ok=True)
    generation = {name: item['generation_origin'] for name, item in
                  dict(report['native_artifacts'], **report['native_libraries']).items()}
    value = dict(schema=1, guest_sccc_compiled=True, report='../ui-platform.json',
        qemu_pid=report.get('qemu_pid'), compiler_sha256=report['native_artifacts']['SCCC1.SCX']['sha256'],
        source_input_sha256=source_snapshot(report['input_sha256']),
        source_snapshot_scope='CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN',
        artifact_generation=generation, source_reuse=report['source_reuse'],
        libraries=report['native_libraries'], applications=report['native_artifacts'],
        generated_library_executed=False, generated_library_executed_this_vm=False,
        installed_library_origin='HOST_GCC_BOOTSTRAP_ONLY_FOR_COMPILER_SESSION',
        current_status=report['status'])
    (directory / 'provenance.json').write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


class RefreshHarness(first.PlatformHarness):
    def append(self, item, okay):
        # 只有真实本VM producer进入Harness；REUSED条目在preflight显式组装。
        item.setdefault('execution_class', 'FRESH')
        item.setdefault('executed_in_this_vm', True)
        item.setdefault('qemu_pid', self.vm.process.pid)
        return super().append(item, okay)


def validated_refresh(path, build_report, proof_hashes):
    """供Root GUI reader共用的纯来源校验；不创建文件或启动VM。"""
    path = first.inside(path, ROOT)
    report = read_json(path)
    reuse = report.get('source_reuse', {})
    mode = reuse.get('kind')
    mode_sources(mode)
    if (report.get('status') != STATUS or report.get('scope') != SCOPE
            or reuse.get('schema') != 1
            or report.get('current_inputs_unchanged') is not True
            or report.get('source_disk_unchanged') is not True
            or report.get('changed_input_output_paths') != []
            or report.get('changed_previous_evidence_paths') != []
            or report.get('changed_native_output_paths') != []
            or report.get('generated_system_libraries_execution') is not False
            or report.get('generated_library_execution_this_vm') is not False):
        raise ValueError('refresh没有成立的合并PASS/保持/未执行新库边界')
    if (any(reuse.get(name) is not True for name in ('no_rebootstrap', 'no_aggregate_rerun',
            'no_platform_rerun', 'no_text_rerun')) or reuse.get('refreshed_SCUI_library_executed') is not False
            or reuse.get('generated_system_libraries_installed') is not False):
        raise ValueError('refresh擅自扩大或混淆本轮执行范围')
    proof, pinned, _ = first.verified_build(build_report)
    if mode == PUBLICATION_MODE and sha(build_report) != PUBLICATION_CURRENT_BUILD_SHA:
        raise ValueError('发布修复只接受已冻结的成功build16')
    if (pinned != proof_hashes or report.get('pinned_build_input_output_sha256') != pinned
            or report.get('build_report_sha256') != sha(build_report)):
        raise ValueError('refresh与当前统一build凭证不同')
    inputs = report['input_sha256']
    for name, expected in inputs.items():
        pin(relative_path(name), ROOT, expected)
    previous_path = first.inside(Path(reuse['previous_report']), ROOT)
    previous, _, groups, previous_cases, previous_sha = previous_round(
        previous_path, report['source_data_sha256'], {}, mode)
    if (previous_sha != reuse.get('previous_report_sha256')
            or reuse.get('previous_build_report_sha256') != previous['build_report_sha256']
            or reuse.get('build_delta') != validate_build_delta(previous, groups, proof, mode)):
        raise ValueError('refresh旧来源SHA或唯一build差异不符')
    old_snapshot = source_snapshot(previous['input_sha256'])
    if (reuse.get('previous_source_input_sha256') != old_snapshot
            or reuse.get('current_source_input_sha256') != source_snapshot(inputs)
            or report.get('source_input_snapshot') != source_snapshot(inputs)):
        raise ValueError('新准备snapshot与旧生成snapshot混淆')
    _, previous_contracts, closures = obtain_artifacts(previous, previous_path, previous_cases, inputs, {}, mode)
    if reuse.get('unchanged_artifact_compile_closures') != closures:
        raise ValueError('reuse真实CLI/include/manifest闭包不符')
    runtime = dict(core_sha256=previous['core_sha256'], installed_libraries=previous['installed_libraries'],
                   scope='OLD_FIRST_VM_EXECUTION_LIBRARIES_NOT_REFRESHED_SCUI')
    if mode == PUBLICATION_MODE:
        if report.get('compiler_default_repair') is not True:
            raise ValueError('发布修复丢失前轮真实default修复资格')
        callers = report.get('caller_executions', [])
        if len(callers) != len(previous['caller_executions']):
            raise ValueError('发布修复改变前轮caller执行数量')
        for old, item in zip(previous['caller_executions'], callers):
            if (any(item.get(key) != value for key, value in old.items())
                    or item.get('execution_class') != 'REUSED' or item.get('executed_this_vm') is not False
                    or item.get('source_report_sha256') != previous_sha
                    or item.get('runtime_dependencies_origin') != runtime):
                raise ValueError('前轮caller/运行库来源被改写为本轮执行')
        aggregate = report.get('aggregate_abi', {})
        old_aggregate = previous['aggregate_abi']
        if (any(aggregate.get(key) != value for key, value in old_aggregate.items() if key != 'executions')
                or len(aggregate.get('executions', [])) != len(old_aggregate['executions'])):
            raise ValueError('发布修复改写前轮aggregate产物或执行范围')
        for old, item in zip(old_aggregate['executions'], aggregate['executions']):
            if (any(item.get(key) != value for key, value in old.items())
                    or item.get('execution_class') != 'REUSED' or item.get('executed_in_this_vm') is not False
                    or item.get('source_report_sha256') != previous_sha):
                raise ValueError('前轮aggregate被混记本轮执行')
    cases = {item['name']: item for item in report['cases']}
    if len(cases) != len(report['cases']):
        raise ValueError('refresh用例名重复')
    fresh_names = {'warm-parent-Shell-command-cache', 'warm-existing-task-history',
                   'warm-parent-Shell-background-table', 'warm-existing-path-I/O',
                   'warm-existing-async-desktop-resources', 'refresh-installed-actual-G1',
                   'refresh-SCUI-compile-actual-owner-page-account'}
    fresh_names.update(SCUI_CASE + suffix for suffix in ('', '-output', '-format-contract', '-log', '-map'))
    if {item['name'] for item in cases.values() if item.get('execution_class') == 'FRESH'} != fresh_names:
        raise ValueError('refresh真实case不等于一次SCUI编译及必要warm/get/owner账')
    for item in cases.values():
        if item.get('execution_class') == 'REUSED':
            original = previous_cases.get(item.get('source_case'), {})
            if (item.get('status') != 'REUSED_PASS' or item.get('original_status') != 'PASS'
                    or item.get('executed_in_this_vm') is not False
                    or item.get('source_report_sha256') != previous_sha or original.get('status') != 'PASS'
                    or item.get('exit_code') != original.get('exit_code')
                    or item.get('generated_sha256') != original.get('generated_sha256')):
                raise ValueError('复用case冒充新VM或偏离原结果：' + item['name'])
            if mode == PUBLICATION_MODE:
                expected_name = 'reused-previous-' + original['name'] if original['name'].startswith('warm-') else original['name']
                if (item['name'] != expected_name or item.get('runtime_dependencies_origin') != runtime
                        or item.get('source_input_sha256') != old_snapshot
                        or any(item.get(key) != value for key, value in original.items() if key not in ('status', 'name'))):
                    raise ValueError('发布修复复用case的原stdout/PID/CPU/PIT/依赖字段被改写')
        elif (item.get('execution_class') != 'FRESH' or item.get('status') != 'PASS'
              or item.get('executed_in_this_vm') is not True or item.get('qemu_pid') != report.get('qemu_pid')):
            raise ValueError('refresh有未知/失败case：' + item['name'])
    libraries, apps, metadata = {}, {}, {}
    if mode == PUBLICATION_MODE:
        expected_artifacts = set(artifact_names(mode))
        if (set(report.get('native_artifacts', {})) | set(report.get('native_libraries', {}))) != expected_artifacts | {'SCUI.SCL'}:
            raise ValueError('发布修复未保留精确新三库/四dynamic App/legacy机器产物集合')
    for name in artifact_names(mode):
        library = name.endswith('.SCL')
        item = report['native_libraries' if library else 'native_artifacts'][name]
        source = previous['native_libraries' if library else 'native_artifacts'][name]
        actual = pin(Path(item['path']), path.parent, source['sha256'], source['bytes'])
        origin = item.get('generation_origin', {})
        if (item.get('sha256') != source['sha256'] or item.get('bytes') != source['bytes']
                or (name != 'SWDEFAULT.SCX' and item.get('format_contract') != previous_contracts[name])
                or item.get('compile_case') != source['compile_case']
                or item.get('generated_this_vm') is not False or origin.get('mode') != 'REUSED'
                or origin.get('report_sha256') != previous_sha
                or origin.get('source_input_sha256') != old_snapshot
                or origin.get('include_manifest_closure') != closures[name]
                or item.get('current_compatibility', {}).get('include_manifest_closure') != closures[name]):
            raise ValueError('复用artifact丢原生成来源/闭包：' + name)
        if name in ('SCCC1.SCX', 'SWDEFAULT.SCX'):
            legacy_g1(actual.read_bytes())
        elif library:
            libraries[name] = actual.read_bytes()
        else:
            apps[name[:-4]] = actual.read_bytes()
        metadata[name] = dict(item, path=str(actual), source='ACTUAL_VALIDATED_REFRESH_ORIGIN',
                              generated_this_vm=False, installed_this_vm=False, executed_this_vm=False)
    item = report['native_libraries']['SCUI.SCL']
    actual = pin(Path(item['path']), path.parent, item['sha256'], item['bytes'])
    current_closure = include_closure(['user/scui/SCUI.C', 'user/scui/SCUI.json'])
    contract = stdlib.source_contract(validate(actual.read_bytes()), read_json(ROOT / 'user/scui/SCUI.json'))
    origin, fresh = item.get('generation_origin', {}), cases.get(SCUI_CASE, {})
    compiler_sha = metadata['SCCC1.SCX']['sha256']
    if (item.get('format_contract') != contract or contract['imports'] != 137 or contract['exports'] != 158
            or item.get('generated_this_vm') is not True or item.get('installed_in_this_vm') is not False
            or item.get('executed_this_generated_file') is not False or origin.get('mode') != 'FRESH'
            or origin.get('source_input_sha256') != source_snapshot(inputs)
            or origin.get('include_manifest_closure') != current_closure
            or origin.get('compiler_sha256') != compiler_sha or item.get('compiler_sha256') != compiler_sha
            or origin.get('qemu_pid') != report.get('qemu_pid')
            or fresh.get('execution_class') != 'FRESH' or fresh.get('exit_code') != 0
            or fresh.get('generated_sha256') != item['sha256'] or fresh.get('generated_bytes') != item['bytes']
            or fresh.get('compiler_sha256') != compiler_sha):
        raise ValueError('新SCUI缺本轮真实G1生成/完整来源或被冒充执行')
    libraries['SCUI.SCL'] = actual.read_bytes()
    metadata['SCUI.SCL'] = dict(item, path=str(actual), source='ACTUAL_VALIDATED_REFRESH_G1',
                               installed_this_vm=False, executed_this_vm=False, current_contract=contract)
    for folder in ('native-artifacts', 'native-libraries'):
        provenance_path = first.inside(path.parent / folder / 'provenance.json', path.parent)
        value = read_json(provenance_path)
        expected_generation = {name: item['generation_origin'] for name, item in
                               dict(report['native_artifacts'], **report['native_libraries']).items()}
        if (value.get('schema') != 1 or first.inside(provenance_path.parent / value['report'], path.parent) != path
                or value.get('current_status') != STATUS or value.get('compiler_sha256') != compiler_sha
                or value.get('source_snapshot_scope') != 'CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN'
                or value.get('source_input_sha256') != source_snapshot(inputs)
                or value.get('artifact_generation') != expected_generation
                or value.get('libraries') != report['native_libraries']
                or value.get('applications') != report['native_artifacts']
                or value.get('source_reuse') != reuse or value.get('generated_library_executed') is not False):
            raise ValueError('refresh双目录provenance不符：' + folder)
    return report, libraries, apps, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--previous-report', type=Path, required=True)
    parser.add_argument('--post-activate-publication-repair', action='store_true')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    mode = PUBLICATION_MODE if args.post_activate_publication_repair else OLD_MODE
    if os.name != 'nt':
        parser.error('本工具只用Windows QEMU，不在WSL运行')
    args.out = args.out.resolve()
    if not args.out.is_relative_to((ROOT / 'build').resolve()) or args.out.exists():
        raise ValueError('只允许全新build内输出目录，不覆盖旧报告/证据')
    report = dict(status='PREFLIGHT', scope=SCOPE, cases=[], native_artifacts={}, native_libraries={},
                  screenshots=[], source_reuse={}, caller_executions=[], GUI_behavior_verified=False,
                  full_M10a2=False, source_disk_unchanged=None, current_inputs_unchanged=None,
                  generated_system_libraries_execution=False, generated_library_execution_this_vm=False,
                  execution_scope='SINGLE_REFRESH_VM_OLD_ACTUAL_G1_COMPILES_CURRENT_SCUI_ONLY')
    current_pins, external_pins, native_pins, original = {}, {}, {}, None
    vm = guest = harness = None

    def persist():
        # QemuSession负责首次建立其out目录；preflight失败时才由此保存诊断。
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'ui-platform.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        if report['native_artifacts']:
            for folder in ('native-artifacts', 'native-libraries'):
                write_provenance(report, args.out / folder)

    def preservation():
        changed = [name for name, expected in current_pins.items()
                   if not (ROOT / name).is_file() or sha(ROOT / name) != expected]
        old_changed = [str(path) for path, expected in external_pins.items()
                       if not path.is_file() or sha(path) != expected]
        native_changed = [str(path) for path, expected in native_pins.items()
                          if not path.is_file() or sha(path) != expected]
        report.update(changed_input_output_paths=changed, changed_previous_evidence_paths=old_changed,
                      changed_native_output_paths=native_changed,
                      current_inputs_unchanged=not changed and not old_changed and not native_changed)
        if original is not None:
            report['source_disk_unchanged'] = sha(args.data) == original
        if changed or old_changed or native_changed or report['source_disk_unchanged'] is False:
            report['status'] = 'FAIL_OR_INTERRUPTED'
            report['preservation_error'] = '当前输入/原始旧证据/只读源盘改变'

    try:
        args.data = args.data.resolve(strict=True)
        args.build_report = first.inside(args.build_report, BUILD)
        args.previous_report = first.inside(args.previous_report, ROOT)
        original = sha(args.data)
        previous, old_proof, old_groups, old_cases, previous_sha = previous_round(
            args.previous_report, original, external_pins, mode)
        proof, proof_hashes, outputs = first.verified_build(args.build_report)
        if mode == PUBLICATION_MODE and sha(args.build_report) != PUBLICATION_CURRENT_BUILD_SHA:
            raise ValueError('发布修复只接受已冻结的成功build16')
        if proof.get('source_files_modified_during_build') != []:
            raise ValueError('新build必须显式source_modified=[]')
        delta = validate_build_delta(previous, old_groups, proof, mode)
        prefix = '/TMP/UI-RF-' + secrets.token_hex(3)
        extra, hashes, excluded = first.payload(prefix, proof_hashes, mode == PUBLICATION_MODE)
        for name in HELPERS:
            hashes[name] = sha(ROOT / name)
        hashes['tools/refresh_m10a2_ui_native.py'] = sha(Path(__file__))
        hashes['docs/UI-NATIVE-REFRESH.md'] = sha(ROOT / 'docs/UI-NATIVE-REFRESH.md')
        # previous的全准备树只是历史snapshot；只有实际生成依赖闭包须逐项不变。
        changed_preparation = {}
        for name, old in previous['input_sha256'].items():
            if name not in hashes:
                raise ValueError('当前payload遗漏旧输入：' + name)
            if old != hashes[name]:
                if name not in mode_sources(mode) and not name.startswith(('tools/', 'docs/')):
                    raise ValueError('payload真实源/资源/产物发生额外变化：' + name)
                changed_preparation[name] = dict(before=old, after=hashes[name],
                    kind='SCUI_IMPLEMENTATION' if name in mode_sources(mode) else 'HOST_EVIDENCE_TOOL_ONLY')
        blobs, contracts, closures = obtain_artifacts(previous, args.previous_report, old_cases, hashes, external_pins, mode)
        old_source = source_snapshot(previous['input_sha256'])
        current_scui = include_closure(['user/scui/SCUI.C', 'user/scui/SCUI.json'])
        if not mode_sources(mode) <= set(current_scui):
            raise ValueError('SCUI实际编译闭包未包含修复源')
        for name, value in current_scui.items():
            if hashes.get(name) != value:
                raise ValueError('SCUI源未在当前固定payload内：' + name)
        report.update(status='RUNNING', input_sha256=hashes,
            source_input_snapshot=source_snapshot(hashes), pinned_build_input_output_sha256=proof_hashes,
            source_data_path=str(args.data), source_data_sha256=original,
            build_report=str(args.build_report), build_report_sha256=sha(args.build_report),
            host_build_wall_seconds=proof['wall_seconds'], core_sha256=sha(BUILD / 'fs/SYS/CORE/CORE.SKM'),
            excluded_source_drafts=excluded, ordinary_login_not_simulated=True,
            official_corpus=copy.deepcopy(previous['official_corpus']),
            aggregate_abi=copy.deepcopy(previous['aggregate_abi']),
            generated_library_execution=previous['generated_library_execution'],
            compiler_default_repair=mode == PUBLICATION_MODE,
            generated_library_execution_source='REUSED_PREVIOUS_AGGREGATE_ONLY_NOT_THIS_VM',
            source_reuse=dict(schema=1, kind=mode, previous_report=str(args.previous_report),
                previous_report_sha256=previous_sha, previous_build_report=previous['build_report'],
                previous_build_report_sha256=previous['build_report_sha256'],
                previous_status=STATUS, previous_source_input_sha256=old_source,
                current_source_input_sha256=source_snapshot(hashes), build_delta=delta,
                changed_preparation_inputs=changed_preparation,
                unchanged_artifact_compile_closures=closures, fresh_scui_compile_closure=current_scui,
                no_rebootstrap=True, no_aggregate_rerun=True, no_platform_rerun=True, no_text_rerun=True,
                refreshed_SCUI_library_executed=False, generated_system_libraries_installed=False,
                current_status_meaning='MERGED_VALID_PREVIOUS_EXECUTION_AND_ONE_FRESH_SCUI_COMPILATION'))
        runtime = dict(core_sha256=previous['core_sha256'], installed_libraries=previous['installed_libraries'],
                       scope='OLD_FIRST_VM_EXECUTION_LIBRARIES_NOT_REFRESHED_SCUI')
        report['superseded_scui_cases'] = []
        for item in previous['cases']:
            if item['name'].startswith(SCUI_CASE) or item['name'] == 'installed-GCC-library-preserved-SCUI.SCL':
                report['superseded_scui_cases'].append(reused_case(
                    item, args.previous_report, previous_sha, old_source, runtime))
                continue
            value = reused_case(item, args.previous_report, previous_sha, old_source, runtime)
            if value['name'].startswith('warm-'):
                value['name'] = 'reused-previous-' + value['name']
            report['cases'].append(value)
        for item in report['aggregate_abi']['executions']:
            item.update(execution_class='REUSED', executed_in_this_vm=False,
                        source_report=str(args.previous_report), source_report_sha256=previous_sha)
        for item in previous['caller_executions']:
            value = copy.deepcopy(item)
            value.update(execution_class='REUSED', executed_this_vm=False,
                         source_report=str(args.previous_report), source_report_sha256=previous_sha,
                         runtime_dependencies_origin=runtime)
            report['caller_executions'].append(value)
        for name in artifact_names(mode):
            library = name.endswith('.SCL')
            source = previous['native_libraries' if library else 'native_artifacts'][name]
            directory = args.out / ('native-libraries' if library else 'native-artifacts')
            local = directory / name
            item = copy.deepcopy(source)
            item.update(path=str(local), source='REUSED_ACTUAL_PREVIOUS_G1_BYTES',
                generated_this_vm=False, installed_this_vm=False, executed_this_vm=False,
                execution_class='REUSED', origin_report=str(args.previous_report), origin_report_sha256=previous_sha,
                generation_origin=generation_origin(source, args.previous_report, previous_sha,
                    previous['input_sha256'], closures[name]),
                current_compatibility=dict(compile_closure_unchanged=True,
                    include_manifest_closure=closures[name], current_contract=contracts[name]))
            report['native_libraries' if library else 'native_artifacts'][name] = item
        compiler = prefix + '/ACTUALG1.SCX'
        extra[compiler[1:]] = blobs['SCCC1.SCX']
        report['compiler'] = dict(path=compiler, sha256=previous['native_artifacts']['SCCC1.SCX']['sha256'],
            source='REUSED_ACTUAL_PREVIOUS_G1_EXECUTABLE', generated_this_vm=False,
            origin_report=str(args.previous_report), origin_report_sha256=previous_sha)
        installed = {}
        for name, spec in first.LIBRARIES.items():
            body = (BUILD / name).read_bytes()
            contract = stdlib.source_contract(validate(body), read_json(ROOT / 'user' / spec[1]))
            extra['SYS/LIB/' + name] = body
            installed[name] = dict(path='/SYS/LIB/' + name, sha256=first.digest(body), bytes=len(body),
                                   source='CURRENT_HOST_GCC_BOOTSTRAP', format_contract=contract,
                                   application_test_executed_this_vm=False)
        report['installed_libraries'] = installed
        current_pins = dict(proof_hashes, **hashes)
        symbols = unique_symbols(BUILD / 'core.sym')
        scoped = first.local_elf_symbols(BUILD / 'core.elf')

        def prepared(image):
            result = first.prepare(image, BUILD, extra, runtime_fixtures=False)
            result.update(complete_source_sha256=hashes, excluded_drafts=excluded,
                installed_libraries=installed, installed_actual_previous_G1=report['compiler'],
                public_sdk_files=sorted(name for name in extra if name.startswith('SYS/INC/')),
                copied_license_files={name: first.digest(body) for name, body in extra.items()
                                      if name.startswith('SYS/LICENSE/')},
                generated_libraries_installed=False, new_native_SCUI_installed=False,
                original_data_copies=1, refresh_only=True)
            report['preparation'] = result
            return result

        with first.harness_configuration():
            with QemuSession(BUILD / 'sandcore.img', args.data, args.out, args.accel, True,
                             audio=False, memory=256, data_prepare=prepared) as vm:
                report['qemu_pid'] = vm.process.pid
                # 构造器已建唯一会话目录；此时才复制小artifact，不能预建out
                # 或改用父目录local导致Harness.relative_to(vm.out)失败。
                for name, item in dict(report['native_artifacts'], **report['native_libraries']).items():
                    local = Path(item['path'])
                    local.parent.mkdir(parents=True, exist_ok=True)
                    local.write_bytes(blobs[name])
                    if sha(local) != item['sha256']:
                        raise ValueError('复制小native artifact字节不同：' + name)
                    native_pins[local] = item['sha256']
                guest = Guest(vm)
                try:
                    harness = RefreshHarness(vm, guest, report, symbols, persist, scoped)
                    wait_first_desktop(guest, symbols, report)
                    guest.connect()
                    harness.warm()
                    harness.file(compiler, 'refresh-installed-actual-G1', expected=blobs['SCCC1.SCX'])
                    before, observed_before = harness.quiet()
                    resources_before = harness.resources()
                    local = args.out / 'native-libraries/SCUI.SCL'
                    target = PACKAGE + '/GENERATED/SCUI.SCL'
                    manifest = read_json(ROOT / 'user/scui/SCUI.json')
                    begin = time.monotonic()
                    body, metadata = harness.compile(SCUI_CASE, compiler, prefix + '/SRC/scui/SCUI.C',
                        target, prefix + '/SRC/scui/SCUI.json', manifest, True, local,
                        compiler_sha256=report['compiler']['sha256'])
                    contract = stdlib.source_contract(validate(body), manifest)
                    if contract['imports'] != 137 or contract['exports'] != 158:
                        raise ValueError('新SCUI真实137imports/158exports合同改变')
                    report['native_libraries']['SCUI.SCL'] = dict(metadata, path=str(local),
                        compiler_sha256=report['compiler']['sha256'], format_contract=contract,
                        source='FRESH_ACTUAL_PREVIOUS_G1_CURRENT_SCUI', execution_class='FRESH',
                        generated_this_vm=True, installed_in_this_vm=False, installed_this_vm=False,
                        executed_this_generated_file=False, executed_this_vm=False,
                        generation_origin=dict(mode='FRESH', report=str(args.out / 'ui-platform.json'),
                            compile_case=SCUI_CASE, qemu_pid=vm.process.pid,
                            compiler_sha256=report['compiler']['sha256'],
                            source_input_sha256=source_snapshot(hashes), include_manifest_closure=current_scui,
                            generated_this_vm=True), current_compatibility=dict(current_contract=contract))
                    native_pins[local] = first.digest(body)
                    report['native_artifacts']['SCCC1.SCX']['executed_this_vm'] = True
                    report['native_artifacts']['SCCC1.SCX']['execution_class_this_vm'] = 'FRESH_COMPILER_EXECUTION_ONLY'
                    after, observed_after = harness.quiet()
                    ledger, reclaimed = harness.ledger(before, after, resources_before, harness.resources(), False)
                    harness.append(dict(name='refresh-SCUI-compile-actual-owner-page-account', **ledger,
                        before_observations=observed_before, after_observations=observed_after), reclaimed)
                    report['refresh_generation'] = dict(compile_case=SCUI_CASE, qemu_pid=vm.process.pid,
                        compiler_sha256=report['compiler']['sha256'], generated_sha256=first.digest(body),
                        generated_bytes=len(body), wall_seconds=time.monotonic() - begin,
                        installed=False, executed=False, source_input_sha256=current_scui)
                    harness.screen('ui-native-scui-refresh-desktop')
                    report['status'] = STATUS
                    persist()
                except BaseException as error:
                    report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                        message=str(error), traceback=traceback.format_exc()))
                    report['failure_console_tail'] = bytes(guest.console[-16384:]).decode('utf-8', 'replace')
                    report['failure_debug_tail'] = bytes(guest.debug[-16384:]).decode('utf-8', 'replace')
                    output = args.out / 'ui-native-refresh-failure.stdout.bin'
                    output.write_bytes(bytes(guest.console))
                    report['failure_stdout'] = dict(path=str(output), sha256=sha(output), bytes=output.stat().st_size)
                    try:
                        report['failure_registers'] = vm.hmp('info registers')
                        if harness:
                            harness.screen('ui-native-scui-refresh-failure')
                    except Exception as capture:
                        report['failure_capture_error'] = repr(capture)
                    persist()
                    raise
                finally:
                    guest.close()
                    preservation()
                    persist()
    except BaseException as error:
        report['status'] = 'FAIL_OR_INTERRUPTED'
        report.setdefault('error', dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc()))
        preservation()
        persist()
        raise
    preservation()
    persist()
    if report['status'] != STATUS:
        raise AssertionError('SCUI刷新或输入/原始证据保持失败')
    print(STATUS + ' (REUSED previous execution + FRESH SCUI compile only)', flush=True)


if __name__ == '__main__':
    main()
