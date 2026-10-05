#!/usr/bin/env python3
"""SandFS 打包器 —— 把字体和用户程序写入沙核数据盘 (sanddata.img, 挂 IDE)
用法: python tools/mkfs.py
输入: kernel/font16.txt (→ font.scf, SCF1MIO 格式) + build/files/*.scx (用户程序)
布局: LBA 0 超级块 | LBA 1 目录表 | LBA 2+ 文件数据
魔数: SandFS=SANDFSMIO  SCF=SCF1MIO (作者尾缀 MIO)"""

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
IMG  = os.path.join(ROOT, "build", "sanddata.img")
TXT  = os.path.join(ROOT, "kernel", "font16.txt")
FSTREE = os.path.join(ROOT, "build", "fs")

FS_LBA = 0
MAGIC = b"SANDFSMIO"
SCF_MAGIC = b"SCF1MIO"
MAX_FILES = 12
DATA_MB = 8


def parse_txt(path):
    """和 mkfont 相同的解析: 返回 [(汉字, 16行)]"""
    glyphs, cur = [], None
    for raw in open(path, encoding="utf-8").read().splitlines():
        s = raw.rstrip()
        st = s.strip()
        if not st:
            continue
        if st.startswith("//"):
            inner = st[2:].strip()
            if inner.startswith("字"):
                if cur:
                    glyphs.append(cur)
                cur = (inner[1:].strip(), [])
            continue
        if cur is not None and len(cur[1]) < 16:
            body = s[:16]
            cur[1].append("".join("#" if ch == "#" else "." for ch in body.ljust(16, ".")))
    if cur:
        glyphs.append(cur)
    return [g for g in glyphs if len(g[1]) == 16]


def make_scf(glyphs):
    """font16.txt → SCF1MIO 二进制: magic(7) + count(4) + 每字(4B utf8槽 + 16行×17B)"""
    out = bytearray(SCF_MAGIC)
    out += struct.pack("<I", len(glyphs))
    for zh, rows in glyphs:
        b = zh.encode("utf-8")
        out += b + b"\x00" * (4 - len(b))
        for r in rows:
            out += r.encode("ascii") + b"\x00"
    return bytes(out)


def main():
    if not os.path.exists(IMG):                   # 数据盘不存在则建 8MB 空盘
        with open(IMG, "wb") as f:
            f.write(b"\x00" * (DATA_MB * 1024 * 1024))
    img = bytearray(open(IMG, "rb").read())

    files = []                                    # (name, bytes)
    scf = make_scf(parse_txt(TXT))
    files.append(("font.scf", scf))

    if os.path.isdir(FSTREE):
        for dirpath, _, fnames in os.walk(FSTREE):
            for n in sorted(fnames):
                p = os.path.join(dirpath, n)
                rel = os.path.relpath(p, FSTREE).replace("\\", "/")
                if os.path.isfile(p):
                    files.append((rel, open(p, "rb").read()))
    files = files[:MAX_FILES]

    # 布局: LBA 0 超级块 | LBA 1 目录表 (每项 40B) | LBA 2+ 数据
    sb = bytearray(512)
    sb[0:9] = MAGIC
    sb[12:16] = struct.pack("<I", len(files))

    dirsec = bytearray(512)
    data = bytearray()
    lba = FS_LBA + 2
    for i, (name, blob) in enumerate(files):
        e = i * 40
        nb = name.encode("utf-8")[:31]
        dirsec[e:e + len(nb)] = nb
        dirsec[e + 32:e + 36] = struct.pack("<I", lba)
        dirsec[e + 36:e + 40] = struct.pack("<I", len(blob))
        data += blob
        data += b"\x00" * ((-len(data)) % 512)    # 对齐到扇区
        print(f"  {name}: {len(blob)}B @ LBA {lba}")
        lba += (len(blob) + 511) // 512      # 按补零后的扇区数推进!

    off = FS_LBA * 512
    img[off:off + 512] = sb
    img[off + 512:off + 1024] = dirsec
    img[off + 1024:off + 1024 + len(data)] = data

    with open(IMG, "wb") as f:
        f.write(img)
    print(f"mkfs: {len(files)} files -> {IMG} (SandFS @ LBA {FS_LBA})")


main()
