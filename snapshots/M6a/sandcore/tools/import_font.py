#!/usr/bin/env python3
"""SandCore 字形提取器 —— 按统一字体策略向 kernel/font16.txt 补字
从字体源以 16px 原生栅格化 (不缩放、无阈值歧义), 生成与手写格式
完全一致的点阵块, 追加到字形表。已存在的字跳过不动。
用法: python tools/import_font.py 开始终端        (要补的字)
      python tools/import_font.py --list          (看现有收字)"""

import io
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkfont import parse

HERE = os.path.dirname(os.path.abspath(__file__))
TXT  = os.path.join(HERE, "..", "kernel", "font16.txt")
ZIP  = r"C:\Users\Administrator\Desktop\projectos\vonwaon-bitmap.ttf.zip"
TTF_IN_ZIP = "VonwaonBitmap-16px.ttf"     # 原生 16px 版本 (font16.txt 头注释指定的源)
PX = 16


def rasterize(chars):
    """返回 {字: 16 行点阵}"""
    from PIL import Image, ImageFont, ImageDraw

    with zipfile.ZipFile(ZIP) as z:
        font = ImageFont.truetype(io.BytesIO(z.read(TTF_IN_ZIP)), PX)

    out = {}
    for ch in chars:
        img = Image.new("L", (PX, PX), 0)
        d = ImageDraw.Draw(img)
        # 用 bbox 把字形对位到 16x16 单元 (原生位图字形在 16px 下即满格)
        x0, y0, x1, y1 = font.getbbox(ch)
        d.text((-x0, -y0), ch, font=font, fill=255)
        px = img.load()
        rows = []
        for r in range(PX):
            rows.append("".join("#" if px[c, r] >= 128 else "." for c in range(PX)))
        out[ch] = rows
    return out


def main():
    have = {zh for zh, _ in parse(TXT)}

    if sys.argv[1:] == ["--list"]:
        print("现收", len(have), "字:", " ".join(sorted(have)))
        return

    args = "".join(sys.argv[1:])           # 允许 "终端" 或 "终 端" 两种写法
    if args == "--list":
        print("现收", len(have), "字:", " ".join(sorted(have)))
        return
    if not args:
        print(__doc__)
        return
    todo = [c for c in args if len(c) == 1 and c not in have]
    dup = [c for c in args if len(c) == 1 and c in have]
    if dup:
        print("已存在, 跳过:", " ".join(dup))

    if not todo:
        print("没有要补的字")
        return

    new = rasterize(todo)
    with open(TXT, "a", encoding="utf-8", newline="\n") as f:
        f.write("\n")
        for ch in todo:
            f.write("// 字 %s\n" % ch)
            for r in new[ch]:
                f.write(r + "\n")
    print("补字", len(todo), "个:", " ".join(todo), "->", TXT)
    print("记得: python tools/font_preview.py 目检后 make")


main()
