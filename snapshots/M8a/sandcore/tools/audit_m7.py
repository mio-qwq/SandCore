#!/usr/bin/env python3
"""mio：验收包的静态完整性核对，与 QEMU 行为证据分工。

这里不以宿主模拟用户程序来代替上机：QEMU 已验证的行为由 results.json
记录。本工具确认最终交付的源码/平映像/SCX/SCF/磁盘字节彼此一致，排除
“代码更新了但盘里仍是旧文件”以及库函数/浮点状态意外进入构建产物。
WSL 只运行 nm/objdump 读取 ELF；不启动 Linux QEMU，不改字体与数据盘。
"""
import hashlib
import json
import py_compile
import re
import struct
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "build/m7-integrity.json"
FONT_HASH = "8f286f8ac7e9c1d714a2dec6613bb9416660a7232fc8d78ffa8024b3c23e782b"


def wsl(arguments):
    # 参数数组避免项目路径中的空格被 shell 再拆分；只读分析沿用主力 ELF。
    result = subprocess.run(["wsl.exe", "--cd", str(ROOT), "-e", *arguments],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return result.stdout.decode("utf-8")


def main():
    for source in (ROOT / "tools").glob("*.py"):
        py_compile.compile(str(source), doraise=True)
    for batch in ROOT.glob("*.bat"):
        batch.read_bytes().decode("ascii")
    font = (ROOT / "kernel/font16.txt").read_bytes()
    assert hashlib.sha256(font).hexdigest() == FONT_HASH, "用户字体发生改变"
    import zipfile
    from mkfont import parse
    original=ROOT/'build/m7-original-font.txt'
    with zipfile.ZipFile(ROOT/'build/SandCore-M6-2026-10-02.zip') as old:
        original.write_bytes(old.read('sandcore/kernel/font16.txt'))
    before=parse(original);after=parse(ROOT/'kernel/font16.txt')
    assert len(before)==27 and len(after)==36 and after[:27]==before
    original.unlink()
    boot = (ROOT / "build/boot.bin").read_bytes()
    kernel = (ROOT / "build/kernel.bin").read_bytes()
    floppy = (ROOT / "build/sandcore.img").read_bytes()
    assert len(boot) == 512 and boot[-2:] == b"\x55\xaa"
    # kernel.bin 保留原始长度；mkimg 只在镜像内补齐末扇区，不改输入文件。
    kernel_sectors = (len(kernel)+511)//512
    assert 0 < len(kernel) <= 256 * 512
    assert len(floppy) == 1474560 and floppy[:512] == boot
    assert floppy[512:512+len(kernel)] == kernel
    assert floppy[512+len(kernel):512+kernel_sectors*512] == bytes(kernel_sectors*512-len(kernel))
    data = (ROOT / "build/sanddata.img").read_bytes()
    assert len(data) == 8 * 1024 * 1024 and data[:9] == b"SANDFSMIO"
    count, directory, version = struct.unpack_from("<3I", data, 12)
    assert 1 <= count <= 192 and directory == 32 and version == 4
    names = set()
    payloads = {}
    for i in range(count):
        # 宿主资源树是发布盘的权威输入；按目录项再读盘上实际字节并比较。
        entry = data[512+i*72:512+(i+1)*72]
        name = entry[:64].split(b"\0", 1)[0].decode("ascii")
        lba, size = struct.unpack_from("<2I", entry, 64)
        assert name and len(name) <= 63 and name not in names
        assert lba >= 33 and lba*512+size <= len(data)
        names.add(name)
        payload = data[lba*512:lba*512+size]
        assert payload == (ROOT / "build/fs" / name).read_bytes(), name + " 未同步到盘"
        payloads[name] = payload
        if name.endswith(".scx"):
            assert payload[:8] == b"SCX1MIO\0" and payload[32:36] == b"MIO\0"
            entry_rva, load, bss, stack, flags, base = struct.unpack_from("<6I", payload, 8)
            assert len(payload) == load+36 and entry_rva < load and base == 0x400000
            assert load+bss <= 0x3D0000 and 4096 <= stack <= 131072 and flags == 0
        elif name.endswith(".scf"):
            assert payload[:8] == b"SCF1MIO\0"
            glyphs = struct.unpack_from("<I", payload, 8)[0]
            assert len(payload) == 12+glyphs*276
    assert struct.unpack_from("<I", payloads["sys/font.scf"], 8)[0] == 36
    for name in names:
        if name.endswith(".lnk"):
            lines = payloads[name].decode("ascii").splitlines()
            assert len(lines) == 3 and lines[1] in names and lines[2] in names
    # 默认盘的原生声明也是交付合同：检查真实文件与当前源码，而不能
    # 只在旧 results.json 里找到 PASS。若之后 make 强制重建启动版，
    # 这里会拒绝继续把宿主产物称作 G2，须重做 publish_native.py。
    native = json.loads((ROOT / 'build/m7-native-runtime.json').read_text(encoding='utf-8'))
    assert native['status'] == 'PASS'
    assert native['compiler'] == hashlib.sha256(payloads['bin/s3c.scx']).hexdigest()
    assert native['api_sha256'] == hashlib.sha256((ROOT / 'user/SCAPI.H').read_bytes()).hexdigest()
    for app, record in native['files'].items():
        assert record['scx_sha256'] == hashlib.sha256(payloads['apps/'+app+'.scx']).hexdigest()
        assert record['source_sha256'] == hashlib.sha256((ROOT / 'user' / (app+'.c')).read_bytes()).hexdigest()
        assert payloads['apps/'+app+'.scx.map'].startswith(b'SCSYM1MIO'), app+' missing native symbols'
    assert native['images'] == {name: hashlib.sha256((ROOT/'build'/name).read_bytes()).hexdigest()
                               for name in ('sandcore.img', 'sanddata.img')}
    symbols = wsl(["nm", "-n", "build/kernel.elf"])
    end = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.split()[-1] == "__bss_end")
    assert 0x100000 < end <= 0x200000
    analyzed = []
    for elf in [ROOT / "build/kernel.elf", *sorted((ROOT / "build").glob("user-*.kernel.elf"))]:
        relative = elf.relative_to(ROOT).as_posix()
        assert not wsl(["nm", "-u", relative]).strip(), relative + " 有外部库符号"
        assembly = wsl(["objdump", "-d", "--no-show-raw-insn", relative])
        for line in assembly.splitlines():
            instruction = re.match(r"\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b(.*)", line)
            if instruction:
                opcode, operands = instruction.groups()
                assert not opcode.startswith("f"), relative + " 有 x87 指令：" + line
                assert not re.search(r"%[xyz]mm\d|%mm\d", operands), relative + " 有向量指令：" + line
        analyzed.append(relative)
    report = {"author": "mio", "status": "PASS", "kernel_bytes": len(kernel), "kernel_sectors": kernel_sectors,
              "bss_end": hex(end), "filesystem_files": count, "font_glyphs": 36,
              "font_sha256": FONT_HASH, "no_undefined_or_float_vector": analyzed,
              "checks": ["Python 语法", "批处理 ASCII", "双盘尺寸/引导签名/载荷",
                         "SandFS v4/SCX/SCF/.lnk 与资源树一致", "字体原 27 字不变/九字同源补入",
                         "BSS 2MB 上界/引导栈分离", "原生 G2/七应用/符号图与当前源码/正式盘一致",
                         "ELF 无外部库符号/x87/MMX/SSE 指令"],
              "images": {name: hashlib.sha256((ROOT / "build" / name).read_bytes()).hexdigest()
                         for name in ("sandcore.img", "sanddata.img")}}
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
