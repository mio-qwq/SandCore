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
REPORT = ROOT / "build/m6-integrity.json"
FONT_HASH = "1ef3876e7adbf0f42dc432dbdf21c927282a7e28bb076613c6cb50fbf0de9b55"


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
    boot = (ROOT / "build/boot.bin").read_bytes()
    kernel = (ROOT / "build/kernel.bin").read_bytes()
    floppy = (ROOT / "build/sandcore.img").read_bytes()
    assert len(boot) == 512 and boot[-2:] == b"\x55\xaa"
    # kernel.bin 保留原始长度；mkimg 只在镜像内补齐末扇区，不改输入文件。
    kernel_sectors = (len(kernel)+511)//512
    assert 0 < len(kernel) <= 64 * 512
    assert len(floppy) == 1474560 and floppy[:512] == boot
    assert floppy[512:512+len(kernel)] == kernel
    assert floppy[512+len(kernel):512+kernel_sectors*512] == bytes(kernel_sectors*512-len(kernel))
    data = (ROOT / "build/sanddata.img").read_bytes()
    assert len(data) == 8 * 1024 * 1024 and data[:9] == b"SANDFSMIO"
    count, directory, version = struct.unpack_from("<3I", data, 12)
    assert count == 20 and directory == 8 and version == 3
    names = set()
    payloads = {}
    for i in range(count):
        # 宿主资源树是发布盘的权威输入；按目录项再读盘上实际字节并比较。
        entry = data[512+i*40:512+(i+1)*40]
        name = entry[:32].split(b"\0", 1)[0].decode("ascii")
        lba, size = struct.unpack_from("<2I", entry, 32)
        assert name and len(name) <= 31 and name not in names
        assert lba >= 9 and lba*512+size <= len(data)
        names.add(name)
        payload = data[lba*512:lba*512+size]
        assert payload == (ROOT / "build/fs" / name).read_bytes(), name + " 未同步到盘"
        payloads[name] = payload
        if name.endswith(".scx"):
            assert payload[:8] == b"SCX1MIO\0" and payload[32:36] == b"MIO\0"
            entry_rva, load, bss, stack, flags, base = struct.unpack_from("<6I", payload, 8)
            assert len(payload) == load+36 and entry_rva < load and base == 0x400000
            assert load+bss <= 0x100000 and 4096 <= stack <= 65536 and flags == 0
        elif name.endswith(".scf"):
            assert payload[:8] == b"SCF1MIO\0"
            glyphs = struct.unpack_from("<I", payload, 8)[0]
            assert len(payload) == 12+glyphs*276
    assert struct.unpack_from("<I", payloads["sys/font.scf"], 8)[0] == 27
    for name in names:
        if name.endswith(".lnk"):
            lines = payloads[name].decode("ascii").splitlines()
            assert len(lines) == 3 and lines[1] in names and lines[2] in names
    symbols = wsl(["nm", "-n", "build/kernel.elf"])
    end = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.split()[-1] == "__bss_end")
    assert end <= 0x8c000
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
              "bss_end": hex(end), "filesystem_files": count, "font_glyphs": 27,
              "font_sha256": FONT_HASH, "no_undefined_or_float_vector": analyzed,
              "checks": ["Python 语法", "批处理 ASCII", "双盘尺寸/引导签名/载荷",
                         "SandFS v3/SCX/SCF/.lnk 与资源树一致", "字体保持接手基线",
                         "BSS 栈边界", "ELF 无外部库符号/x87/MMX/SSE 指令"],
              "images": {name: hashlib.sha256((ROOT / "build" / name).read_bytes()).hexdigest()
                         for name in ("sandcore.img", "sanddata.img")}}
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
