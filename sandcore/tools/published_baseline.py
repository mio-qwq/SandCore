#!/usr/bin/env python3
"""从仓库固定精简镜像/历史源码读取发布字节；兼容本机仍保留的原ZIP。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from mkfs_m9 import read_image

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parent
VERSIONS={
 'M7':dict(original='SandCore-M7-2026-10-02.zip',original_sha='516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc',
           compact_sha='032bfaf5bb51ff45e07ebbebbf73f15c021473c808a604b88dd85d6046172de0'),
 'M8a':dict(original='SandCore-M8a-2026-10-04.zip',original_sha='e9db18c1dd3c6987c4b66a2a33e1bf354e39b6da8b2b920db2cb366f42ca61e5',
            compact_sha='107490f4c2493b5e3813921d8f575113071e05af84d320eea300032911bb7bd5')}

def sha_file(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        while block:=source.read(1048576):h.update(block)
    return h.hexdigest()

class PublishedBaseline:
    def __init__(self,version):
        self.version=version;self.info=VERSIONS[version];self.source_sha=self.info['original_sha']
        self.path=ROOT/'build'/self.info['original']
        self.original=self.path.is_file()
        if not self.original:self.path=REPO/'releases'/('SandCore-'+version+'-build.zip')
        self.storage_sha=self.source_sha if self.original else self.info['compact_sha']
        if sha_file(self.path)!=self.storage_sha:raise ValueError('固定发布来源摘要不一致: '+str(self.path))
        self.archive=zipfile.ZipFile(self.path);self.records=None
        if not self.original:
            manifest=json.loads(self.archive.read('manifest.json'))
            if manifest['source_archive_sha256']!=self.source_sha:raise ValueError('精简包原来源不一致')
            self.files={r['path']:r for r in manifest['files']}
            self.records=read_image(self._compact_read('build/sanddata.img'))
            index=json.loads((REPO/'releases/MANIFEST.json').read_text(encoding='utf-8'))
            self.sources={r['path']:r for r in index['historical_sources'][version]}

    def _compact_read(self,name):
        body=self.archive.read(name);item=self.files[name]
        if len(body)!=item['size'] or hashlib.sha256(body).hexdigest()!=item['sha256']:
            raise ValueError('精简成员摘要不一致: '+name)
        return body

    def namelist(self):
        if self.original:return self.archive.namelist()
        return ['sandcore/build/fs/'+r.name for r in self.records.values() if r.payload is not None]+list(self.sources)

    def read(self,name):
        if self.original:return self.archive.read(name)
        prefix='sandcore/build/fs/'
        if name.startswith(prefix):
            payload=self.records[name[len(prefix):].upper()].payload
            if payload is None:raise ValueError('不能读取目录')
            return payload
        if name=='sandcore/build/sanddata.img':return self._compact_read('build/sanddata.img')
        item=self.sources[name];body=(REPO/'snapshots'/self.version/name).read_bytes()
        if len(body)!=item['size'] or hashlib.sha256(body).hexdigest()!=item['sha256']:
            raise ValueError('历史源码摘要不一致: '+name)
        return body

    def __enter__(self):return self

    def __exit__(self,*args):
        self.archive.close()
        if sha_file(self.path)!=self.storage_sha:raise RuntimeError('发布来源在读取期间变化')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--version',choices=VERSIONS,required=True)
    parser.add_argument('--image-out',required=True,type=Path);args=parser.parse_args();target=args.image_out.resolve()
    if not target.is_relative_to(ROOT/'build/baselines'):raise ValueError('基线仅写入build/baselines独立目录')
    with PublishedBaseline(args.version) as archive:body=archive.read('sandcore/build/sanddata.img')
    if target.exists():
        if target.read_bytes()!=body:raise ValueError('已有基线被修改，拒绝覆盖')
    else:
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write(body)
    print(args.version+' baseline '+str(target)+' SHA256 '+hashlib.sha256(body).hexdigest())

if __name__=='__main__':main()
