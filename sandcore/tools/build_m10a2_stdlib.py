#!/usr/bin/env python3
"""新Root/SC32/标准库/通用库与SCCC统一定向构建，记录准确来源。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tag',required=True)
    args=parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    out=ROOT/'build/m10a2-stdlib-bootstrap';out.mkdir(parents=True,exist_ok=True)
    log=out/(args.tag+'.log')
    if log.exists():raise FileExistsError('不能覆盖已保存的构建记录')
    command=['make','-j4','m10a2-stdlib-bootstrap'];started=time.monotonic();started_ns=time.time_ns()
    with log.open('xb') as stream:
        code=subprocess.call(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    inputs=set()
    for dep in out.rglob('*.d'):
        for row in dep.read_text().replace('\\\n',' ').splitlines():
            if ':' not in row:continue
            for name in row.split(':',1)[1].split():
                path=(ROOT/name).resolve()
                if path.is_file() and path.is_relative_to(ROOT) and not path.is_relative_to(out):inputs.add(path)
    names=('Makefile','m10a2.mk','m9.mk','m10.mk','core.ld','boot/loader.ld','user/start.asm',
        'user/linker.ld','boot/boot.asm','boot/loader_entry.asm','kernel/core_entry.asm',
        'tools/mkcore.py','tools/mkimg.py','tools/mkscx.py','tools/mkscl.py','tools/scl_format.py',
        'tools/scd_format.py','user/runtime/SCLROOT.json','user/sc32/SC32API.json',
        'user/stdlib/SCCLIB.json','user/sclib/SCLIB.json','tests/m10a2/STDLIBGUARD.json','tests/m10a2/STDLIBPROBE.json',
        'tests/m10a2/UTILPROBE.json')
    inputs.update(ROOT/name for name in names)
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    report=dict(scope='M10A2_ROOTTERM_SC32_STDLIB_UTILITY_AND_COMPILER_HOST_BUILD',exit_code=code,
        command=command,wall_seconds=time.monotonic()-started,
        inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for p in sorted(inputs)],
        source_files_modified_during_build=[p.relative_to(ROOT).as_posix() for p in sorted(inputs)
            if p.stat().st_mtime_ns>started_ns],outputs=[])
    for name in ('sandcore.img','fs/SYS/CORE/CORE.SKM','fs/bin/sccc.scx','SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL',
        'STDLIBGUARD.SCL','STDLIBPROBE.SCX','UTILPROBE.SCX'):
        path=out/name
        if path.is_file():report['outputs'].append(dict(path=path.relative_to(ROOT).as_posix(),
            bytes=path.stat().st_size,sha256=digest(path)))
    (out/(args.tag+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('\n'.join(log.read_text(errors='replace').splitlines()[-18:]));print('BUILD_EXIT',code,flush=True)
    return code

if __name__=='__main__':raise SystemExit(main())
