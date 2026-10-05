#!/usr/bin/env python3
"""mio：真实G2 Lens默认PNG解码、整幅ARGB/Fit/原像素与资源回收。

从独立双盘正常开机，不传文件参数、不把PNG预转成SCB夹具。
Pillow仅作为宿主无损解码参考，客体必须执行IMAGE.SCX服务；读取
页表/像素都是只读，完整提交帧一致以后才比较实际上屏图片区域。
"""
import argparse
import json
import shutil
import struct
import time
import traceback
from PIL import Image
import verify_m8_phase2 as phase
import verify_lens as lens
import verify_canvas as canvas

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--native-stage',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    stage=ROOT/args.native_stage
    proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='NATIVE_COMPILE_ONLY'
    native=(stage/'lens.scx').read_bytes();mapping=(stage/'lens.map').read_bytes()
    assert phase.sha(native)==proof['artifacts']['lens']['sha256']
    symbols=phase.theme.native_symbols(mapping)
    asset=ROOT/'assets/pictures/WELCOME-TRUECOLOR-V1.png'
    with Image.open(asset) as picture:
        width,height=picture.size;expected=picture.convert('RGBA').tobytes('raw','BGRA')
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    shutil.copy2(ROOT/'build/sandcore.img',out/'sandcore.img')
    def prepare(disk):
        c.disk_put(disk,'HOME/LENS.SCX',native)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'default-png',prepare,floppy=(out/'sandcore.img').as_posix())
    disk=out/'sanddata-std-128-default-png.img';phase.DISK=disk
    report=dict(author='mio',status='RUNNING',scope='DEFAULT_PNG_REAL_DECODE',
        native_sha256=phase.sha(native),png_sha256=phase.sha(asset.read_bytes()),
        width=width,height=height,inputs={name:phase.sha((ROOT/'build'/name).read_bytes())
            for name in ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},checks=[])
    try:
        assert c.file_content(disk,'SYS/PICTURES/WELCOME.PNG')==asset.read_bytes()
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(q.symbols()['pf_used']);q.text('run HOME/LENS.SCX\n')
        window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'Lens')
        pd=t.word(q.symbols()['tasks']+window['owner']*168)
        def memory(address,count):
            # 只读实际PDE/PTE，合并真正连续的物理段；避免每一页
            # 多次HMP往返把观测成本误当成图片服务卡顿。
            return canvas.user_bytes(window,address,count)
        def word(name):return struct.unpack('<i',memory(symbols[name],4))[0]
        def decoded():
            assert not word('image_error'),memory(symbols['status'],100).split(b'\0')[0]
            return word('loaded')
        phase.wait(decoded,'PNG-service-completed',120)
        assert (word('image_w'),word('image_h'))==(width,height)
        assert memory(symbols['filename'],64).split(b'\0')[0]==b'SYS/PICTURES/WELCOME.PNG'
        assert memory(word('preview'),len(expected))==expected,'complete PNG decode differs from reference'
        report['checks'].append('默认无参数真正PNG/整幅BGRA逐字节一致')
        def submitted():
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                with t.stable_frame():
                    current=next(w for w in t.windows() if w['handle']==window['handle'])
                    raw=memory(word('ui_pixels'),current['cw']*current['ch']*4)
                    if raw==q.memory(current['canvas'],len(raw)):return current,raw
                time.sleep(.12)
            raise AssertionError('no complete submitted frame')
        current,raw=submitted();fw,fh=word('fit_w'),word('fit_h')
        assert fw>0 and fh>0 and word('fit_pixels') and word('fit')
        reference=lens.reference_fit(expected,width,height,fw,fh)
        assert memory(word('fit_pixels'),len(reference))==reference,'complete integer Fit differs'
        left=word('viewport_x')+(word('viewport_w')-fw)//2
        top=word('viewport_y')+(word('viewport_h')-fh)//2
        for y in range(fh):
            start=((top+y)*current['cw']+left)*4
            assert raw[start:start+fw*4]==reference[y*fw*4:(y+1)*fw*4]
        q.shot('01-default-prism-fit');report['checks'].append('完整Fit缓存/实际客户帧与参考一致')
        frames=word('ui_frames');q.key('1');phase.wait(lambda:not word('fit'),'100-percent')
        phase.wait(lambda:word('ui_frames')>frames,'100-percent-submitted',30)
        current,raw=submitted();vw,vh=word('viewport_w'),word('viewport_h')
        left,top=word('viewport_x'),word('viewport_y');px,py=word('pan_x'),word('pan_y')
        assert vw<width and vh<height
        for y in range(vh):
            start=((top+y)*current['cw']+left)*4;source=((py+y)*width+px)*4
            assert raw[start:start+vw*4]==expected[source:source+vw*4]
        q.shot('02-default-prism-original');report['checks'].append('100%逐物理像素裁剪与原PNG一致')
        q.key('esc');phase.idle()
        assert t.word(q.symbols()['pf_used'])==baseline,'image/service/request leaked pages'
        assert t.word(q.symbols()['keyboard_overflow'])==t.word(q.symbols()['event_overflow'])==0
        report.update(status='PASS',fit_width=fw,fit_height=fh,
            decoded_sha256=phase.sha(expected),limitations='仅此默认PNG/1024x768100%真实G2 Lens；其它压缩格式/坏图/OOM/全布局另验')
        report['checks'].append('关闭/服务任务/图片请求页严格回收与零队列溢出')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('DEFAULT PNG PASS',width,height,flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
