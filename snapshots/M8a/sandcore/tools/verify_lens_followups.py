#!/usr/bin/env python3
"""mio：Lens十九组合完成后，串行执行内存/输入及真实应用联动。

等待阶段只读报告，不打开QEMU端口；矩阵失败立即停止。后续每套
各自启停自己的Windows双盘QEMU，避免并发互发输入。成功输入
必须仍是同一核/源/盘，日志和报告单独保存，不能覆盖已成功阶段。
"""
import hashlib,json,subprocess,sys,time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-lens'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    output=STAGE/'followups.json';assert not output.exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs}
    verifier=sha(Path(__file__));deadline=time.monotonic()+3600
    print('等待Lens十九组合完整PASS，等待时未连接QEMU',flush=True)
    while not (STAGE/'matrix/results.json').exists():
        assert not list((STAGE/'matrix').rglob('failure-state.json')),'矩阵失败，后续未启动'
        assert time.monotonic()<deadline,'矩阵等待超时，后续未启动'
        time.sleep(1)
    matrix=json.loads((STAGE/'matrix/results.json').read_text(encoding='utf-8'));assert matrix['status']=='PASS' and len(matrix['cases'])==19
    assert inputs=={name:sha(ROOT/name) for name in inputs}
    steps=(
        ('memory',[sys.executable,'tools/verify_lens_memory.py']),
        ('action-queue',[sys.executable,'tools/verify_lens_action_queue.py']),
        ('canvas-regression',[sys.executable,'-c',"import sys;from pathlib import Path;sys.path.insert(0,'tools');import verify_canvas as c;c.OUT=Path.cwd()/'build/m8-lens/canvas-regression';c.main()"]),
        ('files-regression',[sys.executable,'-c',"import sys;from pathlib import Path;sys.path.insert(0,'tools');import verify_files as f;f.OUT=Path.cwd()/'build/m8-lens/files-regression';f.main()"]),
    )
    for name,command in steps:
        assert not (STAGE/name/'results.json').exists(),name+'成功阶段不可覆盖'
        print('开始Lens联动 '+name,flush=True)
        with (STAGE/(name+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)
        report=json.loads((STAGE/name/'results.json').read_text(encoding='utf-8'));assert report['status']=='PASS'
        for path,digest in report['inputs_sha256'].items():assert sha(ROOT/path)==digest,path
        assert inputs=={path:sha(ROOT/path) for path in inputs};print('通过Lens联动 '+name,flush=True)
    assert sha(Path(__file__))==verifier
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,cases=[name for name,_ in steps],
        limits='当前Lens的真实候选OOM/排队首字/Canvas保存重开/Files分派联动；其余组件全尺寸和全部M8继续')
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Lens联动全部通过',flush=True)


if __name__=='__main__':main()
