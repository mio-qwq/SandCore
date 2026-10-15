#!/usr/bin/env python3
"""第一轮Windows双盘：真实普通平台/Unicode应用与当前SCCC0→G1生成链。

只在本批源码冻结并统一构建后显式运行。生成SC32/SCUI库本轮不安装；
交互GUI及实际原生库安装由第二轮负责，不能把生成文件冒充已执行。
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import struct
import time
import traceback
import zlib

import verify_m10a2_stdlib as stdlib
from scserial import QemuSession, sha
from verify_m9 import Guest, process_cpu
from m10_guest_boot import physical_word, unique_symbols, wait_first_desktop
from verify_m10a2_scl import prepare, stable_pages
from verify_m10a2_dynamic import SOURCE_NAMES
from scl_format import validate
from scd_format import validate_scx
from mkcore import inspect_elf

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/m10a2-ui-bootstrap'
PACKAGE = '/HOME/M10UI'
STANDARD_HEADERS = stdlib.STANDARD_HEADERS
PUBLIC_HEADERS = ('SCTYPES.H', 'SC32API.H', 'SC32GUI.H', 'SC32FONT.H',
                  'SC32PALETTE.H', 'SC32POPUP.H', 'SC32BROKER.H', 'SC32DROP.H', 'SCUI.H')
LIBRARIES = dict(stdlib.LIBRARY_SOURCES, **{'SCUI.SCL': ('scui/SCUI.C', 'scui/SCUI.json')})
APPLICATIONS = ('PLATFORMPROBE', 'SCUITEXT', 'SCUIDEMO')
BROKER_POOLS = {'br_owners', 'br_handles', 'br_blobs', 'br_clips', 'br_formats',
                'br_sessions', 'br_endpoints', 'br_connections', 'br_messages',
                'br_shared_objects', 'br_offers'}
DROP_POOLS = {'drop_windows', 'drop_owners', 'drop_sessions', 'drop_targets', 'drop_tickets'}
BAD_OUTPUT = (b'PLATFORM FAIL', b'SCUI_TEXT_FAIL', b'AGGABI FAIL', b'SCCC ERROR', b'SCL: ', b'[AUTH] login failed')
PLATFORM_MARKERS = (b'PLATFORM_CHILD_PASS', b'PLATFORM_SHARED_PTE ',
                    b'PLATFORM_CLIPBOARD_PASS', b'PLATFORM_IPC_SHARED_PASS',
                    b'PLATFORM_FONT_PALETTE_PASS', b'PLATFORM_FOUNDATION_PASS checks=')


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def inside(path, directory):
    path = path.resolve(strict=True)
    if not path.is_relative_to(directory.resolve()):
        raise ValueError('路径超出声明目录：' + str(path))
    return path


def verified_build(path):
    path = inside(path, BUILD)
    if not re.fullmatch(r'ui-build-[a-z0-9-]+\.json', path.name):
        raise ValueError('必须明确指定本批ui-build-*.json，不自动挑最新记录')
    proof = read_json(path)
    if proof.get('scope') != 'M10A2_SCUI_PLATFORM_COMPILER_UNIFIED_HOST_BUILD':
        raise ValueError('构建证明不是本批完整平台/SCUI目标')
    if proof.get('exit_code') != 0 or proof.get('source_files_modified_during_build'):
        raise ValueError('统一构建失败或构建过程中源码改变')
    paths, outputs = {}, {}
    for category in ('inputs', 'outputs'):
        if not proof.get(category):
            raise ValueError('构建证明缺少' + category)
        local = {}
        for item in proof[category]:
            target = inside(ROOT / item['path'], ROOT)
            if target in local or not re.fullmatch(r'[0-9a-f]{64}', item['sha256']):
                raise ValueError('构建证明存在重复路径或非法摘要')
            if sha(target) != item['sha256']:
                raise ValueError('当前文件不匹配构建证明：' + item['path'])
            if 'bytes' in item and target.stat().st_size != item['bytes']:
                raise ValueError('产物长度不匹配：' + item['path'])
            local[target] = item
            name = target.relative_to(ROOT).as_posix()
            if name in paths and paths[name] != item['sha256']:
                raise ValueError('输入/输出摘要相互冲突：' + name)
            paths[name] = item['sha256']
        if category == 'outputs':
            outputs = local
    required = ('sandcore.img', 'fs/SYS/CORE/CORE.SKM', 'fs/bin/sccc.scx',
                'core.sym', 'core.elf', 'core.bin', 'fs/SYS/UI/UAX15_1.SCU')
    required += tuple(LIBRARIES) + tuple(name + '.SCX' for name in APPLICATIONS)
    for name in required:
        if inside(BUILD / name, BUILD) not in outputs:
            raise ValueError('构建证明缺少必要产物：' + name)
    core = (BUILD / 'fs/SYS/CORE/CORE.SKM').read_bytes()
    from mkfs_m10 import check_main
    check_main(core)
    elf = inspect_elf(BUILD / 'core.elf')
    if (BUILD / 'core.bin').read_bytes() != core[128:]:
        raise ValueError('主核平载荷与固定CORE不同，不能据另版ELF计账')
    base, entry, image, bss_offset, bss = struct.unpack_from('<5I', core, 36)
    if (elf['_core_start'] != base + entry or elf['__core_image_end'] != base + image
        or elf['__bss_start'] != base + bss_offset or elf['__bss_end'] != base + bss_offset + bss):
        raise ValueError('固定主核ELF与CORE映像/BSS边界不同')
    paths[path.relative_to(ROOT).as_posix()] = sha(path)
    return proof, paths, outputs


def payload(prefix, proof_hashes):
    """SRC保留完整实现，INC只放公开头；分页草稿不能混作编译输入。"""
    source_names = set(SOURCE_NAMES) | set(PUBLIC_HEADERS)
    # 本批编译器私有增量也进入自举SRC，不能只复制历史固定include清单。
    source_names.update(p.name for p in (ROOT / 'user').glob('s3c*')
                        if p.is_file() and p.suffix.lower() in ('.c', '.h', '.inc'))
    excluded = []
    folders = ('runtime', 'sc32', 'guiapi', 'fontapi', 'brokerapi', 'dropapi',
               'stdlib', 'sclib', 'scui', 'textdata')
    for folder in folders:
        for path in (ROOT / 'user' / folder).rglob('*'):
            if not path.is_file() or path.suffix.lower() not in ('.c', '.h', '.inc', '.json', '.asm'):
                continue
            name = path.relative_to(ROOT / 'user').as_posix()
            if 'draft' in path.name.lower():
                excluded.append(name)
            else:
                source_names.add(name)
    extra, hashes = {}, {}

    def add(source, target, must_be_pinned=True):
        source = inside(source, ROOT)
        relative = source.relative_to(ROOT).as_posix()
        value = sha(source)
        if must_be_pinned and proof_hashes.get(relative) != value:
            raise ValueError('准备输入未被当前构建证明固定：' + relative)
        body = source.read_bytes()
        if target in extra and extra[target] != body:
            raise ValueError('准备路径有不同正文：' + target)
        extra[target] = body
        hashes[relative] = value

    for name in sorted(source_names):
        add(ROOT / 'user' / name, prefix[1:] + '/SRC/' + name)
    for name in PUBLIC_HEADERS:
        if 'PRIVATE' in name or 'WIRE' in name:
            raise ValueError('私有头不得进入SDK INC')
        add(ROOT / 'user' / name, 'SYS/INC/' + name)
    for name in STANDARD_HEADERS:
        add(ROOT / 'user/stdlib' / name, 'SYS/INC/' + name)
    add(ROOT / 'user/sclib/SCLIB.H', 'SYS/INC/SCLIB.H')
    # 公共ROOT头与旧自举桥都是已发布头；PRIVATE/换栈/WIRE只在SRC。
    add(ROOT / 'user/runtime/SCLDR.H', 'SYS/INC/SCLDR.H')
    for name in ('SCAPI.H', 'SCIO.H'):
        add(ROOT / 'user' / name, 'SYS/INC/' + name)
    for name in APPLICATIONS:
        for suffix in ('.C', '.json'):
            add(ROOT / 'tests/m10a2' / (name + suffix), prefix[1:] + '/' + name + suffix)
        add(BUILD / (name + '.SCX'), PACKAGE[1:] + '/HOST/' + name + '.SCX')
    # 两个方向分别检验真实GCC/SCCC aggregate ABI；同名库只在各caller目录。
    for name in ('AGGABI.H', 'AGGCALLEE.C', 'AGGCALLEE.json', 'AGGPROBE.C', 'AGGPROBE.json'):
        add(ROOT / 'tests/m10a2' / name, prefix[1:] + '/' + name)
    add(BUILD / 'AGGCALLEE.SCL', PACKAGE[1:] + '/AGG/NATIVECALL/AGGCALLEE.SCL')
    add(BUILD / 'AGGPROBE.SCX', PACKAGE[1:] + '/AGG/NATIVELIB/AGGPROBE.SCX')
    add(BUILD / 'fs/bin/sccc.scx', prefix[1:] + '/SCCC0.SCX')
    add(BUILD / 'fs/SYS/UI/UAX15_1.SCU', 'SYS/UI/UAX15_1.SCU')
    if extra['SYS/UI/UAX15_1.SCU'] != (ROOT / 'assets/ui/UAX15_1.SCU').read_bytes():
        raise ValueError('已构建UAX资源与被固定的当前资源不同')
    for name in ('GRAPHEME.SCT', 'WORD.SCT', 'PROVENANCE.json'):
        add(ROOT / 'assets/ui/tests' / name, PACKAGE[1:] + '/' + name)
    license_root = inside(BUILD / 'fs/SYS/LICENSE', BUILD)
    archive_sources = {'UNICODE15_1': ROOT / 'third_party/unicode15_1',
                       'UAX15_1TEST': ROOT / 'tests/m10a2/unicode15_1'}
    for directory, original in archive_sources.items():
        expected = {p.relative_to(original).as_posix(): p for p in original.rglob('*') if p.is_file()}
        actual_root = inside(license_root / directory, license_root)
        actual = {p.relative_to(actual_root).as_posix(): p for p in actual_root.rglob('*') if p.is_file()}
        if set(actual) != set(expected):
            raise ValueError('完整Unicode源码/许可归档缺项：' + directory)
        for name in sorted(expected):
            original_name = expected[name].relative_to(ROOT).as_posix()
            if proof_hashes.get(original_name) != sha(expected[name]) or sha(actual[name]) != sha(expected[name]):
                raise ValueError('Unicode归档与固定原件不同：' + directory + '/' + name)
    for path in license_root.rglob('*'):
        if path.is_file():
            name = path.relative_to(license_root).as_posix()
            # 归档中每份正文也留实际SHA，未列独立output时用固定原件逐字节绑定。
            add(path, 'SYS/LICENSE/' + name, must_be_pinned=False)
    for archived, source in (('M10A2.JSON', 'third_party/M10A2-SOURCES.json'),
                              ('THIRDPARTY.MD', 'docs/THIRD-PARTY.md')):
        path = ROOT / source
        if proof_hashes.get(source) != sha(path) or extra.get('SYS/LICENSE/' + archived) != path.read_bytes():
            raise ValueError('客体第三方索引不是本批固定正文：' + archived)
    # 三个输出目录先有空占位，避免把新目录/索引页归错应用heap。
    for directory in ('COMPILER', 'GENERATED', 'NATIVE_APPS'):
        extra[PACKAGE[1:] + '/' + directory + '/.KEEP'] = b''
    extra[PACKAGE[1:] + '/WARM.DAT'] = b''
    return extra, hashes, excluded


def corpus_info(blob, kind, count):
    if len(blob) < 32 or blob[:8] != b'SCUB15_1':
        raise ValueError('官方oracle转换容器错误')
    version, actual_kind, cases, size, crc, reserved = struct.unpack_from('<6I', blob, 8)
    if (version, actual_kind, cases, size, reserved) != (1, kind, count, len(blob), 0):
        raise ValueError('官方oracle版本/种类/计数不匹配')
    if zlib.crc32(blob[32:]) != crc:
        raise ValueError('官方oracle正文CRC错误')
    at, boundaries = 32, 0
    for _ in range(count):
        if len(blob) - at < 12:
            raise ValueError('oracle记录截断')
        size, number, line = struct.unpack_from('<3I', blob, at)
        at += 12
        padded = (size + 3) & ~3
        if not size or not line or number < 2 or padded + number * 4 > len(blob) - at:
            raise ValueError('oracle记录范围错误')
        text = blob[at:at + size]
        text.decode('utf-8')
        values = struct.unpack_from('<' + str(number) + 'I', blob, at + padded)
        if values[0] != 0 or values[-1] != size or any(a >= b for a, b in zip(values, values[1:])):
            raise ValueError('oracle边界顺序错误')
        if any(blob[at + size:at + padded]):
            raise ValueError('oracle padding非零')
        boundaries += 2 * (number - 1)
        at += padded + number * 4
    if at != len(blob):
        raise ValueError('oracle尾部多余字节')
    return dict(cases=count, boundaries=boundaries, bytes=len(blob), sha256=digest(blob))


def local_elf_symbols(path):
    """只读匹配构建ELF的FILE/OBJECT；同名cache_pages不能错误合并。"""
    blob = path.read_bytes()
    if len(blob) < 52 or blob[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('core符号观察必须绑定ELF32 LE')
    section_offset = struct.unpack_from('<I', blob, 32)[0]
    entry, count = struct.unpack_from('<2H', blob, 46)
    if entry != 40 or section_offset + entry * count > len(blob):
        raise ValueError('core ELF段头范围错误')
    sections = [struct.unpack_from('<10I', blob, section_offset + i * entry) for i in range(count)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        if section[6] >= count or section[9] != 16 or section[5] % 16:
            raise ValueError('core ELF符号表布局错误')
        strings = sections[section[6]]
        names = blob[strings[4]:strings[4] + strings[5]]
        if section[4] + section[5] > len(blob) or len(names) != strings[5]:
            raise ValueError('core ELF符号/字符串截断')
        filename = ''
        for at in range(section[4], section[4] + section[5], 16):
            name, value, width, info, other, index = struct.unpack_from('<IIIBBH', blob, at)
            if name >= len(names) or names.find(b'\0', name) < 0:
                raise ValueError('core ELF符号名错误')
            label = names[name:names.index(0, name)].decode('ascii')
            if info & 15 == 4:
                filename = Path(label).name
            elif info & 15 == 1 and filename and index:
                if filename != 'ttf.c' or label not in ('faces', 'cache_pages', 'hits', 'misses'):
                    continue
                key = filename + '::' + label
                if key in result:
                    raise ValueError('core ELF FILE/OBJECT歧义：' + key)
                result[key] = dict(address=value, bytes=width)
    return result


def native_provenance(report, directory):
    if not directory.is_dir():
        return
    value = dict(schema=1, guest_sccc_compiled=bool(report['native_artifacts'] or report['native_libraries']), report='../ui-platform.json',
        qemu_pid=report['qemu_pid'],
        source_input_sha256={name: expected for name, expected in report['input_sha256'].items()
            if name.startswith('user/') or name.startswith('tests/m10a2/')},
        compiler_sha256=report['native_artifacts'].get('SCCC1.SCX', {}).get('sha256'),
        libraries=report['native_libraries'], applications=report['native_artifacts'],
        generated_library_executed=False, installed_library_origin='HOST_GCC_BOOTSTRAP',
        current_status=report['status'])
    (directory / 'provenance.json').write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


@contextmanager
def harness_configuration():
    # 只更改本Python会话导入模块的配置，不改已验旧脚本一字节。
    saved = (stdlib.BUILD, stdlib.PACKAGE, stdlib.POOL_NAMES)
    stdlib.BUILD, stdlib.PACKAGE = BUILD, PACKAGE
    stdlib.POOL_NAMES = set(saved[2]) | BROKER_POOLS | DROP_POOLS
    try:
        yield
    finally:
        stdlib.BUILD, stdlib.PACKAGE, stdlib.POOL_NAMES = saved


class PlatformHarness(stdlib.Harness):
    def __init__(self, vm, guest, report, symbols, persist, scoped):
        super().__init__(vm, guest, report, symbols, persist)
        self.scoped, self.last_login_tick = scoped, None
        for name, width in (('faces', 312), ('cache_pages', 4), ('hits', 4), ('misses', 4)):
            if scoped.get('ttf.c::' + name, {}).get('bytes') != width:
                raise ValueError('ttf_info对应的i386结构/符号尺寸改变：' + name)
        found = {label.split('@', 1)[0] for label, _ in self.pools}
        if not (BROKER_POOLS | DROP_POOLS) <= found:
            raise ValueError('本批broker/drop真实对象池符号缺项：' + str(sorted((BROKER_POOLS | DROP_POOLS) - found)))

    def font(self):
        # face_t156B的固定i386源布局与ttf_info字段逐项对应；没有调用/注入Ring0。
        base = self.scoped['ttf.c::faces']['address']
        faces = []
        for index, face in enumerate((12, 16)):
            address = base + index * 156
            faces.append(dict(face=face, generation=physical_word(self.vm, address + 124),
                data_bytes=physical_word(self.vm, address + 4),
                data_pages=physical_word(self.vm, address + 8),
                workspace_pages=physical_word(self.vm, address + 16),
                glyphs=physical_word(self.vm, address + 76), mapped=physical_word(self.vm, address + 116)))
        result = dict(faces=faces, method='READ_ONLY_ELF_SCOPED_TTF_INFO_FIELDS',
            layout=dict(face_bytes=156, generation=124, data_pages=8, workspace_pages=16),
            cache_pages=physical_word(self.vm, self.scoped['ttf.c::cache_pages']['address']),
            hits=physical_word(self.vm, self.scoped['ttf.c::hits']['address']),
            misses=physical_word(self.vm, self.scoped['ttf.c::misses']['address']))
        result['owned_pages'] = result['cache_pages'] + sum(x['data_pages'] + x['workspace_pages'] for x in faces)
        return result

    def resources(self):
        values = super().resources()
        for label, address in self.pools:
            values[label]['flags'] = physical_word(self.vm, address + 1092)
        values['font'] = self.font()
        wait = {name.removeprefix('wait_'): physical_word(self.vm, self.symbols[name])
                for name in ('wait_count', 'wait_capacity', 'wait_heap', 'wait_pages')}
        frames = physical_word(self.vm, self.symbols['pf_frames'])
        heap = wait['heap']
        wait['heap_aligned_managed'] = bool(heap >= 0x200000 and not heap & 4095
            and heap < 0x200000 + frames * 4096)
        wait['heap_page_allocated'] = False
        if wait['heap_aligned_managed']:
            index = (heap - 0x200000) // 4096
            bitmap = physical_word(self.vm, self.symbols['pf_bitmap'] + (index // 32) * 4)
            wait['heap_page_allocated'] = bool(bitmap & (1 << (index % 32)))
        # 旧task.c只在空堆pages>1时归还，首个32项/1PF缓存由真实
        # 全局堆持有。读取四字段和PF位，不从pages差额猜其归属。
        values['finite_wait_heap'] = wait
        values['pending'] = {name: physical_word(self.vm, self.symbols[name]) for name in
            ('br_garbage_first', 'br_notify_first', 'gc_first', 'session_first',
             'cleanup_head', 'zombie_head', 'cleanup_revoke_pending') if name in self.symbols}
        return values

    def quiet(self):
        begin = time.monotonic()
        observed = []
        while time.monotonic() - begin < 20:
            value, values = stable_pages(self.vm, self.symbols)
            pending = self.resources()['pending']
            observed.append(dict(pages=value, samples=values, pending=pending))
            ready = all(v == (0xffffffff if name in ('cleanup_head', 'zombie_head') else 0)
                        for name, v in pending.items())
            if ready:
                return value, observed
            if self.guest.errors or self.vm.process.poll() is not None:
                raise RuntimeError('等待真实owner/GC清理时会话终止')
            self.guest.stop.wait(.03)
        raise TimeoutError('实际broker/drop/task清理链未在预算内收束')

    def login_cooldown(self):
        observations = []
        began = time.monotonic()
        while True:
            now = physical_word(self.vm, self.symbols['sc_ticks'])
            elapsed = None if self.last_login_tick is None else (now - self.last_login_tick) & 0xffffffff
            if not observations or observations[-1]['tick'] != now:
                observations.append(dict(tick=now, elapsed=elapsed))
            if elapsed is None or elapsed >= 200:
                self.last_login_tick = now
                return observations
            if time.monotonic() - began > 15 or self.guest.errors or self.vm.process.poll() is not None:
                raise TimeoutError('真实PIT没有越过mio登录200tick限频，不连续重试登录')
            self.guest.stop.wait(.03)

    def ledger(self, before, after, resources_before, resources_after, cold_font):
        pool_deltas, history, unexpected = {}, 0, []
        for label, _ in self.pools:
            old, new = resources_before[label], resources_after[label]
            delta = new['pages'] - old['pages']
            pool_deltas[label] = dict(pages=delta, objects=new['objects'] - old['objects'], flags=new['flags'])
            if new['objects'] != old['objects']:
                unexpected.append('live objects: ' + label)
            if delta:
                if old['flags'] == new['flags'] == 1:
                    history += delta
                else:
                    unexpected.append('non-history pool pages: ' + label)
        font_delta = resources_after['font']['owned_pages'] - resources_before['font']['owned_pages']
        if font_delta and not cold_font:
            unexpected.append('unexpected warmed font page change')
        desktop_delta = resources_after['desktop'].get('pages', 0) - resources_before['desktop'].get('pages', 0)
        if desktop_delta:
            unexpected.append('warmed desktop page change')
        old_wait, new_wait = resources_before['finite_wait_heap'], resources_after['finite_wait_heap']
        wait_cache_delta = 0
        first_wait_cache = (cold_font and old_wait['pages'] == 0 and old_wait['count'] == 0
            and old_wait['capacity'] == 0 and old_wait['heap'] == 0
            and new_wait['pages'] == 1 and new_wait['count'] == 0 and new_wait['capacity'] == 32
            and new_wait['heap_aligned_managed'] and new_wait['heap_page_allocated'])
        if first_wait_cache:
            wait_cache_delta = 1
        elif any(old_wait[key] != new_wait[key] for key in ('pages', 'count', 'capacity', 'heap')):
            unexpected.append('finite wait heap changed outside measured first-page cache')
        if new_wait['pages'] == 1 and (not new_wait['heap_aligned_managed'] or not new_wait['heap_page_allocated']):
            unexpected.append('finite wait cache PF is invalid')
        for name in stdlib.COUNT_NAMES:
            if name in resources_before and name in resources_after and resources_before[name] != resources_after[name]:
                # ttf的cache_pages若唯一，已在真实font账中计一次；不重复或任意豁免。
                if name == 'cache_pages' and self.symbols.get(name) == self.scoped['ttf.c::cache_pages']['address']:
                    continue
                if name == 'wait_pages' and first_wait_cache:
                    continue
                unexpected.append('persistent scalar changed: ' + name)
        actual = after - before
        attributed = history + font_delta + wait_cache_delta
        return dict(before_physical_pages=before, after_physical_pages=after, physical_delta=actual,
            actual_pool_deltas=pool_deltas, actual_history_page_delta=history,
            actual_font_page_delta=font_delta, first_font_context=cold_font,
            actual_first_finite_wait_cache_delta=wait_cache_delta,
            finite_wait_heap_before=old_wait, finite_wait_heap_after=new_wait,
            finite_wait_cache_source='kernel/task.c::wait_reserve initial32 and wait_remove retains empty pages==1',
            actual_desktop_page_delta=desktop_delta, attributed_physical_delta=attributed,
            unexplained_delta=actual - attributed, unexpected=unexpected,
            complete_owner_and_gc_observed=True), not unexpected and actual == attributed

    def ordinary(self, name, app, arguments, contains, timeout=180, cold_font=False, identity=False):
        cooldown = self.login_cooldown()
        before, before_observed = self.quiet()
        rb = self.resources()
        marker = '__UI_LOGIN_' + secrets.token_hex(8)
        if any(c in app + arguments for c in "'\r\n"):
            raise ValueError('本批登录脚本只接受固定路径/参数')
        inner = 'id; ' + app + (' ' + arguments if arguments else '') + '; _ui=$?; echo ' + marker + ':$_ui; exit 0'
        command = "printf '%s\\n' '' '" + inner + "' | su mio"
        item = self.run(name, command, contains=contains, forbidden=BAD_OUTPUT, timeout=timeout)
        # 完成tick晚于实际auth_login，下一轮据它再等200，不能把提前
        # 采样的发命令tick当真正登录时刻而碰限频；不重试失败登录。
        self.last_login_tick = physical_word(self.vm, self.symbols['sc_ticks'])
        match = re.findall(r'(?:^|\r?\n)' + re.escape(marker) + r':([0-9]+)\r?\n', item['output'])
        login_ids = re.findall(r'(?:^|\r?\n)uid=1000 gid=(\d+) realm=0\r?\n', item['output'])
        okay = match == ['0'] and len(login_ids) == 1
        identities = []
        if identity:
            for pid, generation, uid, realm, session, flags in re.findall(
                r'PLATFORM_ID pid=(\d+) gen=(\d+) uid_bits=(\d+) realm=(\d+) sid=(\d+) flags=(\d+)', item['output']):
                identities.append(dict(pid=int(pid), generation=int(generation), uid=int(uid),
                                       realm=int(realm), session=int(session), flags=int(flags)))
            okay = okay and len(identities) == 2 and len({v['pid'] for v in identities}) == 2
            okay = okay and len({v['session'] for v in identities}) == 1
            okay = okay and all(v['uid'] == 1000 and v['realm'] == 0 and v['session']
                               and v['generation'] and not v['flags'] & 2 for v in identities)
            item['actual_process_identities'] = identities
        if not okay:
            self.append(dict(name=name + '-actual-normal-login-status', actual_inner_status=match,
                             actual_login_gid_lines=login_ids, actual_process_identities=identities), False)
        after, after_observed = self.quiet()
        ra = self.resources()
        ledger, reclaimed = self.ledger(before, after, rb, ra, cold_font)
        item.update(ordinary_login='AUTH_EXISTING_SU_MIO_WITH_EXTERNAL_SYSTEM_BLANK_PASSWORD_INPUT',
            ordinary_status_marker=marker, ordinary_exit_code=0, authentication_cooldown=cooldown,
            actual_shell_identity=dict(uid=1000, gid=int(login_ids[0]), realm=0),
            before_observations=before_observed, after_observations=after_observed,
            resources_before=rb, resources_after=ra, **ledger)
        self.persist()
        self.append(dict(name=name + '-actual-owner-page-account', **ledger), reclaimed)
        return item

    def selfcompile_only(self, compiler, prefix, local):
        target = PACKAGE + '/COMPILER/SCCC1.SCX'
        name = 'guest-SCCC0-current-source-to-G1'
        item = self.run(name, compiler + ' ' + prefix + '/SRC/sccc.c ' + target,
                        contains=(b'SCCC OK load=',), forbidden=BAD_OUTPUT, timeout=600)
        blob = self.file(target, name + '-output', local=local)
        valid = len(blob) >= 36 and blob[:8] == b'SCX1MIO\0'
        fields = struct.unpack_from('<7I', blob, 8) if valid else (0,) * 7
        entry, load, bss, stack, flags, base, author = fields
        valid = valid and 0 < load <= 262108 and entry < load and load + bss <= 0x3d0000
        valid = valid and len(blob) == 36 + load and stack == 131072 and flags == 0
        valid = valid and base == 0x400000 and author == 0x004f494d
        contract = dict(load_bytes=load, bss_bytes=bss, stack_bytes=stack, entry_rva=entry,
            load_budget=262108, mapped_budget=0x3d0000, exact_file_bytes=len(blob),
            old_container_not_widened=True)
        self.append(dict(name=name + '-legacy-container', **contract), valid)
        item.update(generated_sha256=digest(blob), generated_bytes=len(blob), guest_sccc_compiled=True,
                    generated_path=str(local), format_contract=contract)
        self.file(target + '.log', name + '-log')
        self.file(target + '.map', name + '-map')
        self.persist()
        return target, blob, dict(path=str(local.resolve()), sha256=digest(blob), bytes=len(blob),
            compile_case=name, guest_target=target, guest_sccc_compiled=True,
            executed_this_generated_file=False, format_contract=contract)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'build/m10a1/sanddata.img')
    parser.add_argument('--accel', choices=('tcg', 'whpx'), default='tcg')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('本主控只启动Windows QEMU，不在WSL运行')
    args.out = args.out.resolve()
    if args.out.exists():
        raise FileExistsError('拒绝覆盖已有VM/失败证据目录')
    args.data = args.data.resolve(strict=True)
    args.build_report = args.build_report.resolve(strict=True)
    proof, proof_hashes, outputs = verified_build(args.build_report)
    prefix = '/TMP/UI-' + secrets.token_hex(3)
    compiler = prefix + '/SCCC0.SCX'
    extra, hashes, excluded = payload(prefix, proof_hashes)
    hashes['tools/verify_m10a2_ui_platform.py'] = sha(Path(__file__))
    for name in ('tools/verify_m10a2_stdlib.py', 'tools/verify_m10a2_scl.py',
                 'tools/verify_m9.py', 'tools/scserial.py', 'tools/m10_guest_boot.py'):
        hashes[name] = sha(ROOT / name)
    manifests = {name: read_json(ROOT / 'user' / spec[1]) for name, spec in LIBRARIES.items()}
    installed = {}
    for name, manifest in manifests.items():
        blob = (BUILD / name).read_bytes()
        contract = stdlib.source_contract(validate(blob), manifest)
        extra['SYS/LIB/' + name] = blob
        installed[name] = dict(path='/SYS/LIB/' + name, bytes=len(blob), sha256=digest(blob),
                               source='HOST_GCC_BOOTSTRAP', format_contract=contract)
    apps = {}
    for name in APPLICATIONS:
        manifest = read_json(ROOT / 'tests/m10a2' / (name + '.json'))
        blob = (BUILD / (name + '.SCX')).read_bytes()
        contract = stdlib.source_contract(validate_scx(blob), manifest, True)
        apps[name] = dict(path=PACKAGE + '/HOST/' + name + '.SCX', bytes=len(blob), sha256=digest(blob),
                          source='HOST_GCC_BOOTSTRAP', manifest=manifest, format_contract=contract)
    oracle = read_json(ROOT / 'assets/ui/tests/PROVENANCE.json')
    expected_boundaries, oracle_info = 0, {}
    for name, kind, count in (('GRAPHEME.SCT', 1, 1187), ('WORD.SCT', 2, 1826)):
        value = corpus_info((ROOT / 'assets/ui/tests' / name).read_bytes(), kind, count)
        item = next(x for x in oracle['outputs'] if x['path'] == name)
        if item['sha256'] != value['sha256'] or item['cases'] != count or item['kind'] != kind:
            raise ValueError('SCT与固定官方转换来源记录不匹配')
        original_file = ROOT / 'tests/m10a2/unicode15_1' / item['original']
        if sha(original_file) != item['original_sha256']:
            raise ValueError('官方原始测试数据改变')
        expected_boundaries += value['boundaries']
        oracle_info[name] = dict(value, original_sha256=item['original_sha256'],
                                 expected_source=item['expected_boundaries_source'])
    text_marker = ('SCUI_TEXT_PASS version=15.1 cases=3013 boundaries=%d' % expected_boundaries).encode()
    symbols = unique_symbols(BUILD / 'core.sym')
    for name in ('pf_used', 'sc_ticks', 'cleanup_head', 'zombie_head', 'cleanup_revoke_pending',
                 'br_garbage_first', 'br_notify_first', 'gc_first', 'session_first',
                 'wait_count', 'wait_capacity', 'wait_heap', 'wait_pages', 'pf_frames', 'pf_bitmap'):
        if name not in symbols:
            raise ValueError('本批主核缺少唯一计账/待办符号：' + name)
    scoped = local_elf_symbols(BUILD / 'core.elf')
    original = sha(args.data)
    report = dict(status='RUNNING', scope='M10A2_UI_PLATFORM_FIRST_WINDOWS_GUEST_ONLY', cases=[],
        input_sha256=hashes, pinned_build_input_output_sha256=proof_hashes,
        source_data_path=str(args.data), source_data_sha256=original,
        build_report=str(args.build_report), build_report_sha256=sha(args.build_report),
        host_build_wall_seconds=proof['wall_seconds'], core_sha256=sha(BUILD / 'fs/SYS/CORE/CORE.SKM'),
        installed_libraries=installed, host_applications=apps, official_corpus=oracle_info,
        compiler=dict(path=compiler, sha256=sha(BUILD / 'fs/bin/sccc.scx'), source='HOST_GCC_SCCC0'),
        native_libraries={}, native_artifacts={}, caller_executions=[], screenshots=[],
        excluded_source_drafts=excluded, ordinary_login_not_simulated=True,
        execution_scope='EXTERNAL_SYSTEM_COMPILE_AND_AUTHENTIC_NORMAL_LOGIN_APPLICATIONS',
        generated_library_execution=False, generated_system_libraries_execution=False,
        GUI_behavior_verified=False, full_M10a2=False,
        unrelated_prior_evidence='REUSED_NOT_RERUN')
    guest = harness = None

    def persist():
        (args.out / 'ui-platform.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def prepared(image):
        result = prepare(image, BUILD, extra, runtime_fixtures=False)
        result.update(installed_libraries=installed, installed_host_applications=apps,
            complete_source_sha256=hashes, excluded_drafts=excluded,
            copied_license_files={name: digest(body) for name, body in extra.items() if name.startswith('SYS/LICENSE/')},
            public_sdk_files=sorted(name for name in extra if name.startswith('SYS/INC/')),
            host_bootstrap_libraries=True, generated_libraries_installed=False)
        report['preparation'] = result
        return result

    try:
        with harness_configuration():
            with QemuSession(BUILD / 'sandcore.img', args.data, args.out, args.accel, True,
                             audio=False, memory=256, data_prepare=prepared) as vm:
                report['qemu_pid'] = vm.process.pid
                guest = Guest(vm)
                try:
                    harness = PlatformHarness(vm, guest, report, symbols, persist, scoped)
                    persist()
                    wait_first_desktop(guest, symbols, report)
                    guest.connect()
                    harness.warm()
                    # 第一轮先运行真实普通父子；首字体/历史页增长按观测单列。
                    host = apps['PLATFORMPROBE']['path']
                    harness.ordinary('HOST-platform-actual-mio-session', host, 'all ' + host,
                        PLATFORM_MARKERS, cold_font=True, identity=True)
                    report['caller_executions'].append(dict(name='PLATFORMPROBE.SCX', source='HOST_GCC',
                        path=host, executed=True, libraries='SYS_LIB_HOST_GCC_FIVE', ordinary_uid=1000))
                    harness.ordinary('HOST-official-3013-text-corpus', apps['SCUITEXT']['path'], '',
                                     (text_marker,), timeout=600)
                    report['caller_executions'].append(dict(name='SCUITEXT.SCX', source='HOST_GCC',
                        path=apps['SCUITEXT']['path'], executed=True, libraries='SYS_LIB_HOST_GCC_FIVE',
                        official_cases=3013, expected_bidirectional_boundaries=expected_boundaries))
                    artifacts, libraries = vm.out / 'native-artifacts', vm.out / 'native-libraries'
                    artifacts.mkdir()
                    libraries.mkdir()
                    g1, g1_blob, g1_item = harness.selfcompile_only(compiler, prefix, artifacts / 'SCCC1.SCX')
                    report['native_artifacts']['SCCC1.SCX'] = g1_item
                    native_provenance(report, artifacts)
                    native_provenance(report, libraries)
                    persist()
                    aggregate_directory = vm.out / 'aggregate-abi'
                    aggregate_directory.mkdir()
                    aggregate = report['aggregate_abi'] = dict(
                        purpose='ACTUAL_GCC_SCCC_AGGREGATE_CALL_RETURN_CALLBACK_STACK_ABI',
                        required_marker='AGGABI PASS checks=55 failures=0',
                        executions=[], artifacts={})
                    for name, shared, target in (
                        ('AGGCALLEE', True, PACKAGE + '/AGG/NATIVELIB/AGGCALLEE.SCL'),
                        ('AGGPROBE', False, PACKAGE + '/AGG/NATIVECALL/AGGPROBE.SCX')):
                        extension = '.SCL' if shared else '.SCX'
                        manifest = read_json(ROOT / 'tests/m10a2' / (name + '.json'))
                        label = 'actual-G1-aggregate-' + name
                        local = aggregate_directory / (name + extension)
                        blob, meta = harness.compile(label, g1, prefix + '/' + name + '.C', target,
                            prefix + '/' + name + '.json', manifest, shared, local,
                            compiler_sha256=digest(g1_blob))
                        aggregate['artifacts'][name + extension] = dict(meta,
                            path=str(local.resolve()), sha256=digest(blob),
                            compiler_sha256=digest(g1_blob), guest_sccc_compiled=True,
                            format_contract=stdlib.source_contract(
                                validate(blob) if shared else validate_scx(blob), manifest, not shared))
                        report['native_artifacts']['SCCC1.SCX']['executed_this_generated_file'] = True
                        persist()
                    for label, app, caller, callee in (
                        ('G1-aggregate-caller-GCC-callee', PACKAGE + '/AGG/NATIVECALL/AGGPROBE.SCX', 'ACTUAL_G1', 'HOST_GCC'),
                        ('GCC-aggregate-caller-G1-callee', PACKAGE + '/AGG/NATIVELIB/AGGPROBE.SCX', 'HOST_GCC', 'ACTUAL_G1')):
                        harness.ordinary(label, app, '', (b'AGGABI PASS checks=55 failures=0',), timeout=120)
                        aggregate['executions'].append(dict(name=label, app=app,
                            caller_source=caller, callee_source=callee, ordinary_uid=1000,
                            check_count=55, actual_callback_crosses_back=True,
                            actual_caller_stack_balance_checked=True, executed=True))
                        if callee == 'ACTUAL_G1':
                            report['generated_library_execution'] = True
                        persist()
                    for name in ('SC32API.SCL', 'SCUI.SCL'):
                        source, manifest = LIBRARIES[name]
                        label = 'actual-G1-library-' + name
                        local = libraries / name
                        blob, meta = harness.compile(label, g1, prefix + '/SRC/' + source,
                            PACKAGE + '/GENERATED/' + name, prefix + '/SRC/' + manifest,
                            manifests[name], True, local, compiler_sha256=digest(g1_blob))
                        report['native_libraries'][name] = dict(meta, path=str(local.resolve()),
                            compiler_sha256=digest(g1_blob), executed_this_generated_file=False,
                            installed_in_this_vm=False,
                            format_contract=stdlib.source_contract(validate(blob), manifests[name]))
                        report['native_artifacts']['SCCC1.SCX']['executed_this_generated_file'] = True
                        native_provenance(report, artifacts)
                        native_provenance(report, libraries)
                        persist()
                    for name in APPLICATIONS:
                        label = 'actual-G1-app-' + name
                        local = artifacts / (name + '.SCX')
                        blob, meta = harness.compile(label, g1, prefix + '/' + name + '.C',
                            PACKAGE + '/NATIVE_APPS/' + name + '.SCX', prefix + '/' + name + '.json',
                            apps[name]['manifest'], False, local, compiler_sha256=digest(g1_blob))
                        report['native_artifacts'][name + '.SCX'] = dict(meta, path=str(local.resolve()),
                            compiler_sha256=digest(g1_blob), executed_this_generated_file=False,
                            format_contract=stdlib.source_contract(validate_scx(blob), apps[name]['manifest'], True))
                        native_provenance(report, artifacts)
                        native_provenance(report, libraries)
                        persist()
                    for name, markers in (('PLATFORMPROBE', PLATFORM_MARKERS), ('SCUITEXT', (text_marker,))):
                        app = PACKAGE + '/NATIVE_APPS/' + name + '.SCX'
                        harness.ordinary('G1-caller-' + name + '-actual-mio-session', app,
                            'all ' + app if name == 'PLATFORMPROBE' else '', markers,
                            timeout=600, identity=name == 'PLATFORMPROBE')
                        report['native_artifacts'][name + '.SCX']['executed_this_generated_file'] = True
                        report['caller_executions'].append(dict(name=name + '.SCX', source='ACTUAL_G1',
                            path=app, executed=True, libraries='SYS_LIB_HOST_GCC_FIVE', ordinary_uid=1000,
                            official_cases=3013 if name == 'SCUITEXT' else None))
                        native_provenance(report, artifacts)
                        native_provenance(report, libraries)
                        persist()
                    # 生成库独立保存，caller目录没有同名SCL；实际安装字节逐个get回确认。
                    for name in LIBRARIES:
                        harness.file('/SYS/LIB/' + name, 'installed-GCC-library-preserved-' + name,
                                     expected=(BUILD / name).read_bytes())
                    harness.screen('ui-platform-first-round-desktop')
                    report['status'] = 'DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS'
                    native_provenance(report, artifacts)
                    native_provenance(report, libraries)
                except BaseException as error:
                    report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                        message=str(error), traceback=traceback.format_exc()))
                    report['failure_console_tail'] = bytes(guest.console[-16384:]).decode('utf-8', 'replace')
                    report['failure_debug_tail'] = bytes(guest.debug[-16384:]).decode('utf-8', 'replace')
                    console_path = vm.out / 'ui-platform-failure.stdout.bin'
                    console_path.write_bytes(bytes(guest.console))
                    report['failure_stdout'] = dict(path=str(console_path), bytes=console_path.stat().st_size,
                                                     sha256=sha(console_path))
                    native_provenance(report, vm.out / 'native-artifacts')
                    native_provenance(report, vm.out / 'native-libraries')
                    try:
                        report['failure_registers'] = vm.hmp('info registers')
                        if harness:
                            harness.screen('ui-platform-failure')
                    except Exception as capture:
                        report['failure_capture_error'] = repr(capture)
                    raise
                finally:
                    try:
                        guest.close()
                    finally:
                        changed = [name for name, value in dict(proof_hashes, **hashes).items()
                                   if not (ROOT / name).is_file() or sha(ROOT / name) != value]
                        report['source_disk_unchanged'] = sha(args.data) == original
                        report['current_inputs_unchanged'] = not changed
                        report['changed_input_output_paths'] = changed
                        if changed or not report['source_disk_unchanged']:
                            report['status'] = 'FAIL_OR_INTERRUPTED'
                        native_provenance(report, vm.out / 'native-artifacts')
                        native_provenance(report, vm.out / 'native-libraries')
                        persist()
    except BaseException as error:
        args.out.mkdir(parents=True, exist_ok=True)
        report['status'] = 'FAIL_OR_INTERRUPTED'
        report.setdefault('error', dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc()))
        persist()
        raise
    if report['status'] == 'FAIL_OR_INTERRUPTED':
        raise AssertionError('输入/清理保持条件失败，不宣布客体通过')
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
