#!/usr/bin/env python3
"""外部COM1停在实际提交边界后硬终止QEMU，再冷启动核对盘上正文。

这是真实客体事务中断，不是离线伪造目录。它验证本版QEMU/宿主缓存
与ATA FLUSH组合，不能声称所有硬件扇区撕裂或物理断电都已覆盖。
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import threading

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm


def dbg(g,command,pattern):
    at=len(g.debug)
    g.vm.pipes['debug'].write(command+b'\n')
    return g.wait(pattern,at,debug=True,timeout=30)


def run(boot,data,out,phase,address):
    original=b'OLD-COMMITTED-'+bytes(range(256))*8
    replacement=b'NEW-COMMITTED-'+bytes(range(255,-1,-1))*12
    report=dict(status='RUNNING',phase=phase,cases=[],screenshots=[])
    expected=replacement if phase=='superblock-flushed' else original
    vm=QemuSession(boot,data,out/'cut','tcg',True,audio=False)
    g=Guest(vm)
    sender=None
    def check(name,condition,**details):
        report['cases'].append(dict(name=name,status='PASS' if condition else 'FAIL',**details))
        if not condition:
            raise AssertionError(name)
    try:
        g.connect()
        g.put_bytes(original,'/TMP/CUTFILE','original')
        before=bytes((vm.out/'sanddata.img').read_bytes()[:512])
        g.client.checked(3,struct.pack('<I',len(replacement))+hashlib.sha256(replacement).digest()+g.client.path('/TMP/CUTFILE'))
        amount=508 if phase=='data-partial' else len(replacement)
        for at in range(0,amount,508):
            block=replacement[at:min(at+508,amount)]
            g.client.checked(4,struct.pack('<I',at)+block)
        if phase!='data-partial':
            dbg(g,b'hello\nhalt',rb'STOP vector=[0-9a-f]+ eip=[0-9a-f]+\r\n')
            target=f'{address:08x}'.encode()
            dbg(g,b'break '+target,rb'OK\r\n')
            dbg(g,b'cont',rb'OK resume\r\n')
            start=len(g.debug)
            errors=[]
            def commit():
                try:
                    g.client.checked(5)
                except BaseException as error:
                    errors.append(repr(error))
            sender=threading.Thread(target=commit,name='power-cut-pending-commit')
            sender.start()
            g.wait(rb'STOP vector=00000003 eip='+target+rb'\r\n',start,debug=True)
            check('real-COM1-commit-boundary',True,address=address)
            picture=vm.out/'commit-boundary.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
        else:
            check('real-incomplete-data-transfer',True,accepted_bytes=amount,total=len(replacement))
        # 停止读者后TerminateProcess，不发BYE/QMP quit或客体关机，也不
        # 继续被暂停的提交。仅关闭这一测试进程，不碰用户正在玩的QEMU。
        g.stop.set()
        g.client.fail(RuntimeError('EXPECTED_POWER_CUT'))
        if sender:
            sender.join(5)
            check('pending-host-request-cancelled',not sender.is_alive())
        g.close()
        vm.process.kill()
        vm.process.wait(5)
        vm.report['expected_power_cut']=True
        vm.close()
        check('actual-hard-process-termination',vm.report['exit_code']!=0,exit_code=vm.report['exit_code'])
        after=bytes((vm.out/'sanddata.img').read_bytes()[:512])
        if phase=='superblock-flushed':
            check('superblock-bank-switched',struct.unpack_from('<I',after,48)[0]!=struct.unpack_from('<I',before,48)[0])
        else:
            check('superblock-original-bank-kept',after==before)
        # 源是刚刚硬终止的真实盘，又复制到独立冷启动目录，禁止离线修盘。
        with QemuSession(boot,vm.out/'sanddata.img',out/'cold','tcg',True,audio=False) as cold:
            reader=Guest(cold)
            try:
                reader.connect()
                body=reader.get_bytes('/TMP/CUTFILE','cold-file')
                check('cold-mount-and-exact-committed-bytes',body==expected,bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
                code,_,_,_=reader.command('printf ALIVE > /TMP/AFTERCUT; cat /TMP/AFTERCUT')
                check('cold-filesystem-can-commit',code==0 and reader.get_bytes('/TMP/AFTERCUT')==b'ALIVE')
            finally:
                reader.close()
        check('original-input-images-unchanged',all(vm.report['source_unchanged'].values()))
        report['status']='POWER_CUT_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL',failure=repr(error))
        raise
    finally:
        if vm.process.poll() is None:
            g.stop.set();g.client.fail(RuntimeError('test-ending'))
            if sender:
                sender.join(5)
            try:
                g.close()
            finally:
                vm.close()
        (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--boot',required=True,type=Path)
    p.add_argument('--data',required=True,action='append',type=Path)
    p.add_argument('--symbols',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args()
    # 当前build25与已测build24内核原字节相同。固定断点必须同时核对
    # 内核摘要与指令，不能把旧编译偏移套在新内核上。
    if sha(a.boot)!='40d002f2567636876afa4c215f29b7f5a917eabc6db3256ae65291653fd3b847':
        raise ValueError('内核变化，需要重新反汇编提交边界')
    symbols=a.symbols.read_text(encoding='ascii')
    if not re.search(r'^00030c20 t write_meta$',symbols,re.M):
        raise ValueError('write_meta符号与边界不一致')
    a.out.mkdir(parents=True,exist_ok=False)
    matrix=dict(status='RUNNING',scope='QEMU_PROCESS_POWER_CUT_NOT_ALL_PHYSICAL_FAILURES',runs=[])
    try:
        for i,data in enumerate(a.data,1):
            for phase,address in [('data-partial',0),('before-directory',0x30dfd),('directory-flushed',0x30e09),('superblock-flushed',0x30e8e)]:
                out=a.out/f'disk-{i}-{phase}'
                out.mkdir()
                result=run(a.boot,data,out,phase,address)
                matrix['runs'].append(result)
                print(f'disk-{i} {phase}: {result["status"]}',flush=True)
        matrix['status']='POWER_CUT_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error))
        raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
