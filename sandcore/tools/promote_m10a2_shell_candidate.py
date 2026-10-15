#!/usr/bin/env python3
"""把已审阅P5候选源码整合到main工作区；不覆盖未知改动或声明整版完成。"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SCAPI='81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','generation-report','runtime-report','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();stage=args.stage.resolve();out=args.out.resolve()
    if not stage.is_relative_to(ROOT/'build') or not out.is_relative_to(ROOT/'build') or out.exists():
        parser.error('需要隔离候选及全新build内凭证')
    review=json.loads((stage/'integration-review.json').read_text(encoding='utf-8'))
    generated=json.loads(args.generation_report.read_text(encoding='utf-8'))
    runtime=json.loads(args.runtime_report.read_text(encoding='utf-8'))
    if (generated.get('status')!='P5_NATIVE_GENERATION_PASS'
        or runtime.get('status')!='P5_FIRST_NATIVE_HOST_CLIENT_PASS'
        or runtime.get('current_inputs_unchanged') is not True or runtime.get('source_disk_unchanged') is not True
        or runtime.get('generation_report_sha256')!=sha(args.generation_report)):
        raise ValueError('需要真实匹配的原生生成/host-client运行与保持证据')
    for name,value in generated['source_snapshot'].items():
        if sha(stage/name)!=value:raise ValueError('候选在验证之后改变：'+name)
    allowed={}
    rebase=stage/'reports/rebase-context-phases-01.json'
    if rebase.is_file():
        allowed['user/scui/ui_dispatch.c']=json.loads(rebase.read_text(encoding='utf-8'))['source_sha256']
    if sha(ROOT/'user/SCAPI.H')!=SCAPI:raise ValueError('SCAPI原字节不符')
    changes={}
    for name in review['candidate_sha256']:
        source=stage/name;target=(ROOT/name).resolve()
        if not target.is_relative_to(ROOT):raise ValueError('源码路径越界')
        before=sha(target) if target.is_file() else None
        after=sha(source)
        if before==after:continue
        baseline=review['baseline_sha256'].get(name)
        if before is not None and before not in (baseline,allowed.get(name)):
            raise ValueError('拒绝覆盖未知工作区改动：'+name)
        changes[name]=dict(before=before,after=after)
    for name,item in changes.items():
        target=ROOT/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes((stage/name).read_bytes())
        if sha(target)!=item['after']:raise ValueError('整合后源码摘要不同：'+name)
    if sha(ROOT/'user/SCAPI.H')!=SCAPI:raise ValueError('SCAPI守恒失败')
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(dict(status='SOURCE_PROMOTED_RUNTIME_SLICE_PASS_NOT_P5_COMPLETE',
        full_M10a2=False,full_P5=False,stage=str(stage),changes=changes,
        generation_report_sha256=sha(args.generation_report),runtime_report_sha256=sha(args.runtime_report),
        scapi_sha256=SCAPI,remaining='File jobs/clipboard/drop/layout persistence/window/session/SDK/component acceptance remains.'),
        ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('P5_SOURCE_PROMOTED',len(changes),'paths; full_P5=False; full_M10a2=False')


if __name__=='__main__':main()
