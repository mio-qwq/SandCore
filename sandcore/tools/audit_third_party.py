#!/usr/bin/env python3
"""第三方固定快照与客体源码归档的发布前检查；第一阶段不执行。

许可选择来自人工登记，摘要检查不冒充法律解释。这里防止许可证遗漏、
静默换源、嵌入组件漏登记及源码归档被截断；不编译、不启动系统或联网。
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'third_party/M9-SOURCES.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative_files(directory):
    result = {}
    for path in directory.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'固定快照不得含符号链接: {path}')
        if path.is_file():
            result[path.relative_to(directory).as_posix()] = path
    return result


def component_files(component):
    if 'hash_catalog' in component:
        catalog = json.loads((CATALOG.parent / component['hash_catalog']).read_text(encoding='utf-8'))
        prefix = component['catalog_prefix']
        hashes = {name[len(prefix):]: value for name, value in catalog['files'].items() if name.startswith(prefix)}
        key = 'stb' if prefix == 'stb/' else 'libwebp'
        if catalog[key]['revision'] != component['revision'] or catalog[key]['url'] != component['upstream']:
            raise ValueError(f'{component["name"]}: 来源版本与原快照登记不符')
    else:
        hashes = component['files']
    if not hashes:
        raise ValueError(f'{component["name"]}: 缺固定文件摘要')
    return hashes


def audit(tree=None):
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    if catalog.get('version') != 1:
        raise ValueError('不支持的来源登记版本')
    allowed = set(catalog['allowed_licenses'])
    count = 0
    for component in catalog['components']:
        if component['license'] not in allowed:
            raise ValueError(f'{component["name"]}: 未登记允许的宽松许可')
        for embedded in component.get('embedded', []):
            if embedded['license'] not in allowed or not embedded.get('upstream') or not embedded.get('revision'):
                raise ValueError(f'{component["name"]}: 内含组件许可/来源不完整')
        source = ROOT / component['source']
        hashes = component_files(component)
        expected = set(hashes) | set(component.get('local_files', []))
        actual = relative_files(source)
        if set(actual) != expected:
            raise ValueError(f'{component["name"]}: 来源文件集合改变; 缺{sorted(expected-set(actual))}, 多{sorted(set(actual)-expected)}')
        for name, sha in hashes.items():
            if digest(actual[name]) != sha:
                raise ValueError(f'{component["name"]}/{name}: 固定摘要改变')
        # 文件名是SandFS整个根相对路径，不只检查末级basename的长度。
        for name in expected:
            guest = component['guest'] + '/' + name
            if len(guest.encode('utf-8')) > 63:
                raise ValueError(f'客体归档路径超过63B: {guest}')
        if tree:
            archived = relative_files(tree / component['guest'])
            if set(archived) != expected:
                raise ValueError(f'{component["name"]}: 客体源码归档不完整或有陈旧文件')
            for name in expected:
                if digest(archived[name]) != digest(actual[name]):
                    raise ValueError(f'{component["name"]}/{name}: 客体副本不同于固定来源')
        count += len(expected)
    if tree:
        notices = {
            'THIRDPARTY.MD': [ROOT / 'docs/THIRD-PARTY.md'],
            'IMAGE.JSON': [ROOT / 'third_party/SOURCES.json'],
            'M9.JSON': [CATALOG],
            'STB.LIC': [ROOT / 'third_party/stb/LICENSE'],
            'WEBP.LIC': [ROOT / 'third_party/libwebp/COPYING'],
            'WEBP.AUTHORS': [ROOT / 'third_party/libwebp/AUTHORS'],
            'WEBP.PATENTS': [ROOT / 'third_party/libwebp/PATENTS'],
            'DR_MP3.LIC': [ROOT / 'user/audio/vendor/LICENSE', ROOT / 'user/audio/vendor/DR-MP3-NOTICE', ROOT / 'user/audio/vendor/MINIMP3-NOTICE'],
            'DR_MP3.MD': [ROOT / 'user/audio/vendor/PROVENANCE.md'],
            'DR_FLAC.LIC': [ROOT / 'user/audio/flacvendor/LICENSE', ROOT / 'user/audio/flacvendor/DR-FLAC-NOTICE'],
            'DR_FLAC.MD': [ROOT / 'user/audio/flacvendor/PROVENANCE.md'],
            'MINIZ.LIC': [ROOT / 'user/compress/vendor/LICENSE'],
            'MINIZ.MD': [ROOT / 'user/compress/vendor/PROVENANCE.md'],
            'VONWAON.LIC': [ROOT / 'assets/licenses/VONWAON.LIC']
        }
        for name, originals in notices.items():
            path = tree / 'SYS/LICENSE' / name
            if not path.is_file() or path.read_bytes() != b''.join(p.read_bytes() for p in originals):
                raise ValueError(f'声明文件缺失或被改写: {path}')
    return dict(status='SOURCE-ARCHIVE-CHECKED', source_files=count,
                product_tests_run=False, catalog_sha256=digest(CATALOG))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, help='已生成的M9文件树；额外核对完整源码/声明副本')
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.tree.resolve() if args.tree else None), ensure_ascii=False))
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f'第三方归档检查失败: {error}') from error


if __name__ == '__main__':
    main()
