#!/usr/bin/env python3
"""只读快照与内容块去重；保留失败/历史/用户会话，不删除原文件。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'backup-output'
EXTERNAL={
 'sandcore/build/SandCore-M7-2026-10-02.zip':'SandCore-M7-2026-10-02.zip',
 'sandcore/build/SandCore-M8a-2026-10-04.zip':'SandCore-M8a-2026-10-04.zip',
 'sandcore/build/SandCore-M9-acceptance-2026-10-05.zip':'SandCore-M9-acceptance-2026-10-05.zip'}

def main():
    OUT.mkdir(exist_ok=True);target=OUT/'SandCore-history-2026-10-05.scbk.zip'
    manifest=dict(format='SCBK1',chunk_bytes=1048576,files=[],archives=[],external_assets=[],blocks={},excluded=[])
    seen=manifest['blocks'];processed=0;compressed=0;last=time.monotonic();start=last
    paths=[]
    for parent,dirs,files in os.walk(ROOT):
        for name in list(dirs):
            rel=(Path(parent)/name).relative_to(ROOT).as_posix()
            if name in ('.git','__pycache__') or rel=='backup-output' or rel=='sandcore/build/host-tools':
                manifest['excluded'].append(dict(path=rel,reason='Git/cache/host download or this backup output'));dirs.remove(name)
        for name in files:
            path=Path(parent)/name
            if path.relative_to(ROOT).as_posix()=='sandcore/build/github-backup-create.log':
                manifest['excluded'].append(dict(path=path.relative_to(ROOT).as_posix(),reason='this backup progress log'))
            else:paths.append(path)
    with zipfile.ZipFile(target,'x',compression=zipfile.ZIP_STORED,allowZip64=True) as archive:
        def consume(stream,name,size):
            nonlocal processed,compressed,last
            record=dict(path=name,size=size,sha256='',chunks=[]);h=hashlib.sha256();actual=0
            while block:=stream.read(1048576):
                digest=hashlib.sha256(block).hexdigest();h.update(block);actual+=len(block);processed+=len(block)
                record['chunks'].append(digest)
                if digest not in seen:
                    # 历史镜像高度重复，块去重负责主要节省；低压缩级别缩短只读快照时间。
                    payload=zlib.compress(block,1);archive.writestr('blocks/'+digest+'.z',payload)
                    seen[digest]=dict(raw=len(block),compressed=len(payload));compressed+=len(payload)
                if time.monotonic()-last>=15:
                    print(json.dumps(dict(processed_mib=round(processed/1048576,1),unique_blocks=len(seen),stored_mib=round(compressed/1048576,1),elapsed_seconds=round(time.monotonic()-start,1))),flush=True);last=time.monotonic()
            if actual!=size:raise ValueError('读取长度变化: '+name)
            record['sha256']=h.hexdigest();return record
        for path in sorted(paths):
            rel=path.relative_to(ROOT).as_posix();stat=path.stat()
            if path.is_symlink():
                manifest['excluded'].append(dict(path=rel,reason='symbolic link; original retained'));continue
            if rel in EXTERNAL:
                h=hashlib.sha256()
                with path.open('rb') as f:
                    while block:=f.read(1048576):h.update(block)
                manifest['external_assets'].append(dict(path=rel,asset=EXTERNAL[rel],size=stat.st_size,sha256=h.hexdigest()));continue
            if path.suffix.lower()=='.zip':
                h=hashlib.sha256()
                with path.open('rb') as f:
                    while block:=f.read(1048576):h.update(block)
                item=dict(path=rel,size=stat.st_size,original_zip_sha256=h.hexdigest(),members=[])
                with zipfile.ZipFile(path) as old:
                    item['comment_hex']=old.comment.hex()
                    for info in old.infolist():
                        if info.is_dir():continue
                        with old.open(info) as f:record=consume(f,info.filename,info.file_size)
                        record.update(date_time=info.date_time,external_attr=info.external_attr,compress_type=info.compress_type)
                        item['members'].append(record)
                manifest['archives'].append(item)
            else:
                success=False
                for attempt in range(3):
                    stat=path.stat()
                    try:
                        with path.open('rb') as f:record=consume(f,rel,stat.st_size)
                    except ValueError as exc:
                        if str(exc).startswith('读取长度变化: '):continue
                        raise
                    after=path.stat()
                    if stat.st_size==after.st_size and stat.st_mtime_ns==after.st_mtime_ns:
                        record['mtime_ns']=after.st_mtime_ns;manifest['files'].append(record);success=True;break
                if not success:manifest['excluded'].append(dict(path=rel,reason='live file changed repeatedly during snapshot'))
        # 重试中暂存的块若未被稳定文件引用，不作为可恢复备份的一部分。
        used={d for r in manifest['files'] for d in r['chunks']}
        used.update(d for a in manifest['archives'] for r in a['members'] for d in r['chunks'])
        manifest['blocks']={k:v for k,v in seen.items() if k in used}
        manifest['summary']=dict(ordinary_files=len(manifest['files']),expanded_archives=len(manifest['archives']),
            zip_members=sum(len(a['members']) for a in manifest['archives']),logical_bytes=sum(r['size'] for r in manifest['files'])+sum(r['size'] for a in manifest['archives'] for r in a['members']),
            unique_blocks=len(used),stored_blocks_bytes=sum(v['compressed'] for v in manifest['blocks'].values()),elapsed_seconds=time.monotonic()-start)
        payload=json.dumps(manifest,ensure_ascii=False,separators=(',',':')).encode();archive.writestr('manifest.json',payload)
    (OUT/'history-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(target) as z:
        if z.testzip():raise ValueError('备份ZIP CRC失败')
    print(json.dumps(dict(status='CREATED_CRC_PASS',bytes=target.stat().st_size,summary=manifest['summary']),ensure_ascii=False),flush=True)

if __name__=='__main__':main()
