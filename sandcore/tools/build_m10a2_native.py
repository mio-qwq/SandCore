#!/usr/bin/env python3
"""只构建SCCC CLI启动产物；记录新编译器实际大小和来源，不生成新盘。"""
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
    out=ROOT/'build/m10a2-native-bootstrap';out.mkdir(parents=True,exist_ok=True)
    log=out/(args.tag+'.log')
    if log.exists():
        raise FileExistsError('不能覆盖既有构建日志')
    command=['make','-j4','m10a2-native-compiler']
    started=time.monotonic()
    with log.open('xb') as stream:
        code=subprocess.call(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    inputs=set()
    for dep in out.glob('*.d'):
        for row in dep.read_text().replace('\\\n',' ').splitlines():
            if ':' in row:
                for name in row.split(':',1)[1].split():
                    path=(ROOT/name).resolve()
                    if path.is_file() and path.is_relative_to(ROOT):
                        inputs.add(path)
    for name in ('Makefile','m10a2.mk','m9.mk','m10.mk','user/start.asm',
                 'user/linker.ld','tools/mkscx.py'):
        inputs.add(ROOT/name)
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    image=out/'fs/bin/sccc.scx'
    report=dict(scope='M10A2_SCCC_HOST_BOOTSTRAP_BUILD',exit_code=code,
                command=command,wall_seconds=time.monotonic()-started,
                inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for p in sorted(inputs)])
    if image.is_file():
        blob=image.read_bytes()
        fields=struct.unpack_from('<7I',blob,8)
        report['compiler']=dict(bytes=len(blob),sha256=digest(image),entry=fields[0],
            load_size=fields[1],bss_size=fields[2],stack_size=fields[3],base=fields[5],
            image_and_bss=fields[1]+fields[2],guest_compiled=False)
    (out/(args.tag+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('\n'.join(log.read_text(errors='replace').splitlines()[-10:]))
    print('BUILD_EXIT',code,flush=True)
    return code

if __name__=='__main__':
    raise SystemExit(main())
