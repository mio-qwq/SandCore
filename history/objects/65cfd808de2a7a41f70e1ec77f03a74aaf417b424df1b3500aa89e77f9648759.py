#!/usr/bin/env python3
"""mio：Studio矩阵及成本报告真正PASS后串行启动调试器核心。

等待只读报告和端口释放。不存在报告不代表失败，日志启动字样也
不代表通过；失败/输入变动立即拒绝启动，不能与前一QEMU抢双盘。
"""
import hashlib,json,socket,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]


def main():
    paths=[ROOT/name for name in ('build/m8-next/debugger.c','build/m8-next/verify_debugger.py',
        'user/SCAPI.H','user/NUI.inc','kernel/font16.txt','build/sandcore.img','build/kernel.sym','build/sanddata.img')]
    pinned={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    stage=ROOT/'build/m8-studio-draft';deadline=time.monotonic()+5400
    print('等待Studio十九组合及长文档成本PASS，未连接QEMU',flush=True)
    while not (stage/'cost/results.json').exists():
        assert not list((stage/'matrix').rglob('failure-state.json')),'Studio矩阵失败，调试器未启动'
        assert not (stage/'cost/failure.png').exists(),'Studio成本失败，调试器未启动'
        assert time.monotonic()<deadline,'等待Studio超时，调试器未启动'
        time.sleep(1)
    for name in ('matrix','cost'):
        report=json.loads((stage/name/'results.json').read_text(encoding='utf-8'))
        assert report['status']=='PASS',name
        if name=='matrix':assert len(report['cases'])==19
    assert pinned=={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in pinned}
    # 报告在finally发quit之前落盘，短暂存在“PASS但旧QEMU仍退出中”。
    # 只探测本项目既定HMP/QMP端口是否已关闭；绝不结束任何进程，
    # 也不把端口可连接误认为可同时开始第二个测试。
    deadline=time.monotonic()+30
    while True:
        busy=False
        for port in (4444,4445):
            with socket.socket() as client:
                client.settimeout(.2)
                if client.connect_ex(('127.0.0.1',port))==0:busy=True
        if not busy:break
        assert time.monotonic()<deadline,'前一测试端口未释放，调试器未启动'
        time.sleep(.5)
    print('Studio十九组合/成本通过，开始真实G2调试器核心',flush=True)
    subprocess.run([sys.executable,str(paths[1])],cwd=ROOT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)


if __name__=='__main__':main()
