#!/usr/bin/env python3
"""从仓库固定M9精简包读出只读基线；不依赖当前开发盘偶然存在。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from mkfs_m9 import read_image

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_SHA = '3e90bda185730b9d7b3039e9cb8cb3219e818483e37ceeef611d6723c9c7bd41'
IMAGE_SHA = 'de6118aac18a01267fe72970aee790e5989d50528f50999dcfa5ad74e1ec1c16'
MEMBER = 'M9-ACCEPTANCE/images/sanddata.img'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    target = args.out.resolve()
    if not target.is_relative_to(ROOT / 'build/baselines'):
        raise ValueError('基线只写入独立build/baselines')
    archive = ROOT.parent / 'releases/SandCore-M9-build.zip'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        raise ValueError('仓库M9包固定摘要不符')
    with zipfile.ZipFile(archive) as source:
        manifest = json.loads(source.read('manifest.json'))
        record = next(item for item in manifest['files'] if item['path'] == MEMBER)
        if record['sha256'] != IMAGE_SHA or record['size'] != 64 * 1024 * 1024:
            raise ValueError('固定M9盘登记不符')
        data = source.read(MEMBER)
    if hashlib.sha256(data).hexdigest() != IMAGE_SHA:
        raise ValueError('M9成员摘要不符')
    read_image(data)
    if target.exists():
        if hashlib.sha256(target.read_bytes()).hexdigest() != IMAGE_SHA:
            raise ValueError('既有只读基线被修改，不覆盖')
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream:
        stream.write(data)


if __name__ == '__main__':
    main()
