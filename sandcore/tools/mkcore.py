#!/usr/bin/env python3
"""从固定主核ELF32平载荷包装SKM2主核；不签名、不处理私钥。

主核的磁盘损坏检测与CORE扩展的Ed25519认证分别处理。入口、BSS和
实际载荷必须和ELF一致，拒绝外部未定义符号/未知段，不截断超预算映像。
"""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

BASE = 0x00800000
LIMIT = 0x01000000
IMAGE_MAX = 0x00400000


def inspect_elf(path):
    body = Path(path).read_bytes()
    if len(body) < 52:
        raise ValueError('ELF头不完整')
    head = struct.unpack_from('<16sHHIIIIIHHHHHH', body)
    if head[0][:7] != b'\x7fELF\x01\x01\x01' or head[1:3] != (2, 3) or head[8] != 52 or head[11] != 40:
        raise ValueError('主核要求小端ELF32/i386可执行映像')
    count, offset = head[12], head[6]
    if not count or offset > len(body) or count * 40 > len(body) - offset:
        raise ValueError('ELF段表越界')
    sections = [struct.unpack_from('<10I', body, offset + i * 40) for i in range(count)]
    symbols = {}
    for section in sections:
        if section[1] != 8 and (section[4] > len(body) or section[5] > len(body) - section[4]):
            raise ValueError('ELF段正文越界')
        if section[1] != 2:
            continue
        if section[6] >= count or section[9] != 16 or section[5] % 16:
            raise ValueError('符号表格式错误')
        strings = sections[section[6]]
        table = body[strings[4]:strings[4] + strings[5]]
        for at in range(section[4], section[4] + section[5], 16):
            name, value, _, _, _, index = struct.unpack_from('<IIIBBH', body, at)
            if name >= len(table):
                raise ValueError('符号名称越界')
            end = table.find(b'\0', name)
            if end < 0:
                raise ValueError('符号名称未终止')
            label = table[name:end].decode('ascii')
            if label:
                if index == 0:
                    raise ValueError('未定义主核符号: ' + label)
                symbols[label] = value
    for name in ('_core_start', '__core_base', '__core_image_end', '__bss_start', '__bss_end'):
        if name not in symbols:
            raise ValueError('缺少布局符号: ' + name)
    if head[4] != symbols['_core_start']:
        raise ValueError('ELF入口与主核入口不一致')
    for section in sections:
        if section[2] & 2 and section[5]:
            if section[3] < BASE or section[5] > LIMIT - section[3]:
                raise ValueError('可装载段超出主核区域')
            if section[1] != 8 and section[3] + section[5] > symbols['__core_image_end']:
                raise ValueError('可装载段在平载荷尾部之外')
    return symbols


def build(elf, payload_path, output):
    symbols = inspect_elf(elf)
    payload = Path(payload_path).read_bytes()
    image_bytes = symbols['__core_image_end'] - BASE
    bss_offset = symbols['__bss_start'] - BASE
    bss_bytes = symbols['__bss_end'] - symbols['__bss_start']
    entry = symbols['_core_start'] - BASE
    if symbols['__core_base'] != BASE or image_bytes != len(payload) or not 0 <= entry < image_bytes:
        raise ValueError('载荷/基址/入口不一致')
    if not 0 < image_bytes <= IMAGE_MAX or bss_offset < image_bytes or bss_offset & 4095 or bss_bytes < 0:
        raise ValueError('主核布局非法')
    if bss_offset + bss_bytes > LIMIT - BASE:
        raise ValueError('主核内存预算越界')
    header = bytearray(b'SKM2MIO\0' + struct.pack('<16I', 2, 128, 1, 0, 0, 1, 1, BASE,
        entry, image_bytes, bss_offset, bss_bytes, bss_offset + bss_bytes, 0, 0, 128 + image_bytes))
    header += hashlib.sha256(payload).digest() + bytes(20)
    header += struct.pack('<I', zlib.crc32(header))
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + payload)
    print(f'CORE: image={image_bytes} B, BSS={bss_bytes} B, entry={entry}, sha256={hashlib.sha256(payload).hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('payload')
    parser.add_argument('out')
    args = parser.parse_args()
    build(args.elf, args.payload, args.out)
