#!/usr/bin/env python3
"""QEMU blkdebug真实块设备EIO：数据/目录/超级块/FLUSH分别注入。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm


def layout(data):
    blob=data.read_bytes();count,ds,bank=struct.unpack_from('<I',blob,12)[0],struct.unpack_from('<I',blob,16)[0],struct.unpack_from('<I',blob,48)[0]
    base=512+bank*ds*512;allocated=set(range(1+2*ds));original=None
    for i in range(count):
        at=base+i*96;name=blob[at:at+64].split(b'\0')[0];lba,size=struct.unpack_from('<II',blob,at+64)
        if lba:
            allocated.update(range(lba,lba+(size+511)//512))
        if name==b'TMP/ALBUM.FLAC':
            original=blob[lba*512:lba*512+size]
    if original is None:
        raise ValueError('缺少已提交测试文件')
    free=next(i for i in range(1+2*ds,len(blob)//512) if i not in allocated)
    return free,1+(bank^1)*ds,original


def run(boot,data,out,name,event,sector,original):
    report=dict(status='RUNNING',fault=dict(name=name,event=event,sector=sector),cases=[],screenshots=[])
    options=dict(event=event)
    if sector is not None:
        options['sector']=sector
    with QemuSession(boot,data,out,'tcg',True,audio=False,disk_debug=options) as vm:
        g=Guest(vm)
        def check(label,condition,**evidence):
            report['cases'].append(dict(name=label,status='PASS' if condition else 'FAIL',**evidence))
            if not condition:
                raise AssertionError(label)
        try:
            g.connect()
            check('original-live-file-before-injection',g.get_bytes('/TMP/ALBUM.FLAC')==original)
            replacement=b'EIO'+bytes(range(256))
            g.client.checked(3,struct.pack('<I',len(replacement))+hashlib.sha256(replacement).digest()+g.client.path('/TMP/ALBUM.FLAC'))
            code,_=g.client.request(4,struct.pack('<I',0)+replacement)
            stage='data'
            if code>=0:
                code,_=g.client.request(5);stage='commit'
            else:
                g.client.checked(6)
            check('actual-ATA-write-or-FLUSH-error-reported',code<0,stage=stage,return_code=code)
            # QMP事件必须证明块层真正返回EIO，不能只凭客体非零码。
            vm.qmp('query-status')
            errors=[x for x in vm.events if x.get('event')=='BLOCK_IO_ERROR']
            check('QEMU-BLOCK_IO_ERROR-event-observed',bool(errors) and all(x['data']['operation']!='read' for x in errors),events=errors)
            check('live-old-committed-file-preserved',g.get_bytes('/TMP/ALBUM.FLAC')==original)
            result,text,_,_=g.command('df')
            check('filesystem-state-readable-after-EIO',result==0,console=text.decode('utf-8','replace'))
            if name=='superblock':
                check('uncertain-superblock-error-freezes-writes',b'READONLY' in text)
                code,_=g.client.request(3,struct.pack('<I',1)+hashlib.sha256(b'x').digest()+g.client.path('/TMP/AFTEREIO'))
                check('faulted-volume-rejects-further-transactions',code<0)
            else:
                g.put_bytes(b'after-eio','/TMP/AFTEREIO','after-eio')
                check('recoverable-EIO-next-transaction-works',g.get_bytes('/TMP/AFTEREIO')==b'after-eio')
            screenshot=out/'iofault.ppm';vm.hmp('screendump "'+screenshot.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            report['status']='IO_FAULT_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            g.close()
    # 使用刚刚失败的真实盘冷启动，验证错误后的持久化状态，而不是
    # 重新挂载原始输入来假装恢复；原输入始终只读保留。
    try:
        with QemuSession(boot,out/'sanddata.img',out/'cold','tcg',True,audio=False) as cold:
            reader=Guest(cold)
            try:
                reader.connect()
                check('cold-old-file-exact-after-EIO',reader.get_bytes('/TMP/ALBUM.FLAC')==original)
                reader.put_bytes(b'cold-after-eio','/TMP/COLDEIO','cold-after-eio')
                check('cold-volume-writable-after-EIO',reader.get_bytes('/TMP/COLDEIO')==b'cold-after-eio')
            finally:
                reader.close()
    except BaseException as error:
        report.update(status='FAIL',failure=repr(error));raise
    finally:
        (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path);p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',runs=[])
    try:
        for i,data in enumerate(a.data,1):
            free,bank,original=layout(data)
            for name,event,sector in [('data','write_aio',free),('directory','write_aio',bank),('superblock','write_aio',0),('flush','flush_to_disk',None)]:
                matrix['runs'].append(run(a.boot,data,a.out/f'disk-{i}-{name}',name,event,sector,original))
                print(f'disk-{i} {name}: IO_FAULT_CASES_PASS',flush=True)
        matrix['status']='IO_FAULT_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
