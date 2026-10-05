#!/usr/bin/env python3
"""mio：owner0真实PNG壁纸、整幅缓存/PCI区域和同盘冷启动。

只改开机前独立副本的主题配置，不改用户选择或个人试玩盘。
源PNG仍由客体IMAGE.SCX解码，参考仅在宿主读取同一原始文件。
"""
import argparse
import json
import shutil
import struct
import traceback
from array import array
from PIL import Image
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    asset=ROOT/'assets/pictures/WELCOME-TRUECOLOR-V1.png'
    with Image.open(asset) as picture:
        sw,sh=picture.size;source=array('I');source.frombytes(picture.convert('RGBA').tobytes('raw','BGRA'))
    rgb=array('I',(pixel&0xFFFFFF for pixel in source)).tobytes()
    width,height=640,480;cropw,croph=sw,sh
    if sw*height>sh*width:cropw=sh*width//height
    else:croph=sw*height//width
    left,top=(sw-cropw)//2,(sh-croph)//2
    reference=array('I',((source[(top+y*croph//height)*sw+left+x*cropw//width]&0xFFFFFF)
                         for y in range(height) for x in range(width))).tobytes()
    config=(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes().replace(
        b'wallpaper=SYS/WALL/AURORA.SCB',b'wallpaper=SYS/PICTURES/WELCOME.PNG')
    assert b'wallpaper=SYS/PICTURES/WELCOME.PNG' in config
    def prepare(disk):
        c.disk_put(disk,'SYS/THEME.CFG',config)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=640\nheight=480\nscale=100\n')
    report=dict(author='mio',status='RUNNING',scope='PRISM_PNG_WALLPAPER_COLD_BOOT',
        inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in
                ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},boots=[])
    for boot in range(2):
        proc=t.launch('std',128,'prism-wall',prepare if not boot else None,
                      reuse=bool(boot),floppy=(out/'sandcore.img').as_posix())
        try:
            phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome');q.key('ret')
            phase.wait(lambda:struct.unpack('<4I',q.memory(kernel['wall'],16))[1:3]==(sw,sh)
                       and not t.word(kernel['wall_ticket']),
                       'owner0-real-wallpaper',90)
            phase.wait(lambda:c.task_states()[1:].count(1)==0,'decoder-reclaimed',30)
            t.point(0,0)
            with t.stable_frame():
                pixels,ww,wh,pages=struct.unpack('<4I',q.memory(kernel['wall'],16))
                assert (ww,wh)==(sw,sh) and q.memory(pixels,len(rgb))==rgb
                cached,cw,ch,cachepages=struct.unpack('<4I',q.memory(kernel['wall_render'],16))
                assert (cw,ch)==(width,height) and q.memory(cached,len(reference))==reference
                # 右下内部区域不含默认左侧图标、任务栏和光标，比较
                # 整个区域PCI真实像素，不能只证明后台缓存存在。
                front=q.memory(t.word(kernel['framebuffer']),width*height*4)
                # 默认第四列Monitor/Race图标及名称延伸到x约500；
                # 从520开始才是已核对截图的纯壁纸区域。此前450
                # 的比较把合法图标文字当成背景，失败证据单独保留。
                for y in range(190,430):
                    start=(y*width+520)*4;end=(y*width+630)*4
                    assert front[start:end]==reference[start:end],('PCI differs',boot,y)
            assert not t.windows() and not phase.faults()
            q.shot(f'{boot+1:02d}-prism-desktop')
            item=dict(boot=boot+1,wall_pages=pages,cache_pages=cachepages,
                pf_used=t.word(kernel['pf_used']),desktop_pages=t.word(kernel['desktop_pages']))
            report['boots'].append(item)
            if boot:
                old=report['boots'][0]
                assert item['pf_used']-item['desktop_pages']==old['pf_used']-old['desktop_pages']
        except Exception as error:
            report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
            if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
            (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            raise
        finally:
            if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    report.update(status='PASS',limitations='仅此PNG/640x480100% Aurora桌面与同盘冷启动；其它格式/主题/OOM/换图回滚另验')
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PRISM WALLPAPER PASS',flush=True)


if __name__=='__main__':main()
