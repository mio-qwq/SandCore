#!/usr/bin/env python3
"""GUI06 default分派故障的单一后端定位：运行无syscall SCX的真实main机器字节。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapter',type=Path,required=True)
    parser.add_argument('--tag',required=True)
    args=parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    out=ROOT/'build/switch-diagnosis'/args.tag
    out.mkdir(parents=True,exist_ok=False)
    source=ROOT/'tests/m10a2/SWDEFAULT.C'
    adapter=args.adapter.resolve(strict=True)
    vfs=out/'VFS';(vfs/'HOME').mkdir(parents=True)
    (vfs/'HOME/SWDEFAULT.C').write_bytes(source.read_bytes())
    report=dict(scope='SCCC_HOST_ADAPTER_NO_SYSCALL_SWITCH_MACHINE_FLOW',
        guest_compilation=False,guest_execution=False,
        source_sha256=sha(source),adapter_sha256=sha(adapter),cases=[])
    def run(label,argv):
        value=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True)
        log=out/(label+'.log');log.write_text(value.stdout+value.stderr,encoding='utf-8')
        report['cases'].append(dict(name=label,command=argv,exit_code=value.returncode,log_sha256=sha(log)))
        return value.returncode
    status=1
    try:
        if run('compile',[str(adapter),'/',str(vfs),'/HOME/SWDEFAULT.C','/HOME/SWDEFAULT.SCX','--legacy','','0']):return 1
        image=vfs/'HOME/SWDEFAULT.SCX';body=image.read_bytes()
        if body[:8]!=b'SCX1MIO\0' or body[36]!=0xe8:raise ValueError('expected canonical legacy call-main CRT')
        entry=0x400000+5+struct.unpack_from('<i',body,37)[0]
        payload=out/'payload.bin';payload.write_bytes(body[36:])
        assembly=out/'run.s'
        assembly.write_text('.section .app,"ax"\n.incbin "'+str(payload)+'"\n'
            '.section .text\n.global _start\n_start:\nmov $'+str(entry)+',%eax\ncall *%eax\n'
            'mov %eax,%ebx\nmov $1,%eax\nint $0x80\n',encoding='ascii')
        linker=out/'run.ld';linker.write_text('ENTRY(_start)\nSECTIONS { . = 0x300000; .text : { *(.text) } . = 0x400000; .app : { *(.app) } }\n',encoding='ascii')
        if run('assemble',['as','--32',str(assembly),'-o',str(out/'run.o')]):return 1
        if run('link',['ld','-m','elf_i386','-T',str(linker),str(out/'run.o'),'-o',str(out/'run')]):return 1
        status=run('actual-machine-main',[str(out/'run')])
        report.update(scx_sha256=sha(image),scx_bytes=len(body),actual_main=entry)
        print('ACTUAL_SWITCH_MAIN_EXIT',status,flush=True)
    finally:
        report['exit_code']=status
        (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return status
if __name__=='__main__':raise SystemExit(main())
