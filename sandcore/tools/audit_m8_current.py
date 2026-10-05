#!/usr/bin/env python3
"""mio：M8第二阶段当前构建的静态审计，独立于历史阶段报告。

每次显式传入新输出目录，只审计这次的引导盘、数据盘和ELF，既不
覆盖M7包，也不沿用256扇区/8MB的旧合同。这里只证明字节和工程
边界；图片行为、实际原生编译、性能及审美仍须QEMU证据。
"""
import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path
from audit_m7 import FONT_HASH, wsl
import audit_api_compat as compat

ROOT = Path(__file__).resolve().parent.parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def folded(name):
    # SandFS只折ASCII，不改变中文或把Unicode名称合并为另一个文件。
    return bytes(value-32 if 97 <= value <= 122 else value for value in name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    assert not (out/'integrity.json').exists(), '不可覆盖已有完整审计报告'
    assert sha((ROOT/'kernel/font16.txt').read_bytes()) == FONT_HASH, '用户字体改变'
    for batch in ROOT.rglob('*.bat'):
        if 'build' not in batch.relative_to(ROOT).parts:
            batch.read_bytes().decode('ascii')
    # 旧验收包整体不变，同时核对当前字体前27字的真实字形。
    m7 = ROOT/'build/SandCore-M7-2026-10-02.zip'
    assert sha(m7.read_bytes()) == '516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc'
    with zipfile.ZipFile(m7) as archive:
        assert archive.read('sandcore/kernel/font16.txt') == (ROOT/'kernel/font16.txt').read_bytes()
    capacity = 384
    asm = (ROOT/'boot/boot.asm').read_text(encoding='utf-8-sig')
    image_tool = (ROOT/'tools/mkimg.py').read_text(encoding='utf-8-sig')
    assert re.search(r'KERNEL_SECTS\s+(?:equ\s+)?384\b', asm, re.I)
    assert re.search(r'KERNEL_SECTS\s*=\s*384\b', image_tool)
    boot = (ROOT/'build/boot.bin').read_bytes()
    kernel = (ROOT/'build/kernel.bin').read_bytes()
    floppy = (ROOT/'build/sandcore.img').read_bytes()
    assert len(boot) == 512 and boot[-2:] == b'\x55\xaa'
    assert 0 < len(kernel) <= capacity*512 and len(floppy) == 1474560
    assert floppy[:512] == boot and floppy[512:512+len(kernel)] == kernel
    sectors = (len(kernel)+511)//512
    assert floppy[512+len(kernel):512+capacity*512] == bytes(capacity*512-len(kernel))
    symbols = wsl(['nm', '-n', 'build/kernel.elf'])
    addresses = {row.split()[-1]:int(row.split()[0],16) for row in symbols.splitlines() if len(row.split()) == 3}
    assert 0x100000 < addresses['__bss_end'] <= 0x200000
    elfs = [ROOT/'build/kernel.elf', ROOT/'build/module-core.elf',
            *sorted((ROOT/'build').glob('user-*.kernel.elf'))]
    names = [elf.relative_to(ROOT).as_posix() for elf in elfs]
    undefined = wsl(['nm', '-u', *names])
    assert not any(row.strip() and not row.rstrip().endswith(':') for row in undefined.splitlines()), undefined
    assembly = wsl(['objdump', '-d', '--no-show-raw-insn', *names])
    sections = wsl(['objdump', '-h', *names])
    assert not re.search(r'\.(?:tdata|tbss)\b', sections), 'SCX没有TLS运行时，不得携带TLS节'
    for row in assembly.splitlines():
        found = re.match(r'\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b(.*)', row)
        if found:
            opcode, operands = found.groups()
            assert not opcode.startswith('f'), 'x87: '+row
            assert not re.search(r'%[xyz]mm\d|%mm\d', operands), 'SIMD/MMX: '+row
    # 软件浮点帮助函数同样不能混进服务或内核，不能只查x87指令。
    assert not re.search(r'__(?:add|sub|mul|div|fix|float|lt|gt|le|ge|eq|ne)\w*(?:sf|df)\w*', undefined)
    sources = json.loads((ROOT/'third_party/SOURCES.json').read_text(encoding='utf-8'))
    for relative, expected in sources['files'].items():
        assert sha((ROOT/'third_party'/relative).read_bytes()) == expected, relative+'第三方快照改变'
    raw = (ROOT/'build/sanddata.img').read_bytes()
    assert len(raw) == 64*1024*1024 and raw[:9] == b'SANDFSMIO'
    count, ds, version = struct.unpack_from('<3I', raw, 12)
    assert version == 4 and ds == 80 and 0 < count <= 512
    disk_files = {}
    ranges = []
    for index in range(count):
        entry = raw[512+index*72:512+(index+1)*72]
        path = entry[:64].split(b'\0',1)[0]
        start, size = struct.unpack_from('<2I', entry, 64)
        if not path:
            continue
        assert len(path) <= 63 and folded(path) not in disk_files
        if not start and not size:
            # SandFS v4显式空目录使用零LBA/零长度，是有效记录而非文件。
            disk_files[folded(path)] = None
            continue
        assert start >= 81 and start*512+size <= len(raw)
        if size:
            assert all(start >= end or start+(size+511)//512 <= begin for begin,end in ranges), path
            ranges.append((start,start+(size+511)//512))
        disk_files[folded(path)] = raw[start*512:start*512+size]
    changed_user_files = []
    for file in (ROOT/'build/fs').rglob('*'):
        if not file.is_file():
            continue
        path = file.relative_to(ROOT/'build/fs').as_posix()
        key = folded(path.encode())
        assert key in disk_files, path+'未上盘'
        if disk_files[key] != file.read_bytes():
            # 构建合并用户配置/作品是既有合同，记录差异而非覆盖用户数据。
            allowed = key.startswith((b'HOME/', b'DESK/', b'SYS/THEMES/')) or key in {
                b'SYS/THEME.CFG', b'SYS/DISPLAY.CFG', b'SYS/USER.CFG', b'SYS/ENV.CFG', b'SYS/MENU.CFG', b'SYS/WALL.CFG'}
            assert allowed, path+'盘内仍为旧构建'
            changed_user_files.append(path)
    scx = {}
    for path, payload in disk_files.items():
        if payload is None or not path.endswith(b'.SCX'):
            continue
        assert payload[:8] == b'SCX1MIO\0' and payload[32:36] == b'MIO\0', path
        entry, load, bss, stack, flags, base = struct.unpack_from('<6I', payload, 8)
        assert 0 <= entry < load <= 262108 and load+bss <= 0x3d0000
        assert 4096 <= stack <= 131072 and base == 0x400000 and flags & ~3 == 0
        extra = payload[36+load:]
        if flags & 2:
            assert extra[:8] == b'SCB2MIO\0' and len(extra) >= 32
            width,height,fmt,image_flags,length,author = struct.unpack_from('<6I',extra,8)
            assert 1 <= width <= 128 and 1 <= height <= 128 and fmt == 2 and not image_flags
            assert length == width*height*4 and author == 0x004f494d and len(extra) == 32+length
        else:
            assert not extra, path+'尾部垃圾'
        scx[path.decode()] = dict(bytes=len(payload),load=load,bss=bss,sha256=sha(payload))
    assert b'SYS/CORE/IMAGE.SCX' in disk_files and b'SYS/CORE/IMAGE.LIC' in disk_files
    compat.OUT = out
    compat.main()
    report = dict(author='mio',status='PASS',scope='STATIC_BUILD_ONLY',kernel_bytes=len(kernel),
                  kernel_sectors=sectors,kernel_capacity=capacity,bss_end=hex(addresses['__bss_end']),
                  filesystem_files=len(disk_files),directory_capacity=512,font_sha256=FONT_HASH,
                  elf_files=names,third_party_files=len(sources['files']),preserved_user_files=changed_user_files,
                  scx_files=scx,images={name:sha((ROOT/'build'/name).read_bytes()) for name in ('sandcore.img','sanddata.img')},
                  limitations='当前宿主引导构建完整性；未证明客体原生编译、功能、性能或审美通过')
    (out/'integrity.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('M8 static audit PASS:',len(names),'ELF,',len(disk_files),'files,',len(kernel),'kernel bytes')


if __name__ == '__main__':
    main()
