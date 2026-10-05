#!/usr/bin/env python3
"""mio：M8 真彩色底座的静态完整性记录。

这个范围不验证主题/图片解码，也不套用 M7 已验收盘的原生来源声明。
上机行为由 verify_truecolor.py 的实际输入、显存断言与截图证明；这里
只读当前平映像、ELF 与用户字库，记录与证据对应的哈希和工程边界。
"""
import hashlib
import json
import re
import sys
from pathlib import Path
from audit_m7 import FONT_HASH, wsl

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / ('build/m8-images' if '--images' in sys.argv else 'build/m8-theme' if '--theme' in sys.argv else 'build/m8-truecolor')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    font = ROOT / 'kernel/font16.txt'
    assert sha(font) == FONT_HASH, '用户字体发生变化'
    for batch in ROOT.glob('*.bat'):
        batch.read_bytes().decode('ascii')
    boot = (ROOT / 'build/boot.bin').read_bytes()
    kernel = (ROOT / 'build/kernel.bin').read_bytes()
    floppy = (ROOT / 'build/sandcore.img').read_bytes()
    assert len(boot) == 512 and boot[-2:] == b'\x55\xaa'
    sectors = (len(kernel)+511)//512
    assert 0 < sectors <= 256 and len(floppy) == 1474560
    assert floppy[:512] == boot and floppy[512:512+len(kernel)] == kernel
    symbols = wsl(['nm', '-n', 'build/kernel.elf'])
    end = next(int(row.split()[0], 16) for row in symbols.splitlines()
               if row.split()[-1] == '__bss_end')
    assert 0x100000 < end <= 0x200000
    analyzed = []
    elf_names=['build/kernel.elf', 'build/user-rgbprobe.kernel.elf']
    if '--theme' in sys.argv or '--images' in sys.argv:
        elf_names += ['build/user-'+name+'.kernel.elf' for name in
                      ('themeprobe','settings','files','ide','debugger','lens','canvas','monitor')]
    if '--images' in sys.argv:
        elf_names += ['build/user-imageprobe.kernel.elf','build/user-s3c.kernel.elf']
    for relative in elf_names:
        assert not wsl(['nm', '-u', relative]).strip(), relative+' 引入外部符号'
        assembly = wsl(['objdump', '-d', '--no-show-raw-insn', relative])
        for row in assembly.splitlines():
            found = re.match(r'\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b(.*)', row)
            if not found:
                continue
            opcode, operands = found.groups()
            assert not opcode.startswith('f'), (relative, row)
            assert not re.search(r'%[xyz]mm\d|%mm\d', operands), (relative, row)
        analyzed.append(relative)
    provenance = {}
    for name in ('sandcore.img', 'sanddata.img', 'kernel.bin'):
        provenance['build/'+name] = sha(ROOT/'build'/name)
    for name in ('user/SCAPI.H', 'user/rgbprobe.c', 'tools/verify_truecolor.py'):
        provenance[name] = sha(ROOT/name)
    report = dict(author='mio', status='PASS', kernel_bytes=len(kernel),
                  kernel_sectors=sectors, bss_end=hex(end), font_sha256=FONT_HASH,
                  checked_elf=analyzed, sha256=provenance,
                  checks=['用户字库未改', '所有批处理ASCII', '引导签名/双盘载荷对应',
                          '内核256扇区及BSS 2MB上界', '所列ELF无外部库/x87/MMX/SSE'])
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'integrity.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
