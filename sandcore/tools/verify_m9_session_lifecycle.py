#!/usr/bin/env python3
"""管理租约/重连/重复帧/任务与截图回收的实际双盘验收。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import time

from scserial import QemuSession
from serial_protocol import frame
from verify_m9 import Guest, png_from_ppm


def read_u32(vm,address):
    text=vm.hmp(f'xp /1wx 0x{address:x}')
    m=re.search(r':\s+0x([0-9a-f]+)',text)
    if not m:
        raise ValueError(text)
    return int(m[1],16)


def ticks_wait(vm,address,count):
    start=read_u32(vm,address);deadline=time.monotonic()+90
    while (read_u32(vm,address)-start)&0xffffffff<count:
        if time.monotonic()>deadline:
            raise TimeoutError('客体PIT未达到租约时长')
        time.sleep(.2)


def run(boot,data,out,symbols):
    report=dict(status='RUNNING',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=False) as vm:
        g=Guest(vm)
        def check(name,condition,**evidence):
            report['cases'].append(dict(name=name,status='PASS' if condition else 'FAIL',**evidence))
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            if not condition:
                raise AssertionError(name)
        try:
            g.connect(heartbeat=False)
            # 每次编译/运行有界后PING；租约专项才故意停止发送管理字节。
            source=Path(__file__).resolve().parents[1]/'tests/m9/LIFECYCLE.C'
            g.put_bytes(source.read_bytes(),'/TMP/LIFE.C','lifecycle-source')
            code,_,_,_=g.command('s3c /TMP/LIFE.C /TMP/LIFE.SCX',timeout=180)
            check('native-lifecycle-probe-compiled',code==0)
            g.client.checked(10)
            for mode in ('normal','fault','normal','fault','normal'):
                before=read_u32(vm,symbols['pf_used'])
                code,output,_,_,_=g.run('/TMP/LIFE.SCX'+(' fault' if mode=='fault' else ''),timeout=45)
                after=read_u32(vm,symbols['pf_used'])
                check('capture-'+mode+'-actual-checks',b'FAIL ' not in output and output.count(b'PASS ')>=15 and (code!=0 if mode=='fault' else code==0),exit_code=code,stdout_hex=output.hex())
                # 首次Shell帧缓存可能按需分配，取第二次正常为稳定基线。
                check('capture-'+mode+'-all-pages-reclaimed',before==after,before=before,after=after)
                g.client.checked(10)
            payload=b'printf ONCE >> /TMP/DUPLICATE\n'
            g.command('rm -f /TMP/DUPLICATE')
            start=len(g.console)
            g.client.input(payload);sequence=g.client.sequence
            g.wait(rb'\[SYSTEM\] # ',start)
            vm.pipes['management'].write(frame(2,sequence,payload))
            vm.pipes['management'].write(frame(2,sequence,b'printf EVIL >> /TMP/DUPLICATE\n'))
            code,_,_,_=g.command('true')
            body=g.get_bytes('/TMP/DUPLICATE')
            check('same-sequence-retry-once-and-altered-body-denied',code==0 and body==b'ONCE',stdout_hex=body.hex())
            old=b'original-committed'
            g.put_bytes(old,'/TMP/LEASEFILE','old')
            new=b'new-uncommitted'
            g.client.checked(3,struct.pack('<I',len(new))+hashlib.sha256(new).digest()+g.client.path('/TMP/LEASEFILE'))
            code,_=g.client.request(4,struct.pack('<I',1)+new)
            check('out-of-order-upload-denied',code<0)
            g.client.checked(4,struct.pack('<I',0)+new[:3])
            g.command('sleep 300 &')
            g.client.checked(11)
            code,_=g.client.request(5)
            check('BYE-revokes-old-transfer-and-session',code==-5)
            start=len(g.console);g.client.hello();g.wait(rb'\[SYSTEM\] # ',start)
            check('reconnect-keeps-only-committed-file',g.get_bytes('/TMP/LEASEFILE')==old)
            code,output,_,_,_=g.run('pgrep -c sleep.scx')
            check('BYE-terminates-management-descendant',code==1 and output==b'0\n')
            g.client.checked(3,struct.pack('<I',len(new))+hashlib.sha256(new).digest()+g.client.path('/TMP/LEASEFILE'))
            g.client.checked(4,struct.pack('<I',0)+new[:3])
            g.command('sleep 300 &')
            print(out.name+': waiting actual 30 second guest lease',flush=True)
            ticks_wait(vm,symbols['sc_ticks'],3100)
            code,_=g.client.request(10)
            check('actual-lease-expiry-revokes-session',code==-5)
            start=len(g.console);g.client.hello();g.wait(rb'\[SYSTEM\] # ',start)
            check('lease-expiry-aborts-uncommitted-file',g.get_bytes('/TMP/LEASEFILE')==old)
            code,output,_,_,_=g.run('pgrep -c sleep.scx')
            check('lease-expiry-terminates-descendant',code==1 and output==b'0\n')
            vm.pipes['management'].write(frame(10,9999)[:12])
            ticks_wait(vm,symbols['sc_ticks'],220)
            check('partial-RX-timeout-recovers-parser',g.client.checked(10)[0]==0)
            g.put_bytes(new,'/TMP/LEASEFILE','after-reconnect')
            check('new-session-can-complete-upload',g.get_bytes('/TMP/LEASEFILE')==new)
            picture=out/'lifecycle.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status']='SESSION_LIFECYCLE_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path)
    p.add_argument('--symbols',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    symbols={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+)\s+[bB]\s+(\w+)$',a.symbols.read_text(encoding='ascii'),re.M)}
    a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',symbols))
            print(f'disk-{i}: SESSION_LIFECYCLE_CASES_PASS',flush=True)
        matrix['status']='SESSION_LIFECYCLE_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
