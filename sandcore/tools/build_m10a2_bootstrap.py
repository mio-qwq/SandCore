#!/usr/bin/env python3
"""用WSL ELF工具链定向构建SCL切片，保留精确退出码和输入摘要。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('tag只能为小写字母、数字或连字符')
    build = ROOT/'build/m10a2-bootstrap'
    build.mkdir(parents=True, exist_ok=True)
    log = build/(args.tag+'.log')
    if log.exists():
        raise FileExistsError('构建日志已存在，不能覆盖旧证据')
    command = ['make', '-j4', 'm10a2-bootstrap']
    started = time.monotonic()
    with log.open('xb') as stream:
        code = subprocess.call(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    inputs = set()
    for dep in build.rglob('*.d'):
        content = dep.read_text().replace('\\\n', ' ')
        for row in content.splitlines():
            if ':' not in row:
                continue
            for name in row.split(':', 1)[1].split():
                path = ROOT/name
                if path.is_file():
                    inputs.add(path)
    for name in ('Makefile', 'm10a2.mk', 'm9.mk', 'm10.mk', 'core.ld', 'user/linker.ld',
                 'tools/mkscl.py', 'tools/scl_format.py', 'tools/mkscx.py',
                 'tools/mkcore.py', 'tools/mkimg.py', 'tests/m10a2/libbase.json',
                 'tests/m10a2/libupper.json', 'user/start.asm', 'boot/boot.asm',
                 'kernel/core_entry.asm', 'boot/loader_entry.asm', 'boot/loader.ld'):
        inputs.add(ROOT/name)
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(scope='M10A2_SCL_HOST_BOOTSTRAP_BUILD', command=command,
                  exit_code=code, wall_seconds=time.monotonic()-started,
                  inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for p in sorted(inputs)],
                  outputs=[dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=digest(p))
                           for p in (build/'sandcore.img',build/'fs/SYS/CORE/CORE.SKM',
                                     build/'SCLPROBE.SCX',build/'BASE.SCL',build/'UPPER.SCL') if p.is_file()])
    (build/(args.tag+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('\n'.join(log.read_text(errors='replace').splitlines()[-12:]))
    print('BUILD_EXIT',code,flush=True)
    return code

if __name__ == '__main__':
    raise SystemExit(main())
