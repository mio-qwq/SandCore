#!/usr/bin/env python3
"""按内容保存被build/试玩忽略的代码和文档；原路径写索引，排除镜像/视频/渲染帧。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[2]
DEST=ROOT/'history'
SOURCE_EXT={'.c','.h','.asm','.inc','.py','.bat','.sh','.ps1','.cpp','.cc','.s','.cl','.glsl','.vert','.frag','.mk','.md','.rst','.svg','.blend'}
NAMES={'makefile','cmakelists.txt','license','copying','authors','patents','license.txt','readme.txt'}

def relevant(name):
    p=Path(name)
    return p.suffix.lower() in SOURCE_EXT or p.name.lower() in NAMES or (
        p.suffix.lower() in ('.txt','.json','.toml','.ini','.yaml','.yml') and ('/docs/' in '/'+name or '/film/' in '/'+name))

def sha(body):return hashlib.sha256(body).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--git',default='git');a=p.parse_args()
    tracked=subprocess.check_output([a.git,'ls-files','-z'],cwd=ROOT).decode('utf-8').rstrip('\0').split('\0')
    existing={};objects={};origins=[]
    for name in tracked:
        if name.startswith('history/') or not (ROOT/name).is_file():continue
        body=(ROOT/name).read_bytes();existing.setdefault(sha(body),name)
    def preserve(name,body):
        digest=sha(body)
        if digest in existing:target=existing[digest]
        else:
            suffix=Path(name.rsplit('::',1)[-1]).suffix.lower() or '.txt'
            target='history/objects/'+digest+suffix
            path=ROOT/target;path.parent.mkdir(parents=True,exist_ok=True)
            if path.exists():
                if path.read_bytes()!=body:raise ValueError('已有历史内容不同，拒绝覆盖')
            else:path.write_bytes(body)
            objects.setdefault(digest,dict(path=target,size=len(body),sha256=digest))
            existing[digest]=target
        origins.append(dict(original=name,stored=target,size=len(body),sha256=digest))
    archives=[]
    for parent,dirs,files in os.walk(ROOT):
        dirs[:]=[d for d in dirs if d not in ('.git','__pycache__','backup-output','history','rebuild-output') and not (Path(parent)/d).relative_to(ROOT).as_posix()=='sandcore/build/host-tools']
        for name in files:
            path=Path(parent)/name;relative=path.relative_to(ROOT).as_posix()
            if relative.startswith(('sandcore/build/','temp miotest/')) and relevant(relative):preserve(relative,path.read_bytes())
            if path.suffix.lower()=='.zip' and not relative.startswith('releases/') and relative!='vonwaon-bitmap.ttf.zip':archives.append(path)
    for path in sorted(archives):
        with zipfile.ZipFile(path) as archive:
            for info in archive.infolist():
                if not info.is_dir() and relevant(info.filename):
                    preserve(path.relative_to(ROOT).as_posix()+'::'+info.filename,archive.read(info))
    DEST.mkdir(exist_ok=True)
    result=dict(format='SandCoreHistoricalSources1',scope='historical source/doc/project only; no images/video/rendered frames; not new product code',
                unique_extra_objects=len(objects),extra_bytes=sum(r['size'] for r in objects.values()),
                objects=sorted(objects.values(),key=lambda r:r['path']),origins=sorted(origins,key=lambda r:r['original']))
    (DEST/'INDEX.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(extra_objects=result['unique_extra_objects'],extra_bytes=result['extra_bytes'],indexed_originals=len(origins)),ensure_ascii=False))

if __name__=='__main__':main()
