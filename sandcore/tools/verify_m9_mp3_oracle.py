#!/usr/bin/env python3
"""FFmpeg独立编码/解码与真实客体dr_mp3全部S16样本对照。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import zlib

from scserial import QemuSession
from verify_m9 import Guest, png_from_ppm


def prepare(ffmpeg,out):
    out.mkdir(parents=True,exist_ok=False);tree=out/'tree/SYS/TEST/MP3QA';tree.mkdir(parents=True)
    fixtures=[];log=[]
    variants=[(24000,1,['-b:a','64k']),(48000,2,['-b:a','128k','-joint_stereo','1']),
              (44100,2,['-q:a','3','-joint_stereo','1']),(32000,2,['-q:a','7','-joint_stereo','0'])]
    for i,(rate,channels,options) in enumerate(variants):
        raw=b''.join(struct.pack('<h',int(16000*math.sin(2*math.pi*(313.7+ch*271)*n/rate)+3000*math.sin(2*math.pi*71.3*n/rate)))
                     for n in range(rate*4//5) for ch in range(channels))
        source=out/f'M{i}.raw';source.write_bytes(raw);target=tree/f'M{i}.MP3';reference=out/f'M{i}.s16'
        encode=[str(ffmpeg),'-hide_banner','-nostdin','-v','error','-f','s16le','-ar',str(rate),'-ac',str(channels),'-i',str(source),'-c:a','libmp3lame',*options,str(target)]
        decode=[str(ffmpeg),'-hide_banner','-nostdin','-v','error','-i',str(target),'-f','s16le','-ac',str(channels),'-ar',str(rate),str(reference)]
        for command in (encode,decode):
            result=subprocess.run(command,capture_output=True,check=True);log.append(dict(command=command,stdout=result.stdout.decode(),stderr=result.stderr.decode()))
        # 保持独立解码器原声道数；单声道按产品公开合同复制到L/R。
        # FFmpeg默认-ac 2会乘sqrt(1/2)，那是重混音，不是解码差异。
        if channels==1:
            mono=reference.read_bytes();reference.write_bytes(b''.join(mono[at:at+2]*2 for at in range(0,len(mono),2)))
        fixtures.append(dict(file=target.name,rate=rate,channels=channels,options=options,reference=str(reference.resolve()),
                             bytes=len(target.read_bytes()),sha256=hashlib.sha256(target.read_bytes()).hexdigest(),reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest()))
    version=subprocess.run([str(ffmpeg),'-version'],capture_output=True,check=True).stdout.decode().splitlines()[0]
    (out/'fixtures.json').write_text(json.dumps(dict(ffmpeg_version=version,fixtures=fixtures,commands=log),indent=2)+'\n')


def run(boot,data,out,fixtures):
    report=dict(status='RUNNING',scope='FOUR_MP3_ENCODER_VARIANTS_ALL_PCM_SAMPLES',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=True) as vm:
        g=Guest(vm)
        def check(name,okay,**details):
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',**details))
            if not okay:
                raise AssertionError(name)
        try:
            g.connect()
            code,output,_,_,_=g.run('/SYS/TEST/AUDIOCHECK.SCX')
            check('original-MP3-WAV-probe-regression',code==0 and b'PASS truncated RIFF rejected' in output)
            for f in fixtures:
                path='/SYS/TEST/MP3QA/'+f['file']
                code,output,_,wall,cpu=g.run('/SYS/TEST/AUDIOCHECK.SCX matrix '+path)
                reference=Path(f['reference']).read_bytes()
                fields=struct.unpack('<8I',output) if len(output)==32 else (0,)*8
                check('MP3-stream-replay-'+f['file'],code==0 and fields[0]==len(reference)//4 and fields[2:6]==(f['rate'],f['channels'],16,2),
                      metadata=fields,wall_seconds=wall,qemu_cpu_seconds=cpu)
                # 客体自身gzip无损传输全部原生PCM，结束与CRC由宿主解码
                # 再确认；不用散列相等假设不同解码器必然逐位同舍入。
                code,_,_,_=g.command('/SYS/TEST/AUDIOCHECK.SCX dump '+path+' | gzip -c > /TMP/ORACLE.GZ')
                captured=zlib.decompress(g.get_bytes('/TMP/ORACLE.GZ'),31)
                check('MP3-exact-frame-count-'+f['file'],code==0 and len(captured)==len(reference),frames=len(captured)//4)
                (out/(f['file']+'.pcm')).write_bytes(captured)
                a=struct.unpack('<'+'h'*(len(captured)//2),captured);b=struct.unpack('<'+'h'*(len(reference)//2),reference)
                maximum=max(abs(x-y) for x,y in zip(a,b));rms=math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))
                check('MP3-all-samples-independent-reference-'+f['file'],maximum<=3 and rms<=1,
                      samples=len(a),maximum_LSB_error=maximum,rms_LSB_error=rms,guest_sha256=hashlib.sha256(captured).hexdigest(),reference_sha256=f['reference_sha256'])
                code,output,_,_,_=g.run('soundplay '+path)
                check('MP3-real-CLI-play-'+f['file'],code==0 and output==b'')
            picture=out/'mp3-oracle.ppm';vm.hmp('screendump "'+picture.as_posix()+'"');report['screenshots'].append(png_from_ppm(picture))
            report['status']='MP3_ORACLE_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');g.close()
    return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--prepare',type=Path);p.add_argument('--ffmpeg',type=Path);p.add_argument('--fixtures',type=Path)
    p.add_argument('--boot',type=Path);p.add_argument('--data',action='append',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    if a.prepare:
        prepare(a.ffmpeg.resolve(),a.prepare);return
    a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',disks=[]);fixtures=json.loads((a.fixtures/'fixtures.json').read_text())['fixtures']
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',fixtures));print(f'disk-{i}: MP3_ORACLE_CASES_PASS',flush=True)
        matrix['status']='MP3_ORACLE_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,indent=2)+'\n')


if __name__=='__main__':
    main()
