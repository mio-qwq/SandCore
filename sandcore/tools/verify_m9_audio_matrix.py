#!/usr/bin/env python3
"""原创WAV独立整数/IEEE参考：采样率、位深、声道及坏容器边界。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm
from verify_m9_session_lifecycle import read_u32


def fnv(data):
    h=2166136261
    for x in data:
        h=((h^x)*16777619)&0xffffffff
    return h


def wav(rate,bits,channels,floating=False,extensible=False,odd=False):
    raw=bytearray();reference=bytearray()
    # 有符号极值/舍入边界和正常音量；浮点另含NaN/Inf，按公开
    # 输出合同归零，不让宿主浮点转换掩盖未定义行为。
    for i in range(241):
        converted=[]
        for ch in range(channels):
            n=((i*3791+ch*19001)&65535)-32768
            if floating:
                x=[-1.5,-1.,-.5,0.,.5,1.,1.5,float('inf'),float('nan')][(i+ch)%9]
                raw.extend(struct.pack('<f',x))
                n=0 if not math.isfinite(x) else max(-32768,min(32767,int(x*32768)))
            elif bits==8:
                raw.append((n+32768)>>8);n=(raw[-1]-128)*256
            else:
                raw.extend(int(n<<(bits-16)).to_bytes(bits//8,'little',signed=True))
            converted.append(n)
        reference.extend(struct.pack('<hh',converted[0],converted[-1]))
    fmt=struct.pack('<HHIIHH',0xfffe if extensible else 3 if floating else 1,channels,rate,rate*channels*(bits//8),channels*(bits//8),bits)
    if extensible:
        fmt+=struct.pack('<HHI',22,bits,3 if channels==2 else 4)+struct.pack('<IHH8s',3 if floating else 1,0,0x10,b'\x80\0\0\xaa\0\x38\x9b\x71')
    body=b'WAVE'
    if odd:
        body+=b'JUNK'+struct.pack('<I',3)+b'abc\0'
    body+=b'fmt '+struct.pack('<I',len(fmt))+fmt+b'data'+struct.pack('<I',len(raw))+raw
    if len(raw)&1:
        body+=b'\0'
    return b'RIFF'+struct.pack('<I',len(body))+body,struct.pack('<6I',241,fnv(reference),rate,channels,bits,1)


def prepare(out):
    out.mkdir(parents=True,exist_ok=False);tree=out/'tree/SYS/TEST/AUDQA';tree.mkdir(parents=True)
    fixtures=[]
    variants=[(r,16,c,False,False,False) for r in (8000,11025,22050,44100,48000,96000,192000) for c in (1,2)]
    variants += [(48000,b,c,False,False,False) for b in (8,24,32) for c in (1,2)]
    variants += [(48000,32,c,True,False,False) for c in (1,2)]
    variants += [(48000,16,2,False,True,False),(48000,32,2,True,True,False),(8000,8,1,False,False,True)]
    for i,variant in enumerate(variants):
        payload,expected=wav(*variant);name=f'V{i}.WAV';(tree/name).write_bytes(payload)
        fixtures.append(dict(file=name,valid=True,format=variant,expected_hex=expected.hex(),sha256=hashlib.sha256(payload).hexdigest()))
    original,_=wav(44100,16,2)
    bad=[('truncated-header',original[:10]),('truncated-data',original[:-1]),('short-fmt',original[:16]+struct.pack('<I',15)+original[20:]),
         ('bad-byte-rate',original[:28]+struct.pack('<I',1)+original[32:]),('bad-block-align',original[:32]+struct.pack('<H',1)+original[34:]),
         ('outside-rate',original[:24]+struct.pack('<II',7999,7999*4)+original[32:]),('three-channels',original[:22]+struct.pack('<H',3)+original[24:]),
         ('invalid-format',original[:20]+struct.pack('<H',7)+original[22:]),('unaligned-PCM',original[:40]+struct.pack('<I',963)+original[44:]),
         ('missing-fmt',original[:12]+original[36:]),('bad-RIFF-end',original[:4]+struct.pack('<I',0xffffffff)+original[8:])]
    for i,(label,payload) in enumerate(bad):
        name=f'B{i}.WAV';(tree/name).write_bytes(payload);fixtures.append(dict(file=name,valid=False,boundary=label,sha256=hashlib.sha256(payload).hexdigest()))
    (out/'fixtures.json').write_text(json.dumps(dict(fixtures=fixtures),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'{len(variants)} valid formats, {len(bad)} invalid containers',flush=True)


def run(boot,data,out,fixtures,pages):
    report=dict(status='RUNNING',scope='DECLARED_WAV_FORMAT_BOUNDARIES_ONLY',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=True) as vm:
        g=Guest(vm)
        try:
            g.connect();g.run('true')
            baseline=read_u32(vm,pages)
            for f in fixtures:
                mode='matrix' if f['valid'] else 'reject'
                code,output,_,elapsed,cpu=g.run('/SYS/TEST/AUDIOCHECK.SCX '+mode+' /SYS/TEST/AUDQA/'+f['file'])
                okay=code==0 and (len(output)==32 and output[:24]==bytes.fromhex(f['expected_hex']) and output[28:]==b'\0'*4 if f['valid'] else output==b'')
                report['cases'].append(dict(name=mode+'-'+f['file'],status='PASS' if okay else 'FAIL',fixture=f,
                                           exit_code=code,stdout_hex=output.hex(),wall_seconds=elapsed,qemu_cpu_seconds=cpu))
                if not okay:
                    raise AssertionError(f['file']+': '+repr((code,output)))
            after=read_u32(vm,pages)
            report['cases'].append(dict(name='all-decoder-pages-reclaimed',status='PASS' if baseline==after else 'FAIL',before=baseline,after=after))
            if baseline!=after:
                raise AssertionError('audio decoder page leak')
            # 同一私有声卡环境内实际运行不同输入率的流式CLI，最终PCM
            # 捕获由QEMU自己完成；格式转换全参考不借播放器非零退出。
            for index in (0,13,20):
                code,output,_,_,_=g.run('soundplay /SYS/TEST/AUDQA/V'+str(index)+'.WAV')
                report['cases'].append(dict(name='actual-soundplay-rate-'+str(index),status='PASS' if code==0 and output==b'' else 'FAIL'))
                if code or output:
                    raise AssertionError('actual CLI soundplay failed')
            picture=out/'audio-formats.ppm';vm.hmp('screendump "'+picture.as_posix()+'"');report['screenshots'].append(png_from_ppm(picture))
            report['status']='AUDIO_MATRIX_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--prepare',type=Path);p.add_argument('--boot',type=Path);p.add_argument('--data',action='append',type=Path)
    p.add_argument('--fixtures',type=Path);p.add_argument('--symbols',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    if a.prepare:
        prepare(a.prepare);return
    if not all((a.boot,a.data,a.fixtures,a.symbols,a.out)):
        p.error('运行需boot/data/fixtures/symbols/out')
    pages=int(re.search(r'^([0-9a-f]+) b pf_used$',a.symbols.read_text(),re.M)[1],16)
    fixtures=json.loads((a.fixtures/'fixtures.json').read_text())['fixtures'];a.out.mkdir(parents=True,exist_ok=False)
    matrix=dict(status='RUNNING',disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',fixtures,pages));print(f'disk-{i}: AUDIO_MATRIX_CASES_PASS',flush=True)
        matrix['status']='AUDIO_MATRIX_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
