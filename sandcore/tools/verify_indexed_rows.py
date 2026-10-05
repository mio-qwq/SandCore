#!/usr/bin/env python3
"""mio：旧画布行优化的实际原生程序/完整客户区/旧核对照。

固定图案通过客体SCCC编译后在旧核与新核运行；显示放大和恢复
来自真正按键，客体只读取。逐物理像素对照当前DAC颜色，包含
全部256槽及1/2/3倍整数放大，避免“截图看起来一样”代替等价。
"""
import argparse
import hashlib
import json
import shutil
import struct
import time
from pathlib import Path
import verify_m8_phase2 as phase

ROOT=phase.ROOT
t=phase.t;q=phase.q;compiler=phase.compiler
OUT=None
CURRENT_SYMBOLS=q.symbols


def bind(out,symbols=None):
    phase.OUT=t.OUT=q.OUT=compiler.OUT=phase.theme.OUT=out
    compiler.v.windows=t.windows
    q.symbols=(lambda:symbols) if symbols else CURRENT_SYMBOLS


def value(window,address):
    pd=t.word(q.symbols()['tasks']+window['owner']*168)
    return t.word(t.physical(pd,address))


def actual_frame(window):
    scale=(window['w']-2)//window['cw']
    display_scale=t.word(q.symbols()['scale_percent'])
    # TITLE_H在原生模式按显示缩放，不能用旧M7的固定13替代。
    title=24*display_scale//100
    width,height=window['cw']*scale,window['ch']*scale
    screen_width=t.word(q.symbols()['gfx_width'])
    address=t.word(q.symbols()['native_bb'])+((window['y']+title)*screen_width+window['x']+1)*4
    raw=q.memory(address,((height-1)*screen_width+width)*4)
    actual=b''.join(raw[row*screen_width*4:(row*screen_width+width)*4] for row in range(height))
    colors=struct.unpack('<256I',q.memory(q.symbols()['colors'],1024))
    expected=bytearray(width*height*4)
    for y in range(height):
        sy=y//scale
        for x in range(width):
            sx=x//scale
            slot=(sx*37+sy*53+(sx*sy)%251)&255
            struct.pack_into('<I',expected,(y*width+x)*4,colors[slot]&0xffffff)
    # 圆角外的客户矩形位置是壁纸/边框，不是索引源像素；直接矩形
    # 参考不能把这些位置误算缺点。内部所有像素先对照独立调色板
    # 参考，含256槽与放大余数。完整矩形（包括圆角露底）另以同一
    # 场景旧核逐字节摘要对照，边界并没有被验收省略。
    margin=16*scale;checked=0
    for y in range(margin,height-margin):
        first=(y*width+margin)*4;last=(y*width+width-margin)*4
        assert actual[first:last]==expected[first:last],('indexed physical row differs',scale,display_scale,y)
        checked+=width-2*margin
    return dict(scale=scale,width=width,height=height,sha256=phase.sha(actual),pixels=width*height,
                independent_palette_reference_pixels=checked,full_rectangle_in_old_new_comparison=True)


def case(label,floppy,symbols,scale,native,mapping):
    out=OUT/label;out.mkdir()
    bind(out,symbols)
    shutil.copy2(floppy,out/'sandcore.img')
    def prepare(disk):
        compiler.disk_put(disk,'HOME/INDEX.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth=1024\nheight=768\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'indexed',prepare,floppy=(out/'sandcore.img').as_posix())
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome')
        t.open_shell(True);phase.idle()
        q.text('run HOME/INDEX.SCX\n')
        address=phase.theme.native_symbols(mapping)['index_probe']
        window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'index-window')
        phase.wait(lambda:value(window,address)==1,'index-first-frame')
        results=[]
        for stage,key in enumerate((None,'1','2')):
            if key:q.key(key)
            phase.wait(lambda:value(window,address)==stage+1,'index-stage')
            t.point(0,0)
            phase.wait(lambda:t.word(q.symbols()['dirty'])==0,'composed-frame')
            deadline=time.monotonic()+15
            while True:
                try:
                    with t.stable_frame():
                        current=t.windows()[-1]
                        result=actual_frame(current)
                    break
                except AssertionError:
                    if time.monotonic()>=deadline:raise
                    time.sleep(.1)
            q.shot(str(stage)+'-indexed')
            results.append(result)
        q.key('esc');phase.idle()
        (out/'results.json').write_text(json.dumps(dict(status='PASS',frames=results,
            kernel_sha256=phase.sha(floppy.read_bytes()),native_sha256=phase.sha(native)),indent=2)+'\n',encoding='utf-8')
        return results
    except Exception:
        q.shot('failure');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
        q.symbols=CURRENT_SYMBOLS


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    args=parser.parse_args();OUT=ROOT/args.out;OUT.mkdir(parents=True,exist_ok=False)
    build=OUT/'compile';build.mkdir();bind(build)
    g2=(ROOT/args.compiler_stage/'g2.scx').read_bytes()
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/indexprobe.c',(ROOT/'user/indexprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'compile-index',prepare)
    phase.DISK=build/'sanddata-std-128-compile-index.img'
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome')
        t.open_shell(True)
        native,mapping,_=phase.compile_source('BIN/G2.SCX','SYS/SRC/indexprobe.c','HOME/INDEX.SCX')
        (OUT/'index-native.scx').write_bytes(native);(OUT/'index-native.map').write_bytes(mapping)
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    before=ROOT/'build/m8-phase2-resume-20261003-01/before-build'
    old_symbols=t.table_symbols(before/'kernel.sym');new_symbols=CURRENT_SYMBOLS()
    results={}
    for scale in (100,150):
        old=case('old-'+str(scale),before/'sandcore.img',old_symbols,scale,native,mapping)
        new=case('new-'+str(scale),ROOT/'build/sandcore.img',new_symbols,scale,native,mapping)
        assert old==new,('old/new mismatch',scale,old,new)
        results[str(scale)]=dict(old=old,new=new)
    (OUT/'results.json').write_text(json.dumps(dict(author='mio',status='PASS',scope='INDEXED_ROWS_ONLY',
        frames=results,native_sha256=phase.sha(native),limitations='1024/100%和150%，实际1/2/3倍客户区；完整M8其它矩阵继续'),
        ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('INDEXED ROWS PASS',flush=True)


if __name__=='__main__':main()
