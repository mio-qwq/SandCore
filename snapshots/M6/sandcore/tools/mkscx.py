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
    hdr = (MAGIC
           + entry.to_bytes(4, "little")
           + len(payload).to_bytes(4, "little")          # load_size
           + (0).to_bytes(4, "little")                   # bss_size
           + (8192).to_bytes(4, "little")                # stack_size
           + (0).to_bytes(4, "little")                   # flags
           + base.to_bytes(4, "little")
           + AUTHOR)                                     # reserved = 署名
    assert len(hdr) == 36
    open(dst, "wb").write(hdr + payload)
    print(f"mkscx: {src} -> {dst} (entry=0x{entry:x} base=0x{base:x} "
          f"load={len(payload)}B)")
    return 0


sys.exit(main())
