#!/usr/bin/env python3
"""SandCore SCX 打包器 —— 把平二进制用户程序装进 SCX1MIO 容器
用法: python tools/mkscx.py <flat.bin> <out.scx> [entry_hex] [base_hex]
SCX v1 头 (36B): magic "SCX1MIO"(8) + entry/load_size/bss_size/stack_size/
flags/base/reserved("MIO") 各 4B, 之后是装载映像。规范见 docs/FS.md §3"""

import sys
import argparse
import struct

MAGIC = b"SCX1MIO\x00"
AUTHOR = b"MIO\x00"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('src');parser.add_argument('dst')
    parser.add_argument('entry',nargs='?',default='0')
    parser.add_argument('base',nargs='?',default='0x400000')
    parser.add_argument('symbols',nargs='?')
    parser.add_argument('--icon',help='optional complete SCB2 ARGB icon, max 128x128')
    args=parser.parse_args();src,dst=args.src,args.dst
    entry,base=int(args.entry,0),int(args.base,0)

    payload = open(src, "rb").read()
    bss=0
    stack=8192
    if args.symbols:
        symbols={}
        for row in open(args.symbols,encoding="utf-8"):
            cols=row.split()
            if len(cols)==3: symbols[cols[2]]=int(cols[0],16)
        if symbols["__user_image_end"]-base != len(payload):
            raise ValueError("载荷尾部与链接符号不一致")
        bss=symbols["__user_end"]-base-len(payload)
        stack=131072
    if not payload or entry>=len(payload) or len(payload)>262108 or bss<0 or len(payload)+bss>0x3D0000:
        raise ValueError("SCX 尺寸或入口超出 M7 加载器合同")
    icon=b''
    if args.icon:
        icon=open(args.icon,'rb').read()
        if len(icon)<32 or icon[:8]!=b'SCB2MIO\0':raise ValueError('内置图标必须是SCB2')
        w,h,fmt,flags,n,author=struct.unpack_from('<6I',icon,8)
        if not 1<=w<=128 or not 1<=h<=128 or fmt!=2 or flags or n!=w*h*4 or author!=0x004F494D or len(icon)!=32+n:
            raise ValueError('SCX内置图标的头/尺寸/精确长度无效')
    hdr = (MAGIC
           + entry.to_bytes(4, "little")
           + len(payload).to_bytes(4, "little")          # load_size
           + bss.to_bytes(4, "little")                  # BSS 由加载器清零
           + stack.to_bytes(4, "little")                # C 编译器递归需要 128KB 栈
           + (2 if icon else 0).to_bytes(4, "little")     # bit1仅表示独立SCB2资源
           + base.to_bytes(4, "little")
           + AUTHOR)                                     # reserved = 署名
    assert len(hdr) == 36
    open(dst, "wb").write(hdr + payload + icon)
    print(f"mkscx: {src} -> {dst} (entry=0x{entry:x} base=0x{base:x} "
          f"load={len(payload)}B)")
    return 0


sys.exit(main())
