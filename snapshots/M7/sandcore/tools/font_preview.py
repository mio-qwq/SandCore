#!/usr/bin/env python3
"""SandCore 字形预览器 —— 不用开机, 一张 PNG 看全部中文字形。
用法: python tools/font_preview.py
输出: build/font_preview.png (深色底亮色字, 和系统里的观感一致)
改字形的工作流: 编辑 kernel/font16.txt → 跑本脚本看图 → 满意了 make"""

import os
import sys
import zlib
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkfont import parse

SCALE = 3          # 每个点阵像素放大 3 倍
COLS = 5           # 每行排 5 个字
BG = (16, 18, 30)      # PAL_CON_BG 深靛
FG = (235, 232, 220)   # PAL_TITLE 米白
GRID = (60, 64, 90)    # 格线


def png_write(path, w, h, px):
    """px[y][x] = (r,g,b), 纯标准库 PNG 编码"""
    raw = b"".join(b"\x00" + b"".join(bytes(px[y][x]) for x in range(w)) for y in range(h))

    def chunk(t, p):
        return struct.pack(">I", len(p)) + t + p + struct.pack(">I", zlib.crc32(t + p) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    txt = os.path.join(here, "..", "kernel", "font16.txt")
    out = os.path.join(here, "..", "build", "font_preview.png")

    glyphs = parse(txt)
    n = len(glyphs)
    rows = (n + COLS - 1) // COLS

    cell = 16 * SCALE            # 字形区
    pad = 12                     # 单元留白
    cw, ch = cell + pad * 2, cell + pad * 2
    W, H = cw * COLS + pad, ch * rows + pad

    px = [[BG for _ in range(W)] for _ in range(H)]

    for idx, (zh, art) in enumerate(glyphs):
        gx = pad + (idx % COLS) * cw
        gy = pad + (idx // COLS) * ch
        # 单元格的左上格线 (标记单元起点)
        for i in range(cw - 4):
            px[gy + 2][gx + 2 + i] = GRID
        for j in range(ch - 4):
            px[gy + 2 + j][gx + 2] = GRID
        # 点阵
        for r in range(16):
            for c in range(16):
                if art[r][c] == "#":
                    for dy in range(SCALE):
                        for dx in range(SCALE):
                            px[gy + 2 + r * SCALE + dy][gx + 2 + c * SCALE + dx] = FG

    os.makedirs(os.path.dirname(out), exist_ok=True)
    png_write(out, W, H, px)
    print(f"font_preview: {n} 字 -> {out}")
    print("字形顺序:", " ".join(g[0] for g in glyphs))


main()
