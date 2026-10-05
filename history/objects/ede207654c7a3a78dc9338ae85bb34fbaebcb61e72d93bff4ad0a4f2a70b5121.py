#!/usr/bin/env python3
"""SandFS 打包器 —— 把字体和用户程序写入沙核数据盘 (sanddata.img, 挂 IDE)
用法: python tools/mkfs.py
输入: build/fs/ 目录树（字体、图标、快捷方式与用户程序）
布局: LBA 0 超级块 | LBA 1..32 目录表 | LBA 33+ 文件数据
魔数: SandFS=SANDFSMIO  SCF=SCF1MIO (作者尾缀 MIO)"""

import os
import struct
import argparse
import json
import hashlib
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
IMG  = os.path.join(ROOT, "build", "sanddata.img")
FSTREE = os.path.join(ROOT, "build", "fs")

FS_LBA = 0
MAGIC = b"SANDFSMIO"
MAX_FILES = 192
DIR_SECTORS = 32
DATA_MB = 64


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--factory',action='store_true',help='explicitly rebuild a factory disk without user files')
    args=parser.parse_args()
    original=Path(IMG).read_bytes() if Path(IMG).exists() else b''
    img=bytearray(original or b'\0'*(DATA_MB*1024*1024))

    files = []                                    # (name, bytes)
    previous={}
    # M8构建更新资源而非清空作品。先完整验证旧盘，不能把损坏目录
    # 当“没有用户文件”后覆盖；所有新资源/空间校验都完成后才原子替换。
    if not args.factory and img[:9]==MAGIC:
        count,ds,ver=struct.unpack_from('<III',img,12)
        if ver!=4 or ds!=32 or count>MAX_FILES:raise ValueError('旧盘不是合法v4；请先备份/迁移')
        for i in range(count):
            raw,start,size=struct.unpack_from('<64sII',img,512+i*72)
            if not raw[0] or raw[-1]:raise ValueError('旧目录名称损坏')
            name=raw.split(b'\0',1)[0].decode('utf-8');key=name.upper()
            if key in previous:raise ValueError('旧盘存在重复路径')
            if start==0 and size==0:blob=None
            elif start<33 or start*512+size>len(img):raise ValueError('旧盘数据越界')
            else:blob=bytes(img[start*512:start*512+size])
            previous[key]=(name,blob)
    if len(img)<DATA_MB*1024*1024:
        # 先保留完整旧盘再扩容；源文件仍不修改，所有检查在临时新盘上。
        # 扩容不通过--factory删除用户条目，HOME/DESK/用户主题合并规则不变。
        backup=Path(ROOT)/'build/M8-before-images-sanddata.img'
        if not backup.exists():backup.write_bytes(original)
        img.extend(b'\0'*(DATA_MB*1024*1024-len(img)))
    if len(img)>DATA_MB*1024*1024 or len(img)%512:raise ValueError('本版只接受至64MB扇区对齐数据盘')

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
    published={name.upper():(name,blob) for name,blob in files}
    migration_file=Path(ROOT)/'build/m8-default-migrations.json'
    migrations=json.loads(migration_file.read_text(encoding='utf-8')) if migration_file.exists() else {}
    for key,item in previous.items():
        # HOME/DESK和用户配置属于用户；已有项保留，新预装项补齐。
        # SCX、字库、核心模块、源码头文件来自本次发布树，必须更新。
        editable=key.startswith(('HOME/','DESK/','SYS/THEMES/')) or key in ('SYS/DISPLAY.CFG','SYS/MENU.CFG','SYS/WALL.CFG','SYS/THEME.CFG','SYS/USER.CFG','SYS/ENV.CFG')
        known=migrations.get(key,[])
        if isinstance(known,str):known=[known]
        unchanged_default=item[1] is not None and hashlib.sha256(item[1]).hexdigest() in known
        if key not in published or (editable and not unchanged_default):published[key]=item
    files=sorted(published.values(),key=lambda item:item[0].upper())
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
    sb[24:28] = struct.pack("<I", len(img)//512)

    dirsec = bytearray(DIR_SECTORS*512)
    data = bytearray()
    lba = FS_LBA + 1 + DIR_SECTORS
    for i, (name, blob) in enumerate(files):
        e = i * 72
        nb = name.encode("utf-8")
        if len(nb) > 63:
            raise ValueError(f"文件路径超过 63 字节：{name}")
        dirsec[e:e + len(nb)] = nb
        if blob is None:continue  # 显式空目录保留start=0/size=0
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

    temporary=Path(IMG).with_suffix('.img.tmp')
    with temporary.open("wb") as f:
        f.write(img)
    try:
        os.replace(temporary,IMG)
        target=Path(IMG)
        # 旧锁释放后，默认盘就是最新产物；清掉本工具的过期暂存盘，
        # 避免验证器按路径存在性选到上一次版本。仅删除固定生成文件。
        staged=Path(ROOT)/'build/m8-runtime/sanddata.img'
        if staged.exists():staged.unlink()
    except PermissionError:
        # Windows QEMU以文件锁保护正在运行的盘。不能杀用户虚拟机，
        # 更不能回落到原地覆写绕过锁；把已完整验证的发布盘放独立目录。
        # 自动验证明确选择该盘，用户关闭旧QEMU后重新build才更新默认盘。
        target=Path(ROOT)/'build/m8-runtime/sanddata.img'
        target.parent.mkdir(parents=True,exist_ok=True)
        os.replace(temporary,target)
        print('mkfs: active disk locked; staged build/m8-runtime/sanddata.img')
    print(f"mkfs: {len(files)} files -> {target} (SandFS @ LBA {FS_LBA})")


if __name__ == "__main__":
    main()
