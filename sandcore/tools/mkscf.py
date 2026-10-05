#!/usr/bin/env python3
"""mio：把文本点阵封装为 SCF1MIO，字体与图标共用同一磁盘布局。"""
import struct
import sys
from pathlib import Path
from mkfont import parse


def encode(glyphs):
    out = bytearray(b"SCF1MIO\0" + struct.pack("<I", len(glyphs)))
    for label, rows in glyphs:
        name = label.encode("utf-8")
        if len(name) > 3 or len(rows) != 16:
            raise ValueError("SCF 标签最多三字节，点阵必须为 16 行")
        out += name.ljust(4, b"\0")
        for row in rows:
            if len(row) != 16 or set(row) - set("#."):
                raise ValueError("点阵每行必须为 16 个 # 或 .")
            out += row.encode("ascii") + b"\0"
    return bytes(out)


if __name__ == "__main__":
    src, dst = map(Path, sys.argv[1:3])
    glyphs = parse(src)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(encode(glyphs))
    print(f"mkscf: {len(glyphs)} glyphs -> {dst}")
