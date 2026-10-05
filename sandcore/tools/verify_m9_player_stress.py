#!/usr/bin/env python3
"""复查最终播放器主题/创建/退出与串口心跳的连续生命周期。"""
import argparse
import json
from pathlib import Path
import struct
import time
from scserial import QemuSession
from verify_m9 import Guest,Suite
from verify_m9_player import symbols,memory,windows,sound_window,screenshot,visible_art,png_pixels,desktop_restored

def run(a,data,out):
    report=dict(status='RUNNING',cases=[],screenshots=[],scope='20_THEME_PLAYER_LIFECYCLES_WITH_REAL_HEARTBEATS')
    addresses=symbols(a.symbols);ref=png_pixels(a.fixtures/'cover.png')
    with QemuSession(a.boot,data,out,'tcg',True) as vm:
        g=Guest(vm);s=Suite(g,report,{})
        try:
            g.connect();vm.hmp('sendkey shift')
            s.case('native-theme-helper','s3c /SYS/TEST/THEME.C /TMP/STRESSTHEME.SCX',timeout=240)
            scale=struct.unpack('<I',memory(vm,addresses['scale_percent'],1))[0]
            for i in range(20):
                theme='classic' if i%2 else 'aurora'
                s.case('theme-'+str(i),'/TMP/STRESSTHEME.SCX '+theme,('PASS '+theme.capitalize()+'\n').encode())
                time.sleep(.35);baseline=screenshot(vm,'cycle-'+str(i)+'-desktop',report)
                before=struct.unpack('<I',memory(vm,addresses['pf_used'],1))[0]
                code,_,_,_=g.command('/APPS/SOUND.SCX /TMP/ALBUM.FLAC &')
                if code:raise AssertionError('启动退出码')
                deadline=time.monotonic()+15
                while True:
                    try:
                        w=sound_window(vm,addresses)
                        if visible_art(screenshot(vm,'cycle-'+str(i)+'-large',report),w,ref,scale):break
                    except AssertionError:pass
                    if time.monotonic()>deadline:raise AssertionError('连续启动/画面超时')
                    time.sleep(.1)
                (out/(theme+'-large.png')).write_bytes((out/('cycle-'+str(i)+'-large.png')).read_bytes())
                vm.hmp('sendkey m');deadline=time.monotonic()+12
                while True:
                    small=sound_window(vm,addresses);screen=screenshot(vm,'cycle-'+str(i)+'-mini',report)
                    if small['ch']==112*scale//100 and visible_art(screen,small,ref,scale,True) and desktop_restored(screen,baseline,small,scale):break
                    if time.monotonic()>deadline:raise AssertionError('连续迷你封面/完整桌面恢复超时')
                    time.sleep(.1)
                (out/(theme+'-mini.png')).write_bytes((out/('cycle-'+str(i)+'-mini.png')).read_bytes())
                vm.hmp('sendkey esc');deadline=time.monotonic()+10
                while any(x['pid']==w['pid'] for x in windows(vm,addresses)):
                    if time.monotonic()>deadline:raise AssertionError('连续退出超时')
                    time.sleep(.1)
                code,_,_,_=g.command('wait; echo STRESS-ALIVE')
                after=struct.unpack('<I',memory(vm,addresses['pf_used'],1))[0]
                if code or before!=after:raise AssertionError('连续退出/页回收异常')
                report['cases'].append(dict(name='player-'+str(i)+'-cover-heartbeat-exit-pages',status='PASS',pages=after));s.persist()
            report['status']='PASS'
        except BaseException as e:report.update(status='FAIL',failure=repr(e));raise
        finally:s.persist();g.close()
    return report

def main():
    p=argparse.ArgumentParser()
    for name in ('boot','symbols','fixtures','out'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--data',required=True,action='append',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    matrix=dict(status='RUNNING',runs=[])
    try:
        for i,data in enumerate(a.data,1):matrix['runs'].append(run(a,data,a.out/f'disk-{i}'))
        matrix['status']='PASS'
    except BaseException as e:matrix.update(status='FAIL',failure=repr(e));raise
    finally:(a.out/'matrix.json').write_text(json.dumps(matrix,indent=2)+'\n')

if __name__=='__main__':main()
