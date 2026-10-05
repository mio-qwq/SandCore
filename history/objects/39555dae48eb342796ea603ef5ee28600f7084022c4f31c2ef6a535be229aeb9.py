#!/usr/bin/env python3
"""mio：Studio十九组合成功后串行跑同G2长文档成本。
等待只读；失败不打开QEMU，不把已启动日志当PASS。
"""
import hashlib,json,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]


def main():
    paths=(ROOT/'build/m8-next/ide.c',ROOT/'build/m8-next/verify_studio_cost.py')
    pinned={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    matrix=ROOT/'build/m8-studio-draft/matrix';deadline=time.monotonic()+3600
    print('等待Studio十九组合PASS，未连接QEMU',flush=True)
    while not (matrix/'results.json').exists():
        assert not list(matrix.rglob('failure-state.json')),'矩阵失败，成本未启动'
        assert time.monotonic()<deadline,'矩阵等待超时，成本未启动'
        time.sleep(1)
    report=json.loads((matrix/'results.json').read_text(encoding='utf-8'));assert report['status']=='PASS' and len(report['cases'])==19
    assert pinned=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned}
    print('Studio矩阵通过，开始真实G2長文档成本对照',flush=True)
    subprocess.run([sys.executable,str(paths[1])],cwd=ROOT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)


if __name__=='__main__':main()
