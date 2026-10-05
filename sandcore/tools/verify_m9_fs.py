#!/usr/bin/env python3
"""实际启动Windows QEMU，验证SandFS提交边界和损坏目录的拒绝挂载。

离线构造提交前后状态，不把这些用例说成真实掉电/扇区原子性证明。
每个变体和运行盘均另存，不改输入盘；坏卷也必须保留COM1诊断。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import threading
import time
import zlib

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ('inactive-corruption', 'uncommitted-directory', 'committed-directory',
            'superblock-crc', 'active-directory-crc', 'entry-reserved',
            'invalid-mode', 'zero-generation', 'negative-uid', 'duplicate-name',
            'overlapping-extents', 'missing-parent', 'false-disk-capacity')


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def put32(data, offset, value):
    struct.pack_into('<I', data, offset, value)


def baseline(path):
    data = bytearray(path.read_bytes())
    ds, count, bank = u32(data, 16), u32(data, 12), u32(data, 48)
    if (data[:9] != b'SANDFSMIO' or u32(data, 20) != 5 or
            ds not in (192, 384) or bank not in (0, 1) or
            u32(data, 508) != zlib.crc32(data[:508])):
        raise ValueError('输入不是有效v5超级块')
    active = 512 + bank * ds * 512
    inactive = 512 + (1-bank) * ds * 512
    if u32(data, 44) != zlib.crc32(data[active:active+ds*512]):
        raise ValueError('输入活动目录校验失败')
    names = [bytes(data[active+i*96:active+i*96+64]).split(b'\0')[0]
             for i in range(count)]
    tone = names.index(b'SYS/TEST/TONE.MP3')
    offset = active + tone * 96
    start, size = u32(data, offset+64), u32(data, offset+68)
    if not size or start*512+size > len(data):
        raise ValueError('缺少非空MP3基准文件')
    metadata = tuple(u32(data, offset+n) for n in (68, 72, 76, 80, 88))
    payload = bytes(data[start*512:start*512+size])
    return data, ds, count, active, inactive, tone, metadata, payload


def variant(source, destination, name):
    data, ds, count, active, inactive, tone, metadata, payload = baseline(source)
    offset = active + tone*96
    modified = metadata
    valid = name in VARIANTS[:3]
    if name == 'inactive-corruption':
        data[inactive+tone*96+92] ^= 1
    elif name in ('uncommitted-directory', 'committed-directory'):
        # 模拟新目录已写完整，但超级块尚未/已经切换的两个不同落盘状态。
        data[inactive:inactive+ds*512] = data[active:active+ds*512]
        target = inactive+tone*96
        generation = u32(data, 56)+1
        if generation > 0xFFFFFFFF:
            raise ValueError('对象代数无法为夹具递增')
        for field, value in ((72, 123456), (76, 77), (80, 3), (88, generation)):
            put32(data, target+field, value)
        if name == 'committed-directory':
            put32(data, 48, 1-u32(data, 48))
            put32(data, 52, u32(data, 52)+1)
            put32(data, 56, generation)
            put32(data, 44, zlib.crc32(data[inactive:inactive+ds*512]))
            put32(data, 508, zlib.crc32(data[:508]))
            modified = (metadata[0], 123456, 77, 3, generation)
    elif name == 'superblock-crc':
        data[508] ^= 1
    elif name == 'active-directory-crc':
        data[offset+92] ^= 1
    else:
        if name == 'entry-reserved':
            put32(data, offset+92, 1)
        elif name == 'invalid-mode':
            put32(data, offset+80, 64)
        elif name == 'zero-generation':
            put32(data, offset+88, 0)
        elif name == 'negative-uid':
            put32(data, offset+72, 0xFFFFFFFE)
        elif name in ('duplicate-name', 'overlapping-extents'):
            other = next(active+i*96 for i in range(count) if i != tone
                         and u32(data, active+i*96+68))
            if name == 'duplicate-name':
                data[other:other+64] = data[offset:offset+64]
            else:
                put32(data, other+64, u32(data, offset+64))
        elif name == 'missing-parent':
            path = b'FS_BAD_PARENT/TONE.MP3'
            data[offset:offset+64] = path.ljust(64, b'\0')
        elif name == 'false-disk-capacity':
            put32(data, 24, len(data)//512+1)
        else:
            raise ValueError('未知变体')
        # 结构性破坏重新生成正确CRC，才能证明不是只检查了CRC便放行。
        put32(data, 44, zlib.crc32(data[active:active+ds*512]))
        put32(data, 508, zlib.crc32(data[:508]))
    destination.write_bytes(data)
    return dict(name=name, expected_mount=valid, expected_metadata=modified,
                expected_sectors=len(data)//512, expected_directory_sectors=ds,
                input_sha256=sha(destination), payload_sha256=hashlib.sha256(payload).hexdigest()), payload


def symbols(path):
    result = {}
    for line in path.read_text(encoding='ascii').splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+[bBdD]\s+(\w+)', line)
        if match:
            result[match[2]] = int(match[1], 16)
    for name in ('fs_disk_sectors', 'dir_sectors'):
        if not 0x10000 <= result.get(name, 0) < 0x400000:
            raise ValueError('缺少同批内核低地址诊断符号: '+name)
    return result


def run(boot, data, out, fixture, payload, addresses):
    report = dict(status='RUNNING', fixture=fixture, cases=[], screenshots=[])
    with QemuSession(boot, data, out, 'tcg', True, audio=False) as vm:
        guest = Guest(vm)
        def check(name, actual, expected):
            report['cases'].append(dict(name=name, actual=actual, expected=expected,
                                        status='PASS' if actual == expected else 'FAIL'))
            if actual != expected:
                raise AssertionError(name+': '+repr(actual)+' != '+repr(expected))
        try:
            vm.pipes['debug'].write(b'hello\nhalt\n')
            guest.wait(rb'STOP vector=[0-9a-f]+ eip=[0-9a-f]+\r\n', debug=True)
            for name, expected in (('fs_disk_sectors', fixture['expected_sectors']),
                                   ('dir_sectors', fixture['expected_directory_sectors'])):
                at = len(guest.debug)
                vm.pipes['debug'].write(f'mem {addresses[name]:08x} 4\n'.encode())
                value = guest.wait(rb'DATA ([0-9a-f]{8})\r\n', at, debug=True)[1]
                actual = int.from_bytes(bytes.fromhex(value.decode()), 'little')
                check(name+'-real-COM1-memory', actual, expected if fixture['expected_mount'] else 0)
            at = len(guest.debug)
            vm.pipes['debug'].write(b'cont\n')
            guest.wait(rb'OK resume\r\n', at, debug=True)
            if fixture['expected_mount']:
                guest.connect()
                code, text, _, _ = guest.command('stat /SYS/TEST/TONE.MP3')
                check('stat-success', code, 0)
                match = re.search(rb'kind=1 bytes=(\d+) uid=(-?\d+) gid=(-?\d+) rw=(\d+) generation=(\d+)', text)
                check('committed-file-metadata', tuple(map(int, match.groups())) if match else None,
                      tuple(fixture['expected_metadata']))
                check('committed-file-bytes', hashlib.sha256(guest.get_bytes('/SYS/TEST/TONE.MP3')).hexdigest(),
                      hashlib.sha256(payload).hexdigest())
                code, text, _, _ = guest.command('echo M9-FS-COMMIT-ALIVE')
                check('SYSTEM-Shell-alive', code == 0 and b'M9-FS-COMMIT-ALIVE' in text.splitlines(), True)
            else:
                status, body = guest.client.request(1, struct.pack('<I', 1))
                check('no-Shell-on-invalid-volume', status, -4)
                check('HELLO-protocol-body', body.hex(), struct.pack('<II', 1, 512).hex())
                status, _ = guest.client.request(3, struct.pack('<I', 1)+b'\0'*32+b'/TMP/REJECT\0')
                check('no-management-write-session', status, -5)
            # HMP实际采屏保留拒绝挂载/正常启动的客体外观，不拿宿主预览充证据。
            screenshot = vm.out/'filesystem.ppm'
            vm.hmp('screendump "'+screenshot.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            report['status'] = 'FS_BOUNDARY_CASES_PASS_REMAINDER_PENDING'
        except BaseException as error:
            report.update(status='FAIL', failure=repr(error))
            raise
        finally:
            (out/'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
            guest.close()
    if vm.report['exit_code'] != 0 or not all(vm.report['source_unchanged'].values()):
        raise AssertionError('退出失败或变体源盘改变')
    return report


def debug_only(boot, data, out, addresses):
    """驱动发布工具本身，而不是只绕过宿主工具测内核API。"""
    out.mkdir(parents=True, exist_ok=False)
    process = subprocess.Popen([sys.executable, '-u', str(ROOT/'tools/scserial.py'),
            '--boot', str(boot.resolve()), '--data', str(data.resolve()),
            '--out', str((out/'session').resolve()), '--headless', '--debug-only'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=dict(os.environ, PYTHONIOENCODING='utf-8'), bufsize=0)
    condition = threading.Condition()
    output = bytearray()
    def read():
        while block := process.stdout.read(1):
            with condition:
                output.extend(block)
                condition.notify_all()
        with condition:
            condition.notify_all()
    reader = threading.Thread(target=read, name='M9-debug-only-menu')
    reader.start()
    report = dict(status='RUNNING', scope='PUBLISHED_HOST_TOOL_COM1_WITH_INVALID_FS')
    def wait(pattern, start=0, timeout=60):
        expression = re.compile(pattern)
        deadline = time.monotonic()+timeout
        with condition:
            while True:
                match = expression.search(output, start)
                if match:
                    return match
                if process.poll() is not None:
                    raise RuntimeError('独立调试工具提前退出')
                remaining = deadline-time.monotonic()
                if remaining <= 0:
                    raise TimeoutError('独立调试菜单超时: '+repr(pattern))
                condition.wait(min(.1, remaining))
    def send(command):
        process.stdin.write((command+'\n').encode('utf-8'))
        process.stdin.flush()
    try:
        wait('COM1独立调试模式'.encode())
        send('hello')
        wait(rb'SCDBG1 hello halt regs mem')
        send('halt')
        wait(rb'STOP vector=[0-9a-f]+ eip=[0-9a-f]+\r\n')
        start = len(output)
        send(f'mem {addresses["fs_disk_sectors"]:08x} 4')
        wait(rb'DATA 00000000\r\n', start)
        start = len(output)
        send('regs')
        wait(rb'cr3=[0-9a-f]+\r\n', start)
        send(':management')
        wait('独立调试模式不连接COM2'.encode())
        send(':put unused /TMP/REJECT')
        wait('独立COM1模式不传文件'.encode())
        send(':hmp screendump "'+(out/'debug-only.ppm').resolve().as_posix()+'"')
        start = len(output)
        send('cont')
        wait(rb'OK resume\r\n', start)
        send(':quit')
        process.wait(timeout=15)
        reader.join(timeout=3)
        state = json.loads((out/'session/session.json').read_text(encoding='utf-8'))
        if process.returncode or state['exit_code'] or not all(state['source_unchanged'].values()):
            raise AssertionError('独立COM1退出失败或源镜像改变')
        # COM2固定开机公告不是HELLO/会话回复；独立模式只允许这行。
        if ((out/'session/management.bin').read_bytes() != b'SandCore M9 | management transport ready\r\n'
                or b'[SYSTEM] # ' in output):
            raise AssertionError('独立COM1不应悄悄建立COM2 SYSTEM会话')
        report.update(status='PASS', host_exit_code=process.returncode, qemu_exit_code=state['exit_code'],
                      source_unchanged=state['source_unchanged'], screenshot=png_from_ppm(out/'debug-only.ppm'))
    except BaseException as error:
        report.update(status='FAIL', failure=repr(error))
        raise
    finally:
        if process.poll() is None:
            try:
                send(':quit')
                process.wait(timeout=15)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate()
                process.wait(timeout=5)
        reader.join(timeout=3)
        (out/'console.bin').write_bytes(output)
        (out/'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--variant', choices=VARIANTS, action='append')
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) < 2 or len(set(p.resolve() for p in args.data)) != len(args.data):
        parser.error('使用Windows Python，提供至少两份不同来源v5盘')
    if args.variant and len(set(args.variant)) != len(args.variant):
        parser.error('变体不能重复计数')
    addresses = symbols(args.symbols)
    args.out.mkdir(parents=True, exist_ok=False)
    inputs = args.out/'inputs'
    inputs.mkdir()
    originals = {str(p.resolve()): sha(p) for p in args.data}
    matrix = dict(status='RUNNING', scope='OFFLINE_COMMIT_BOUNDARIES_AND_CORRUPT_V5_MOUNTS',
                  not_proven=['physical power loss', 'torn-sector atomicity', 'all runtime I/O failures'], runs=[])
    try:
        for i, source in enumerate(args.data, 1):
            for name in args.variant or VARIANTS:
                data = inputs/f'disk-{i}-{name}.img'
                fixture, payload = variant(source, data, name)
                result = run(args.boot, data, args.out/f'disk-{i}-{name}', fixture, payload, addresses)
                matrix['runs'].append(result)
                print(f'disk {i} {name}: {len(result["cases"])} cases PASS', flush=True)
            if i == 1 and 'superblock-crc' in (args.variant or VARIANTS):
                matrix['host_debug_only'] = debug_only(args.boot, inputs/f'disk-{i}-superblock-crc.img',
                                                      args.out/'host-debug-only', addresses)
                print('published host --debug-only with invalid FS PASS', flush=True)
        matrix['status'] = 'FS_BOUNDARY_CASES_PASS_REMAINDER_PENDING'
    except BaseException as error:
        matrix.update(status='FAIL', failure=repr(error))
        raise
    finally:
        matrix['original_source_unchanged'] = {p: sha(p) == digest for p, digest in originals.items()}
        (args.out/'matrix.json').write_text(json.dumps(matrix, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if not all(matrix['original_source_unchanged'].values()):
        raise AssertionError('原始测试输入被修改')


if __name__ == '__main__':
    main()
