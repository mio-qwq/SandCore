#!/usr/bin/env python3
"""一次Windows双盘会话核验SCCLIB/C1..C5/R1；生成库与执行库证据分列。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import struct
import time
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm, process_cpu
from m10_guest_boot import physical_word, unique_symbols, wait_first_desktop
from verify_m10a2_scl import prepare, stable_pages
from verify_m10a2_dynamic import SOURCE_NAMES
from scl_format import NO_ENTRY, validate
from scd_format import validate_scx

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m10a2-stdlib-bootstrap'
PACKAGE = '/HOME/M10STD'
LIBRARY_SOURCES = {
    'SCLDR.SCL': ('runtime/SCLROOT.C', 'runtime/SCLROOT.json'),
    'SC32API.SCL': ('sc32/SC32API.C', 'sc32/SC32API.json'),
    'SCCLIB.SCL': ('stdlib/SCCLIB.C', 'stdlib/SCCLIB.json'),
    'SCLIB.SCL': ('sclib/SCLIB.C', 'sclib/SCLIB.json'),
}
STANDARD_HEADERS = ('stddef.h', 'limits.h', 'stdint.h', 'stdbool.h', 'stdarg.h',
                    'errno.h', 'assert.h', 'string.h', 'ctype.h', 'stdio.h', 'stdlib.h')
PUBLIC_HEADERS = ('SCAPI.H', 'SCTYPES.H', 'SCIO.H', 'SC32API.H', 'SC32GUI.H')
POOL_NAMES = {'records', 'owners', 'handles', 'descriptions', 'native_pool',
              'window_pool', 'view_pool', 'capture_pool', 'endpoint_pool',
              'pipe_pool', 'job_pool', 'transaction_pool'}
COUNT_NAMES = ('wait_pages', 'dir_pages', 'meta_pages', 'allocation_pages',
               'space_pages', 'cache_pages', 'capture_pages')
LIFE_SUFFIX = b'APP_B\nAPP_PLUS\nGUARD_CB\nAPP_A\nGUARD_FINI\n'
UTILITY_BYTES = bytes.fromhex('7fefbe78563412efcdab8904030201ac0204f09f9982')


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def json_read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def source_contract(tables, manifest, application=False):
    """比较完整声明而非只数名字；函数机器长度允许两个编译器各自生成。"""
    kind = {'function': 1, 'data': 2}
    wanted_imports = {(x['name'], x['dependency'], kind[x['kind']], x['size'])
                      for x in manifest['imports']}
    imports = {(x['name'], x['dependency'], x['kind'], x['size'])
               for x in tables['imports']}
    wanted_deps = {(x['name'], x['abi_major'], x['min_minor'], x['min_version'])
                   for x in manifest['dependencies']}
    deps = {(x['name'], x['abi_major'], x['min_minor'], x['min_version'])
            for x in tables['dependencies']}
    problems = []
    if imports != wanted_imports or len(imports) != len(manifest['imports']):
        problems.append('imports')
    if deps != wanted_deps or len(deps) != len(manifest['dependencies']):
        problems.append('dependencies')
    if not application:
        exports = {(x['name'], x['kind']) for x in tables['exports']}
        expected = {(x['name'], kind[x['kind']]) for x in manifest['exports']}
        if exports != expected or len(exports) != len(manifest['exports']):
            problems.append('exports')
        for field in ('name', 'abi_major', 'abi_minor', 'library_version'):
            if tables[field] != manifest[field]:
                problems.append(field)
        for field in ('init', 'fini'):
            if (tables[field + '_rva'] == NO_ENTRY) != (manifest[field] is None):
                problems.append(field)
    if problems:
        raise ValueError('格式表与当前清单不匹配：' + ', '.join(problems))
    return dict(format=tables['format'], imports=len(imports), dependencies=len(deps),
                exports=len(tables.get('exports', [])), contract_exact=True)


def verified_build(path):
    proof = json_read(path)
    if proof.get('exit_code') != 0 or proof.get('source_files_modified_during_build'):
        raise ValueError('统一构建失败或构建期间源码发生变化')
    outputs = {}
    for category in ('inputs', 'outputs'):
        for item in proof[category]:
            target = (ROOT / item['path']).resolve(strict=True)
            if not target.is_relative_to(ROOT) or sha(target) != item['sha256']:
                raise ValueError('当前文件不匹配构建记录：' + item['path'])
            if category == 'outputs':
                outputs[target] = item
    needed = ('sandcore.img', 'fs/SYS/CORE/CORE.SKM', 'fs/bin/sccc.scx',
              'SCLDR.SCL', 'SC32API.SCL', 'SCCLIB.SCL', 'SCLIB.SCL',
              'STDLIBPROBE.SCX', 'STDLIBGUARD.SCL')
    for name in needed:
        if (BUILD / name).resolve(strict=True) not in outputs:
            raise ValueError('构建记录缺少本次必要产物：' + name)
    return proof, outputs


def source_payload(prefix):
    names = set(SOURCE_NAMES) | set(PUBLIC_HEADERS)
    for folder in ('runtime', 'sc32', 'guiapi', 'stdlib', 'sclib'):
        for path in (ROOT / 'user' / folder).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.c', '.h', '.inc', '.json', '.asm'):
                name = path.relative_to(ROOT / 'user').as_posix()
                # 未接线的并行broker稿不属于这批冻结库，不能造成假输入漂移。
                if name != 'sc32/SC32BROKERWIRE.H':
                    names.add(name)
    extra, hashes = {}, {}
    for name in sorted(names):
        path = ROOT / 'user' / name
        extra[prefix[1:] + '/SRC/' + name] = path.read_bytes()
        hashes['user/' + name] = sha(path)
    # 私有ROOT/库头只随完整SRC保存；公共SDK不安装私有换栈builtin声明。
    for name in PUBLIC_HEADERS:
        extra['SYS/INC/' + name] = (ROOT / 'user' / name).read_bytes()
    for name in STANDARD_HEADERS:
        extra['SYS/INC/' + name] = (ROOT / 'user/stdlib' / name).read_bytes()
    extra['SYS/INC/SCLIB.H'] = (ROOT / 'user/sclib/SCLIB.H').read_bytes()
    for name in ('STDLIBPROBE.C', 'STDLIBPROBE.json', 'STDLIBGUARD.C', 'STDLIBGUARD.json',
                 'SHARERUN.C', 'SHARERUN.json'):
        path = ROOT / 'tests/m10a2' / name
        extra[prefix[1:] + '/' + name] = path.read_bytes()
        hashes['tests/m10a2/' + name] = sha(path)
    return extra, hashes


def native_bundle(directory, hashes):
    """第二轮必须取首轮真正get回的字节与compile记录，不能把GCC复制改名。"""
    directory = directory.resolve(strict=True)
    provenance = json_read(directory / 'provenance.json')
    if provenance.get('schema') != 1 or not provenance.get('guest_sccc_compiled'):
        raise ValueError('原生库目录没有客体生成来源记录')
    for name, expected in provenance['source_input_sha256'].items():
        if name not in hashes or hashes[name] != expected:
            raise ValueError('原生库生成后的源已改变：' + name)
    origin_path = (directory / provenance['report']).resolve(strict=True)
    if not origin_path.is_relative_to(directory.parent):
        raise ValueError('原生来源报告不在首轮输出目录内')
    origin = json_read(origin_path)
    if not origin.get('source_disk_unchanged') or not origin.get('current_inputs_unchanged'):
        raise ValueError('首轮源盘/输入保持条件没有成立')
    cases = {x['name']: x for x in origin['cases']}

    def obtain(item):
        path = (directory / item['path']).resolve(strict=True)
        if not path.is_relative_to(directory.parent) or sha(path) != item['sha256']:
            raise ValueError('客体生成文件与来源摘要不同：' + item['path'])
        case = cases.get(item['compile_case'])
        if not case or case['status'] != 'PASS' or case.get('exit_code') != 0:
            raise ValueError('缺少真实成功编译记录：' + item['compile_case'])
        if case.get('generated_sha256') != item['sha256']:
            raise ValueError('实际编译记录与生成文件不同')
        return path.read_bytes()

    libraries = {}
    for name, (_, manifest_name) in LIBRARY_SOURCES.items():
        item = provenance['libraries'][name]
        libraries[name] = obtain(item)
        source_contract(validate(libraries[name]), json_read(ROOT / 'user' / manifest_name))
    probe = obtain(provenance['application'])
    source_contract(validate_scx(probe), json_read(ROOT / 'tests/m10a2/STDLIBPROBE.json'), True)
    guard = obtain(provenance['guard'])
    source_contract(validate(guard), json_read(ROOT / 'tests/m10a2/STDLIBGUARD.json'))
    return libraries, probe, guard, dict(directory=str(directory),
        provenance_sha256=sha(directory / 'provenance.json'), report=str(origin_path),
        report_sha256=sha(origin_path), origin_status=origin['status'],
        guest_sccc_compiled=True, executed_in_this_vm=False)


class Harness:
    def __init__(self, vm, guest, report, symbols, persist, reused=None):
        self.vm, self.guest, self.report = vm, guest, report
        self.symbols, self.persist = symbols, persist
        self.reused = reused or {}
        self.shared_runner = None
        self.pools = []
        for line in (BUILD / 'core.sym').read_text(encoding='ascii').splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2] in POOL_NAMES:
                self.pools.append((fields[2] + '@' + fields[0], int(fields[0], 16)))

    def resources(self):
        values = {}
        for label, address in self.pools:
            # i386 objpool_t的当前真实字段；同名私有池用地址区分，不能合并丢账。
            words = re.findall(r'0x([0-9a-fA-F]{8})',
                               self.vm.hmp('xp /4wx 0x%08x' % (address + 1096)))
            if len(words) != 4:
                raise ValueError('对象池只读计账格式改变')
            values[label] = dict(zip(('objects', 'pages', 'peak_pages', 'high_water'),
                                     (int(word, 16) for word in words)))
        for name in COUNT_NAMES:
            if name in self.symbols:
                values[name] = physical_word(self.vm, self.symbols[name])
        values['desktop'] = self.desktop_resources()
        return values

    def desktop_resources(self):
        # 仅HMP读当前core。desktop_state_t全为4B整数/32位指针或字节数组，
        # desk_item292B、image_reader164B、icon_cache260B；完整输入摘要已核。
        # 读真实所属页/候选与映射状态，不能凭2025这个差额给缓存豁免。
        state = physical_word(self.vm, self.symbols['selected_state'])
        result = dict(state_pointer=state,
            native_backbuffer=physical_word(self.vm, self.symbols['native_bb']),
            width=physical_word(self.vm, self.symbols['gfx_width']),
            height=physical_word(self.vm, self.symbols['gfx_height']),
            pending_images=physical_word(self.vm, self.symbols['desktop_io_pending']))
        if not state:
            return result
        for name, offset in (('pages', 32560), ('map_width', 32572), ('map_height', 32576),
                             ('needs_flush', 32580), ('attempted', 32584), ('error', 32588),
                             ('background_reader_phase', 24800)):
            result[name] = physical_word(self.vm, state + offset)
        for name, offset in (('background', 24680), ('scaled_background', 24696)):
            words = re.findall(r'0x([0-9a-fA-F]{8})',
                               self.vm.hmp('xp /4wx 0x%08x' % (state + offset)))
            if len(words) != 4:
                raise ValueError('桌面字节观察格式改变')
            result[name] = dict(zip(('pixels', 'width', 'height', 'pages'),
                                    (int(word, 16) for word in words)))
        return result

    def warm_desktop(self):
        # 首桌面帧只表示可交互；旧壁纸reader仍按16KiB异步加载，完成后
        # 才生成当前模式背景。等待该旧资源事实，不执行任何新SCL测试。
        before, observed_before = stable_pages(self.vm, self.symbols)
        initial = self.desktop_resources()
        started = time.monotonic()
        observations, last, ready = [], None, False
        while time.monotonic() - started < 90:
            if self.guest.errors or self.vm.process.poll() is not None:
                raise RuntimeError('等待旧桌面资源期间会话中断')
            value = self.desktop_resources()
            stamp = json.dumps(value, sort_keys=True)
            if stamp != last:
                observations.append(dict(seconds=time.monotonic() - started, **value))
                last = stamp
            ready = bool(value.get('state_pointer') and value.get('attempted')
                and not value.get('needs_flush') and not value.get('pending_images')
                and not value.get('background_reader_phase')
                and (not value['background']['pixels'] or
                     (value['map_width'] == value['width'] and value['map_height'] == value['height'])))
            if ready:
                break
            self.guest.stop.wait(.2)
        after, observed_after = stable_pages(self.vm, self.symbols)
        final = self.desktop_resources()
        item = dict(name='warm-existing-async-desktop-resources',
            source='kernel/desktop.c::desktop_poll/desktop_wallpaper; current desktop owns scaled_background',
            before_physical_pages=before, after_physical_pages=after, physical_delta=after - before,
            before_desktop=initial, after_desktop=final, observations=observations,
            before_observations=observed_before, after_observations=observed_after,
            wall_seconds=time.monotonic() - started, complete_existing_desktop_resources=ready,
            layout=dict(state_bytes=32660, background_offset=24680, scaled_background_offset=24696,
                        background_reader_phase_offset=24800, pages_offset=32560, map_offset=32572))
        item['desktop_owned_page_delta'] = final.get('pages', 0) - initial.get('pages', 0)
        item['attributed_all_delta'] = item['physical_delta'] == item['desktop_owned_page_delta']
        self.append(item, ready)
        self.report['warm_observations'].append(item)
        self.persist()

    def append(self, item, okay):
        item['status'] = 'PASS' if okay else 'FAIL'
        self.report['cases'].append(item)
        self.report.pop('active_case', None)
        self.persist()
        print(item['name'] + ': ' + item['status'], flush=True)
        if not okay:
            raise AssertionError(item['name'])
        return item

    def file(self, path, label, expected=None, local=None):
        destination = local or self.vm.out / ('file-' + label)
        begin, cpu = time.monotonic(), process_cpu(self.vm.process)
        transfer = self.guest.client.get(path, destination)
        blob = destination.read_bytes()
        item = dict(name=label, guest=path, path=str(destination), bytes=len(blob),
                    sha256=digest(blob), generation=transfer['generation'],
                    wall_seconds=time.monotonic() - begin,
                    qemu_cpu_seconds=process_cpu(self.vm.process) - cpu)
        if expected is not None:
            item.update(expected_bytes=len(expected), expected_sha256=digest(expected),
                        expected_hex=expected.hex())
        self.append(item, expected is None or blob == expected)
        return blob

    def run(self, name, command, expected=0, contains=(), forbidden=(), timeout=180,
            reclaim=False, files=()):
        if name in self.reused:
            source = self.reused[name]
            item = dict(name=name, status='REUSED_PASS', command=command,
                source_report=self.report['reused_evidence']['report'], source_case=name,
                source_report_sha256=self.report['reused_evidence']['sha256'],
                exit_code=source['exit_code'], executed_in_this_vm=False,
                source_physical_delta=source.get('physical_delta'),
                unchanged_executable_library_core_and_source_inputs=True)
            self.report['cases'].append(item)
            self.persist()
            print(name + ': REUSED_PASS', flush=True)
            return item
        item = dict(name=name, command=command, expected_exit=expected)
        if reclaim:
            before, observations = stable_pages(self.vm, self.symbols)
            item.update(before_physical_pages=before, before_observations=observations,
                        resources_before=self.resources())
        start = len(self.guest.console)
        begin, cpu = time.monotonic(), process_cpu(self.vm.process)
        self.report['active_case'] = item
        self.persist()
        print('RUN ' + name, flush=True)
        try:
            code, output, wall, used = self.guest.command(command, timeout=timeout)
        except BaseException as error:
            output = bytes(self.guest.console[start:])
            item.update(exit_code=None, output=output.decode('utf-8', 'replace'),
                        stdout_sha256=digest(output), stdout_bytes=len(output),
                        wall_seconds=time.monotonic() - begin, error=repr(error))
            self.report['cases'].append(dict(item, status='FAIL_OR_INTERRUPTED'))
            self.persist()
            raise
        stdout = self.vm.out / ('case-%03d.stdout.bin' % len(self.report['cases']))
        stdout.write_bytes(output)
        item.update(exit_code=code, output=output.decode('utf-8', 'replace'),
                    stdout_path=str(stdout), stdout_sha256=digest(output), stdout_bytes=len(output),
                    wall_seconds=wall, qemu_cpu_seconds=used)
        okay = code == expected and all(marker in output for marker in contains)
        okay = okay and all(marker not in output for marker in forbidden)
        # 返回码/marker失败时先保完整真实输出，不让缺失文件掩盖原故障。
        if not okay:
            self.append(item, False)
        for path, label, content in files:
            self.file(path, label, content)
        if reclaim:
            after, observations = stable_pages(self.vm, self.symbols)
            item.update(after_physical_pages=after, after_observations=observations,
                        physical_delta=after - before, resources_after=self.resources())
            okay = okay and before == after
        return self.append(item, okay)

    def warm(self):
        self.report['warm_observations'] = []
        steps = (
            ('parent-Shell-command-cache', 'true',
             'user/m9/sh.c::command_frames：按命令层首次分配，归父Shell至其退出'),
            ('existing-task-history', 'ls ' + PACKAGE + '/HOST',
             'kernel/task_store.c records OBJPOOL_KEEP_IDLE：既有PID历史；当前页逐项留账'),
            ('parent-Shell-background-table', 'echo STDLIB_BG_WARM & wait',
             'user/m9/sh.c::background_reserve：16*8B首次后台表由父Shell保留'),
            ('existing-path-I/O', 'printf warm > ' + PACKAGE + '/WARM.DAT; cat ' + PACKAGE + '/WARM.DAT',
             '只预热既有路径/旧SCX与Shell重定向；不执行新库测试也不抹页差额'),
        )
        for name, command, reason in steps:
            before, observed_before = stable_pages(self.vm, self.symbols)
            resources_before = self.resources()
            item = self.run('warm-' + name, command)
            after, observed_after = stable_pages(self.vm, self.symbols)
            self.report['warm_observations'].append(dict(name=name, source=reason,
                before=before, after=after, delta=after - before,
                observations_before=observed_before, observations_after=observed_after,
                resources_before=resources_before, resources_after=self.resources(),
                command_case=item['name'], attributed_all_delta=False))
            self.persist()

        self.warm_desktop()

    def dialog(self, name, app, pipe_input):
        mode = 'pipe' if pipe_input else 'tty'
        command = app + ' ' + mode
        if pipe_input:
            command = '(read line; printf \'%s\\n\' "$line") | ' + command
        before, observed = stable_pages(self.vm, self.symbols)
        item = dict(name=name, command=command, before_physical_pages=before,
                    before_observations=observed, resources_before=self.resources(),
                    input='Q\n', channel='EXTERNAL_SYSTEM_MANAGEMENT_STDIN',
                    expected_exit=0, physical_keyboard_verified=False)
        self.report['active_case'] = item
        self.persist()
        print('RUN ' + name, flush=True)
        begin, cpu = time.monotonic(), process_cpu(self.vm.process)
        start = len(self.guest.console)
        try:
            marker, start = self.guest.dialog(command, rb'STDLIB_INPUT_READY ' + mode.encode() + rb'\r?\n')
        except BaseException as error:
            output = bytes(self.guest.console[start:])
            item.update(exit_code=None, output=output.decode('utf-8', 'replace'),
                        stdout_bytes=len(output), stdout_sha256=digest(output),
                        wall_seconds=time.monotonic() - begin, error=repr(error))
            self.report['cases'].append(dict(item, status='FAIL_OR_INTERRUPTED'))
            self.persist()
            raise
        item['ready_seconds'] = time.monotonic() - begin
        # READY已经flush。短观察期内不送输入，确认实际reader未把空流作EOF；
        # 然后只经已有外部管理stdin送一行，不声称真实GUI/PS2键盘覆盖。
        self.guest.stop.wait(.2)
        interim = bytes(self.guest.console[start:])
        # 串口可以回显命令文本，其中也有echo的marker字样；只认实际数字完成行。
        premature = re.search(rb'(?:\r?\n)' + re.escape(marker) + rb':[0-9]+\r?\n', interim) is not None
        premature = premature or b'STDLIB_' + mode.upper().encode() + b'_PASS' in interim
        item['completed_before_input'] = premature
        if premature:
            self.append(item, False)
        self.guest.client.input(b'Q\n')
        code, output = self.guest.finish_dialog(marker, start, timeout=120)
        after, observations = stable_pages(self.vm, self.symbols)
        item.update(exit_code=code, output=output.decode('utf-8', 'replace'),
                    stdout_bytes=len(output), stdout_sha256=digest(output),
                    wall_seconds=time.monotonic() - begin,
                    qemu_cpu_seconds=process_cpu(self.vm.process) - cpu,
                    after_physical_pages=after, after_observations=observations,
                    physical_delta=after - before, resources_after=self.resources())
        stdout = self.vm.out / ('case-%03d.stdout.bin' % len(self.report['cases']))
        stdout.write_bytes(output)
        item['stdout_path'] = str(stdout)
        return self.append(item, code == 0 and b'STDLIB_' + mode.upper().encode() +
                           b'_PASS failures=0' in output and b'STDLIB FAIL' not in output and before == after)

    def suite(self, app, label):
        bad = (b'STDLIB FAIL', b'STDLIB_INIT_EXIT_MAIN_RAN', b'STDLIB_RECURSIVE_RETURNED')
        self.run(label + '-core', app + ' core', contains=(b'STDLIB_CORE_PASS failures=0',),
                 forbidden=bad, reclaim=True)
        self.run(label + '-stdio-six-modes', app + ' stdio',
                 contains=(b'STDLIB_STDIO_PASS failures=0',), forbidden=bad, reclaim=True,
                 files=((PACKAGE + '/STDIO.DAT', label + '-stdio-data', b'1234'),))
        for mode, code, filename in (('return', 37, 'RETURN.LOG'), ('exit', 38, 'EXIT.LOG')):
            self.run(label + '-lifecycle-' + mode, app + ' ' + mode, expected=code,
                contains=(('STDLIB_LIFECYCLE_ARMED ' + mode + ' exit=' + str(code)).encode(),),
                forbidden=bad, reclaim=True, files=((PACKAGE + '/' + filename,
                label + '-' + mode + '-events', b'MAIN:' + mode.encode() + b'\n' + LIFE_SUFFIX),))
        self.run(label + '-exit-during-guard-init', app + ' init-exit', expected=42,
                 forbidden=bad, reclaim=True,
                 files=((PACKAGE + '/INIT.LOG', label + '-init-events', b'INIT_EXIT\n'),))
        self.run(label + '-recursive-exit-is-abnormal', app + ' recursive', expected=1,
                 contains=(b'STDLIB_RECURSIVE_ARMED', b'STDLIB_RECURSIVE_CALLBACK',
                           b'SCL: invalid normal termination'), forbidden=bad, reclaim=True,
                 files=((PACKAGE + '/RECURSE.LOG', label + '-recursive-no-fini', b''),))
        if not self.shared_runner:
            raise ValueError('缺少真实共享STD启动程序')
        self.run(label + '-shared-standard-position', self.shared_runner + ' ' + app + ' > ' + PACKAGE + '/SHARED.OUT',
                 forbidden=bad, reclaim=True, files=(
                     (PACKAGE + '/SHARED.OUT', label + '-shared-bytes', b'BA'),
                     (PACKAGE + '/SHARED.RES', label + '-shared-result',
                      b'STDLIB_SHARED_PASS position=2 fpos=2\n')))
        self.dialog(label + '-empty-pipe-waits-and-real-EOF', app, True)
        self.dialog(label + '-empty-management-TTY-waits', app, False)

    def compile(self, name, compiler, source, target, manifest_path, declared, library, local,
                compiler_sha256=None):
        command = compiler + ' ' + source + ' ' + target + (' --scl ' if library else ' --dynamic ') + manifest_path
        marker = b'SCCC OK SCL NATIVE' if library else b'SCCC OK SCX DYNAMIC'
        item = self.run(name, command, contains=(marker,), timeout=300)
        blob = self.file(target, name + '-output', local=local)
        try:
            tables = validate(blob) if library else validate_scx(blob)
            contract = source_contract(tables, declared, not library)
        except BaseException as error:
            self.append(dict(name=name + '-format-contract', generated_sha256=digest(blob),
                             generated_bytes=len(blob), error=repr(error)), False)
        item.update(generated_sha256=digest(blob), generated_bytes=len(blob),
                    generated_path=str(local), guest_target=target, guest_sccc_compiled=True,
                    executed_this_generated_file=False, format_contract=contract,
                    compiler_sha256=compiler_sha256 or self.report['compiler']['sha256'])
        self.append(dict(name=name + '-format-contract', **contract,
                         generated_sha256=digest(blob)), True)
        self.file(target + '.log', name + '-log', marker + b'\n')
        self.file(target + '.map', name + '-map')
        self.persist()
        return blob, dict(path=str(local.relative_to(self.vm.out / 'native-libraries'))
                         if local.is_relative_to(self.vm.out / 'native-libraries')
                         else '../' + local.relative_to(self.vm.out).as_posix(),
                         sha256=digest(blob), bytes=len(blob), compile_case=name,
                         guest_target=target, guest_sccc_compiled=True)

    def selfcompile(self, compiler, prefix, artifacts):
        target = prefix + '/BUILT/SCCC1.SCX'
        item = self.run('guest-SCCC-self-build-G1', compiler + ' ' + prefix +
                        '/SRC/sccc.c ' + target, contains=(b'SCCC OK load=',), timeout=300)
        blob = self.file(target, 'guest-SCCC-G1-output', local=artifacts / 'SCCC1.SCX')
        if len(blob) < 36 or blob[:8] != b'SCX1MIO\0':
            self.append(dict(name='G1-real-legacy-SCX-container', bytes=len(blob)), False)
        entry, load, bss, stack, flags, base, author = struct.unpack_from('<7I', blob, 8)
        valid = (0 < load <= 262108 and entry < load and load + bss <= 0x3d0000
                 and len(blob) == 36 + load and 4096 <= stack <= 131072 and stack % 4096 == 0
                 and flags == 0 and base == 0x00400000 and author == 0x004f494d)
        self.append(dict(name='G1-real-legacy-SCX-container', load_bytes=load, bss_bytes=bss,
                         stack_bytes=stack, entry_rva=entry, exact_file_bytes=len(blob),
                         load_budget=262108, mapped_budget=0x3d0000), valid)
        item.update(generated_sha256=digest(blob), generated_bytes=len(blob),
                    generated_path=str(artifacts / 'SCCC1.SCX'), guest_sccc_compiled=True)
        self.file(target + '.log', 'guest-SCCC-G1-log')
        self.file(target + '.map', 'guest-SCCC-G1-map')
        self.run('G1-standard-app-directory', 'mkdir -p ' + PACKAGE + '/G1')
        self.run('G1-GCC-guard-for-AL', 'cp ' + PACKAGE + '/HOST/STDLIBGUARD.SCL ' +
                 PACKAGE + '/G1/STDLIBGUARD.SCL')
        app = PACKAGE + '/G1/STDLIBPROBE.SCX'
        app_blob, _ = self.compile('actual-G1-builds-standard-app', target,
            prefix + '/STDLIBPROBE.C', app, prefix + '/STDLIBPROBE.json',
            json_read(ROOT / 'tests/m10a2/STDLIBPROBE.json'), False,
            artifacts / 'STDLIBPROBE-G1.SCX', compiler_sha256=digest(blob))
        self.run('actual-G1-standard-app-executes', app + ' core',
                 contains=(b'STDLIB_CORE_PASS failures=0',), forbidden=(b'STDLIB FAIL',), reclaim=True)
        self.report['guest_compiler_G1'] = dict(sha256=digest(blob), bytes=len(blob),
            generated_in_this_vm=True, executed_in_this_vm=True, load_bytes=load,
            bss_bytes=bss, mapped_budget=0x3d0000, application_sha256=digest(app_blob),
            actual_G1_compiled_application_executed=True, GCC_guard=True)
        self.persist()

    def screen(self, name):
        path = self.vm.out / (name + '.ppm')
        self.vm.hmp('screendump "' + path.as_posix() + '"')
        self.report['screenshots'].append(png_from_ppm(path))
        path.unlink()
        self.persist()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    parser.add_argument('--native-libraries', type=Path)
    parser.add_argument('--skip-library-build', action='store_true')
    parser.add_argument('--guest-guard', action='store_true',
                        help='第二轮完整模式用客体Guard；另保留GCC Guard的AL检查')
    parser.add_argument('--utility-source', type=Path)
    parser.add_argument('--reuse-report', type=Path,
                        help='只复用输入未变的已通过HOST六组，失败/后续行为仍实际运行')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('本主控只启动Windows QEMU；不要在WSL运行它')
    if args.guest_guard and not args.native_libraries:
        parser.error('--guest-guard需要首轮真实原生库来源目录')
    args.out = args.out.resolve()
    args.data = args.data.resolve(strict=True)
    args.build_report = args.build_report.resolve(strict=True)
    if args.out.exists():
        raise FileExistsError('不能覆盖已有VM/失败证据目录')
    proof, outputs = verified_build(args.build_report)
    prefix = '/TMP/STD-' + secrets.token_hex(3)
    compiler = prefix + '/SCCC0.SCX'
    extra, hashes = source_payload(prefix)
    hashes['tools/verify_m10a2_stdlib.py'] = sha(Path(__file__))
    manifests = {name: json_read(ROOT / 'user' / spec[1]) for name, spec in LIBRARY_SOURCES.items()}
    host_probe = (BUILD / 'STDLIBPROBE.SCX').read_bytes()
    host_guard = (BUILD / 'STDLIBGUARD.SCL').read_bytes()
    source_contract(validate_scx(host_probe), json_read(ROOT / 'tests/m10a2/STDLIBPROBE.json'), True)
    source_contract(validate(host_guard), json_read(ROOT / 'tests/m10a2/STDLIBGUARD.json'))
    libraries = {name: (BUILD / name).read_bytes() for name in LIBRARY_SOURCES}
    native_info = None
    native_probe = native_guard = None
    if args.native_libraries:
        libraries, native_probe, native_guard, native_info = native_bundle(args.native_libraries, hashes)
    installed = {}
    for name, blob in libraries.items():
        contract = source_contract(validate(blob), manifests[name])
        extra['SYS/LIB/' + name] = blob
        installed[name] = dict(bytes=len(blob), sha256=digest(blob), format_contract=contract,
                               source='GUEST_SCCC_PRIOR_VM' if native_info else 'HOST_GCC_BOOTSTRAP')
    extra[compiler[1:]] = (BUILD / 'fs/bin/sccc.scx').read_bytes()
    extra[PACKAGE[1:] + '/HOST/STDLIBPROBE.SCX'] = host_probe
    extra[PACKAGE[1:] + '/HOST/STDLIBGUARD.SCL'] = host_guard
    # 测试文件先存在，避免把新增目录索引页误归应用heap；实际模式仍真截断/写。
    for name in ('STDIO.DAT', 'RETURN.LOG', 'EXIT.LOG', 'INIT.LOG', 'RECURSE.LOG',
                 'SHARED.OUT', 'SHARED.RES', 'WARM.DAT'):
        extra[PACKAGE[1:] + '/' + name] = b''
    if native_info:
        extra[PACKAGE[1:] + '/NATIVE/STDLIBPROBE.SCX'] = native_probe
        extra[PACKAGE[1:] + '/NATIVE/STDLIBGUARD.SCL'] = host_guard
        extra[PACKAGE[1:] + '/GUARDN/STDLIBPROBE.SCX'] = native_probe
        extra[PACKAGE[1:] + '/GUARDN/STDLIBGUARD.SCL'] = native_guard
    utility = None
    if args.utility_source:
        source = args.utility_source.resolve(strict=True)
        manifest_path = source.with_suffix('.json')
        manifest = json_read(manifest_path)
        if manifest['name'] != 'UTILPROBE.SCX' or manifest['kind'] != 'application':
            raise ValueError('可选utility入口必须有正式UTILPROBE清单')
        extra[prefix[1:] + '/UTILPROBE.C'] = source.read_bytes()
        extra[prefix[1:] + '/UTILPROBE.json'] = manifest_path.read_bytes()
        for path in (source, manifest_path):
            if not path.is_relative_to(ROOT):
                raise ValueError('utility源必须来自当前工作区，以保输入摘要可核对')
            hashes[path.relative_to(ROOT).as_posix()] = sha(path)
        utility = manifest
    original = sha(args.data)
    symbols = unique_symbols(BUILD / 'core.sym')
    if 'pf_used' not in symbols:
        raise ValueError('缺少当前core的唯一物理页计数符号')
    report = dict(status='RUNNING', scope='SCCLIB_C1_C5_R1_DIRECTED_WINDOWS_GUEST',
        cases=[], installed_libraries=installed, native_install_provenance=native_info,
        input_sha256=hashes, source_data_sha256=original, build_report_sha256=sha(args.build_report),
        host_build_wall_seconds=proof['wall_seconds'], core_sha256=sha(BUILD / 'fs/SYS/CORE/CORE.SKM'),
        compiler=dict(path=compiler, sha256=sha(BUILD / 'fs/bin/sccc.scx'), source='HOST_GCC_SCCC0'),
        host_application=dict(sha256=digest(host_probe), bytes=len(host_probe), source='HOST_GCC_BOOTSTRAP'),
        GCC_guard=dict(sha256=digest(host_guard), bytes=len(host_guard),
                       poisoned_EAX_for_Bool=True, valid_return_register='AL'),
        native_guard_executed=False, library_generation_skipped=args.skip_library_build,
        screenshots=[], execution_scope='EXTERNAL_SYSTEM_RING3_MANAGEMENT_SESSION',
        physical_keyboard_verified=False, GUI_behavior_verified=False,
        full_Phase3=False, full_M10a2=False, unrelated_prior_evidence='REUSED_NOT_RERUN')
    reused = {}
    if args.reuse_report:
        previous_path = args.reuse_report.resolve(strict=True)
        previous = json_read(previous_path)
        if native_info or previous['installed_libraries'] != installed or previous['core_sha256'] != report['core_sha256']:
            raise ValueError('只可复用相同HOST库与主核的证据')
        if previous['host_application'] != report['host_application'] or previous['GCC_guard'] != report['GCC_guard']:
            raise ValueError('复用应用或Guard字节已变')
        if not previous.get('current_inputs_unchanged') or not previous.get('source_disk_unchanged'):
            raise ValueError('旧证据没有满足输入保持')
        for name, value in previous['input_sha256'].items():
            if name != 'tools/verify_m10a2_stdlib.py' and hashes.get(name) != value:
                raise ValueError('复用证据的源码输入已变：' + name)
        allowed = ('core', 'stdio-six-modes', 'lifecycle-return', 'lifecycle-exit',
                   'exit-during-guard-init', 'recursive-exit-is-abnormal')
        wanted = {'host-libraries-host-caller-' + name for name in allowed}
        for item in previous['cases']:
            if item['name'] in wanted and item['status'] == 'PASS' and item.get('physical_delta') == 0:
                reused[item['name']] = item
        report['reused_evidence'] = dict(report=str(previous_path), sha256=sha(previous_path),
            cases=list(reused), reason='only harness corrected; same source/application/guard/libraries/core',
            verified_current_inputs=True, failed_cases_reused=False)
    guest = harness = None

    def persist():
        (args.out / 'stdlib.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def prepared(image):
        result = prepare(image, BUILD, extra, runtime_fixtures=False)
        result.update(host_bootstrap_libraries=not bool(native_info),
                      guest_sccc_compiled=bool(native_info), installed_libraries=installed)
        return result

    try:
        with QemuSession(BUILD / 'sandcore.img', args.data, args.out, args.accel, True,
                         audio=False, memory=256, data_prepare=prepared) as vm:
            report['qemu_pid'] = vm.process.pid
            guest = Guest(vm)
            harness = Harness(vm, guest, report, symbols, persist, reused)
            persist()
            try:
                wait_first_desktop(guest, symbols, report)
                guest.connect()
                harness.warm()
                harness.run('shared-runner-output-directory', 'mkdir -p ' + prefix + '/SHARED')
                shared_runner = prefix + '/SHARED/SHARERUN.SCX'
                harness.compile('guest-SCCC-shared-STD-runner', compiler, prefix + '/SHARERUN.C',
                    shared_runner, prefix + '/SHARERUN.json',
                    json_read(ROOT / 'tests/m10a2/SHARERUN.json'), False, vm.out / 'SHARERUN.SCX')
                harness.shared_runner = shared_runner
                app = PACKAGE + ('/GUARDN' if args.guest_guard else '/NATIVE') + '/STDLIBPROBE.SCX'
                if not native_info:
                    app = PACKAGE + '/HOST/STDLIBPROBE.SCX'
                label = 'native-libraries-native-caller' if native_info else 'host-libraries-host-caller'
                harness.suite(app, label)
                report['installed_library_execution'] = dict(source=installed, application=app,
                    guard='GUEST_SCCC' if args.guest_guard else 'GCC_POISONED_AL', complete_declared_modes=True)
                report['native_guard_executed'] = args.guest_guard

                native_dir = vm.out / 'native-libraries'
                artifacts = vm.out / 'native-artifacts'
                native_dir.mkdir()
                artifacts.mkdir()
                native_app = PACKAGE + '/NATIVE/STDLIBPROBE.SCX'
                provenance = dict(schema=1, guest_sccc_compiled=True, report='../stdlib.json',
                    source_input_sha256={name: value for name, value in hashes.items() if name.startswith('user/')
                        or name.startswith('tests/m10a2/STDLIB')},
                    compiler_sha256=report['compiler']['sha256'], qemu_pid=vm.process.pid, libraries={})
                if native_info:
                    # 第二轮用来源已核实的首轮APP，不为同一证据再编一次。
                    harness.run('native-caller-GCC-AL-library', native_app + ' core',
                        contains=(b'STDLIB_CORE_PASS failures=0',), forbidden=(b'STDLIB FAIL',), reclaim=True)
                    report['native_application'] = dict(source=native_info, bytes=len(native_probe),
                        sha256=digest(native_probe), executed=True, GCC_guard=True)
                else:
                    harness.run('native-package-directory', 'mkdir -p ' + PACKAGE + '/NATIVE')
                    harness.run('GCC-guard-copied-for-real-AL-check', 'cp ' + PACKAGE +
                                '/HOST/STDLIBGUARD.SCL ' + PACKAGE + '/NATIVE/STDLIBGUARD.SCL')
                    native_probe, provenance['application'] = harness.compile('guest-SCCC-stdlib-app', compiler,
                        prefix + '/STDLIBPROBE.C', native_app, prefix + '/STDLIBPROBE.json',
                        json_read(ROOT / 'tests/m10a2/STDLIBPROBE.json'), False, artifacts / 'STDLIBPROBE.SCX')
                    harness.run('native-caller-GCC-AL-library', native_app + ' core',
                        contains=(b'STDLIB_CORE_PASS failures=0',), forbidden=(b'STDLIB FAIL',), reclaim=True)
                    report['native_application'] = dict(bytes=len(native_probe), sha256=digest(native_probe),
                        guest_compiled=True, executed=True, GCC_guard=True)
                    harness.run('guest-guard-output-directory', 'mkdir -p ' + PACKAGE + '/GUARDN')
                    _, provenance['guard'] = harness.compile('guest-SCCC-guard-library', compiler,
                        prefix + '/STDLIBGUARD.C', PACKAGE + '/GUARDN/STDLIBGUARD.SCL',
                        prefix + '/STDLIBGUARD.json', json_read(ROOT / 'tests/m10a2/STDLIBGUARD.json'),
                        True, artifacts / 'STDLIBGUARD.SCL')
                    harness.run('selfcompiler-output-directory', 'mkdir -p ' + prefix + '/BUILT')
                    harness.selfcompile(compiler, prefix, artifacts)

                if utility is not None:
                    harness.run('utility-home-directory', 'mkdir -p /HOME/M10A2UTIL')
                    target = '/HOME/M10A2UTIL/UTILPROBE.SCX'
                    harness.compile('guest-SCCC-utility-app', compiler, prefix + '/UTILPROBE.C', target,
                                    prefix + '/UTILPROBE.json', utility, False, artifacts / 'UTILPROBE.SCX')
                    harness.run('real-utility-public-workflow', 'cd /HOME/M10A2UTIL && ' + target,
                        contains=(b'UTIL BEGIN v1', b'containers drops=4/4/3',
                                  b'events first=1 late=2 drop=3', b'model notices=7 visited=4',
                                  b'cache load=5 drop=5 oom=1', b'UTIL PASS checks='),
                        forbidden=(b'UTIL FAIL',), reclaim=True,
                        files=(('/HOME/M10A2UTIL/UTILDATA.BIN', 'utility-binary-exact', UTILITY_BYTES),))

                if not args.skip_library_build:
                    harness.run('guest-library-output-directory', 'mkdir -p ' + prefix + '/BUILT')
                    for name, (source, manifest) in LIBRARY_SOURCES.items():
                        _, provenance['libraries'][name] = harness.compile('guest-SCCC-library-' + name,
                            compiler, prefix + '/SRC/' + source, prefix + '/BUILT/' + name,
                            prefix + '/SRC/' + manifest, manifests[name], True, native_dir / name)
                    report['generated_libraries'] = dict(provenance['libraries'],
                        executed_this_generated_files=False, installed_only_on_explicit_second_vm=True)
                    if not native_info:
                        (native_dir / 'provenance.json').write_text(
                            json.dumps(provenance, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                        report['native_provenance'] = dict(path=str(native_dir / 'provenance.json'),
                            sha256=sha(native_dir / 'provenance.json'))

                # 安装字节仍由受保护系统路径持有；这里只读取回，不借管理Ring3替换。
                for name, blob in libraries.items():
                    harness.file('/SYS/LIB/' + name, 'installed-protected-' + name, blob)
                harness.screen('stdlib-desktop')
                report['status'] = ('DECLARED_NATIVE_LIBRARY_STDLIB_CASES_PASS' if native_info
                                    else 'DECLARED_HOST_AND_NATIVE_CALLER_STDLIB_CASES_PASS')
                persist()
            except BaseException as error:
                report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                    message=str(error), traceback=traceback.format_exc()))
                report['failure_console_tail'] = bytes(guest.console[-16384:]).decode('utf-8', 'replace')
                report['failure_debug_tail'] = bytes(guest.debug[-16384:]).decode('utf-8', 'replace')
                try:
                    report['failure_registers'] = vm.hmp('info registers')
                except Exception as capture:
                    report['register_capture_error'] = repr(capture)
                try:
                    harness.screen('stdlib-failure')
                except Exception as capture:
                    report['screenshot_capture_error'] = repr(capture)
                raise
            finally:
                try:
                    guest.close()
                except BaseException as close_error:
                    report.setdefault('cleanup_errors', []).append(repr(close_error))
                    if report['status'] != 'FAIL_OR_INTERRUPTED':
                        report['status'] = 'FAIL_OR_INTERRUPTED'
                        raise
                finally:
                    report['source_disk_unchanged'] = sha(args.data) == original
                    report['current_inputs_unchanged'] = all(sha(ROOT / name) == value for name, value in hashes.items())
                    if not report['source_disk_unchanged'] or not report['current_inputs_unchanged']:
                        report['status'] = 'FAIL_OR_INTERRUPTED'
                        report['input_preservation_error'] = '本次源盘或当前源发生变化'
                    persist()
    except BaseException as error:
        if not args.out.exists():
            args.out.mkdir(parents=True)
        report['status'] = 'FAIL_OR_INTERRUPTED'
        report.setdefault('error', dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc()))
        persist()
        raise
    if report['status'] == 'FAIL_OR_INTERRUPTED':
        raise AssertionError('源/清理保持条件失败，不能宣布guest通过')
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
