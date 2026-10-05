#!/usr/bin/env python3
"""实际驱动外部串口菜单：输入触发内核断点，暂停超过ACK期限后继续。"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt':
        parser.error('使用Windows Python')
    args.out.mkdir(parents=True,exist_ok=False)
    address=None
    for line in args.symbols.read_text(encoding='utf-8').splitlines():
        fields=line.split()
        if len(fields)==3 and fields[2]=='serial_management_isr':
            address=int(fields[0],16)
    if address is None or not 0x10000<=address<0x100000:
        raise ValueError('缺少同批内核符号')
    command=[sys.executable,'-u',str(ROOT/'tools/scserial.py'),
             '--boot',str(args.boot.resolve()),'--data',str(args.data.resolve()),
             '--out',str((args.out/'session').resolve()),'--headless','--accel','tcg']
    process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT,env=dict(os.environ,PYTHONIOENCODING='utf-8'),bufsize=0)
    condition=threading.Condition()
    output=bytearray()
    def read():
        while chunk:=process.stdout.read(1):
            with condition:
                output.extend(chunk)
                condition.notify_all()
        with condition:
            condition.notify_all()
    reader=threading.Thread(target=read,name='M9-host-menu-output')
    reader.start()
    report=dict(status='RUNNING',scope='EXTERNAL_HOST_MENU_BREAKPOINT_ACK_COORDINATION',kernel_symbol='serial_management_isr',kernel_address=address)
    def wait(pattern,start=0,timeout=60):
        expression=re.compile(pattern)
        deadline=time.monotonic()+timeout
        with condition:
            while True:
                match=expression.search(output,start)
                if match:
                    return match
                if process.poll() is not None:
                    raise RuntimeError('串口菜单提前退出')
                remaining=deadline-time.monotonic()
                if remaining<=0:
                    raise TimeoutError('宿主菜单等待超时: '+repr(pattern))
                condition.wait(min(.1,remaining))
    def send(text):
        process.stdin.write((text+'\n').encode('utf-8'))
        process.stdin.flush()
    try:
        wait(rb'\[SYSTEM\] # ')
        send(':debug')
        send('hello')
        send('halt')
        stopped=wait(rb'STOP vector=[0-9a-f]+ eip=[0-9a-f]+\r\n')
        start=len(output)
        send(f'mem {address:08x} 1')
        original=wait(rb'DATA ([0-9a-f]{2})\r\n',start)[1]
        start=len(output)
        send(f'break {address:08x}')
        send(f'mem {address:08x} 1')
        wait(rb'DATA cc\r\n',start)
        start=len(output)
        send('cont')
        wait(rb'OK resume\r\n',start)
        send(':management')
        start=len(output)
        send('date')
        # 在COM2硬件IRQ入口停住，保证INPUT尚未处理/ACK尚未发送。
        # 管理线程必须返回宿主菜单，用户仍可在COM1恢复真实现场。
        wait('管理ACK等待cont'.encode('utf-8'),start)
        paused=time.monotonic()
        # 大于原3秒*3次重发期限，确认是暂停活动时间而非碰巧赶上ACK。
        with condition:
            while time.monotonic()-paused<12:
                condition.wait(min(.25,12-(time.monotonic()-paused)))
                if process.poll() is not None or '管理操作失败'.encode('utf-8') in output[start:]:
                    raise RuntimeError('暂停期间请求超时或菜单退出')
        report['paused_wall_seconds']=time.monotonic()-paused
        send(':debug')
        send(f'clear {address:08x}')
        position=len(output)
        send(f'mem {address:08x} 1')
        restored=wait(rb'DATA ([0-9a-f]{2})\r\n',position)[1]
        if restored!=original:
            raise AssertionError('宿主菜单clear没有恢复原字节')
        send('cont')
        wait(rb'OK resume\r\n',position)
        # COM2输出在management选中时可见，等一次心跳/活动ACK完成后再提交。
        send(':management')
        time.sleep(.25)
        position=len(output)
        send('echo M9-HOST-BREAK-RESUMED')
        wait(rb'\r?\nM9-HOST-BREAK-RESUMED\r?\n',position)
        send(':quit')
        process.wait(timeout=15)
        reader.join(timeout=3)
        state=json.loads((args.out/'session/session.json').read_text(encoding='utf-8'))
        if process.returncode or state['exit_code'] or not all(state['source_unchanged'].values()):
            raise AssertionError('退出失败或源盘改变')
        report.update(status='PASS',host_exit_code=process.returncode,qemu_exit_code=state['exit_code'],source_unchanged=state['source_unchanged'],original_byte=original.decode())
    except BaseException as error:
        report.update(status='FAIL',failure=repr(error))
        raise
    finally:
        if process.poll() is None:
            try:
                send(':quit')
                process.wait(timeout=15)
            except (OSError,subprocess.TimeoutExpired):
                process.terminate()
                process.wait(timeout=5)
        reader.join(timeout=3)
        (args.out/'console.bin').write_bytes(output)
        (args.out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('外部串口菜单真实断点/12秒暂停/继续ACK/后续命令/源盘不变 PASS')


if __name__=='__main__':main()
