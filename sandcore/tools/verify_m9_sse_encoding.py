#!/usr/bin/env python3
"""SandAsm每种新增整数SSE2编码与独立GNU as逐字节对照。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm

ROOT=Path(__file__).resolve().parents[1]


def fixture(out):
    source=(ROOT/'user/ASMSSE2.inc').read_text(encoding='utf-8')
    binary=re.findall(r'\{"(\w+)",0x[0-9A-F]+\}',source.split('for(u32 i=0;',1)[0])
    instructions=[]
    for i,op in enumerate(binary):
        d=i%8;s=(i*3+4)%8
        instructions.extend([(f'{op} xmm{d},xmm{s}',False),(f'{op} xmm{d},[esp+128]',True),
                             (f'{op} xmm{d},[ebp-128]',True),(f'{op} xmm{d},[0x1941b0]',False)])
    for op in ('movdqa','movdqu'):
        instructions.extend([(f'{op} xmm7,xmm0',False),(f'{op} xmm2,[esi+512]',True),(f'{op} [esp-512],xmm4',True)])
    instructions.extend([('movd xmm4,eax',False),('movd edx,xmm6',False),('movd xmm1,[edi+256]',True),
                         ('movd [ebp-256],xmm5',True),('pmovmskb ecx,xmm7',False),('pshufd xmm2,xmm7,255',False),
                         ('pshufd xmm3,[esp+1024],0',True)])
    for op in ('psllw','pslld','psllq','psrlw','psrld','psrlq','psraw','psrad','pslldq','psrldq'):
        instructions.extend([(f'{op} xmm5,0',False),(f'{op} xmm7,255',False)])
        if op not in ('pslldq','psrldq'):
            instructions.extend([(f'{op} xmm3,xmm6',False),(f'{op} xmm1,[ebp+256]',True)])
    # SandAsm源码上限8192；三个独立片段避免用测试体积触发合法容量拒绝。
    fixtures=[]
    for begin in range(0,len(instructions),90):
        rows=instructions[begin:begin+90]
        stem=out/f'encoding-{begin//90}'
        stem.with_suffix('.asm').write_text('[bits 32]\n[org 0x400000]\n_start:\n'+'\n'.join(x[0] for x in rows)+'\nret\n',encoding='ascii')
        stem.with_suffix('.S').write_text('.intel_syntax noprefix\n.text\n.global _start\n_start:\n'+'\n'.join(('{disp32} ' if forced else '')+text for text,forced in rows)+'\nret\n',encoding='ascii')
        linux='/mnt/'+str(stem.resolve()).replace('\\','/')[0].lower()+str(stem.resolve()).replace('\\','/')[2:]
        script=f"as --32 '{linux}.S' -o '{linux}.o' && objcopy -j .text -O binary '{linux}.o' '{linux}.bin'"
        subprocess.run(['wsl.exe','sh','-lc',script],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        fixtures.append(dict(stem=stem,rows=len(rows),payload=stem.with_suffix('.bin').read_bytes()))
    return fixtures,len(instructions)


def run(boot,data,out,fixtures):
    report=dict(status='RUNNING',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=False) as vm:
        g=Guest(vm)
        def check(name,condition,**evidence):
            report['cases'].append(dict(name=name,status='PASS' if condition else 'FAIL',**evidence))
            if not condition:
                raise AssertionError(name)
        try:
            g.connect()
            for i,f in enumerate(fixtures):
                g.put_bytes(f['stem'].with_suffix('.asm').read_bytes(),f'/TMP/SSE{i}.ASM','sse-source')
                code,output,_,_=g.command(f'sandasm /TMP/SSE{i}.ASM /TMP/SSE{i}.SCX')
                check(f'native-SandAsm-encoding-{i}',code==0,console=output.decode('utf-8','replace'))
                actual=g.get_bytes(f'/TMP/SSE{i}.SCX','sse-binary')
                check(f'GNU-as-exact-bytes-{i}',actual[36:]==f['payload'],instructions=f['rows'],
                      bytes=len(f['payload']),sha256=hashlib.sha256(f['payload']).hexdigest())
            for i,text in enumerate(['movdqu [eax],[ebx]','paddd eax,xmm0','pshufd xmm0,xmm1,256',
                                     'pslldq xmm0,xmm1','movd xmm0,xmm1','pxor xmm8,xmm0']):
                preserved=b'OLD-VALID-OUTPUT'
                g.put_bytes(preserved,'/TMP/KEEP.SCX','old-output')
                g.put_bytes(('[bits 32]\n[org 0x400000]\n_start:\n'+text+'\nret\n').encode(),'/TMP/BAD.ASM','invalid-encoding')
                code,_,_,_=g.command('sandasm /TMP/BAD.ASM /TMP/KEEP.SCX')
                check('invalid-encoding-preserves-'+str(i),code!=0 and g.get_bytes('/TMP/KEEP.SCX')==preserved,instruction=text,exit_code=code)
            picture=out/'assembler.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status']='SSE_ENCODING_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path);p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);fixtures,total=fixture(a.out)
    matrix=dict(status='RUNNING',instructions_per_disk=total,disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',fixtures))
            print(f'disk-{i}: {total} instruction forms exact, 6 invalid forms rejected',flush=True)
        matrix['status']='SSE_ENCODING_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
