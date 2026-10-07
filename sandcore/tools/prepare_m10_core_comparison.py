#!/usr/bin/env python3
"""只在独立盘副本替换主核，核对其它内容/权限/位置以支持性能对照。"""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import shutil
import struct
import traceback
import zlib

from mkfs_m10 import check_main, digest_file, digest_payload, mapped, release_views
from mkfs_m9 import ENTRY, fold, read_image


def prepare(source, destination, main):
    before_hash = digest_file(source)
    report = dict(source=str(source.resolve()), source_sha256=before_hash,
                  destination=str(destination.resolve()), status='PREPARING')
    with ExitStack() as stack:
        original = mapped(stack, source)
        records = read_image(original, copy_payload=False)
        stack.callback(lambda: release_views(records))
        if struct.unpack_from('<I', original, 20)[0] != 5:
            raise ValueError('精确对照输入必须是已验证v5开发盘')
        sectors, bank, commit, counter = (struct.unpack_from('<I', original, offset)[0]
                                         for offset in (16, 48, 52, 56))
        counter = max(counter, max(record.generation for record in records.values()))
        if commit == 0xffffffff or counter == 0xffffffff:
            raise ValueError('代数耗尽，拒绝回绕')
        metadata = bytearray(original[(1+bank*sectors)*512:(1+(bank+1)*sectors)*512])
        entries, core_index = [], None
        for index in range(len(records)):
            fields = ENTRY.unpack_from(metadata, index*96)
            name = fields[0].split(b'\0', 1)[0].decode('utf-8')
            entries.append((fold(name), fields[1], fields[2]))
            if fold(name) == 'SYS/CORE/CORE.SKM':
                core_index = index
        if core_index is None:
            raise ValueError('原盘没有磁盘主核')
        core_record = records['SYS/CORE/CORE.SKM']
        old_main = bytes(core_record.payload)
        check_main(old_main)
        old_start, old_size = entries[core_index][1:]
        old_blocks, new_blocks = (old_size+511)//512, (len(main)+511)//512
        # 优先沿用原位置及扇区余量，避免挪动测试源码/SCX/字体。确实
        # 放不下时仅将新主核追加到空闲尾部，所有其它文件物理位置保持。
        following = min((at for name, at, size in entries
                         if name != 'SYS/CORE/CORE.SKM' and size and at >= old_start+old_blocks),
                        default=len(original)//512)
        new_start = old_start if old_start+new_blocks <= following else max(
            1+2*sectors, max(at+(size+511)//512 for _, at, size in entries))
        if new_start+new_blocks > len(original)//512:
            raise ValueError('副本容量不足，拒绝覆盖其它文件')
        expected = {key: (record.name, digest_payload(record.payload), record.uid, record.gid,
                          record.mode, record.generation) for key, record in records.items()}
        struct.pack_into('<II', metadata, core_index*96+64, new_start, len(main))
        struct.pack_into('<I', metadata, core_index*96+88, counter+1)
        superblock = bytearray(original[:512])
        struct.pack_into('<I', superblock, 44, zlib.crc32(metadata))
        struct.pack_into('<II', superblock, 52, commit+1, counter+1)
        struct.pack_into('<I', superblock, 508, zlib.crc32(superblock[:508]))
        # xb拒绝覆盖任何已有产物；原盘只有只读映射，恢复副本不改动。
        with source.open('rb') as input_stream, destination.open('xb') as output:
            shutil.copyfileobj(input_stream, output, 1024*1024)
            output.seek(new_start*512)
            output.write(main)
            output.write(bytes(new_blocks*512-len(main)))
            output.seek(512)
            output.write(metadata)
            output.write(metadata)
            output.seek(0)
            output.write(superblock)
        with ExitStack() as check_stack:
            candidate = mapped(check_stack, destination)
            checked = read_image(candidate, copy_payload=False)
            check_stack.callback(lambda: release_views(checked))
            if set(checked) != set(records):
                raise AssertionError('副本文件集合改变')
            preserved = []
            for key, record in checked.items():
                actual = (record.name, digest_payload(record.payload), record.uid, record.gid,
                          record.mode, record.generation)
                if key == 'SYS/CORE/CORE.SKM':
                    before = expected[key]
                    if actual != (before[0], digest_payload(main), before[2], before[3], before[4], counter+1):
                        raise AssertionError('主核副本内容或身份不符')
                else:
                    if actual != expected[key]:
                        raise AssertionError('其它文件内容/身份/代数改变：'+record.name)
                    preserved.append(dict(path=record.name, sha256=actual[1], uid=record.uid,
                                          gid=record.gid, mode=record.mode, generation=record.generation))
            # 两bank只改主核的start/size/generation；其它96B目录项原字节
            # 相同，同时证明未为了对照改变其它文件的位置或权限。
            baseline = original[(1+bank*sectors)*512:(1+(bank+1)*sectors)*512]
            for index in range(len(records)):
                if index != core_index and metadata[index*96:(index+1)*96] != baseline[index*96:(index+1)*96]:
                    raise AssertionError('其它物理目录项改变')
            if original[32:44] != candidate[32:44]:
                raise AssertionError('根权限改变')
        report.update(status='ONLY_MAIN_CHANGED_RUNTIME_NOT_VERIFIED', files=len(records),
                      main_before_sha256=digest_payload(old_main), main_after_sha256=digest_payload(main),
                      old_extent=dict(start=old_start, bytes=old_size),
                      new_extent=dict(start=new_start, bytes=len(main)),
                      commit_before=commit, commit_after=commit+1, preserved=preserved,
                      recovery_preserved=expected['SYS/RECOVERY/CORE.SKM'][1] if 'SYS/RECOVERY/CORE.SKM' in expected else None)
    report['source_unchanged'] = digest_file(source)==before_hash
    if not report['source_unchanged']:
        raise AssertionError('只读原盘在对照准备中发生外部改动')
    report['destination_sha256'] = digest_file(destination)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if len(args.data)<2 or len(set(path.resolve(strict=True) for path in args.data)) != len(args.data):
        parser.error('至少两只不同来源输入盘')
    main_blob = args.core.read_bytes()
    check_main(main_blob)
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='PREPARING', core=str(args.core.resolve()),
                  core_sha256=digest_payload(main_blob), disks=[])
    try:
        for index, source in enumerate(args.data, 1):
            report['disks'].append(prepare(source, args.out/f'disk-{index}.img', main_blob))
        report['status'] = 'ONLY_MAIN_CHANGED_RUNTIME_NOT_VERIFIED'
    except BaseException as error:
        report['status'] = 'FAIL_OR_INTERRUPTED'
        report['error'] = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
        raise
    finally:
        (args.out/'comparison-inputs.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
