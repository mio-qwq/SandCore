#!/usr/bin/env python3
"""在独立坏主核盘验证原恢复核，不能用当前候选替换恢复副本冒充验收。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import traceback

from mkcore import build as pack_core, inspect_elf
from mkfs_m10 import check_main, mapped, release_views
from mkfs_m9 import read_image
from m10_verification_report import checkpoint
from scserial import sha
from verify_m10_core import artifacts, prepare, run_profile, specification, symbol_table

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL_RECOVERY_SHA = 'fa9f0d396a873aecac87052bd4487a168c6ef492087a3da089dd76d8b4b91cee'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--recovery-build', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--legacy-wall', type=Path, required=True)
    parser.add_argument('--openssl', default='openssl')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) != 2 or len({p.resolve(strict=True) for p in args.data}) != 2:
        parser.error('需要Windows Python与两个不同来源盘')
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='RUNNING', scope='ORIGINAL_PRESERVED_RECOVERY_AND_USER_SIGNED_EXTENSIONS_ONLY',
                  private_key_access='NONE', disks=[])
    matrix = args.out/'preserved-recovery-matrix.json'
    checkpoint(matrix, report)
    try:
        key, signed = artifacts(args.receipt, args.openssl)
        current = args.core.read_bytes()
        recovery_path = args.recovery_build/'fs/SYS/CORE/CORE.SKM'
        recovery = recovery_path.read_bytes()
        check_main(current)
        check_main(recovery)
        if sha(recovery_path) != ORIGINAL_RECOVERY_SHA or current == recovery or key not in recovery[128:]:
            raise ValueError('需要原fa9f0d39恢复核、不同的当前主核和相同用户公钥')
        # 原ELF、平载荷、符号和包装头须一致；不能用新核符号观察旧核。
        reproduced = args.out/'original-recovery-repacked.SKM'
        pack_core(args.recovery_build/'core.elf', args.recovery_build/'core.bin', reproduced)
        if reproduced.read_bytes() != recovery:
            raise ValueError('原恢复核与原ELF/平载荷不一致')
        symbols_path = args.recovery_build/'core.sym'
        symbols = symbol_table(symbols_path)
        elf_symbols = inspect_elf(args.recovery_build/'core.elf')
        for name in ('boot_stage', 'sc_ticks', 'wm_frames', 'snapshot', 'residents',
                     'allocations', 'callbacks', 'irq_heads', 'service_head', 'scene_owner'):
            if symbols.get(name) != elf_symbols.get(name) or name not in symbols:
                raise ValueError('原恢复核符号不匹配：'+name)
        files = [(ROOT/'tools/verify_m10_preserved_recovery.py', 'tools/verify_m10_preserved_recovery.py'),
                 (ROOT/'tools/verify_m10_core.py', 'tools/verify_m10_core.py'),
                 (ROOT/'tools/scserial.py', 'tools/scserial.py'),
                 (ROOT/'tools/m10_guest_boot.py', 'tools/m10_guest_boot.py'),
                 (ROOT/'tools/verify_m9.py', 'tools/verify_m9.py'),
                 (ROOT/'tests/m10/M10CORE.C', 'tests/m10/M10CORE.C'),
                 (ROOT/'user/SCAPI.H', 'user/SCAPI.H'), (ROOT/'user/SCIO.H', 'user/SCIO.H'),
                 (recovery_path, 'original/CORE.SKM'), (args.recovery_build/'core.elf', 'original/core.elf'),
                 (args.recovery_build/'core.bin', 'original/core.bin'), (symbols_path, 'original/core.sym'),
                 (args.core, 'current/CORE.SKM'), (args.boot, 'loader/sandcore.img')]
        manifest = []
        for source, name in files:
            blob = source.read_bytes()
            destination = args.out/'source-freeze'/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(blob)
            manifest.append(dict(path=name, bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest()))
        report.update(current_core_sha256=sha(args.core), original_recovery_sha256=sha(recovery_path),
                      original_recovery_payload_sha256=recovery[72:104].hex(),
                      original_symbols_sha256=sha(symbols_path), public_key_sha256=hashlib.sha256(key).hexdigest(),
                      original_recovery_reproduced_from_elf=True, frozen_files=manifest)
        checkpoint(matrix, report)
        legacy = args.legacy_wall.read_bytes()
        for number, source in enumerate(args.data, 1):
            # 先核实际来源的恢复文件，随后只在测试副本构造坏主核和
            # 已由用户签署的正向/失败初始化模块集合；源盘保持只读。
            with ExitStack() as stack:
                records = read_image(mapped(stack, source), False)
                stack.callback(lambda: release_views(records))
                if bytes(records['SYS/RECOVERY/CORE.SKM'].payload) != recovery:
                    raise ValueError('来源恢复文件不是原fa9f0d39字节')
                if bytes(records['SYS/CORE/CORE.SKM'].payload) != current:
                    raise ValueError('来源主核与指定当前候选不一致')
            spec = specification('recovery', signed, current, legacy)
            spec['recovery'] = recovery
            prepared = args.out/f'disk-{number}-original-recovery-input.img'
            preparation = prepare(source, prepared, spec)
            checkpoint(prepared.with_suffix('.preparation.json'), preparation)
            outcome = run_profile(args.boot, prepared, args.out/f'disk-{number}-original-recovery',
                                  'recovery', spec, symbols, recovery[72:104].hex())
            if outcome['status'] != 'DECLARED_CASES_PASS' or sha(source) != preparation['source_sha256']:
                raise AssertionError('原恢复核未通过或只读来源改变')
            report['disks'].append(dict(source=str(source.resolve()), preparation=preparation, outcome=outcome))
            checkpoint(matrix, report)
            print(f'disk-{number}/original-preserved-recovery: PASS', flush=True)
        report['status'] = 'TWO_SOURCES_ORIGINAL_PRESERVED_RECOVERY_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                      message=str(error), traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(matrix, report)


if __name__ == '__main__':
    main()
