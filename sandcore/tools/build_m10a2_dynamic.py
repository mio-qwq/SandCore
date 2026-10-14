#!/usr/bin/env python3
"""定向构建动态SCX、独立三环SCLDR根及对应内核，不构建冻结游戏。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag',required=True)
    args=parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    out=ROOT/'build/m10a2-dynamic-bootstrap';out.mkdir(parents=True,exist_ok=True)
    log=out/(args.tag+'.log')
    if log.exists():
        raise FileExistsError('不能覆盖已有构建记录')
    command=['make','-j4','m10a2-dynamic-bootstrap'];started=time.monotonic()
    with log.open('xb') as stream:
        code=subprocess.call(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    inputs=set()
    for dep in out.rglob('*.d'):
        for row in dep.read_text().replace('\\\n',' ').splitlines():
            if ':' in row:
                for name in row.split(':',1)[1].split():
                    path=(ROOT/name).resolve()
                    if path.is_file() and path.is_relative_to(ROOT):inputs.add(path)
    for name in ('Makefile','m10a2.mk','m9.mk','m10.mk','user/start.asm','user/linker.ld',
                 'core.ld','boot/loader.ld','boot/boot.asm','boot/loader_entry.asm',
                 'kernel/core_entry.asm','tools/mkscx.py','tools/mkcore.py','tools/mkimg.py',
                 'tools/mkscl.py','tools/scl_format.py','tools/mkdscx.py','tools/scd_format.py',
                 'user/runtime/SCLROOT.json','tests/m10a2/DYNAPP.json'):
        inputs.add(ROOT/name)
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    report=dict(scope='M10A2_DYNAMIC_SCX_BOOTSTRAP_BUILD',exit_code=code,command=command,
        wall_seconds=time.monotonic()-started,
        inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for p in sorted(inputs)])
    report['outputs']=[]
    for name in ('sandcore.img','fs/SYS/CORE/CORE.SKM','fs/bin/sccc.scx','SCLDR.SCL','DYNAPP.SCX'):
        path=out/name
        if path.is_file():
            item=dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,sha256=digest(path))
            if path.suffix.upper()=='.SCX':
                values=struct.unpack_from('<7I',path.read_bytes(),8)
                item.update(load=values[1],bss=values[2],image_and_bss=values[1]+values[2])
            report['outputs'].append(item)
    (out/(args.tag+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('\n'.join(log.read_text(errors='replace').splitlines()[-20:]))
    print('BUILD_EXIT',code,flush=True);return code

if __name__=='__main__':raise SystemExit(main())
