#!/usr/bin/env python3
"""当前M9构建/两盘/字体/内核指令与每个SCX布局，不能替代运行。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from audit_m7 import wsl,FONT_HASH
from mkfs_m9 import read_image
from audit_third_party import audit

ROOT=Path(__file__).resolve().parents[1]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--build',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    build=a.build;kernel=(build/'kernel.bin').read_bytes();boot=(build/'boot.bin').read_bytes();floppy=(build/'sandcore.img').read_bytes()
    assert boot[-2:]==b'\x55\xaa' and len(boot)==512 and len(floppy)==1474560
    assert len(kernel)<=640*512 and floppy[:512]==boot and floppy[512:512+len(kernel)]==kernel
    assert floppy[512+len(kernel):512+640*512]==bytes(640*512-len(kernel))
    assert re.search(r'KERNEL_SECTS\s+equ\s+640', (ROOT/'boot/boot.asm').read_text(encoding='utf-8-sig'))
    assert re.search(r'KERNEL_SECTS\s*=\s*640', (ROOT/'tools/mkimg.py').read_text(encoding='utf-8-sig'))
    symbols=wsl(['nm','-n',(build/'kernel.elf').as_posix()]);bss=int(re.search(r'^([0-9a-f]+) B __bss_end$',symbols,re.M)[1],16)
    assert bss<=0x200000
    # 点开头文件是Makefile工具链探测产物，不是内核/模块/SCX。
    elfs=[x.as_posix() for x in sorted(build.glob('*.elf')) if not x.name.startswith('.')]
    undefined=wsl(['nm','-u',*elfs]);assert not any(x.strip() and not x.rstrip().endswith(':') for x in undefined.splitlines()),undefined
    sections=wsl(['objdump','-h',*elfs]);assert not re.search(r'\.(?:tdata|tbss|dynamic|interp)\b',sections)
    disassembly=wsl(['objdump','-d','--no-show-raw-insn',(build/'kernel.elf').as_posix()])
    state=[]
    for line in disassembly.splitlines():
        m=re.match(r'\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b(.*)',line)
        if not m:continue
        opcode,operands=m.groups();assert not re.search(r'%[xyz]mm\d|%mm\d',operands),line
        if opcode.startswith('f'):
            assert opcode in ('fninit','fxsave','fxrstor','fnsave','frstor'),line;state.append(line.strip())
    assert sha(ROOT/'kernel/font16.txt')==FONT_HASH
    for path in ROOT.glob('*.bat'):path.read_bytes().decode('ascii')
    disks=[]
    for path in a.data:
        records=read_image(path.read_bytes());entries=[]
        for name,record in records.items():
            payload=record.payload
            if payload is None or not name.endswith('.SCX'):continue
            assert payload[:8]==b'SCX1MIO\0' and payload[32:36]==b'MIO\0',name
            entry,load,zero,stack,flags,base=struct.unpack_from('<6I',payload,8)
            assert 0<=entry<load<=262108 and load+zero<=0x3d0000 and base==0x400000 and 4096<=stack<=131072 and flags&~3==0,name
            tail=payload[36+load:]
            if flags&2:
                assert tail[:8]==b'SCB2MIO\0' and len(tail)>=32,name
                width,height,fmt,image_flags,length,author=struct.unpack_from('<6I',tail,8)
                assert 1<=width<=128 and 1<=height<=128 and fmt==2 and image_flags==0 and length==width*height*4 and author==0x004f494d and len(tail)==32+length,name
            else:assert not tail,name
            entries.append(dict(path=name,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
        for file in (build/'fs').rglob('*'):
            if not file.is_file():continue
            name=file.relative_to(build/'fs').as_posix().upper()
            assert records[name].payload==file.read_bytes(),name
        disks.append(dict(path=str(path.resolve()),sha256=sha(path),entries=len(records),scx=entries))
    result=dict(status='STATIC_BUILD_PASS',scope='CURRENT_BYTES_LAYOUT_NO_RUNTIME_CLAIM',elf_count=len(elfs),elfs=elfs,
                kernel_bytes=len(kernel),kernel_capacity_sectors=640,kernel_bss_end=hex(bss),kernel_extended_state_instructions=state,
                boot_sha256=sha(build/'sandcore.img'),font_sha256=FONT_HASH,third_party=audit((build/'fs').resolve()),disks=disks)
    a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'STATIC_BUILD_PASS: {len(elfs)} ELF; kernel {len(kernel)}B; disks '+str([len(x['scx']) for x in disks]))


if __name__=='__main__':main()
