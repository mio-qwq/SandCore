#!/usr/bin/env python3
"""从固定M8a包复用已发布主题资源，只补M9树，不重启M8生成或渲染。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from published_baseline import PublishedBaseline,VERSIONS,ROOT
M8A_SHA=VERSIONS['M8a']['original_sha']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree',type=Path,required=True)
    args=parser.parse_args()
    tree=args.tree.resolve()
    build=(ROOT/'build').resolve()
    if not tree.is_relative_to(build) or tree==build/'fs' or tree.name!='fs':
        raise ValueError('只允许build/<M9独立目录>/fs')
    copied=[]
    with PublishedBaseline('M8a') as archive:
        for name in sorted(archive.namelist()):
            prefix='sandcore/build/fs/'
            if not name.startswith(prefix) or name.endswith('/'):
                continue
            relative=name[len(prefix):]
            key=relative.upper()
            if not key.startswith(('SYS/THEMES/','SYS/ICONS/','SYS/WALLPAPERS/')):
                continue
            if any(part in ('','.','..') for part in relative.split('/')):
                raise ValueError('归档路径非法')
            target=tree/key
            # M9已有的新资源由自身规则负责。补完整旧主题资源，不把
            # 这个只读安装步骤变成覆盖Sound新资源或重绘旧图标的入口。
            if target.exists():
                continue
            payload=archive.read(name)
            target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as output:
                output.write(payload)
            copied.append(dict(path=key,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
    result=dict(scope='M8A_EXISTING_VISUAL_BYTES_REUSE_ONLY',baseline_sha256=M8A_SHA,files=copied)
    (tree.parent/'m9-visual-baseline.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f'M9默认主题资源：{len(copied)}份M8a现有文件复用，未运行生成/渲染')


if __name__=='__main__':main()
