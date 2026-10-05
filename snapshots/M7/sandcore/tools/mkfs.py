#!/usr/bin/env python3
"""SandFS 打包器 —— 把字体和用户程序写入沙核数据盘 (sanddata.img, 挂 IDE)
用法: python tools/mkfs.py
输入: build/fs/ 目录树（字体、图标、快捷方式与用户程序）
布局: LBA 0 超级块 | LBA 1..32 目录表 | LBA 33+ 文件数据
魔数: SandFS=SANDFSMIO  SCF=SCF1MIO (作者尾缀 MIO)"""

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
IMG  = os.path.join(ROOT, "build", "sanddata.img")
FSTREE = os.path.join(ROOT, "build", "fs")

FS_LBA = 0
MAGIC = b"SANDFSMIO"
MAX_FILES = 192
DIR_SECTORS = 32
DATA_MB = 8


def main():
    if not os.path.exists(IMG):                   # 数据盘不存在则建 8MB 空盘
        with open(IMG, "wb") as f:
            f.write(b"\x00" * (DATA_MB * 1024 * 1024))
    img = bytearray(open(IMG, "rb").read())

    files = []                                    # (name, bytes)

    if os.path.isdir(FSTREE):
        for dirpath, dirs, fnames in os.walk(FSTREE):
            dirs.sort()
            for n in sorted(fnames):
                p = os.path.join(dirpath, n)
                rel = os.path.relpath(p, FSTREE).replace("\\", "/")
                # Windows 文件树忽略大小写，旧 sys 目录会吞掉新 SYS 拼写；
                # 盘上约定核心/模块目录仍统一大写，与宿主目录显示无关。
                if rel.upper().startswith(("SYS/CORE/","SYS/MOD/")): rel=rel.upper()
                if os.path.isfile(p):
                    files.append((rel, open(p, "rb").read()))
    if len(files) > MAX_FILES:
        raise ValueError(f"目录表只能容纳 {MAX_FILES} 个文件，拒绝静默丢弃资源")

    # v4 目录 32 扇区、72B 项、63B 完整路径；内核继续兼容旧 40B 项。
    # 文件名按 ASCII 大小写折叠去重，不能打包两个运行时无法区分的名字。
    names=[name.upper() for name,blob in files]
    if len(set(names)) != len(names): raise ValueError("大小写不同的重复路径")
    sb = bytearray(512)
    sb[0:9] = MAGIC
    sb[12:16] = struct.pack("<I", len(files))
    sb[16:20] = struct.pack("<I", DIR_SECTORS)
    sb[20:24] = struct.pack("<I", 4)

    dirsec = bytearray(DIR_SECTORS*512)
    data = bytearray()
    lba = FS_LBA + 1 + DIR_SECTORS
    for i, (name, blob) in enumerate(files):
        e = i * 72
        nb = name.encode("utf-8")
        if len(nb) > 63:
            raise ValueError(f"文件路径超过 63 字节：{name}")
        dirsec[e:e + len(nb)] = nb
        dirsec[e + 64:e + 68] = struct.pack("<I", lba)
        dirsec[e + 68:e + 72] = struct.pack("<I", len(blob))
        data += blob
        data += b"\x00" * ((-len(data)) % 512)    # 对齐到扇区
        print(f"  {name}: {len(blob)}B @ LBA {lba}")
        lba += (len(blob) + 511) // 512      # 按补零后的扇区数推进!

    off = FS_LBA * 512
    img[off:off + 512] = sb
    start = off + (1+DIR_SECTORS)*512
    if start+len(data) > len(img):
        raise ValueError("资源总长超过数据盘容量")
    img[off + 512:start] = dirsec
    img[start:start+len(data)] = data

    with open(IMG, "wb") as f:
        f.write(img)
    print(f"mkfs: {len(files)} files -> {IMG} (SandFS @ LBA {FS_LBA})")


if __name__ == "__main__":
    main()
