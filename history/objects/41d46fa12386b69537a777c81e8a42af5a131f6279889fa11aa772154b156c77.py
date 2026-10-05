#!/usr/bin/env python3
"""mio：并发提交资格专测PASS之后串行启动Studio十九组合。
等待只读新报告，失败立即退出且不打开QEMU，不用旧文件代替。
"""
import hashlib,json,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]


def main():
    source=ROOT/'build/m8-next/ide.c';verifier=ROOT/'build/m8-next/verify_studio_matrix.py'
    pinned={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,verifier)}
    report=ROOT/'build/m8-studio-draft/revision/results.json';failure=report.parent/'failure-state.json'
    deadline=time.monotonic()+1800;print('等待Studio提交版本专测PASS，未连接QEMU',flush=True)
    while not report.exists():
        assert not failure.exists(),'提交版本专测失败，矩阵未启动'
        assert time.monotonic()<deadline,'等待超时，矩阵未启动'
        time.sleep(1)
    assert json.loads(report.read_text(encoding='utf-8'))['status']=='PASS'
    assert pinned=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned}
    print('提交版本通过，开始Studio十九组合',flush=True)
    subprocess.run([sys.executable,str(verifier)],cwd=ROOT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)


if __name__=='__main__':main()
