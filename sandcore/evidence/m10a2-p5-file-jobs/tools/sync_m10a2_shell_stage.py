#!/usr/bin/env python3
"""主源整合后只同步明确变更的代码到既有隔离构建，不复制盘或覆盖未知差异。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--baseline-report',type=Path,required=True)
    parser.add_argument('--previous-sync',type=Path,action='append',default=[])
    parser.add_argument('--tag',required=True)
    args=parser.parse_args();stage=args.stage.resolve()
    if not stage.is_relative_to(ROOT/'build') or not args.tag.replace('-','').isalnum():
        parser.error('需要build内候选与安全tag')
    baseline=json.loads(args.baseline_report.read_text(encoding='utf-8'))['source_snapshot']
    for previous_path in args.previous_sync:
        previous=json.loads(previous_path.read_text(encoding='utf-8'))
        if previous.get('baseline_report_sha256')!=sha(args.baseline_report):
            raise ValueError('前次同步来源不同')
        for name,item in previous['changes'].items():
            if baseline.get(name)!=item['before']:raise ValueError('同步链前值不同：'+name)
            baseline[name]=item['after']
    output=stage/'reports'/('sync-'+args.tag+'.json')
    if output.exists():raise FileExistsError(output)
    tracked=subprocess.run(['git','diff','--name-only','HEAD'],cwd=ROOT.parent,capture_output=True,text=True,check=True).stdout.splitlines()
    new=subprocess.run(['git','ls-files','--others','--exclude-standard'],cwd=ROOT.parent,capture_output=True,text=True,check=True).stdout.splitlines()
    paths=[]
    for name in sorted(set(tracked+new)):
        if not name.startswith('sandcore/'):continue
        name=name[9:]
        if name.startswith(('kernel/','user/')) or name in ('Makefile','m10a2.mk'):
            paths.append(name)
    changes={}
    for name in paths:
        source=ROOT/name;target=stage/name
        if not source.is_file():raise ValueError('不同步删除或缺失源：'+name)
        before=sha(target) if target.is_file() else None
        after=sha(source)
        if before==after:continue
        if before!=baseline.get(name):raise ValueError('候选含未知变化：'+name)
        changes[name]=dict(before=before,after=after)
    for name,item in changes.items():
        target=stage/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/name).read_bytes())
        if sha(target)!=item['after']:raise ValueError('同步后字节不同：'+name)
    output.write_text(json.dumps(dict(scope='MAIN_SOURCE_INCREMENTAL_SYNC_NOT_BUILD_OR_GUEST_PASS',
        baseline_report_sha256=sha(args.baseline_report),changes=changes,
        previous_sync_sha256=[sha(path) for path in args.previous_sync],no_disks_or_history_copied=True),indent=2)+'\n',encoding='utf-8')
    print('SOURCE_SYNC',len(changes),'paths',output)


if __name__=='__main__':main()
