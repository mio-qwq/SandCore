#!/usr/bin/env python3
"""只向M9构建树安装固定M7原版SCX，不改冻结的M8树或legacy档案。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from published_baseline import PublishedBaseline, VERSIONS, ROOT
BASELINE_SHA=VERSIONS['M7']['original_sha']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree',type=Path,required=True)
    args=parser.parse_args()
    tree=args.tree.resolve()
    build=(ROOT/'build').resolve()
    # 原M8发布树不能因补兼容资源而被写入；M9须使用独立BUILD。
    if not tree.is_relative_to(build) or tree==build/'fs' or tree.name!='fs':
        raise ValueError('只允许build/<M9独立目录>/fs')
    installed=[]
    with PublishedBaseline('M7') as archive:
        for name in sorted(archive.namelist()):
            if not name.startswith(('sandcore/build/fs/apps/','sandcore/build/fs/bin/')) or not name.lower().endswith('.scx'):
                continue
            short=name[len('sandcore/build/fs/'):].upper()
            if any(part in ('','.','..') for part in short.split('/')):
                raise ValueError('归档路径非法')
            payload=archive.read(name)
            target=tree/'LEGACY'/short
            if target.exists() and target.read_bytes()!=payload:
                raise ValueError('M9树中LEGACY被改写：'+short)
            target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists():
                with target.open('xb') as output:
                    output.write(payload)
            installed.append(dict(path='LEGACY/'+short,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
    if not installed:
        raise ValueError('原包没有SCX')
    result=dict(scope='IMMUTABLE_M7_SCX_INSTALL_ONLY_RUNTIME_PENDING',baseline_sha256=BASELINE_SHA,files=installed)
    (tree.parent/'m9-legacy-install.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f'M9 LEGACY：{len(installed)}份固定M7原版SCX；冻结树未写入')


if __name__=='__main__':main()
