#!/usr/bin/env python3
"""坏TTF独立副本的真实启动/原子激活/旧字形回退；不作任意字体承诺。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

from mkfs_m10 import mapped, release_views, digest_file, digest_payload, write_image
from mkfs_m9 import read_image, Record
from m10_guest_boot import unique_symbols, wait_first_desktop
from m10_failure_snapshot import close_guest, failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ('original', 'missing12', 'missing16', 'both-missing', 'truncated12',
            'bad-table16', 'bad-cmap16', 'bad-loca16', 'bad-outline16')


def tables(raw):
    return {raw[12+n*16:16+n*16]:dict(record=12+n*16, offset=struct.unpack_from('>I', raw, 20+n*16)[0],
                                    bytes=struct.unpack_from('>I', raw, 24+n*16)[0])
            for n in range(struct.unpack_from('>H', raw, 4)[0])}


def damaged(raw, profile):
    if profile == 'truncated12':
        return raw[:12]
    blob = bytearray(raw)
    directory = tables(raw)
    if profile == 'bad-table16':
        struct.pack_into('>I', blob, directory[b'cmap']['record']+8, 0xFFFFFFFC)
        return bytes(blob)
    # 深层用例重新计算被修改表的checksum，避免只在校验和处就拒绝，
    # 误称已验证cmap/loca/轮廓的真实结构边界。上游原字节不改。
    tag = b'cmap' if profile == 'bad-cmap16' else b'loca' if profile == 'bad-loca16' else b'glyf'
    item = directory[tag]
    if tag == b'cmap':
        struct.pack_into('>I', blob, item['offset']+8, 0xFFFFFFFC)
    elif tag == b'loca':
        short = struct.unpack_from('>H', raw, directory[b'head']['offset']+50)[0] == 0
        struct.pack_into('>H' if short else '>I', blob, item['offset'], 0xFFFF if short else 0xFFFFFFFF)
    else:
        # 首个非空glyph是.notdef的真实简单轮廓；负轮廓数不得作为
        # 复合字形误读成巨大点列表，整张脸应原子拒绝并释放暂存。
        struct.pack_into('>h', blob, item['offset'], -1)
    body = bytes(blob[item['offset']:item['offset']+item['bytes']])
    body += bytes((-len(body)) % 4)
    checksum = sum(struct.unpack('>'+str(len(body)//4)+'I', body)) & 0xFFFFFFFF
    struct.pack_into('>I', blob, item['record']+4, checksum)
    return bytes(blob)


def prepare(source, destination, profile):
    original_sha = digest_file(source)
    mask = 3
    with ExitStack() as stack:
        image = mapped(stack, source)
        records = read_image(image, copy_payload=False)
        stack.callback(lambda: release_views(records))
        original = {key:(digest_payload(record.payload), record.uid, record.gid, record.mode, record.generation)
                    for key, record in records.items()}
        controlled = []
        root = struct.unpack_from('<iiI', image, 32)
        commit, counter = struct.unpack_from('<II', image, 52)
        counter = max(counter, max(record.generation for record in records.values()))
        if commit == 0xFFFFFFFF:
            raise ValueError('目录代数耗尽')
        for face in (12, 16):
            name = f'SYS/FONT/PHOENIX{face}.TTF'
            missing = profile == f'missing{face}' or profile == 'both-missing'
            malformed = profile == 'truncated12' and face == 12 or profile.startswith('bad-') and face == 16
            if not missing and not malformed:
                continue
            prior = records[name]
            controlled.append(name)
            mask &= ~(1 if face == 12 else 2)
            if missing:
                del records[name]
            else:
                body = damaged(bytes(prior.payload), profile)
                if counter == 0xFFFFFFFF:
                    raise ValueError('对象代数耗尽')
                counter += 1
                records[name] = Record(prior.name, body, prior.uid, prior.gid, prior.mode, counter)
            # 替换的旧视图不再在records里，由原输入映射拥有者先撤销。
            if isinstance(prior.payload, memoryview):
                prior.payload.release()
        directory, count, used = write_image(destination, records, len(image)//1048576, root, commit+1, counter)
        with ExitStack() as check_stack:
            copied = mapped(check_stack, destination)
            checked = read_image(copied, copy_payload=False)
            check_stack.callback(lambda: release_views(checked))
            for key, record in records.items():
                actual = checked[key]
                if (digest_payload(actual.payload), actual.uid, actual.gid, actual.mode, actual.generation) != (
                        digest_payload(record.payload), record.uid, record.gid, record.mode, record.generation):
                    raise AssertionError('副本回读不符：'+key)
            for key, before in original.items():
                if key in controlled:
                    continue
                record = checked[key]
                if (digest_payload(record.payload), record.uid, record.gid, record.mode, record.generation) != before:
                    raise AssertionError('其它原对象被改变：'+key)
        if digest_file(source) != original_sha:
            raise AssertionError('只读来源改变')
        return dict(status='PREPARED_RUNTIME_PENDING', profile=profile, source_sha256=original_sha,
                    source_unchanged=True, controlled_paths=controlled, preserved_objects=len(original)-len(controlled),
                    active_mask=mask, output_sha256=digest_file(destination), entries=count,
                    directory_sectors=directory, data_end=used)


def run_profile(boot, data, symbols, preparation, out):
    report = dict(status='RUNNING', scope='DECLARED_TTF_REJECTION_AND_FALLBACK_ONLY',
                  preparation=preparation, cases=[], screenshots=[])
    with QemuSession(boot, data, out, 'tcg', True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        try:
            guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n', timeout=60, debug=True)
            wait_first_desktop(guest, symbols, report)
            guest.connect()
            body = (ROOT/'tests/m10/M10FBAD.C').read_bytes()
            report['fixture_source_sha256'] = hashlib.sha256(body).hexdigest()
            guest.put_bytes(body, '/TMP/M10FBAD.C', 'font-rejection-source')
            run_case(guest, report, 'native-bad-font-probe', 's3c /TMP/M10FBAD.C /TMP/M10FBAD.SCX', timeout=240)
            run_case(guest, report, 'atomic-face-rejection-and-original-ABI',
                     '/TMP/M10FBAD.SCX '+str(preparation['active_mask']), (b'bad_font_failures=0',), timeout=120)
            vm.hmp('sendkey esc')
            path = vm.out/'font-rejection-desktop.ppm'
            vm.hmp('screendump "'+path.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(path))
            report['status'] = 'DECLARED_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                          message=str(error), traceback=traceback.format_exc()))
            failure_snapshot(vm, symbols, report)
            raise
        finally:
            try:
                close_guest(guest, report)
            finally:
                report['input_unchanged'] = sha(data) == preparation['output_sha256']
                checkpoint(vm.out/'font-edges.json', report)
    if not report['input_unchanged']:
        raise AssertionError('原输入副本改变')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--profiles', nargs='+', choices=PROFILES, default=list(PROFILES))
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) < 2 or len(set(p.resolve(strict=True) for p in args.data)) != len(args.data):
        parser.error('需要Windows Python和双来源只读盘')
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='RUNNING', scope='DECLARED_BAD_TTF_CASES_NOT_WHOLE_FONT', disks=[],
                  remaining=['scaled glyph independent pixels', 'OOM/cache fallback and reinitialization',
                             'Notes/other GUI all layouts and original old SCX'])
    checkpoint(args.out/'font-edges-matrix.json', report)
    try:
        symbols = unique_symbols(args.symbols)
        for n, source in enumerate(args.data, 1):
            disk = dict(source=str(source.resolve()), profiles=[])
            report['disks'].append(disk)
            for profile in args.profiles:
                data = args.out/f'disk-{n}-{profile}-input.img'
                preparation = prepare(source, data, profile)
                checkpoint(data.with_suffix('.preparation.json'), preparation)
                disk['profiles'].append(run_profile(args.boot, data, symbols, preparation,
                                                    args.out/f'disk-{n}-{profile}'))
                checkpoint(args.out/'font-edges-matrix.json', report)
                print(f'disk-{n}/{profile}: DECLARED_CASES_PASS', flush=True)
            disk['source_unchanged'] = sha(source) == disk['profiles'][0]['preparation']['source_sha256']
            if not disk['source_unchanged']:
                raise AssertionError('只读源盘改变')
        report['status'] = 'DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__, message=str(error)))
        raise
    finally:
        checkpoint(args.out/'font-edges-matrix.json', report)


if __name__ == '__main__':
    main()
