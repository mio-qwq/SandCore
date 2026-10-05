#!/usr/bin/env python3
"""M9正式全尺寸桌面/播放器成本采样；实测，不伪称优化或FPS达标。"""
import argparse
import json
from pathlib import Path
import re
import statistics
import struct
import time

from scserial import QemuSession
from verify_m9 import Guest, process_cpu, png_from_ppm
from verify_m9_player import sound_window, windows, memory


def run(boot,data,out,symbols):
    report=dict(status='RUNNING',scope='ACTUAL_FULL_RESOLUTION_COST_MEASUREMENT_NO_BEFORE_AFTER_CLAIM',measurements=[],cases=[],screenshots=[])
    with QemuSession(boot,data,out,'tcg',True,audio=True) as vm:
        g=Guest(vm)
        def value(name):return struct.unpack('<I',memory(vm,symbols[name],1))[0]
        def sample(name,seconds=4):
            pit=value('sc_ticks');frames=value('wm_frames');queued=value('dma_queued');completed=value('completed');under=value('underruns')
            cpu=process_cpu(vm.process);start=time.monotonic();time.sleep(seconds);wall=time.monotonic()-start
            report['measurements'].append(dict(state=name,wall_seconds=wall,pit_ticks=(value('sc_ticks')-pit)&0xffffffff,
                qemu_cpu_seconds=process_cpu(vm.process)-cpu,wm_display_submissions=(value('wm_frames')-frames)&0xffffffff,
                dma_queue_start=queued,dma_queue_end=value('dma_queued'),completed_dma_blocks=(value('completed')-completed)&0xffffffff,
                new_underruns=(value('underruns')-under)&0xffffffff,live_pages=value('pf_used')))
            path=out/(name+'.ppm');vm.hmp('screendump "'+path.as_posix()+'"');report['screenshots'].append(png_from_ppm(path))
        try:
            g.connect();g.command('true');g.command('sleep 1')
            report['windows_before']=windows(vm,symbols);baseline=value('pf_used');sample('desktop-idle')
            latency=[]
            for i in range(12):
                code,output,wall,cpu=g.command('printf M9-LATENCY')
                if code or b'M9-LATENCY' not in output:
                    raise AssertionError('serial shell latency command failed')
                latency.append(dict(wall_seconds=wall,qemu_cpu_seconds=cpu))
            values=sorted(x['wall_seconds'] for x in latency)
            report['serial_command_roundtrip']=dict(samples=latency,median_seconds=statistics.median(values),p95_seconds=values[math_index(len(values))],
                maximum_seconds=max(values),scope='host scheduling + framed ACK + guest command + complete prompt, not PS2 input latency')
            g.command('/APPS/SOUND.SCX /TMP/LONG.WAV &')
            deadline=time.monotonic()+20
            while True:
                try:large=sound_window(vm,symbols);break
                except AssertionError:
                    if time.monotonic()>deadline:raise
                    time.sleep(.05)
            sample('player-active',2)
            vm.hmp('sendkey spc');time.sleep(.25);sample('player-paused-large')
            vm.hmp('sendkey m');time.sleep(.25);small=sound_window(vm,symbols);sample('player-paused-mini')
            if large['pid']!=small['pid'] or large['handle']!=small['handle']:
                raise AssertionError('mini switched process or window')
            vm.hmp('sendkey esc');deadline=time.monotonic()+10
            while any(x['pid']==large['pid'] for x in windows(vm,symbols)):
                if time.monotonic()>deadline:raise AssertionError('player exit timed out')
                time.sleep(.05)
            g.command('true');after=value('pf_used')
            report['cases'].append(dict(name='player-exit-exact-page-reclaim',status='PASS' if baseline==after else 'FAIL',before=baseline,after=after))
            if baseline!=after:raise AssertionError('performance run page leak')
            active=next(x for x in report['measurements'] if x['state']=='player-active')
            report['cases'].append(dict(name='actual-active-DMA-consumption',status='PASS' if active['completed_dma_blocks']>0 else 'FAIL'))
            if not active['completed_dma_blocks']:raise AssertionError('active measurement did not play')
            report['status']='MEASURED_CORRECTNESS_PASS'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error));raise
        finally:
            (out/'measurements.json').write_text(json.dumps(report,indent=2)+'\n');g.close()
    return report


def math_index(count):return max(0,(count*95+99)//100-1)


def main():
    p=argparse.ArgumentParser();p.add_argument('--boot',required=True,type=Path);p.add_argument('--data',required=True,action='append',type=Path)
    p.add_argument('--symbols',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    symbols={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+)\s+[bB]\s+(\w+)$',a.symbols.read_text(),re.M)}
    a.out.mkdir(parents=True,exist_ok=False);matrix=dict(status='RUNNING',disks=[])
    try:
        for i,data in enumerate(a.data,1):
            matrix['disks'].append(run(a.boot,data,a.out/f'disk-{i}',symbols));print(f'disk-{i}: MEASURED_CORRECTNESS_PASS',flush=True)
        matrix['status']='MEASURED_CORRECTNESS_PASS'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error));raise
    finally:
        (a.out/'matrix.json').write_text(json.dumps(matrix,indent=2)+'\n')


if __name__=='__main__':main()
