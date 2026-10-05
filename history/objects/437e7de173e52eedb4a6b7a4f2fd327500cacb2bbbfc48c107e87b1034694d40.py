#!/usr/bin/env python3
"""mio：真实调试器核心PASS后串行运行独立探针与十九布局。

等待不占QEMU；失败阻止后续。每份报告绑定当次输入和验证器，
并等前一HMP/QMP真正关闭，再启动下一只独立副本盘，避免抢端口。
"""
import hashlib,json,socket,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];STAGE=ROOT/'build/m8-debugger-draft'


def free_ports():
    deadline=time.monotonic()+30
    while True:
        busy=False
        for port in (4444,4445):
            with socket.socket() as client:
                client.settimeout(.2)
                if client.connect_ex(('127.0.0.1',port))==0:busy=True
        if not busy:return
        assert time.monotonic()<deadline,'前一独立测试端口未释放，不启动后续'
        time.sleep(.5)


def main():
    cases=(('decode','verify_debugger_decode.py'),('lifecycle','verify_debugger_lifecycle.py'),
        ('action-queue','verify_debugger_queue.py'),('matrix','verify_debugger_matrix.py'))
    paths=[ROOT/'build/m8-next'/name for _,name in cases]+[ROOT/'build/m8-next/debugger.c']
    pinned={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    report=STAGE/'results.json';deadline=time.monotonic()+7200
    print('等待调试器真实核心PASS，未连接QEMU',flush=True)
    while not report.exists():
        assert not (STAGE/'failure-state.json').exists() and not (STAGE/'failure.png').exists(),'调试器核心失败，后续未启动'
        assert time.monotonic()<deadline,'调试器核心等待超时，后续未启动'
        time.sleep(1)
    assert json.loads(report.read_text(encoding='utf-8'))['status']=='PASS'
    reports=[]
    for tag,name in cases:
        assert pinned=={path:hashlib.sha256(path.read_bytes()).hexdigest() for path in pinned}
        assert not (STAGE/tag/'results.json').exists(),'当前批不得复用旧结果：'+tag
        free_ports();print('开始Debugger '+tag,flush=True)
        subprocess.run([sys.executable,str(ROOT/'build/m8-next'/name)],cwd=ROOT,check=True,creationflags=subprocess.CREATE_NO_WINDOW)
        result=json.loads((STAGE/tag/'results.json').read_text(encoding='utf-8'));assert result['status']=='PASS'
        reports.append(tag);print('通过Debugger '+tag,flush=True)
    free_ports()
    (STAGE/'followups.json').write_text(json.dumps(dict(author='mio',status='PASS',cases=reports,
        pinned_sha256={str(path.relative_to(ROOT)):digest for path,digest in pinned.items()},
        limits='独立调试器原生探针/生命周期/首字/十九布局，正式预装与Files/Studio联合分派及全M8另行验证'),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
