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
# 源字体归项目根资源：按脚本位置定位，使验收包解压到别处仍能使用
# 用户指定的同一字库，而不是悄悄读回旧工作区的绝对路径。
ZIP  = os.path.normpath(os.path.join(HERE, "..", "..", "vonwaon-bitmap.ttf.zip"))
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
    if sys.argv[1:] == ["--ascii"]:
        # 同源提取缓存独立于用户的汉字文本，避免重排/覆盖 font16.txt。
        # 栅格化使用统一基线，不能把每个字自己的 bbox 顶边都移到 0：
        # 那会让 g/j 下降部消失，并让大小写看起来拥有同样字高。
        import hashlib, json
        from PIL import Image, ImageFont, ImageDraw
        with zipfile.ZipFile(ZIP) as z:
            raw = z.read(TTF_IN_ZIP)
        font = ImageFont.truetype(io.BytesIO(raw), PX)
        rows = []
        for code in range(32, 127):
            image = Image.new("L", (16, 16), 0)
            ImageDraw.Draw(image).text((0, 0), chr(code), font=font, fill=255)
            rows.append([sum((1 << (15-x)) for x in range(16) if image.getpixel((x,y)) >= 128) for y in range(16)])
        target = os.path.join(HERE, "..", "assets", "font", "phoenix-ascii.json")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            json.dump(dict(source=TTF_IN_ZIP, ttf_sha256=hashlib.sha256(raw).hexdigest(), rows16=rows), f, indent=2)
        print("凤凰 ASCII 同源缓存 ->", target)
        return
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
