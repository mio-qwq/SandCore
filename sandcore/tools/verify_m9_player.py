#!/usr/bin/env python3
"""Windows QEMU双盘实际FLAC解码/封面/定位、两主题及串口截图闭环。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import time
import zlib

from scserial import QemuSession, sha
from verify_m9 import Guest, Suite, png_from_ppm

def symbols(path):
    result={}
    for line in path.read_text().splitlines():
        match=re.fullmatch(r'([0-9a-fA-F]+)\s+[bBdD]\s+(\w+)',line)
        if match:result[match[2]]=int(match[1],16)
    if not all(0x10000<=result.get(k,0)<0x400000 for k in ('nwins','wins','scale_percent','pf_used')):
        raise ValueError('必须提供同批内核窗口诊断符号')
    return result

def memory(vm,address,words):
    raw=vm.hmp(f'xp /{words}wx 0x{address:x}')
    values=[]
    for line in raw.splitlines():
        if ':' in line:values.extend(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{8})\b',line.split(':',1)[1]))
    if len(values)!=words:raise AssertionError('HMP没有返回完整真实RAM: '+repr(raw))
    return struct.pack('<'+'I'*words,*values)

def windows(vm,addresses):
    count=struct.unpack('<I',memory(vm,addresses['nwins'],1))[0]
    if count>6:raise AssertionError('窗口数量超过实际合同')
    result=[]
    for slot in range(count):
        body=memory(vm,addresses['wins']+slot*80,20)
        head=struct.unpack_from('<7i',body)
        result.append(dict(handle=head[2],pid=head[1],x=head[3],y=head[4],w=head[5],h=head[6],
                           title=body[28:40].split(b'\0')[0].decode('utf-8','replace'),
                           cw=struct.unpack_from('<i',body,44)[0],ch=struct.unpack_from('<i',body,48)[0]))
    return result

def sound_window(vm,addresses):
    items=[w for w in windows(vm,addresses) if w['title'].startswith('SandAudio')]
    if len(items)!=1:raise AssertionError('必须只有一个真实播放器窗口: '+repr(items))
    return items[0]

def screenshot(vm,label,report):
    path=vm.out/(label+'.ppm');vm.hmp('screendump "'+path.as_posix()+'"')
    entry=png_from_ppm(path)
    # 等待画面就绪会重复抓同名文件；索引应描述保留下来的最终文件。
    report['screenshots']=[p for p in report['screenshots'] if p['path']!=entry['path']]
    report['screenshots'].append(entry)
    body=path.read_bytes();h=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',body)
    return int(h[1]),int(h[2]),body[h.end():]

def png_pixels(path):
    # 只读本工具生成的RGB PNG参考；产品图片由客体IMAGE真实解码。
    data=path.read_bytes();at=8;compressed=b'';width=height=0
    while at<len(data):
        n=struct.unpack_from('>I',data,at)[0];kind=data[at+4:at+8];body=data[at+8:at+8+n];at+=n+12
        if kind==b'IHDR':width,height,depth,color,_,_,_=struct.unpack('>IIBBBBB',body)
        if kind==b'IDAT':compressed+=body
    if depth!=8 or color!=2:raise ValueError('参考必须RGB8')
    rows=zlib.decompress(compressed);pixels=bytearray();prior=bytearray(width*3)
    for y in range(height):
        offset=y*(width*3+1);filter_=rows[offset];line=bytearray(rows[offset+1:offset+1+width*3])
        for i in range(width*3):
            a=line[i-3] if i>=3 else 0;b=prior[i];c=prior[i-3] if i>=3 else 0
            if filter_==1:p=a
            elif filter_==2:p=b
            elif filter_==3:p=(a+b)//2
            elif filter_==4:
                base=a+b-c;pa,pb,pc=abs(base-a),abs(base-b),abs(base-c);p=a if pa<=pb and pa<=pc else b if pb<=pc else c
            elif filter_==0:p=0
            else:raise ValueError('参考PNG filter错误')
            line[i]=(line[i]+p)&255
        pixels.extend(line);prior=line
    return width,height,bytes(pixels)

def visible_art(screen,window,reference,scale,mini=False):
    width,height,body=screen;sw,sh,source=reference
    # 布局单位和真实像素分别核对，继承盘可能是150%或200%，不能猜100%。
    side=84 if mini else max(180,min(window['ch']*100//scale-166,(window['cw']*100//scale)*2//3-40))
    side=side*scale//100
    x=window['x']+1+(10 if mini else 24)*scale//100
    y=window['y']+(window['h']-window['ch']-1)+(10 if mini else 90)*scale//100
    if side<1 or x+side>width or y+side>height:return False
    for dy in (6,side//4,side//2,side*3//4,side-7):
        for dx in (6,side//4,side//2,side*3//4,side-7):
            actual=body[((y+dy)*width+x+dx)*3:((y+dy)*width+x+dx)*3+3]
            at=((dy*sh//side)*sw+(dx*sw//side))*3
            if actual!=source[at:at+3]:return False
    return True

def desktop_restored(screen,baseline,window,scale):
    # 验证整个窗口外的工作区，而不是只看封面几个锚点。
    # 阴影有独立边界，任务栏会更新时钟/窗口按钮；两者明确排除。
    width,height,pixels=screen
    if screen[:2]!=baseline[:2]:return False
    pad=12*scale//100
    left=max(0,window['x']-pad);right=min(width,window['x']+window['w']+pad)
    top=max(0,window['y']-pad);bottom=min(height,window['y']+window['h']+pad)
    for y in range(height-32*scale//100):
        spans=((0,left),(right,width)) if top<=y<bottom else ((0,width),)
        for x0,x1 in spans:
            at=(y*width+x0)*3;end=(y*width+x1)*3
            if pixels[at:end]!=baseline[2][at:end]:return False
    return True

def run(boot,data,out,fixtures,addresses,cpu='qemu32',gui=True,preloaded=False,bench=False):
    reference=json.loads((fixtures/'fixtures.json').read_text())
    report=dict(status='RUNNING',scope='PLAYER_FLAC_COVER_MINI_ONLY',cpu=cpu,cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=True,cpu=cpu) as vm:
        guest=Guest(vm);suite=Suite(guest,report,{})
        def record(name,actual,expected):
            item=dict(name=name,status='PASS' if actual==expected else 'FAIL',actual=actual,expected=expected)
            report['cases'].append(item);suite.persist()
            if actual!=expected:raise AssertionError(name+': '+repr(actual)+' != '+repr(expected))
        try:
            guest.connect();vm.hmp('sendkey shift')
            scale=struct.unpack('<I',memory(vm,addresses['scale_percent'],1))[0]
            report['scale_percent']=scale
            names=[f['file'] for f in reference['fixtures']]+['ALBUM.FLAC','ALBUM.MP3','TRUNC.FLAC','CRC.FLAC','cover.png']
            if not preloaded:
                for name in names:guest.put_bytes((fixtures/name).read_bytes(),'/TMP/'+name,'player-'+name)
            if bench:
                report['put_benchmark']=[]
                for n in (65536,65536,262144):
                    content=os.urandom(n);begin=time.monotonic()
                    result=guest.put_bytes(content,'/TMP/PUTBENCH.BIN','speed')
                    seconds=time.monotonic()-begin
                    report['put_benchmark'].append(dict(bytes=n,wall_seconds=seconds,kib_per_second=n/seconds/1024,
                                                        sha256=hashlib.sha256(content).hexdigest(),response=result))
                    # SHA256在真实客体重新读取已提交文件，验证速率测量包含正确完整提交。
                    suite.case('put-speed-complete-'+str(len(report['put_benchmark'])), 'sha256sum /TMP/PUTBENCH.BIN',
                               contains=hashlib.sha256(content).hexdigest().encode())
            for f in reference['fixtures']:
                suite.case('FLAC-PCM-move-seek-bounds-'+f['file'], '/SYS/TEST/PLAYERTEST.SCX decode /TMP/'+f['file'],bytes.fromhex(f['expected_hex']))
                suite.case('FLAC-CLI-play-'+f['file'],'soundplay /TMP/'+f['file'],b'',timeout=60)
            suite.case('FLAC-embedded-metadata-does-not-change-PCM','/SYS/TEST/PLAYERTEST.SCX decode /TMP/ALBUM.FLAC',bytes.fromhex(reference['fixtures'][0]['expected_hex']))
            for name in ('TRUNC.FLAC','CRC.FLAC'):
                suite.case('FLAC-corrupt-'+name,'/SYS/TEST/PLAYERTEST.SCX bad /TMP/'+name,b'PASS corrupt FLAC rejected\n')
            suite.case('native-place-bounds','/SYS/TEST/PLAYERTEST.SCX place',b'PASS native place bounds clamp legacy reject\n')
            if gui:suite.case('MP3-WAV-decode-regression','/SYS/TEST/AUDIOCHECK.SCX',contains=b'PASS truncated RIFF rejected')
            if gui:
                expected=(reference['art_title']+'\n'+reference['art_artist']+'\n').encode()+bytes.fromhex(reference['art_expected_hex'])
                for name in ('ALBUM.FLAC','ALBUM.MP3'):
                    suite.case('embedded-cover-metadata-pixels-cleanup-'+name,'/SYS/TEST/PLAYERTEST.SCX art /TMP/'+name,expected,timeout=90)
                suite.case('sidecar-cover-pixels','/SYS/TEST/PLAYERTEST.SCX art /TMP/MONO.FLAC',b'MONO.FLAC\nLocal library\n'+bytes.fromhex(reference['art_expected_hex']))
                suite.case('native-compile-theme','s3c /SYS/TEST/THEME.C /TMP/PLAYTHEME.SCX',timeout=240)
                ref=png_pixels(fixtures/'cover.png')
                for theme in ('aurora','classic'):
                    suite.case('player-theme-'+theme,'/TMP/PLAYTHEME.SCX '+theme,('PASS '+theme.capitalize()+'\n').encode())
                    time.sleep(.35)
                    baseline=screenshot(vm,theme+'-desktop-baseline',report)
                    before_pages=struct.unpack('<I',memory(vm,addresses['pf_used'],1))[0]
                    guest.command('/APPS/SOUND.SCX /TMP/ALBUM.FLAC &')
                    deadline=time.monotonic()+25
                    while True:
                        try:
                            large=sound_window(vm,addresses);screen=screenshot(vm,theme+'-large',report)
                            if visible_art(screen,large,ref,scale):break
                        except AssertionError:pass
                        if time.monotonic()>deadline:raise AssertionError('真实大屏未显示内嵌封面')
                        time.sleep(.2)
                    record(theme+'-large-cover-visible',True,True)
                    vm.hmp('sendkey m');deadline=time.monotonic()+10
                    while True:
                        small=sound_window(vm,addresses)
                        if small['cw']==360*scale//100 and small['ch']==112*scale//100:break
                        if time.monotonic()>deadline:raise AssertionError('M没有进入迷你')
                        time.sleep(.1)
                    record(theme+'-same-window-pid',(small['handle'],small['pid']),(large['handle'],large['pid']))
                    deadline=time.monotonic()+10
                    while True:
                        screen=screenshot(vm,theme+'-mini',report)
                        if visible_art(screen,small,ref,scale,True) and desktop_restored(screen,baseline,small,scale):break
                        if time.monotonic()>deadline:raise AssertionError('迷你封面或窗口外完整桌面未恢复')
                        time.sleep(.1)
                    record(theme+'-lower-left',(small['x'],small['y']+small['h']),(10*scale//100,screen[1]-32*scale//100))
                    record(theme+'-mini-cover',visible_art(screen,small,ref,scale,True),True)
                    record(theme+'-full-desktop-outside-mini-restored',desktop_restored(screen,baseline,small,scale),True)
                    suite.case(theme+'-window-capture',f'capture {small["handle"]} /TMP/PLAYERMINI.BMP')
                    captured=suite.capture_bytes('/TMP/PLAYERMINI.BMP',theme+'-mini-serial')
                    record(theme+'-native-capture-size',struct.unpack_from('<ii',captured,18),(360*scale//100,-112*scale//100))
                    vm.hmp('sendkey o');deadline=time.monotonic()+10
                    while True:
                        opened=sound_window(vm,addresses)
                        if opened['cw']==large['cw'] and opened['ch']==large['ch']:break
                        if time.monotonic()>deadline:raise AssertionError('迷你Open未展开浏览器')
                        time.sleep(.1)
                    screenshot(vm,theme+'-mini-open',report)
                    vm.hmp('sendkey esc');deadline=time.monotonic()+10
                    while True:
                        canceled=sound_window(vm,addresses)
                        if canceled['cw']==small['cw'] and canceled['ch']==small['ch']:break
                        if time.monotonic()>deadline:raise AssertionError('取消选曲未恢复迷你')
                        time.sleep(.1)
                    record(theme+'-mini-open-cancel-same-window',canceled,small)
                    vm.hmp('sendkey m');deadline=time.monotonic()+10
                    while True:
                        restored=sound_window(vm,addresses)
                        if restored['cw']==large['cw'] and restored['ch']==large['ch']:break
                        if time.monotonic()>deadline:raise AssertionError('M没有恢复大屏')
                        time.sleep(.1)
                    record(theme+'-restore-geometry',restored,large)
                    vm.hmp('sendkey spc');time.sleep(.15)
                    screenshot(vm,theme+'-pause',report)
                    vm.hmp('sendkey f');deadline=time.monotonic()+10
                    expected_full=(0,0,screen[0],screen[1]-32*scale//100)
                    while True:
                        full=sound_window(vm,addresses)
                        if (full['x'],full['y'],full['w'],full['h'])==expected_full:break
                        if time.monotonic()>deadline:raise AssertionError('F未真正最大化窗口')
                        time.sleep(.1)
                    record(theme+'-fullscreen',(full['x'],full['y'],full['w'],full['h']),expected_full)
                    screenshot(vm,theme+'-full',report)
                    vm.hmp('sendkey esc');deadline=time.monotonic()+10
                    while any(w['title'].startswith('SandAudio') for w in windows(vm,addresses)):
                        if time.monotonic()>deadline:raise AssertionError('播放器未正常退出回收窗口')
                        time.sleep(.1)
                    record(theme+'-close-window-reclaimed',True,True)
                    guest.command('wait')
                    after_pages=struct.unpack('<I',memory(vm,addresses['pf_used'],1))[0]
                    record(theme+'-heap-canvas-cover-pages-reclaimed',after_pages,before_pages)
                code,body,_,_=guest.command('ls /TMP')
                record('cover-temp-not-left',code==0 and b'SC-COVER-' not in body,True)
            report['status']='PLAYER_CASES_PASS_REMAINDER_PENDING'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error))
            try:screenshot(vm,'failure',report)
            except BaseException:pass
            try:
                (vm.out/'failure-registers.txt').write_text(vm.hmp('info registers'),encoding='utf-8')
                vm.qmp.command('stop')
                (vm.out/'failure-paused-registers.txt').write_text(vm.hmp('info registers'),encoding='utf-8')
            except BaseException:pass
            raise
        finally:suite.persist();guest.close()
    if vm.report['exit_code']!=0 or not all(vm.report['source_unchanged'].values()):raise AssertionError('QEMU失败或源盘被写')
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('boot','symbols','fixtures','out'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--data',type=Path,required=True,action='append')
    parser.add_argument('--no-fpu-only',action='store_true')
    parser.add_argument('--preloaded',action='store_true',help='音频夹具已打入独立测试盘，减少重复UART传输')
    parser.add_argument('--bench-put',action='store_true',help='首盘实测完整提交吞吐并在客体复核SHA')
    args=parser.parse_args()
    if len(args.data)<2 or len({p.resolve() for p in args.data})!=len(args.data):parser.error('需要不同来源双盘')
    args.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',runs=[])
    try:
        for i,data in enumerate(args.data,1):
            matrix['runs'].append(run(args.boot,data,args.out/f'disk-{i}',args.fixtures,symbols(args.symbols),
                                      'qemu32,-fpu,-fxsr,-sse,-sse2' if args.no_fpu_only else 'qemu32',not args.no_fpu_only,
                                      args.preloaded,args.bench_put and i==1))
        matrix['status']='PLAYER_CASES_PASS_REMAINDER_PENDING'
    except BaseException as error:matrix.update(status='FAIL',failure=repr(error));raise
    finally:(args.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
