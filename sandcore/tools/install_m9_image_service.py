#!/usr/bin/env python3
"""M9复用固定M8a已发布图片服务原字节；不启动冻结组件重编/渲染。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from published_baseline import PublishedBaseline,VERSIONS,ROOT
M8A_SHA=VERSIONS['M8a']['original_sha']

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tree',type=Path,required=True)
    args=parser.parse_args();tree=args.tree.resolve();build=(ROOT/'build').resolve()
    if not tree.is_relative_to(build) or tree==build/'fs' or tree.name!='fs':raise ValueError('只允许独立M9文件树')
    files=[]
    with PublishedBaseline('M8a') as archive:
        for name in ('IMAGE.SCX','IMAGE.LIC'):
            body=archive.read('sandcore/build/fs/sys/CORE/'+name);target=tree/'SYS/CORE'/name
            target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists() and target.read_bytes()!=body:raise ValueError('现有图片服务不同于固定发布字节，拒绝覆盖')
            if not target.exists():target.write_bytes(body)
            files.append(dict(path='SYS/CORE/'+name,bytes=len(body),sha256=hashlib.sha256(body).hexdigest()))
    (tree.parent/'m9-image-service.json').write_text(json.dumps(dict(baseline_sha256=M8A_SHA,files=files),indent=2)+'\n')
    print('M9图片服务：2份固定M8a原字节复用，未重编/渲染')

if __name__=='__main__':main()
