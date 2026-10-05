#!/usr/bin/env python3
"""mio：调试器同批全验证后，串行运行Monitor核心与十九布局。

等待不连接或占用QEMU。源/规则/核/公共头/字体/默认盘在启动
时钉住摘要；失败阻止后续。不复用旧布局结果，不结束外来VM。
"""
import hashlib,json,socket,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];DEBUG=ROOT/'build/m8-debugger-draft';STAGE=ROOT/'build/m8-monitor-draft'


def free_ports():
    deadline=time.monotonic()+30
    while True:
        busy=False
        for port in (4444,4445):
            with socket.socket() as client:
                client.settimeout(.2)
                if client.connect_ex(('127.0.0.1',port))==0:busy=True
        if not busy:return
        assert time.monotonic()<deadline,'前批QEMU未释放端口，不启动Monitor'
        time.sleep(.5)


def main():
    names=('build/m8-next/monitor.c','build/m8-next/verify_monitor.py','build/m8-next/verify_monitor_matrix.py',
        'user/SCAPI.H','user/NUI.inc','kernel/font16.txt','build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym')
    pinned={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}
    deadline=time.monotonic()+7200;proof=DEBUG/'followups.json'
    print('等待本批db XX/真实栈调试器核心及四项后续PASS，未连接QEMU',flush=True)
    while not proof.exists():
        assert not list(DEBUG.rglob('failure.png')) and not list(DEBUG.rglob('failure-state.json')),'调试器本批失败，Monitor未启动'
        assert time.monotonic()<deadline,'调试器等待超时，Monitor未启动'
        time.sleep(1)
    report=json.loads(proof.read_text(encoding='utf-8'));assert report['status']=='PASS' and report['cases']==['decode','lifecycle','action-queue','matrix']
    matrix=json.loads((DEBUG/'matrix/results.json').read_text(encoding='utf-8'));assert matrix['status']=='PASS' and len(matrix['cases'])==19
    reports=[]
    for tag,script in (('core','verify_monitor.py'),('matrix','verify_monitor_matrix.py')):
        assert pinned=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in pinned}
        destination=STAGE/'results.json' if tag=='core' else STAGE/'matrix/results.json'
        assert not destination.exists(),'当前批Monitor不得复用旧结果：'+tag
        free_ports();print('开始Monitor '+tag,flush=True)
        subprocess.run([sys.executable,str(ROOT/'build/m8-next'/script)],cwd=ROOT,check=True,
            stdout=sys.stdout,stderr=sys.stderr,creationflags=subprocess.CREATE_NO_WINDOW)
        result=json.loads(destination.read_text(encoding='utf-8'));assert result['status']=='PASS'
        reports.append(tag);print('通过Monitor '+tag,flush=True)
    free_ports();STAGE.mkdir(parents=True,exist_ok=True)
    (STAGE/'followups.json').write_text(json.dumps(dict(author='mio',status='PASS',cases=reports,pinned_sha256=pinned,
        limits='Monitor独立候选核心/十九布局阶段；动作帧排队专测、正式预装/相关旧目录ABI与其它组件/全M8继续'),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
