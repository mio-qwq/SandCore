#!/usr/bin/env python3
"""M10a1独立256MiB盘：只读迁移、保留内容/身份/权限、主核/恢复分型。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import mmap
import os
from pathlib import Path
import secrets
import shutil
import struct
import zlib
from mkfs_m9 import Record, read_image, normalize, fold, defaults, ENTRY, MAGIC
from core_signature import verify

MAX_BYTES = 256 * 1024 * 1024


def digest_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def digest_payload(payload):
    return hashlib.sha256(payload).hexdigest() if payload is not None else None


def check_main(blob):
    if len(blob) < 128 or blob[:8] != b'SKM2MIO\0':
        raise ValueError('磁盘主核必须是SKM2 MAIN，不能用历史壁纸SKM1代替')
    (version, header, kind, flags, number, amin, amax, base, entry, image, bss_offset,
     bss, memory, relocs, cpu, file_size) = struct.unpack_from('<16I', blob, 8)
    if (version != 2 or header != 128 or kind != 1 or flags or number or amin != 1 or amax != 1 or
            base != 0x800000 or not 0 < image <= 0x400000 or entry >= image or bss_offset < image or
            bss_offset & 4095 or bss_offset > 0x800000 or bss > 0x800000 - bss_offset or
            memory != bss_offset + bss or relocs or cpu or file_size != len(blob) or
            len(blob) != 128 + image or any(blob[104:124]) or
            zlib.crc32(blob[:124]) != struct.unpack_from('<I', blob, 124)[0] or
            hashlib.sha256(blob[128:]).digest() != blob[72:104]):
        raise ValueError('主核格式/资源/摘要不匹配')


def mapped(stack, path):
    stream = stack.enter_context(path.open('rb'))
    if not 512 <= os.fstat(stream.fileno()).st_size <= MAX_BYTES:
        raise ValueError('源镜像长度不在支持范围')
    return stack.enter_context(mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ))


def release_views(records):
    for record in records.values():
        if isinstance(record.payload, memoryview):
            record.payload.release()


def parents(records, generation):
    for record in list(records.values()):
        parts = record.name.split('/')
        for index in range(1, len(parts)):
            name = '/'.join(parts[:index])
            key = fold(name)
            if key in records:
                if records[key].payload is not None:
                    raise ValueError('普通文件被用作父目录：' + name)
            else:
                uid, gid, mode = defaults(name, True)
                if key in ('SYS/RECOVERY', 'SYS/M10BASE'):
                    uid = gid = -1
                records[key] = Record(name, None, uid, gid, mode, generation())


def write_image(path, records, megabytes, root, commit_generation, object_generation):
    ordered = sorted(records.values(), key=lambda record: fold(record.name))
    # 目录升级不改96B记录；容量由双bank扇区数描述，旧192/384仍可读取。
    # 留至少1/4空间给用户新文件，不删第三方源码来适配旧2048项。
    need = len(ordered) + max(256, len(ordered) // 4)
    directory = next((size for size in (384, 768, 1536) if size * 512 // 96 >= need), None)
    if directory is None:
        raise ValueError('完整源码及用户文件超过本轮最大目录容量，拒绝丢弃内容')
    data_start = 1 + directory * 2
    metadata = bytearray(directory * 512)
    cursor = data_start
    with path.open('xb') as stream:
        stream.truncate(megabytes * 1024 * 1024)
        for index, record in enumerate(ordered):
            at, size = (0, 0) if record.payload is None else (cursor, len(record.payload))
            if record.payload is not None:
                blocks = (size + 511) // 512
                if blocks > megabytes * 2048 - cursor:
                    raise ValueError('源文件/历史副本/完整许可超过输出容量')
                stream.seek(cursor * 512)
                stream.write(record.payload)
                cursor += blocks
            ENTRY.pack_into(metadata, index * 96, record.name.encode('utf-8'), at, size,
                            record.uid, record.gid, record.mode, int(record.payload is None), record.generation, 0)
        stream.seek(512)
        stream.write(metadata)
        stream.write(metadata)
        superblock = bytearray(512)
        superblock[:9] = MAGIC
        struct.pack_into('<4I', superblock, 12, len(ordered), directory, 5, megabytes * 2048)
        struct.pack_into('<Iii4I', superblock, 28, 96, *root, zlib.crc32(metadata), 0, commit_generation)
        struct.pack_into('<I', superblock, 56, object_generation)
        struct.pack_into('<I', superblock, 508, zlib.crc32(superblock[:508]))
        stream.seek(0)
        stream.write(superblock)
        stream.flush()
        os.fsync(stream.fileno())
    return directory, len(ordered), cursor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-image', type=Path, required=True)
    parser.add_argument('--tree', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--legacy-wall', type=Path, required=True)
    parser.add_argument('--public-key', type=Path)
    parser.add_argument('--signed-dir', type=Path)
    parser.add_argument('--megabytes', type=int, choices=(8, 16, 32, 64, 128, 256), default=256)
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    source, destination = args.source_image.resolve(strict=True), args.out.resolve()
    if source == destination or (destination.exists() and os.path.samefile(source, destination)):
        raise ValueError('输出不能指向/硬链接至输入盘')
    if destination.exists() and not args.replace:
        raise FileExistsError('独立输出盘已存在；显式--replace才保留备份后更新')
    main_blob = args.core.read_bytes()
    check_main(main_blob)
    legacy = args.legacy_wall.read_bytes()
    if legacy[:8] != b'SKM1MIO\0':
        raise ValueError('历史壁纸副本必须保留真实SKM1字节')
    source_hash = digest_file(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.tmp-' + secrets.token_hex(8))
    records, checked = {}, {}
    report = dict(status='MIGRATED_RUNTIME_NOT_VERIFIED', source_sha256=source_hash, replacements=[], preserved=[])
    with ExitStack() as stack:
        source_map = mapped(stack, source)
        records = read_image(source_map, copy_payload=False)
        stack.callback(lambda: release_views(records))
        original = {key: (digest_payload(record.payload), record.uid, record.gid, record.mode, record.generation)
                    for key, record in records.items()}
        source_version = struct.unpack_from('<I', source_map, 20)[0]
        root = struct.unpack_from('<iiI', source_map, 32) if source_version == 5 else (0, 0, 23)
        if root[0] < -1 or root[1] < -1 or root[2] & ~63:
            raise ValueError('源卷根身份/权限损坏')
        commit = struct.unpack_from('<I', source_map, 52)[0] if source_version == 5 else 0
        counter = max((record.generation for record in records.values()), default=0)
        if source_version == 5:
            counter = max(counter, struct.unpack_from('<I', source_map, 56)[0])
        if commit == 0xffffffff:
            raise ValueError('目录代数耗尽，拒绝回绕')

        def generation():
            nonlocal counter
            if counter == 0xffffffff:
                raise ValueError('对象代数耗尽，拒绝回绕')
            counter += 1
            return counter

        archive_counter = 0

        def archive_record(prior):
            nonlocal archive_counter
            while True:
                archive_counter += 1
                archived = f'SYS/M10BASE/{archive_counter:08X}.OLD'
                if fold(archived) not in records:
                    break
            records[fold(archived)] = Record(archived, prior.payload, prior.uid, prior.gid, prior.mode, prior.generation)
            report['replacements'].append(dict(path=prior.name, archive=archived,
                                               sha256=digest_payload(prior.payload), uid=prior.uid,
                                               gid=prior.gid, mode=prior.mode, generation=prior.generation))

        def publish(name, body, force=False, system=False):
            key = fold(normalize(name))
            prior = records.get(key)
            if prior and prior.payload is None:
                raise ValueError('发布文件覆盖源目录：' + name)
            if prior and prior.payload == body:
                return
            if prior and not force and (key.startswith(('HOME/', 'DESK/', 'USERS/', 'SYS/AUTH/',
                                                       'SYS/THEMES/', 'SYS/WALLPAPERS/', 'SYS/M10BASE/', 'SYS/RECOVERY/')) or
                                        key in ('SYS/DISPLAY.CFG', 'SYS/THEME.CFG', 'SYS/WALL.CFG', 'SYS/MENU.CFG')):
                return
            if prior:
                # 被新版工具替换的原件也保留内容与原身份，索引记原完整路径。
                archive_record(prior)
            uid, gid, mode = (prior.uid, prior.gid, prior.mode) if prior else defaults(name)
            if system:
                uid = gid = -1
            records[key] = Record(prior.name if prior else normalize(name), body, uid, gid, mode, generation())

        tree_seen = set()
        for path in sorted(args.tree.rglob('*')):
            if path.is_symlink():
                raise ValueError('发布树不接收符号链接')
            if not path.is_file():
                continue
            name = normalize(path.relative_to(args.tree).as_posix())
            key = fold(name)
            if key in tree_seen:
                raise ValueError('发布树有大小写别名冲突')
            tree_seen.add(key)
            if key == 'SYS/CORE/CORE.SKM':
                continue
            if key.startswith('SYS/CORE/') and key.endswith('.SKM'):
                raise ValueError('签名扩展只从独立signed-dir引入，不把普通构建树当信任输入')
            publish(name, path.read_bytes())
        publish('SYS/CORE/CORE.SKM', main_blob, force=True, system=True)
        publish('LEGACY/SKM/WALL-V1.SKM', legacy)
        recovery = records.get('SYS/RECOVERY/CORE.SKM')
        recovery_valid = False
        if recovery:
            try:
                check_main(recovery.payload)
                recovery_valid = recovery.uid == -1 and recovery.gid == -1
            except (ValueError, TypeError):
                pass
        if not recovery_valid:
            publish('SYS/RECOVERY/CORE.SKM', main_blob, force=True, system=True)
        report['recovery'] = dict(source='preserved_source' if recovery_valid else 'initial_copy_of_candidate',
                                  known_good='REQUIRES_QEMU_EVIDENCE', sha256=digest_payload(records['SYS/RECOVERY/CORE.SKM'].payload))
        signed = []
        if args.signed_dir:
            if not args.public_key:
                raise ValueError('接收扩展必须给出与内核构建相同的用户公钥')
            key = args.public_key.read_bytes()
            numbers = set()
            for path in sorted(args.signed_dir.iterdir()):
                if not path.is_file() or path.is_symlink() or path.suffix.upper() != '.SKM' or path.name.upper() == 'CORE.SKM':
                    raise ValueError('signed-dir只接收直接扩展SKM文件')
                blob = path.read_bytes()
                item = verify(key, blob[:-64], blob[-64:])
                if item['number'] in numbers:
                    raise ValueError('待发布扩展编号冲突，拒绝整批发布')
                numbers.add(item['number'])
                item.update(path='SYS/CORE/' + path.name, file_sha256=hashlib.sha256(blob).hexdigest())
                signed.append(item)
                publish(item['path'], blob, force=True, system=True)
        report['signed_extensions'] = signed
        report['user_key'] = 'CONFIGURED_RUNTIME_PENDING' if args.public_key else 'ABSENT_ALL_CORE_EXTENSIONS_REFUSED'
        # 索引只记录这次迁移；历史盘及历史索引的冲突也先归原件副本。
        # 必须在序列化前归档旧索引，否则新索引漏掉自己的前任映射，
        # 客体内没有来源路径可追到该.OLD，而宿主报告却额外多一项。
        prior_index = records.get('SYS/M10BASE/INDEX.JSON')
        if prior_index:
            if prior_index.payload is None:
                raise ValueError('旧迁移索引为目录')
            archive_record(prior_index)
            del records['SYS/M10BASE/INDEX.JSON']
        index = json.dumps(report['replacements'], ensure_ascii=False, indent=2).encode('utf-8') + b'\n'
        publish('SYS/M10BASE/INDEX.JSON', index, force=True, system=True)
        parents(records, generation)
        try:
            directory, count, used = write_image(temporary, records, args.megabytes, root, commit + 1, counter)
            with ExitStack() as check_stack:
                output_map = mapped(check_stack, temporary)
                checked = read_image(output_map, copy_payload=False)
                try:
                    for name, expected in records.items():
                        actual = checked[name]
                        if (digest_payload(actual.payload), actual.uid, actual.gid, actual.mode, actual.generation) != (
                                digest_payload(expected.payload), expected.uid, expected.gid, expected.mode, expected.generation):
                            raise ValueError('输出回读不匹配：' + name)
                    for name, fields in original.items():
                        actual = checked[name]
                        if (digest_payload(actual.payload), actual.uid, actual.gid, actual.mode, actual.generation) == fields:
                            report['preserved'].append(name)
                        elif not any(fold(item['path']) == name for item in report['replacements']):
                            raise ValueError('源对象没有保留原件或归档：' + name)
                finally:
                    release_views(checked)
                    checked = {}
            if digest_file(source) != source_hash:
                raise RuntimeError('输入盘在迁移期间变化，拒绝发布')
            if destination.exists():
                backup = destination.with_name(destination.name + '.before-' + secrets.token_hex(8))
                with destination.open('rb') as src, backup.open('xb') as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)
                    dst.flush()
                    os.fsync(dst.fileno())
            os.replace(temporary, destination)
            report.update(directory_sectors=directory, capacity=directory * 512 // 96, entries=count,
                          disk_bytes=args.megabytes * 1024 * 1024, data_end=used,
                          output_sha256=digest_file(destination), output=str(destination))
            destination.with_suffix(destination.suffix + '.migration.json').write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        finally:
            # 成功与失败都先撤mmap视图；大盘不同时常驻源/输出的整盘副本。
            release_views(records)
            if temporary.exists():
                temporary.unlink()


if __name__ == '__main__':
    main()
