#!/usr/bin/env python3
"""mio：把凤凰 ASCII 提取缓存编成内核表；普通构建只需 Python 标准库。

缓存由 import_font.py --ascii 从用户规定的同一个 16px TTF 提取。
不用 WSL 的 Pillow，也不回退到 BIOS/另一套字库。16px 行保留原生
字形；8px 行把相邻两行合并，用于既有 10px 行距的 Shell 和控件。
"""
import json
import sys
from pathlib import Path

def generate(source):
    record = json.loads(Path(source).read_text(encoding='utf-8'))
    assert record['source'] == 'VonwaonBitmap-16px.ttf'
    assert len(record['rows16']) == 95
    lines = ['/* mio：自动生成，来源为统一凤凰点阵体，勿直接编辑。 */',
             '#ifndef PHOENIX_ASCII_DATA_H', '#define PHOENIX_ASCII_DATA_H']
    for name, ctype, count, bits in [('sc_font','unsigned char',8,8),
                                   ('sc_latin16','unsigned short',16,16)]:
        lines.append(f'static const {ctype} {name}[95][{count}]={{')
        for rows in record['rows16']:
            assert len(rows) == 16 and all(0 <= n <= 0xffff for n in rows)
            values = rows if count == 16 else [(rows[i*2] | rows[i*2+1]) >> 8 for i in range(8)]
            lines.append('{' + ','.join(f'0x{n:0{bits//4}X}' for n in values) + '},')
        lines.append('};')
    lines.append('#endif')
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    target = Path(sys.argv[2]); target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(generate(sys.argv[1]), encoding='utf-8')
