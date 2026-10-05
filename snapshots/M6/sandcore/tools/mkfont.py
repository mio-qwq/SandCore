#!/usr/bin/env python3
"""SandCore 字形表转换器
把人可读的点阵文本 kernel/font16.txt 编译成 C 头文件
build/font16_data.h (被 kernel/font16.h 包含)。
用法: python tools/mkfont.py [font16.txt] [输出头文件]
(不传参数时使用默认路径, Makefile 就这么调)"""

import os
import sys

INK = '#'      # 生成 C 数据时, 墨统一归一化成 '#'
PAPER = '.'    # 纸归一化成 '.'


def parse(path):
    """解析字形表 → [(汉字, 16 行点阵), ...]; 语法错误抛 ValueError"""
    glyphs = []           # [(zh, [16 行])]
    cur = None            # (zh, rows) 正在收集的字
    lines = open(path, encoding="utf-8").read().splitlines()

    for lineno, raw in enumerate(lines, 1):
        s = raw.rstrip()
        if not s.strip():
            continue
        # 「字」标记可以写成 "字 X" 或 "// 字 X" (注释风格更醒目)
        t = s.strip()
        if t.startswith("//"):
            t2 = t[2:].strip()
            if not t2.startswith("字"):
                continue                 # 普通注释
            t = t2                       # 是字标记, 继续往下解析
        if t.startswith("字"):
            if cur:
                if len(cur[1]) != 16:
                    raise ValueError(f"第 {lineno} 行: 新字「{t[1:].strip()}」出现, 但上一个字"
                                     f"「{cur[0]}」只有 {len(cur[1])} 行 (必须恰好 16 行)")
                glyphs.append(cur)
            zh = t[1:].strip()
            if len(zh) != 1:
                raise ValueError(f"第 {lineno} 行: 「字」后面应恰好一个汉字, 读到「{zh}」")
            cur = (zh, [])
            continue

        # 点阵行
        if cur is None:
            raise ValueError(f"第 {lineno} 行: 点阵出现在任何「字」标记之前")
        if len(cur[1]) >= 16:
            raise ValueError(f"第 {lineno} 行: 字「{cur[0]}」已满 16 行, 这行是多余的")
        body = s[:16]
        if len(s) > 16:
            raise ValueError(f"第 {lineno} 行: 点阵超过 16 列 (多 {len(s)-16} 列)")
        row = "".join(INK if ch not in (".", " ") else PAPER for ch in body)
        row += PAPER * (16 - len(row))            # 短行右侧补纸
        cur[1].append(row)

    if cur:
        if len(cur[1]) != 16:
            raise ValueError(f"文件结尾: 最后一个字「{cur[0]}」只有 {len(cur[1])} 行 (必须恰好 16 行)")
        glyphs.append(cur)
    if not glyphs:
        raise ValueError("文件里一个字都没有")

    seen = {}
    for zh, rows in glyphs:
        if zh in seen:
            raise ValueError(f"重复的字「{zh}」(首次出现在第 {seen[zh]} 个字)")
        seen[zh] = len(seen) + 1
        if len(rows) != 16:
            raise ValueError(f"字「{zh}」行数 {len(rows)} ≠ 16")
    return glyphs


def gen_c(glyphs):
    out = []
    out.append("/* ============================================================")
    out.append(" *  自动生成文件 —— 勿直接修改!")
    out.append(" *  源文件: kernel/font16.txt   生成器: tools/mkfont.py")
    out.append(" *  字形归用户 mio 所有; 代理补字仅用 tools/import_font.py")
    out.append(" *  ============================================================ */")
    out.append("#ifndef ZH16_DATA_H")
    out.append("#define ZH16_DATA_H")
    out.append("")
    out.append("typedef struct { const char *zh; const char *r[16]; } zh16_t;")
    out.append("")
    out.append("static const zh16_t zh16_tab[] = {")
    for zh, rows in glyphs:
        out.append('{"%s", {' % zh)
        for r in rows:
            out.append('"%s",' % r)
        out.append("}},")
    out.append("};")
    out.append("")
    out.append("#define ZH16_N ((int)(sizeof(zh16_tab) / sizeof(zh16_tab[0])))")
    out.append("")
    out.append("#endif /* ZH16_DATA_H */")
    return "\n".join(out) + "\n"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "kernel", "font16.txt")
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "build", "font16_data.h")

    glyphs = parse(src)
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    open(dst, "w", encoding="utf-8", newline="\n").write(gen_c(glyphs))
    print(f"mkfont: {len(glyphs)} 字 -> {dst}")


if __name__ == "__main__":
    main()
