#!/usr/bin/env python3
"""mio：按用户明确批准的范围发布 M8a，保留完整 M8 的未达成预期。

本工具只保存已经存在的字节，不构建、不启动 QEMU、不安装新原生产物。
它不能替代 package_m8.py 的完整 M8 验收合同，也不修改其成功门槛。
发布镜像是现有 build-29 开发盘，来源和未达成项在公开说明/清单中显式记录。
ZIP 写完必须从 ZIP 自身逐项重读；文件完整性通过与功能/性能通过分别记录。
历史大镜像/显存快照/整片帧原地保留，列索引而不重新复制32GB历史工作区。
"""

import datetime
import hashlib
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SYSTEM = ROOT / 'sandcore'
BUILD = SYSTEM / 'build'
OUTPUT = BUILD / 'SandCore-M8a-2026-10-04.zip'
RECORD = BUILD / 'm8-frozen-20261004-01'
CONTENT_DIRS = ('assets', 'boot', 'docs', 'kernel', 'legacy', 'modules',
                'third_party', 'tools', 'user')
CONTENT_FILES = ('Makefile', 'README.md', 'build.bat', 'run.bat', 'run.sh', 'linker.ld')
EXPECTED_FONT = '8f286f8ac7e9c1d714a2dec6613bb9416660a7232fc8d78ffa8024b3c23e782b'
EXPECTED_API = '3822d8180006dc869926538fff6293378217bf7df927f8e9e647a483dd092f78'
UNMET = [
    '游戏现代画质与稳定可玩速度/争取60FPS未达标，保留当前实现',
    '42秒连续宣传片、画质/时域验收和新系统视频播放器未完成',
    '全部组件在当前最终源码/核下统一显示/缩放/主题/交互矩阵未完成',
    '默认盘仍为build-29开发产物，未完成全部最新G2统一原生发布及冷启动',
    '本M8a包不等于原完整M8全项验收通过',
]


def sha(path):
    """镜像和历史包按块摘要，避免把大量证据同时载入宿主内存。"""
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')


