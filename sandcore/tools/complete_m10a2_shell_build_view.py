#!/usr/bin/env python3
"""Add unchanged source dependencies to an already reviewed P5 candidate.

Copies source files, not disks, old build trees, private keys, or game/movie
outputs. Existing staged edits always win. The current P4 sources are read only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--stage',required=True,type=Path);a.add_argument('--build-report',required=True,type=Path)
    a.add_argument('--extend-tag',help='为已有候选补缺失依赖，另存不可覆盖的来源记录')
    args=a.parse_args()
    stage=args.stage.resolve();report=args.build_report.resolve()
    if not stage.is_relative_to((ROOT/'build').resolve()) or not report.is_relative_to((ROOT/'build').resolve()):a.error('paths must stay under build/')
    if not (stage/'finalization-review.json').is_file():a.error('requires a finalized build view')
    if args.extend_tag:
        if not (stage/'build-view.json').is_file() or not args.extend_tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.extend_tag):
            a.error('requires an existing build view and a lowercase extension tag')
        destination_report=stage/'build-view-extensions'/(args.extend_tag+'.json')
    else:
        destination_report=stage/'build-view.json'
    if destination_report.exists():a.error('refuse to overwrite an existing build-view record')
    build=json.loads(report.read_text(encoding='utf-8'))
    if build['exit_code']!=0 or build['source_files_modified_during_build']!=[]:raise ValueError('unaccepted baseline build')
    files=set()
    # 增量UI构建报告只列本次读到的依赖，不能充当冷CORE的完整源码表。
    # lwIP等Make生成规则还要读未触发的固定上游源；复制完整第三方源码
    # 闭包，仍不复制磁盘、历史build或游戏/影片输出。
    for folder in ['kernel','user','boot','tools','third_party']:
        for p in (ROOT/folder).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ['.pyc','.pyo']:
                files.add(p.relative_to(ROOT))
    for item in build['inputs']:files.add(Path(item['path']))
    files.update(p.relative_to(ROOT) for p in ROOT.glob('*.ld'))
    files.update(p.relative_to(ROOT) for p in ROOT.glob('*.mk'))
    files.add(Path('assets/trust/M10-OWNER-ED25519.PUB'))
    # UI增量build不包含冷启动字形依赖；候选CORE必须从原始字形源构建。
    files.add(Path('assets/font/phoenix-ascii.json'))
    added={}
    for relative in sorted(files):
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('invalid source path')
        destination=stage/relative
        if destination.exists():continue
        source=ROOT/relative
        if source.suffix.lower() in ('.key','.pem','.pfx','.p12') or 'PRIVATE' in source.name.upper():
            # Private C implementation headers are source, not signing material.
            if source.suffix not in ('.H','.h','.c','.C','.inc'):raise ValueError('refuse private material: '+str(relative))
        body=source.read_bytes();destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(body);shutil.copystat(source,destination)
        added[relative.as_posix()]=dict(bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    record=dict(status='SOURCE_BUILD_VIEW_NOT_COMPILED',baseline_build_sha256=sha(report),added=added,
                copies_disks=False,copies_game_outputs=False,active_sources_modified=False)
    record['baseline_build_report']=str(report)
    record['extension_tag']=args.extend_tag
    destination_report.parent.mkdir(parents=True,exist_ok=True)
    destination_report.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(files=len(added),bytes=sum(x['bytes'] for x in added.values()),status=record['status'])))
if __name__=='__main__':main()
