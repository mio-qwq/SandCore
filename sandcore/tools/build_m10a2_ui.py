#!/usr/bin/env python3
"""SCUI/平台本批实现收齐后统一定向构建并固定输入/产物来源。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUTPUTS = ('sandcore.img', 'fs/SYS/CORE/CORE.SKM', 'core.sym', 'core.elf', 'core.bin', 'fs/bin/sccc.scx',
           'SCLDR.SCL', 'SC32API.SCL', 'SCCLIB.SCL', 'SCLIB.SCL', 'SCUI.SCL',
           'SCUIDEMO.SCX', 'PLATFORMPROBE.SCX', 'SCUITEXT.SCX',
           'AGGCALLEE.SCL', 'AGGPROBE.SCX',
           'fs/SYS/UI/UAX15_1.SCU')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_paths():
    paths = set()
    for folder in ('kernel', 'user/runtime', 'user/sc32', 'user/guiapi', 'user/fontapi',
                   'user/brokerapi', 'user/dropapi', 'user/stdlib', 'user/sclib',
                   'user/scui', 'user/textdata', 'third_party/unicode15_1',
                   'tests/m10a2/unicode15_1', 'assets/ui'):
        for path in (ROOT / folder).rglob('*'):
            if path.is_file():
                paths.add(path)
    paths.update((ROOT / 'user').glob('*.H'))
    paths.update((ROOT / 'user').glob('s3c*'))
    for name in ('Makefile', 'm9.mk', 'm10.mk', 'm10a2.mk', 'core.ld', 'boot/loader.ld',
                 'boot/boot.asm', 'boot/loader_entry.asm', 'boot/loader.c', 'user/sccc.c',
                 'user/start.asm', 'user/linker.ld', 'user/COMPILEUI.inc',
                 'third_party/M10A2-SOURCES.json', 'docs/THIRD-PARTY.md', 'tools/mkui_textdata.py',
                 'tools/mkui_texttests.py', 'tools/audit_third_party.py', 'tools/mkcore.py',
                 'tools/mkimg.py', 'tools/mkscx.py', 'tools/mkscl.py', 'tools/mkdscx.py',
                 'tools/scl_format.py', 'tools/scd_format.py', 'tools/build_m10a2_ui.py',
                 'tests/m10a2/SCUIDEMO.C', 'tests/m10a2/SCUIDEMO.json',
                 'tests/m10a2/PLATFORMPROBE.C', 'tests/m10a2/PLATFORMPROBE.json',
                 'tests/m10a2/SCUITEXT.C', 'tests/m10a2/SCUITEXT.json'):
        paths.add(ROOT / name)
    for name in ('AGGABI.H', 'AGGCALLEE.C', 'AGGCALLEE.json', 'AGGPROBE.C', 'AGGPROBE.json'):
        paths.add(ROOT / 'tests/m10a2' / name)
    for name in ('SWDEFAULT.C','DEFAULTAPI.C','DEFAULTAPI.json'):
        paths.add(ROOT / 'tests/m10a2' / name)
    return {p.resolve() for p in paths if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    out = ROOT / 'build/m10a2-ui-bootstrap'
    out.mkdir(parents=True, exist_ok=True)
    log = out / (args.tag + '.log')
    if log.exists() or log.with_suffix('.json').exists():
        raise FileExistsError('不能覆盖已保存的构建记录')
    inputs = input_paths()
    before = {p: digest(p) for p in inputs}
    command = ['make', '-j4', 'm10a2-ui-bootstrap']
    start = time.monotonic()
    with log.open('xb') as stream:
        code = subprocess.call(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    for dep in out.rglob('*.d'):
        for row in dep.read_text().replace('\\\n', ' ').splitlines():
            if ':' not in row:
                continue
            for name in row.split(':', 1)[1].split():
                path = (ROOT / name).resolve()
                if path.is_file() and path.is_relative_to(ROOT) and not path.is_relative_to(out):
                    inputs.add(path)
    changed = [p.relative_to(ROOT).as_posix() for p, value in before.items() if not p.is_file() or digest(p) != value]
    report = dict(scope='M10A2_SCUI_PLATFORM_COMPILER_UNIFIED_HOST_BUILD', exit_code=code,
        command=command, wall_seconds=time.monotonic() - start,
        source_files_modified_during_build=sorted(changed),
        inputs=[dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p)) for p in sorted(inputs)],
        outputs=[])
    for name in OUTPUTS:
        path = out / name
        if path.is_file():
            report['outputs'].append(dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=digest(path)))
    log.with_suffix('.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('\n'.join(log.read_text(errors='replace').splitlines()[-22:]))
    print('BUILD_EXIT', code, flush=True)
    return code


if __name__ == '__main__':
    raise SystemExit(main())
