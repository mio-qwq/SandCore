#!/usr/bin/env python3
"""只读统计原TTF映射与项目源码缺字；全量实现前只写源码、不执行。"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent


def unicode_mapping(path):
    raw = path.read_bytes()
    u16 = lambda at: struct.unpack_from('>H', raw, at)[0]
    u32 = lambda at: struct.unpack_from('>I', raw, at)[0]
    if len(raw) < 12 or u32(0) != 0x10000:
        raise ValueError('需要独立TrueType文件')
    tables = {}
    for index in range(u16(4)):
        at = 12 + index * 16
        tag = raw[at:at + 4].decode('ascii')
        offset, length = u32(at + 8), u32(at + 12)
        if tag in tables or offset > len(raw) or length > len(raw) - offset:
            raise ValueError('表目录重复/越界')
        tables[tag] = (offset, length)
    glyphs = u16(tables['maxp'][0] + 4)
    cmap, length = tables['cmap']
    candidates = []
    for index in range(u16(cmap + 2)):
        at = cmap + 4 + index * 8
        platform, encoding, offset = u16(at), u16(at + 2), u32(at + 4)
        if offset > length - 2:
            raise ValueError('字符子表越界')
        base = cmap + offset
        fmt = u16(base)
        if (platform == 0 or platform == 3 and encoding in (1, 10)) and fmt in (4, 12):
            candidates.append((fmt, base))
    if not candidates:
        raise ValueError('没有Unicode cmap4/12')
    fmt, base = max(candidates)
    result = {}
    if fmt == 4:
        count = u16(base + 6) // 2
        limit = base + u16(base + 2)
        for index in range(count):
            start = u16(base + 16 + count * 2 + index * 2)
            end = u16(base + 14 + index * 2)
            delta = u16(base + 16 + count * 4 + index * 2)
            slot = base + 16 + count * 6 + index * 2
            offset = u16(slot)
            for scalar in range(start, end + 1):
                if offset:
                    at = slot + offset + (scalar - start) * 2
                    if at + 2 > limit:
                        raise ValueError('glyphIdArray越界')
                    value = u16(at)
                    glyph = (value + delta) & 65535 if value else 0
                else:
                    glyph = (scalar + delta) & 65535
                if glyph >= glyphs:
                    raise ValueError('映射引用不存在字形')
                if glyph and not 0xd800 <= scalar <= 0xdfff:
                    result[scalar] = glyph
    else:
        for index in range(u32(base + 12)):
            at = base + 16 + index * 12
            first, last, initial = u32(at), u32(at + 4), u32(at + 8)
            if first > last or last > 0x10ffff or initial + last - first >= glyphs:
                raise ValueError('完整Unicode组范围损坏')
            for scalar in range(first, last + 1):
                glyph = initial + scalar - first
                if glyph and not 0xd800 <= scalar <= 0xdfff:
                    result[scalar] = glyph
    return result, dict(file=str(path), sha256=hashlib.sha256(raw).hexdigest(),
                        glyphs=glyphs, mapped_scalars=len(result),
                        mapped_glyphs=len(set(result.values())), cmap_format=fmt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, action='append')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    fonts = args.font or [ROOT / f'third_party/vonwaon/VonwaonBitmap-{pixels}px.ttf' for pixels in (12, 16)]
    corpus, occurrences, locations = set(), Counter(), {}
    files = []
    for directory in ('kernel', 'boot', 'modules', 'user'):
        for path in sorted((ROOT / directory).rglob('*')):
            if not path.is_file() or path.suffix.lower() not in ('.c', '.h', '.inc', '.asm'):
                continue
            text = path.read_text(encoding='utf-8-sig')
            name = path.relative_to(ROOT).as_posix()
            files.append(dict(path=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
            for number, line in enumerate(text.splitlines(), 1):
                for char in line:
                    scalar = ord(char)
                    if scalar < 32 or 127 <= scalar <= 159:
                        continue
                    corpus.add(scalar)
                    occurrences[scalar] += 1
                    items = locations.setdefault(scalar, [])
                    location = f'{name}:{number}'
                    if len(items) < 10 and location not in items:
                        items.append(location)
    reports = []
    for font in fonts:
        mapping, report = unicode_mapping(font)
        missing = sorted(corpus - mapping.keys())
        report.update(source_unique_scalars=len(corpus), source_missing_unique=len(missing),
                      source_covered_occurrences=sum(count for scalar, count in occurrences.items() if scalar in mapping),
                      missing=[dict(scalar=f'U+{scalar:04X}', char=chr(scalar), occurrences=occurrences[scalar],
                                    locations=locations[scalar]) for scalar in missing])
        reports.append(report)
    output = dict(status='HOST_SOURCE_MAPPING_ONLY_RUNTIME_PENDING', fonts=reports, source_files=files)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(output, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
