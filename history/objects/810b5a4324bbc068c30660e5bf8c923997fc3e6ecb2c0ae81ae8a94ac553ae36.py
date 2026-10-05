#!/usr/bin/env python3
"""把 SandFS 历史卷迁移为独立 v5 副本，或合并 M9 发布树。

不覆盖输入盘；真实目录、UID/GID/rw、双目录bank与CRC均写进新卷。
--replace 只允许显式替换另一个输出盘，先保留完整旧输出。
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import secrets
import struct
import zlib

MAGIC = b'SANDFSMIO'
ENTRY = struct.Struct('<64sIIiiIIII')
DIRECTORY_SECTORS = 384
DATA_START = 769
CAPACITY = 2048
DEFAULT_UID = 1000


def fold(name):
    return name.translate(str.maketrans('abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'))


def normalize(name):
    parts = []
    for part in name.split('/'):
        if not part or part == '.':
            continue
        if part == '..':
            if not parts:
                raise ValueError('路径越过根目录')
            parts.pop()
        else:
            if any(ord(char) < 33 or char in '\\:' for char in part):
                raise ValueError('非法路径字符')
            parts.append(part)
    result = '/'.join(parts)
    if len(result.encode('utf-8')) > 63:
        raise ValueError('路径超过63字节')
    return result


@dataclass
class Record:
    name: str
    payload: bytes | None
    uid: int = 0
    gid: int = 0
    mode: int = 23
    generation: int = 1


def defaults(name, directory=False):
    key = fold(name)
    if key == 'SYS/AUTH' or key.startswith('SYS/AUTH/'):
        return -1, -1, 3
    if key in ('SYS/CORE', 'SYS/MOD') or key.startswith(('SYS/CORE/', 'SYS/MOD/')):
        return -1, -1, 23
    if key == 'HOME/ROOT' or key.startswith('HOME/ROOT/'):
        return 0, 0, 3
    if key == 'HOME' or key.startswith('HOME/'):
        return DEFAULT_UID, DEFAULT_UID, 23
    if key == 'DESK' or key.startswith('DESK/') or key.startswith('SYS/THEMES/'):
        return DEFAULT_UID, DEFAULT_UID, 23
    if key in ('SYS/DISPLAY.CFG', 'SYS/THEME.CFG', 'SYS/WALL.CFG', 'SYS/MENU.CFG'):
        return DEFAULT_UID, DEFAULT_UID, 23
    return 0, 0, 23


def read_image(blob):
    if len(blob) < 512 or len(blob) % 512 or blob[:9] != MAGIC:
        raise ValueError('不是有效SandFS整盘')
    count, sectors, version = struct.unpack_from('<III', blob, 12)
    if version == 5:
        if sectors not in (192,DIRECTORY_SECTORS) or struct.unpack_from('<I', blob, 28)[0] != ENTRY.size:
            raise ValueError('v5目录布局不匹配')
        if zlib.crc32(blob[:508]) != struct.unpack_from('<I', blob, 508)[0]:
            raise ValueError('v5超级块CRC失败')
        bank, generation = struct.unpack_from('<II', blob, 48)
        if bank > 1 or not generation:
            raise ValueError('非法目录bank/代数')
        start = (1 + bank * sectors) * 512
        metadata = blob[start:start + sectors * 512]
        if zlib.crc32(metadata) != struct.unpack_from('<I', blob, 44)[0]:
            raise ValueError('v5目录CRC失败')
        capacity, stride, name_bytes, data_start = sectors*512//96, 96, 64, 1+sectors*2
    elif version == 4 and sectors in (32, 80):
        metadata = blob[512:(1 + sectors) * 512]
        capacity, stride, name_bytes, data_start = (192 if sectors == 32 else 512), 72, 64, 1 + sectors
    elif version <= 3 and (sectors or 1) in (1, 8):
        sectors = sectors or 1
        metadata = blob[512:(1 + sectors) * 512]
        capacity, stride, name_bytes, data_start = sectors * 512 // 40, 40, 32, 1 + sectors
    else:
        raise ValueError('不支持的SandFS版本')
    if count > capacity:
        raise ValueError('目录计数越界')
    declared = struct.unpack_from('<I', blob, 24)[0] if version >= 4 else 0
    declared = declared or 16384
    if not data_start <= declared <= 131072 or declared > len(blob) // 512:
        raise ValueError('源盘声明容量与实际容量不符')
    records, extents = {}, []
    for i in range(count):
        offset = i * stride
        raw = metadata[offset:offset + name_bytes]
        if len(raw) != name_bytes or not raw[0] or raw[-1]:
            raise ValueError('非法目录名称')
        name = raw.split(b'\0', 1)[0].decode('utf-8')
        if normalize(name) != name:
            raise ValueError('源盘名称不是规范完整路径')
        key = fold(name)
        if key in records:
            raise ValueError('源盘重复路径')
        at, size = struct.unpack_from('<II', metadata, offset + name_bytes)
        if at == 0 and size == 0 and version >= 4:
            payload = None
        else:
            if at < data_start or at > declared or (size + 511) // 512 > declared - at:
                raise ValueError('源盘数据范围越界')
            payload = blob[at * 512:at * 512 + size]
            extent = (at, at + (size + 511) // 512)
            if size:
                extents.append(extent)
        uid, gid, mode = defaults(name, payload is None)
        generation = 1
        if version == 5:
            uid, gid, mode, flags, generation, reserved = struct.unpack_from('<iiIIII', metadata, offset + 72)
            if uid < -1 or gid < -1 or mode & ~63 or flags != int(payload is None) or not generation or reserved:
                raise ValueError('源盘身份/类型/权限损坏')
        records[key] = Record(name, payload, uid, gid, mode, generation)
    extents.sort()
    if any(left[1] > right[0] for left, right in zip(extents, extents[1:])):
        raise ValueError('源盘重叠数据，拒绝将普通文件别名迁入核心范围')
    return records


def directories(records):
    for record in list(records.values()):
        parts = record.name.split('/')
        for index in range(1, len(parts)):
            parent = '/'.join(parts[:index])
            key = fold(parent)
            if key in records:
                if records[key].payload is not None:
                    raise ValueError('普通文件被用作父目录')
            else:
                uid, gid, mode = defaults(parent, True)
                records[key] = Record(parent, None, uid, gid, mode)
    # 账户库父目录由发布盘提供，避免首次设密码时临时补未授权目录。
    for name in ('SYS', 'SYS/CORE', 'SYS/MOD', 'SYS/AUTH', 'HOME', 'HOME/ROOT', 'USERS', 'TMP'):
        key = fold(name)
        if key not in records:
            uid, gid, mode = defaults(name, True)
            if name == 'HOME/ROOT':
                uid, gid, mode = 0, 0, 3
            elif name == 'TMP':
                uid, gid, mode = 0, 0, 63
            records[key] = Record(name, None, uid, gid, mode)
    for record in records.values():
        key = fold(record.name)
        if key in ('SYS/CORE', 'SYS/MOD', 'SYS/AUTH') or key.startswith(('SYS/CORE/', 'SYS/MOD/', 'SYS/AUTH/')):
            record.uid = record.gid = -1
            if key == 'SYS/AUTH' or key.startswith('SYS/AUTH/'):
                record.mode = 3


def make_image(records, megabytes=64):
    directories(records)
    ordered = sorted(records.values(), key=lambda item: fold(item.name))
    if len(ordered) > CAPACITY:
        raise ValueError('超过2048项，拒绝丢弃用户文件')
    image = bytearray(megabytes * 1024 * 1024)
    metadata = bytearray(DIRECTORY_SECTORS * 512)
    cursor = DATA_START
    for index, record in enumerate(ordered):
        payload = record.payload
        at, size = (0, 0) if payload is None else (cursor, len(payload))
        if payload is not None:
            blocks = (size + 511) // 512
            if cursor + blocks > len(image) // 512:
                raise ValueError('源文件与新目录超过输出盘容量')
            image[cursor * 512:cursor * 512 + size] = payload
            cursor += blocks
        ENTRY.pack_into(metadata, index * ENTRY.size, record.name.encode('utf-8'),
                        at, size, record.uid, record.gid, record.mode, int(payload is None),
                        record.generation, 0)
    image[512:(1 + DIRECTORY_SECTORS) * 512] = metadata
    image[(1 + DIRECTORY_SECTORS) * 512:DATA_START * 512] = metadata
    image[:9] = MAGIC
    struct.pack_into('<IIII', image, 12, len(ordered), DIRECTORY_SECTORS, 5, len(image) // 512)
    struct.pack_into('<IiiIIII', image, 28, ENTRY.size, 0, 0, 23, zlib.crc32(metadata), 0, 1)
    struct.pack_into('<I', image, 56, max((r.generation for r in ordered), default=0))
    struct.pack_into('<I', image, 508, zlib.crc32(image[:508]))
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-image', type=Path)
    parser.add_argument('--tree', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--replace', action='store_true')
    parser.add_argument('--megabytes', type=int, choices=[8, 16, 32, 64], default=64)
    args = parser.parse_args()
    if not args.source_image and not args.tree:
        parser.error('至少指定源镜像或发布树')
    destination = args.out.resolve()
    if args.source_image and destination == args.source_image.resolve():
        raise ValueError('输出不能是输入盘')
    if destination.exists() and not args.replace:
        raise FileExistsError('输出已存在；显式--replace才允许替换独立输出盘')
    source = args.source_image.read_bytes() if args.source_image else b''
    records = read_image(source) if source else {}
    original = {key: hashlib.sha256(record.payload).hexdigest()
                for key, record in records.items() if record.payload is not None}
    if args.tree:
        for path in sorted(args.tree.rglob('*')):
            if not path.is_file():
                continue
            name = normalize(path.relative_to(args.tree).as_posix())
            key = fold(name)
            blob = path.read_bytes()
            if key in records:
                prior = records[key]
                if prior.generation == 0xFFFFFFFF:
                    raise ValueError('对象代数耗尽，拒绝回绕')
                record = Record(prior.name, blob, prior.uid, prior.gid, prior.mode, prior.generation + 1)
            else:
                uid, gid, mode = defaults(name)
                record = Record(name, blob, uid, gid, mode)
            records[key] = record
    image = make_image(records, args.megabytes)
    # 交付工具运行时会回读全结构/CRC及每份内容，不仅核对文件长度。
    checked = read_image(image)
    for key, record in records.items():
        actual = checked[key]
        if actual.payload != record.payload or (actual.uid, actual.gid, actual.mode) != (record.uid, record.gid, record.mode):
            raise ValueError('新卷回读校验失败')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.tmp-' + secrets.token_hex(8))
    try:
        with temporary.open('xb') as stream:
            stream.write(image)
            stream.flush()
            os.fsync(stream.fileno())
        if destination.exists():
            backup = destination.with_name(destination.name + '.before-' + secrets.token_hex(8))
            with backup.open('xb') as stream:
                stream.write(destination.read_bytes())
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()
    if args.source_image and hashlib.sha256(args.source_image.read_bytes()).digest() != hashlib.sha256(source).digest():
        raise RuntimeError('输入盘在迁移期间发生变化')
    report = dict(version=5, entries=len(records), output=str(destination),
                  output_sha256=hashlib.sha256(image).hexdigest(),
                  source_sha256=hashlib.sha256(source).hexdigest() if source else None,
                  preserved=[key for key, digest in original.items() if checked[key].payload is not None
                             and hashlib.sha256(checked[key].payload).hexdigest() == digest])
    destination.with_suffix(destination.suffix + '.migration.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'SandFS v5：{len(records)}项，{len(image)}字节，输入盘未修改')


if __name__ == '__main__':
    main()
