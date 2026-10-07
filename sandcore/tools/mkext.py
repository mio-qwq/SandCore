#!/usr/bin/env python3
"""将兼容SKM1装载正文封装为待用户离线签署的SKM2扩展消息，不签署。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

HEADER = 128
SIGNATURE = 64
IMAGE_MAX = 4 * 1024 * 1024
MEMORY_MAX = 16 * 1024 * 1024


def check_message(message):
    if len(message) < HEADER or message[:8] != b'SKM2MIO\0':
        raise ValueError('扩展消息魔数/头损坏')
    values = struct.unpack_from('<16I', message, 8)
    (version, header, kind, flags, number, abi_min, abi_max, base, entry,
     image, bss_offset, bss, memory, relocs, cpu, file_size) = values
    if (version != 2 or header != HEADER or kind != 2 or flags or
            not 1 <= number <= 0x7fffffff or abi_min != 1 or abi_max != 1 or base or
            not 0 < image <= IMAGE_MAX or entry >= image or bss_offset != image or
            bss > MEMORY_MAX - image or memory != image + bss or relocs > image // 4 or
            cpu & ~1 or file_size != len(message) + SIGNATURE or
            len(message) != HEADER + image + relocs * 4 or any(message[104:124]) or
            zlib.crc32(message[:124]) != struct.unpack_from('<I', message, 124)[0] or
            hashlib.sha256(message[HEADER:]).digest() != message[72:104]):
        raise ValueError('扩展消息格式/摘要/资源/ABI损坏')
    previous = -4
    for index in range(relocs):
        offset = struct.unpack_from('<I', message, HEADER + image + index * 4)[0]
        if offset > image - 4 or offset < previous + 4:
            raise ValueError('扩展重定位范围/顺序/重叠损坏')
        if struct.unpack_from('<I', message, HEADER + offset)[0] >= memory:
            raise ValueError('扩展重定位指向映像/BSS之外')
        previous = offset
    return dict(number=number, image_bytes=image, bss_bytes=bss, memory_bytes=memory,
                relocations=relocs, entry_offset=entry, required_cpu=cpu,
                payload_sha256=message[72:104].hex(), message_sha256=hashlib.sha256(message).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', type=Path, required=True)
    parser.add_argument('--number', type=int, required=True)
    parser.add_argument('--required-sse2', action='store_true')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    legacy = args.legacy.read_bytes()
    if len(legacy) < 32 or legacy[:8] != b'SKM1MIO\0':
        raise ValueError('需要完整SKM1载荷，SCX/ELF/主核不能互换')
    entry, image, bss, relocs, abi, marker = struct.unpack_from('<6I', legacy, 8)
    if abi != 1 or marker != 0x004f494d or len(legacy) != 32 + image + relocs * 4:
        raise ValueError('SKM1长度/ABI损坏')
    body = legacy[32:]
    header = b'SKM2MIO\0' + struct.pack('<16I', 2, HEADER, 2, 0, args.number, 1, 1, 0,
                                     entry, image, image, bss, image + bss, relocs,
                                     int(args.required_sse2), HEADER + len(body) + SIGNATURE)
    header += hashlib.sha256(body).digest() + bytes(20)
    message = header + struct.pack('<I', zlib.crc32(header)) + body
    report = check_message(message)
    report.update(status='UNSIGNED_USER_SIGNATURE_REQUIRED', algorithm='Ed25519',
                  signed_bytes='128-byte header, initialized payload, relocation table; signature excluded',
                  legacy_sha256=hashlib.sha256(legacy).hexdigest(), source=str(args.legacy))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(message)
    args.out.with_suffix(args.out.suffix + '.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
