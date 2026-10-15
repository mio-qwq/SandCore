#!/usr/bin/env python3
"""固定Unicode15.1属性→SCUI连续区间资源；不引入第三方分段实现。"""
import argparse
from array import array
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/unicode15_1'
GCB = {name: index for index, name in enumerate(('Other', 'CR', 'LF', 'Control',
    'Extend', 'ZWJ', 'Regional_Indicator', 'Prepend', 'SpacingMark', 'L', 'V', 'T', 'LV', 'LVT'))}
WB = {name: index for index, name in enumerate(('Other', 'CR', 'LF', 'Newline',
    'Extend', 'ZWJ', 'Regional_Indicator', 'Format', 'Katakana', 'Hebrew_Letter', 'ALetter',
    'Single_Quote', 'Double_Quote', 'MidNumLet', 'MidLetter', 'MidNum', 'Numeric', 'ExtendNumLet', 'WSegSpace'))}
INCB = {'None': 0, 'Consonant': 1, 'Linker': 2, 'Extend': 3}

def parse(path):
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        body = line.split('#', 1)[0].strip()
        if not body:
            continue
        fields = [part.strip() for part in body.split(';')]
        span = fields[0].split('..')
        start, end = int(span[0], 16), int(span[-1], 16)
        if not 0 <= start <= end <= 0x10ffff:
            raise ValueError('%s:%d范围非法' % (path.name, number))
        yield start, end, fields[1:]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/ui/UAX15_1.SCU')
    args = parser.parse_args()
    provenance = json.loads((SOURCE / 'PROVENANCE.json').read_text())
    if provenance['version'] != '15.1.0':
        raise ValueError('原数据版本不匹配')
    for item in provenance['sources']:
        if hashlib.sha256((SOURCE / item['path']).read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('固定原数据已改变：' + item['path'])
    values = array('I', [0]) * 0x110000
    for name, table, shift, mask in (('GraphemeBreakProperty.txt', GCB, 0, 15),
                                     ('WordBreakProperty.txt', WB, 8, 31 << 8)):
        assigned = bytearray(0x110000)
        for start, end, fields in parse(SOURCE / name):
            code = table[fields[0]] << shift
            for scalar in range(start, end + 1):
                if assigned[scalar]:
                    raise ValueError('同属性范围重叠：' + name)
                assigned[scalar] = 1
                values[scalar] = (values[scalar] & ~mask) | code
    for start, end, fields in parse(SOURCE / 'emoji-data.txt'):
        if fields[0] == 'Extended_Pictographic':
            for scalar in range(start, end + 1):
                values[scalar] |= 1 << 16
    assigned = bytearray(0x110000)
    for start, end, fields in parse(SOURCE / 'DerivedCoreProperties.txt'):
        if fields[0] == 'InCB':
            code = INCB[fields[1]] << 20
            for scalar in range(start, end + 1):
                if assigned[scalar]:
                    raise ValueError('InCB范围重叠')
                assigned[scalar] = 1
                values[scalar] |= code
    rows = []
    first, value = 0, values[0]
    for scalar in range(1, 0x110000):
        if values[scalar] != value:
            rows.append((first, scalar - 1, value))
            first, value = scalar, values[scalar]
    rows.append((first, 0x10ffff, value))
    payload = b''.join(struct.pack('<III', *row) for row in rows)
    header = b'SCUT15_1' + struct.pack('<14I', 64, 1, 15, 1, 0, 64 + len(payload),
        len(rows), 12, zlib.crc32(payload), 0x10ffff, 0x00311f0f, 0, 0, 0)
    blob = header + payload
    if len(blob) > 65536:
        raise ValueError('真实属性资源超单次64KiB读取预算，请扩流式读取而非截断数据')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.exists() and args.out.read_bytes() != blob:
        raise FileExistsError('已有不同资源必须人工核对来源后更新，不能覆盖')
    args.out.write_bytes(blob)
    report = dict(version='15.1.0', format='SCUT15_1', bytes=len(blob), records=len(rows),
        sha256=hashlib.sha256(blob).hexdigest(), payload_crc32=zlib.crc32(payload),
        original_data_unchanged=True, source_provenance_sha256=hashlib.sha256((SOURCE / 'PROVENANCE.json').read_bytes()).hexdigest(),
        generated_by='tools/mkui_textdata.py', guest_executed=False, copied_segmentation_implementation=False)
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
