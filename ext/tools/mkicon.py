#!/usr/bin/env python3
"""mkicon.py —— 像素画文本 -> SCB2 图标（docs/IMAGE.md §1 格式）。

输入格式（纯 ASCII，# 开头为注释）：
    # 注释行
    32 32                    宽 高
    . 000000                 调色板：字符 空格 RRGGBB（可多行，'.' 或空格固定=透明）
    K F4B942                 ...
    <H 行，每行 W 个调色板字符>

输出：完整 SCB2 文件（魔数 SCB2MIO\\0 + 宽高 + 格式2 + flags0 +
正文长度 + MIO\\0 + BGRA 正文）。行长/行数/未知字符一律报错，
不生成半份图标。
"""
import sys


def main():
    if len(sys.argv) != 3:
        print("usage: mkicon.py <icon.txt> <out.scb>", file=sys.stderr)
        return 2
    lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
    lines = [l for l in lines if l.strip() and not l.lstrip().startswith("#")]
    if not lines:
        print("mkicon: empty art", file=sys.stderr)
        return 1
    head = lines[0].split()
    if len(head) != 2:
        print("mkicon: first line must be 'W H'", file=sys.stderr)
        return 1
    w, h = int(head[0]), int(head[1])
    if not (1 <= w <= 128 and 1 <= h <= 128):
        print("mkicon: icon must be 1..128 per side", file=sys.stderr)
        return 1
    palette = {".": None, " ": None}          # 透明固定项
    row = 1
    while row < len(lines) and len(lines[row].split()) == 2 and \
            len(lines[row].split()[0]) == 1:
        ch, rgb = lines[row].split()
        if len(rgb) != 6 or any(c not in "0123456789abcdefABCDEF" for c in rgb):
            print(f"mkicon: bad color '{rgb}'", file=sys.stderr)
            return 1
        palette[ch] = bytes.fromhex(rgb)      # R,G,B
        row += 1
    art = lines[row:]
    if len(art) != h:
        print(f"mkicon: need {h} rows, got {len(art)}", file=sys.stderr)
        return 1
    pixels = bytearray()
    for r, line in enumerate(art):
        if len(line) != w:
            print(f"mkicon: row {r} has {len(line)} chars, need {w}",
                  file=sys.stderr)
            return 1
        for ch in line:
            if ch not in palette:
                print(f"mkicon: unknown char '{ch}'", file=sys.stderr)
                return 1
            rgb = palette[ch]
            if rgb is None:
                pixels += b"\0\0\0\0"         # 透明（A=0）
            else:
                pixels += bytes([rgb[2], rgb[1], rgb[0], 255])  # BGRA
    body = len(pixels)
    out = (b"SCB2MIO\0"
           + w.to_bytes(4, "little") + h.to_bytes(4, "little")
           + (2).to_bytes(4, "little") + (0).to_bytes(4, "little")
           + body.to_bytes(4, "little") + b"MIO\0"
           + bytes(pixels))
    open(sys.argv[2], "wb").write(out)
    print(f"mkicon: {sys.argv[1]} -> {sys.argv[2]} ({w}x{h}, {32+body}B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
