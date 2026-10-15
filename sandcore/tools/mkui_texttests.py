#!/usr/bin/env python3
"""固定官方15.1边界oracle转换为客体值记录，不复制分段实现。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tests/m10a2/unicode15_1'
EXPECTED = {
    'GraphemeBreakTest.txt': 'ed9c5e92fd0911ccbeeb63c97cb19c519ea272ff1112ce843abd991582dd848f',
    'WordBreakTest.txt': '69506b2253b8defb1f15b1476397376285cfda3bafd9304ee50d48bbfa0a2ab4',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/ui/tests')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    for kind, (name, wanted) in enumerate(EXPECTED.items(), 1):
        original = (SOURCE / name).read_bytes()
        if hashlib.sha256(original).hexdigest() != wanted:
            raise ValueError('官方oracle固定摘要改变：' + name)
        rows = []
        for line_number, line in enumerate(original.decode('utf-8').splitlines(), 1):
            words = line.split('#', 1)[0].split()
            if not words:
                continue
            if len(words) % 2 != 1 or words[0] != '÷' or words[-1] != '÷':
                raise ValueError((name, line_number, '边界行格式不完整'))
            text, boundaries = bytearray(), []
            for index, word in enumerate(words):
                if index % 2 == 0:
                    if word not in ('÷', '×'):
                        raise ValueError((name, line_number, '未知边界符'))
                    if word == '÷':
                        boundaries.append(len(text))
                else:
                    text.extend(chr(int(word, 16)).encode('utf-8'))
            if not text or boundaries[0] != 0 or boundaries[-1] != len(text):
                raise ValueError((name, line_number, '缺首尾边界'))
            row = struct.pack('<III', len(text), len(boundaries), line_number)
            row += text + bytes((-len(text)) & 3)
            row += struct.pack('<' + 'I' * len(boundaries), *boundaries)
            rows.append(row)
        payload = b''.join(rows)
        blob = b'SCUB15_1' + struct.pack('<6I', 1, kind, len(rows), 32 + len(payload), zlib.crc32(payload), 0) + payload
        target = args.out / ('GRAPHEME.SCT' if kind == 1 else 'WORD.SCT')
        if target.exists() and target.read_bytes() != blob:
            raise FileExistsError('拒绝覆盖不同固定oracle：' + str(target))
        target.write_bytes(blob)
        records.append(dict(path=target.name, kind=kind, cases=len(rows), bytes=len(blob),
            sha256=hashlib.sha256(blob).hexdigest(), original=name, original_sha256=wanted,
            expected_boundaries_source='Official divide/multiply markers, no implementation-derived expectations'))
    report = dict(version='15.1.0', inputs_unchanged=True, guest_executed=False, outputs=records)
    (args.out / 'PROVENANCE.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
