#!/usr/bin/env python3
"""nano完整4MiB和边界文件、scdbg暂停时8KiB输入背压/EOF。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm
from verify_m9_session_lifecycle import read_u32


def prepare(out):
    out.mkdir(parents=True,exist_ok=False);tree=out/'tree/SYS/TEST/EDIQA';tree.mkdir(parents=True)
    for name,body in [('BIG.TXT',b'A'*4194304),('OVER.TXT',b'A'*4194305),('LINES.TXT',b'a\n'*131072),('NUL.TXT',b'a\0b')]:
        (tree/name).write_bytes(body)


def run(boot,data,out,pages):
    report=dict(status='RUNNING',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=False) as vm:
        g=Guest(vm)
        def check(name,okay,**details):
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',**details))
            if not okay:raise AssertionError(name)
        try:
            g.connect();g.command('true');before=read_u32(vm,pages)
            marker,start=g.dialog('nano /SYS/TEST/EDIQA/BIG.TXT',rb'SandNano')
            at=len(g.console);g.client.input(b'X');g.wait(rb'Edit failed; buffer preserved',at)
            check('nano-full-4MiB-overflow-edit-denied',True)
            at=len(g.console);g.client.input(b'\x0f');g.wait(rb'Write file:',at)
            at=len(g.console);g.client.input(b'\n');g.wait(rb'Saved',at,timeout=90)
            g.client.input(b'\x18');code,_=g.finish_dialog(marker,start)
            check('nano-full-4MiB-save-exit',code==0)
            check('nano-full-4MiB-exact-page-reclaim',before==read_u32(vm,pages),before_pages=before,after_pages=read_u32(vm,pages))
            code,_,_,_=g.command('gzip -c /SYS/TEST/EDIQA/BIG.TXT > /TMP/BIG.GZ')
            body=zlib.decompress(g.get_bytes('/TMP/BIG.GZ'),31)
            check('nano-full-saved-body-exact',code==0 and body==b'A'*4194304,bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
            for name in ('OVER.TXT','LINES.TXT','NUL.TXT'):
                before=read_u32(vm,pages);code,output,_,_,_=g.run('nano /SYS/TEST/EDIQA/'+name)
                check('nano-reject-'+name,code==1,exit_code=code,stdout_hex=output.hex())
                check('nano-reject-reclaims-'+name,before==read_u32(vm,pages))
            source=b'#include "SCIO.H"\nint main(void){u8 b[512];int n;u32 total=0;while((n=cli_read(0,b,512))>0)total+=(u32)n;if(n<0)return 2;cli_text(1,"QUEUECOUNT:");cli_number(1,(int)total);cli_text(1,"\\n");return 0;}\n'
            g.put_bytes(source,'/TMP/QUEUE.C','queue-probe');code,_,_,_=g.command('s3c /TMP/QUEUE.C /TMP/QUEUE.SCX')
            check('native-scdbg-input-probe',code==0)
            before=read_u32(vm,pages);marker,start=g.dialog('scdbg /TMP/QUEUE.SCX',rb'scdbg> ')
            accepted=0
            for i in range(20):
                at=len(g.console);g.client.input(b'input '+b'Q'*500+b'\n');done=g.wait(rb'scdbg> ',at)
                output=bytes(g.console[at:done.start()]);accepted+=b'scdbg: input queue/closed' not in output
            check('scdbg-paused-input-backpressure',0<accepted<20,accepted_commands=accepted,rejected_commands=20-accepted)
            at=len(g.console);g.client.input(b'regs\n');g.wait(rb'scdbg> ',at)
            check('scdbg-full-queue-prompt-responsive',b'Paused EIP=' in bytes(g.console[at:]) or b'EIP=' in bytes(g.console[at:]))
            at=len(g.console);g.client.input(b'eof\n');g.wait(rb'scdbg> ',at)
            at=len(g.console);g.client.input(b'input AFTEREOF\n');g.wait(rb'scdbg: input queue/closed',at);g.wait(rb'scdbg> ',at)
            check('scdbg-input-after-EOF-rejected',True)
            at=len(g.console);g.client.input(b'cont\n');count=g.wait(rb'QUEUECOUNT:([0-9]+)\r?\n',at)
            check('scdbg-backpressure-lossless-EOF',int(count[1])==accepted*501,received_bytes=int(count[1]),expected_bytes=accepted*501)
            g.wait(rb'Target exited 0',count.end());g.client.input(b'quit\n');code,_=g.finish_dialog(marker,start)
            check('scdbg-input-target-and-debugger-exit',code==0)
            check('scdbg-full-queue-all-pages-reclaimed',before==read_u32(vm,pages),before_pages=before,after_pages=read_u32(vm,pages))
            picture=out/'cli-limits.ppm';vm.hmp('screendump "'+picture.as_posix()+'"');report['screenshots'].append(png_from_ppm(picture))
            report['status']='CLI_LIMITS_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--prepare',type=Path);p.add_argument('--boot',type=Path);p.add_argument('--data',action='append',type=Path)
    p.add_argument('--symbols',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    if a.prepare:prepare(a.prepare);return
    pages=int(re.search(r'^([0-9a-f]+) b pf_used$',a.symbols.read_text(),re.M)[1],16);a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',pages));print(f'disk-{i}: CLI_LIMITS_CASES_PASS',flush=True)
        matrix['status']='CLI_LIMITS_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
