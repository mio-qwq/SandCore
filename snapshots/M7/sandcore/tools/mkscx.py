#!/usr/bin/env python3
"""SandCore SCX 打包器 —— 把平二进制用户程序装进 SCX1MIO 容器
用法: python tools/mkscx.py <flat.bin> <out.scx> [entry_hex] [base_hex]
SCX v1 头 (36B): magic "SCX1MIO"(8) + entry/load_size/bss_size/stack_size/
flags/base/reserved("MIO") 各 4B, 之后是装载映像。规范见 docs/FS.md §3"""

import sys

MAGIC = b"SCX1MIO\x00"
AUTHOR = b"MIO\x00"


def main():
    if len(sys.argv) < 3:
        print("usage: mkscx.py <flat.bin> <out.scx> [entry_hex=0] [base_hex=0x400000]")
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    entry = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    base = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x400000

    payload = open(src, "rb").read()
    bss=0
    stack=8192
    if len(sys.argv)>5:
        symbols={}
        for row in open(sys.argv[5],encoding="utf-8"):
            cols=row.split()
            if len(cols)==3: symbols[cols[2]]=int(cols[0],16)
        if symbols["__user_image_end"]-base != len(payload):
            raise ValueError("载荷尾部与链接符号不一致")
        bss=symbols["__user_end"]-base-len(payload)
        stack=131072
    if not payload or entry>=len(payload) or len(payload)>262108 or bss<0 or len(payload)+bss>0x3D0000:
        raise ValueError("SCX 尺寸或入口超出 M7 加载器合同")
    hdr = (MAGIC
           + entry.to_bytes(4, "little")
           + len(payload).to_bytes(4, "little")          # load_size
           + bss.to_bytes(4, "little")                  # BSS 由加载器清零
           + stack.to_bytes(4, "little")                # C 编译器递归需要 128KB 栈
           + (0).to_bytes(4, "little")                   # flags
           + base.to_bytes(4, "little")
           + AUTHOR)                                     # reserved = 署名
    assert len(hdr) == 36
    open(dst, "wb").write(hdr + payload)
    print(f"mkscx: {src} -> {dst} (entry=0x{entry:x} base=0x{base:x} "
          f"load={len(payload)}B)")
    return 0


sys.exit(main())
