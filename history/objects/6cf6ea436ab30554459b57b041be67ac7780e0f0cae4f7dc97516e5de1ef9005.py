#!/usr/bin/env python3
"""mio：Lens全部串行联动通过后再启动独立Studio核心验证。

等待只读报告/本轮日志，不连接QEMU；失败退出不抢占既有端口。
Studio只读正式源码、注入独立测试副本，默认开发双盘不重建。
"""
import hashlib,json,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    verifier=ROOT/'build/m8-next/verify_studio.py';source=ROOT/'build/m8-next/ide.c'
    pinned={path:sha(path) for path in (verifier,source)}
    lens=ROOT/'build/m8-lens';deadline=time.monotonic()+3600
    print('等待Lens全部联动PASS，未连接QEMU；Studio草稿已固定',flush=True)
    while not (lens/'followups.json').exists():
        assert not list((lens/'matrix').rglob('failure-state.json')),'Lens矩阵失败，Studio未启动'
        for name in ('memory','action-queue','canvas-regression','files-regression'):
            log=lens/(name+'.log')
            if log.exists():assert 'Traceback (most recent call last)' not in log.read_text(encoding='utf-8',errors='replace'),name+'失败，Studio未启动'
        assert time.monotonic()<deadline,'Lens联动等待超时，Studio未启动'
        time.sleep(1)
    report=json.loads((lens/'followups.json').read_text(encoding='utf-8'));assert report['status']=='PASS'
    assert pinned=={path:sha(path) for path in pinned},'等待中草稿变化，须重新固定后启动'
    for name,digest in report['inputs_sha256'].items():assert sha(ROOT/name)==digest,name
    print('Lens联动通过，开始独立Studio草稿真实G2核心验证',flush=True)
    subprocess.run([sys.executable,str(verifier)],cwd=ROOT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)


if __name__=='__main__':main()