def source_files():
    """显式选择工程树；不遍历 build 来意外把32GB历史盘全塞进发布包。"""
    files = {ROOT / name for name in ('AGENTS.md', 'HANDOFF.md', 'M8_GOAL.md',
                                    'AUDIT-NOTES.md', 'vonwaon-bitmap.ttf.zip')}
    files.update(SYSTEM / name for name in CONTENT_FILES)
    for directory in CONTENT_DIRS:
        files.update(p for p in (SYSTEM / directory).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.tmp'))
    for name in ('boot.bin', 'kernel.bin', 'kernel.elf', 'kernel.sym',
                 'sandcore.img', 'sanddata.img', 'SandCore-M7-2026-10-02.zip',
                 'm7-integrity.json', 'm7-native-runtime.json'):
        files.add(BUILD / name)
    files.update(p for p in (BUILD / 'fs').rglob('*') if p.is_file())
    files.update(BUILD.glob('user-*.sym'))
    files.update(BUILD.glob('user-*.kernel.elf'))
    return files


def evidence_files():
    """报告包含失败，绝不改状态；大原件留原地，精选截图只作为对应历史证据。"""
    files = set()
    # 各批JSON报告/身份/停止记录小于8MiB时随包保存；不将其内容改成M8a PASS。
    for path in BUILD.rglob('*.json'):
        if RECORD in path.parents or path.stat().st_size > 8 * 1024 * 1024:
            continue
        files.add(path)
    # 这些目录只选已有截图与实际SCX/map，不重新运行测试，也不复制测试盘/pmemsave。
    roots = [BUILD / name for name in ('m8-truecolor', 'm8-theme', 'm8-images',
             'm8-userspace', 'm8-system', 'm8-settings', 'm8-canvas', 'm8-files',
             'm8-lens', 'm8-frame-irq', 'm8-damage', 'm8-idle-yield')]
    run = BUILD / 'm8-phase2-resume-20261003-01'
    roots += [run / name for name in ('native-09', 'native-apps-13',
              'race-shadow-640-01', 'lens-default-png-02', 'prism-wallpaper-03')]
    for directory in roots:
        files.update(p for p in directory.rglob('*') if p.is_file()
                     and p.suffix.lower() in ('.png', '.scx', '.map'))
    # 母版原帧未全入包；试帧预览是图片而非成片，名字和报告保留其真实范围。
    for name in ('math-adaptive-20261004-02', 'math-adaptive-20261004-03'):
        directory = BUILD / 'film' / name
        files.update(p for p in directory.rglob('*') if p.is_file() and p.name.startswith('preview'))
    return files


def main():
    if OUTPUT.exists() or OUTPUT.with_suffix('.zip.pending').exists():
        raise FileExistsError('拒绝覆盖已有发布包或未完成包：' + str(OUTPUT))
    RECORD.mkdir(parents=True, exist_ok=True)
    if not (RECORD / 'processes.json').is_file():
        raise FileNotFoundError('先保存封存时后台进程检查记录')
    if sha(SYSTEM / 'kernel/font16.txt') != EXPECTED_FONT or sha(SYSTEM / 'user/SCAPI.H') != EXPECTED_API:
        raise RuntimeError('封存前字体或SCAPI发生了未登记变化')

    # 先保存历史目录索引，再输出新发布文件，避免索引把自己反复包含或追逐新文件。
    history = []
    for path in sorted(BUILD.rglob('*')):
        if path.is_file() and RECORD not in path.parents:
            stat = path.stat()
            history.append({'path': path.relative_to(ROOT).as_posix(), 'bytes': stat.st_size,
                            'mtime_ns': stat.st_mtime_ns})
    history_record = {'magic': 'SCM8HISTORY1MIO', 'author': 'mio', 'version': 1,
                      'scope': 'EXISTING_BUILD_METADATA_NOT_FULL_BYTE_COPY',
                      'files': history, 'count': len(history),
                      'bytes': sum(item['bytes'] for item in history)}
    (RECORD / 'BUILD-HISTORY-INDEX.json').write_bytes(json_bytes(history_record))
    files = source_files() | evidence_files() | {RECORD / 'processes.json', RECORD / 'BUILD-HISTORY-INDEX.json'}
    entries = {}
    for path in sorted(files):
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError('缺失文件或归档路径越界：' + str(path))
        entries[path.relative_to(ROOT).as_posix()] = path

    manifest = {'magic': 'SCM8ARELEASE1MIO', 'version': 1, 'author': 'mio',
                'release': 'M8a', 'date': '2026-10-04',
                'created_at': datetime.datetime.now().astimezone().isoformat(),
                'status': 'USER_AUTHORIZED_PARTIAL_SCOPE_RELEASE',
                'authorization': '用户明确命名M8a并批准现在发布；本次部分收束/跳过获许可，以后未经许可不得重复',
                'accepted_scope': '现有主体成果够用且获用户认可；真彩色/M8a组件成果保留',
                'unmet_expectations': UNMET, 'complete_m8_verified': False,
                'new_build_or_qemu_tests': False,
                'runtime_origin': 'build-29 current development disks; HOST development components and separately preserved genuine G2 evidence',
                'mounts': {'sandcore.img': 'if=floppy', 'sanddata.img': 'if=ide'},
                'historical_build_bytes_preserved_in_place': history_record['bytes'],
                'files': {}}
    for name, path in entries.items():
        manifest['files'][name] = {'size': path.stat().st_size, 'sha256': sha(path)}
    manifest_bytes = json_bytes(manifest)
    pending = OUTPUT.with_suffix('.zip.pending')
    with zipfile.ZipFile(pending, 'x', zipfile.ZIP_DEFLATED, compresslevel=4) as archive:
        for name, path in entries.items():
            archive.write(path, name)
        archive.writestr('M8A-MANIFEST.json', manifest_bytes)
        archive.writestr('BUILD-HISTORY-INDEX.json', json_bytes(history_record))

    # 对写入的实际ZIP逐成员流式核对：同时验证CRC、长度和SHA，不凭写入返回就宣布发布。
    with zipfile.ZipFile(pending) as archive:
        expected_names = set(entries) | {'M8A-MANIFEST.json', 'BUILD-HISTORY-INDEX.json'}
        if len(archive.namelist()) != len(expected_names) or set(archive.namelist()) != expected_names:
            raise RuntimeError('ZIP成员缺失、重复或出现未登记文件')
        for name, record in manifest['files'].items():
            result = hashlib.sha256()
            size = 0
            with archive.open(name) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    size += len(block)
                    result.update(block)
            if size != record['size'] or result.hexdigest() != record['sha256']:
                raise RuntimeError('ZIP实际字节核对失败：' + name)
            # 打包过程中源文件如被其它任务修改，不能交付一份混合时间点的快照。
            if sha(entries[name]) != record['sha256']:
                raise RuntimeError('归档期间源文件变化：' + name)
        if archive.read('M8A-MANIFEST.json') != manifest_bytes or archive.testzip() is not None:
            raise RuntimeError('ZIP清单或CRC核对失败')
    # Windows rename 在目标已经存在时拒绝，保留此前发布包，不用replace悄悄覆盖。
    os.rename(pending, OUTPUT)
    digest = sha(OUTPUT)
    OUTPUT.with_suffix('.sha256').write_text(digest + '  ' + OUTPUT.name + '\n', encoding='ascii')
    (RECORD / 'manifest.json').write_bytes(manifest_bytes)
    summary = {'magic': 'SCM8APACKAGE1MIO', 'version': 1, 'author': 'mio', 'release': 'M8a',
               'status': 'PACKAGE_BYTES_VERIFIED', 'archive': str(OUTPUT),
               'bytes': OUTPUT.stat().st_size, 'sha256': digest, 'files': len(entries),
               'verification': 'ZIP member uniqueness, complete CRC, exact byte length and SHA256, unchanged input files',
               'new_build_or_qemu_tests': False, 'complete_m8_verified': False}
    (RECORD / 'package-result.json').write_bytes(json_bytes(summary))
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
