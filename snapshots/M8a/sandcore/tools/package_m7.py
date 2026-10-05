#!/usr/bin/env python3
"""mio：收录 M6/M7 联合验收包并逐文件验证交付字节。

源码完整收录，build 则显式列举：默认双盘、原生产物资源树、首次启动
快照与诊断 ELF、当前成功证据。旧超大日志、pmemsave 暂存内存、曾经
失败的 timeout/diagnostic 和未成片的预览不充当本次成功证据。历史 M6
原包原样保留，让接手者可核对原 27 字和历史 ABI，不能拿新版重新覆盖。

打包前核对结果、发布摘要及当前文件；打包后从 ZIP 逐项重读并验 CRC/
SHA256。manifest 是包内每个文件的摘要，外部 .sha256 是整个 ZIP 摘要，
二者避免“文件夹里的内容对，但交付的压缩字节错”这一验收盲区。
"""
import hashlib
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SYSTEM = ROOT / 'sandcore'
BUILD = SYSTEM / 'build'
OUTPUT = BUILD / 'SandCore-M7-2026-10-02.zip'
FONT_HASH = '8f286f8ac7e9c1d714a2dec6613bb9416660a7232fc8d78ffa8024b3c23e782b'


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def collect():
    """只收录可复验的必要构建内容；集合消除规则重叠的重复成员。"""
    files = {ROOT / 'AGENTS.md', ROOT / 'HANDOFF.md', ROOT / 'vonwaon-bitmap.ttf.zip'}
    for item in SYSTEM.rglob('*'):
        rel = item.relative_to(SYSTEM)
        if item.is_file() and rel.parts[0] != 'build' and '__pycache__' not in rel.parts and item.suffix != '.pyc':
            files.add(item)
    for name in ('boot.bin', 'kernel.bin', 'kernel.elf', 'kernel.sym', 'sandcore.img',
                 'sanddata.img', 'M7-bootstrap-sanddata.img', 'm6a-source-baseline.zip',
                 'SandCore-M6-2026-10-02.zip', 'm7-native-runtime.json', 'm7-integrity.json'):
        files.add(BUILD / name)
    files.update(BUILD.glob('user-*.sym'))
    files.update(BUILD.glob('user-*.kernel.elf'))
    files.update(item for item in (BUILD / 'fs').rglob('*') if item.is_file())
    # 上一轮故障与早期单景预览仍在工作区，按名字排除，避免冒充当前矩阵。
    excluded = {'timeout.png', 'race-diagnostic.png', 'lumen-a.png', 'lumen-b.png',
                'race-a.png', 'race-b.png', 'world-a.png', 'world-b.png',
                '06-self-compiler-output-runs.png'}
    screenshot_counts = {}
    for directory, expected in (('m7-base', 10), ('m7-compiler', 20), ('m7-graphics', 35),
                                ('m7-modules', 2), ('m6a-m7', 11), ('m6-apps-m7', 24)):
        evidence = BUILD / directory
        shots = [item for item in evidence.glob('*.png') if item.name not in excluded]
        assert len(shots) == expected, f'{directory}: {len(shots)} != {expected}'
        screenshot_counts[directory] = len(shots)
        files.update(shots)
        for name in ('results.json', 'qemu-command.json', 'sanddata-test.img'):
            files.add(evidence / name)
        for name in ('commands.json', 'native-provenance.json'):
            if (evidence / name).exists():
                files.add(evidence / name)
    files.update((BUILD / 'm7-compiler').glob('s3c-generation*.scx'))
    files.add(BUILD / 'm7-compiler/feature.scx')
    files.update((BUILD / 'm7-graphics').glob('*.scx'))
    for item in files:
        if not item.is_file():
            raise FileNotFoundError(item)
    return sorted(files, key=lambda p: p.relative_to(ROOT).as_posix()), screenshot_counts


def main():
    assert digest((SYSTEM / 'kernel/font16.txt').read_bytes()) == FONT_HASH
    audit = json.loads((BUILD / 'm7-integrity.json').read_text(encoding='utf-8'))
    runtime = json.loads((BUILD / 'm7-native-runtime.json').read_text(encoding='utf-8'))
    assert audit['status'] == runtime['status'] == 'PASS'
    for name in ('sandcore.img', 'sanddata.img'):
        actual = digest((BUILD / name).read_bytes())
        assert actual == audit['images'][name] == runtime['images'][name], name + ' changed after audit'
    for directory in ('m7-base', 'm7-compiler', 'm7-graphics', 'm7-modules', 'm6a-m7', 'm6-apps-m7'):
        result = json.loads((BUILD / directory / 'results.json').read_text(encoding='utf-8'))
        if isinstance(result, list):
            assert result and all('通过' in value for value in result), directory
        else:
            assert result, directory
            for key, value in result.items():
                if value is False or value == 'FAIL':
                    raise AssertionError(directory + ': ' + key)
    files, counts = collect()
    manifest = {'author': 'mio', 'milestone': 'M6 + M7 Welcome to Graphics',
                'status': '自动化通过，待用户界面验收', 'screenshots': counts,
                'runtime_compiler': 'native G2', 'files': {}}
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for item in files:
            name = item.relative_to(ROOT).as_posix()
            blob = item.read_bytes()
            manifest['files'][name] = {'size': len(blob), 'sha256': digest(blob)}
            archive.writestr(name, blob)
        archive.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
    with zipfile.ZipFile(OUTPUT) as archive:
        assert archive.testzip() is None, 'ZIP CRC failed'
        for name, record in manifest['files'].items():
            blob = archive.read(name)
            assert len(blob) == record['size'] and digest(blob) == record['sha256'], name
    checksum = digest(OUTPUT.read_bytes())
    OUTPUT.with_suffix('.sha256').write_text(checksum + '  ' + OUTPUT.name + '\n', encoding='ascii')
    print(f'PASS: {len(files)} files, {sum(counts.values())} screenshots, {OUTPUT.stat().st_size} bytes')
    print(OUTPUT)
    print('SHA256:', checksum)


if __name__ == '__main__':
    main()
