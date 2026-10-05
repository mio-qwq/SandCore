#!/usr/bin/env python3
"""校验并恢复SandCore去重备份；只使用标准库，不覆盖已有文件。"""
import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path,PurePosixPath
import shutil
import time
import zipfile
import zlib

def safe(name):
    p=PurePosixPath(name)
    if p.is_absolute() or not p.parts or any(x in ('','..') or ':' in x or '\\' in x for x in p.parts):
        raise ValueError('备份路径不安全: '+name)
    return p

def file_sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(1024*1024):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--verify-only',action='store_true')
    p.add_argument('--path',action='append',default=[],help='只恢复指定普通文件；可重复指定');a=p.parse_args()
    if not a.verify_only:
        if not a.out:p.error('恢复需要--out')
        if a.out.exists() and any(a.out.iterdir()):raise ValueError('目标目录必须为空或不存在')
        a.out.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(a.archive) as archive:
        manifest=json.loads(archive.read('manifest.json'));seen=set();last=time.monotonic();processed=0;count=0
        if manifest['format']!='SCBK1':raise ValueError('未知格式')
        # 同一64MiB镜像常在数百个测试目录出现；有限缓存减少重复解压，不省略整文件摘要。
        @lru_cache(maxsize=128)
        def read_block(digest):
            block=zlib.decompress(archive.read('blocks/'+digest+'.z'))
            if len(block)>1048576 or hashlib.sha256(block).hexdigest()!=digest:raise ValueError('块摘要不一致')
            return block
        def content(record,target=None):
            nonlocal last,processed,count
            h=hashlib.sha256();size=0;stream=None
            if target:
                target.parent.mkdir(parents=True,exist_ok=True);stream=target.open('xb')
            try:
                for digest in record['chunks']:
                    block=read_block(digest)
                    h.update(block);size+=len(block);seen.add(digest)
                    if stream:stream.write(block)
            finally:
                if stream:stream.close()
            if size!=record['size'] or h.hexdigest()!=record['sha256']:raise ValueError('文件内容不一致: '+record['path'])
            processed+=size;count+=1
            if time.monotonic()-last>=15:
                print(json.dumps(dict(files_verified=count,logical_mib=round(processed/1048576,1))),flush=True);last=time.monotonic()
        selected=set(a.path)
        known={record['path'] for record in manifest['files']}
        if selected-known:raise ValueError('指定文件不在普通文件清单: '+str(sorted(selected-known)))
        for record in manifest['files']:
            if selected and record['path'] not in selected:continue
            rel=safe(record['path']);content(record,None if a.verify_only else a.out/rel)
        for item in ([] if selected else manifest['archives']):
            origin=safe(item['path'])
            for record in item['members']:
                rel=safe(record['path'])
                content(record,None if a.verify_only else a.out/'ARCHIVE-CONTENTS'/origin/rel)
        for item in ([] if selected else manifest['external_assets']):
            source=a.assets/item['asset'];rel=safe(item['path'])
            if not source.is_file() or source.stat().st_size!=item['size'] or file_sha(source)!=item['sha256']:
                raise ValueError('外部附件缺失/摘要不一致: '+item['asset'])
            if not a.verify_only:
                target=a.out/rel;target.parent.mkdir(parents=True,exist_ok=True)
                if target.exists():raise ValueError('目标文件已存在')
                shutil.copy2(source,target)
        if not selected and seen!=set(manifest['blocks']):raise ValueError('块索引不完整')
        if not a.verify_only:
            (a.out/'BACKUP-RESTORE-MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(('VERIFIED' if a.verify_only else 'RESTORED')+': '+str(count)+' files; '+str(len(seen))+' verified chunks',flush=True)

if __name__=='__main__':main()
