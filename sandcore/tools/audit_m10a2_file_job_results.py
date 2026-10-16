#!/usr/bin/env python3
"""只审计已实际取回的文件结果与CRC日志；不启动新的客体或修改原报告。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def journal(path, operation):
    body = path.read_bytes()
    if len(body) < 64 or body[:8] != b'SCSFJ1\0\0':
        raise ValueError('恢复头不完整：' + str(path))
    header = struct.unpack_from('<14I', body, 8)
    if header[0:2] != (64, 1) or header[6] != 1000 or header[8] != 0:
        raise ValueError('恢复头版本/普通身份错误')
    if zlib.crc32(body[:60]) != header[13]:
        raise ValueError('恢复头CRC错误')
    frames, offset = [], 64
    while offset < len(body):
        if len(body) - offset < 24:
            raise ValueError('截断frame')
        size, version, kind, sequence, count, checksum = struct.unpack_from('<6I', body, offset)
        if (size != 256 or version != 1 or count != 232 or sequence != len(frames) + 1
                or size > len(body) - offset):
            raise ValueError('恢复frame尺寸/连续序号错误')
        value = body[offset + 24:offset + size]
        if zlib.crc32(value, zlib.crc32(body[offset:offset + 20])) != checksum:
            raise ValueError('恢复frame CRC错误')
        row = struct.unpack_from('<24I', value)
        if row[:2] != (232, 1) or row[5] != operation or row[22:24] != (0, 0):
            raise ValueError('恢复ACK版本/操作错误')
        frames.append(dict(type=kind, sequence=sequence, item=row[4], state=row[6], stage=row[7],
            status=struct.unpack_from('<i', value, 32)[0], flags=row[19], bytes=row[12],
            worker_pid=row[20], worker_generation=row[21]))
        offset += size
    if len(frames) != 3 or frames[0]['type'] != 0x100 or frames[-1]['type'] != 0x101:
        raise ValueError('此单文件工作流缺少START/实际提交/FINISH')
    committed = frames[1]
    if (committed['item'] != 1 or committed['status'] != 0 or committed['state'] != 1
            or committed['stage'] != (3 if operation == 1 else 4)
            or not committed['flags'] & 1 or committed['bytes'] != 131079):
        raise ValueError('恢复日志未记录本次真实持久操作')
    return dict(path=path.name, sha256=digest(path), bytes=len(body), header_crc=True,
        frame_crcs=True, ordinary_uid=header[6], frames=frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.runtime_report.read_text(encoding='utf-8'))
    if (report.get('status') not in ('P5_TYPED_PATH_AND_REAL_FILE_JOBS_PASS','P5_REAL_CROSS_PROCESS_FILE_CLIPBOARD_PASS')
            or not report.get('current_inputs_unchanged') or not report.get('source_disk_unchanged')):
        raise ValueError('需要实际通过且输入/原盘保持的原始报告')
    root = args.runtime_report.parent
    for row in report['actual_file_results']:
        path = root / row['name']
        if path.stat().st_size != row['bytes'] or digest(path) != row['sha256']:
            raise ValueError('取回文件与实际运行报告不同')
    pattern = bytes((i * 37 + 19) & 255 for i in range(131079))
    for name in (row['name'] for row in report['actual_file_results'] if row['name'].endswith('.BIN')):
        if (root / name).read_bytes() != pattern:
            raise ValueError('实际二进制内容错误')
    audit = dict(status='ACTUAL_COPY_MOVE_BINARY_AND_RECOVERY_CRC_PASS',
        runtime_report_sha256=digest(args.runtime_report), driver_sha256=digest(Path(__file__)),
        binary_bytes=131079, binary_sha256=hashlib.sha256(pattern).hexdigest(),
        journals=[journal(root / name, operation) for name, operation in (('CP.LOG', 1), ('MV.LOG', 2))],
        new_guest_execution=False, original_report_unchanged=True, full_P5=False, full_M10a2=False)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(audit, ensure_ascii=False, indent=2) + '\n')
    print(audit['status'])


if __name__ == '__main__':
    main()
