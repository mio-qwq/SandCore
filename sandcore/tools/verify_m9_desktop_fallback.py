#!/usr/bin/env python3
"""缺CORE/原M7场景/新CORE的真实桌面恢复，以及VGA原字节画面回归。"""
import argparse
import json
from pathlib import Path
import struct
import time
from mkfs_m9 import read_image,make_image,Record
from scserial import QemuSession,sha
from verify_m9 import Guest
from verify_m9_player import symbols,screenshot,sound_window,windows,desktop_restored,memory


def run(boot,data,out,addresses,check_window=True):
    report=dict(status='RUNNING',cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True) as vm:
        g=Guest(vm)
        try:
            g.connect();vm.hmp('sendkey shift');g.command('true');time.sleep(.4)
            scale=struct.unpack('<I',memory(vm,addresses['scale_percent'],1))[0]
            baseline=screenshot(vm,'desktop-before',report)
            if check_window:
                g.command('/APPS/SOUND.SCX /TMP/ALBUM.FLAC &')
                deadline=time.monotonic()+15
                while True:
                    try:large=sound_window(vm,addresses);break
                    except AssertionError:
                        if time.monotonic()>deadline:raise
                        time.sleep(.1)
                time.sleep(.4);screenshot(vm,'large',report);vm.hmp('sendkey m')
                deadline=time.monotonic()+12
                while True:
                    small=sound_window(vm,addresses);screen=screenshot(vm,'mini',report)
                    if small['ch']==112*scale//100 and desktop_restored(screen,baseline,small,scale):break
                    if time.monotonic()>deadline:raise AssertionError('窗口外残留旧画面')
                    time.sleep(.1)
                report['cases'].append(dict(name='complete-workspace-outside-mini-restored',status='PASS'))
                vm.hmp('sendkey esc');deadline=time.monotonic()+10
                while any(w['pid']==large['pid'] for w in windows(vm,addresses)):
                    if time.monotonic()>deadline:raise AssertionError('窗口没有退出')
                    time.sleep(.1)
                g.command('wait');time.sleep(.3)
                final=screenshot(vm,'desktop-after',report)
                count=baseline[0]*(baseline[1]-32*scale//100)*3
                if final[:2]!=baseline[:2] or final[2][:count]!=baseline[2][:count]:
                    raise AssertionError('关闭后完整工作区没有恢复原像素')
                report['cases'].append(dict(name='close-complete-workspace-exact',status='PASS'))
            report.update(status='PASS',resolution=baseline[:2],baseline_sha256=__import__('hashlib').sha256(baseline[2]).hexdigest())
        except BaseException as e:report.update(status='FAIL',failure=repr(e));raise
        finally:
            (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');g.close()
    return report,baseline


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',type=Path,required=True);p.add_argument('--old-boot',type=Path,required=True)
    p.add_argument('--data',type=Path,required=True);p.add_argument('--old-data',type=Path,required=True)
    p.add_argument('--symbols',type=Path,required=True);p.add_argument('--old-symbols',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',runs=[])
    try:
        original=read_image(a.data.read_bytes());old=read_image(a.old_data.read_bytes());addresses=symbols(a.symbols)
        for mode in ('new-core','legacy-core','missing-core','vga-missing-core'):
            records=dict(original)
            if mode=='legacy-core':records['SYS/CORE/CORE.SKM']=old['SYS/CORE/CORE.SKM']
            if 'missing-core' in mode:records.pop('SYS/CORE/CORE.SKM',None)
            if mode.startswith('vga'):
                records['SYS/DISPLAY.CFG']=Record('SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=320\nheight=200\nscale=100\n',-1,-1,3)
            path=a.out/(mode+'.img');path.write_bytes(make_image(records,64))
            result,baseline=run(a.boot,path,a.out/mode,addresses,not mode.startswith('vga'));matrix['runs'].append(result)
            if mode.startswith('vga'):
                prior,old_screen=run(a.old_boot,path,a.out/'vga-original-kernel',symbols(a.old_symbols),False)
                if baseline!=old_screen:raise AssertionError('VGA兜底与原内核像素变化')
                matrix['runs'].append(prior);matrix['vga_exact']=dict(status='PASS',bytes=len(baseline[2]))
        matrix['status']='PASS'
    except BaseException as e:matrix.update(status='FAIL',failure=repr(e));raise
    finally:(a.out/'matrix.json').write_text(json.dumps(matrix,indent=2)+'\n')


if __name__=='__main__':main()
