#!/usr/bin/env python3
"""mio：真实键鼠到已提交画面的宿主延迟与客体合成统计。

QMP发真实PS/2移动，pmemsave只读最后已提交光标位置；主循环在
gfx_swap后记位置，因而不能把ISR收到事件冒充“已经显示”。HMP观测
自身有开销，报告轮询下限，结果不冒称精确硬件输入到光子延迟。
"""
import json
import hashlib
import socket
import statistics
import struct
import sys
import time
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler

ROOT=Path(__file__).resolve().parent.parent
TAG=sys.argv[1] if len(sys.argv)>1 else 'current'
WIDTH=int(sys.argv[2]) if len(sys.argv)>2 else 1024
HEIGHT=1080 if WIDTH==1920 else 768
OUT=ROOT/'build/m8-latency'/TAG;OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=OUT;q=t.q

def event(events):
    with socket.create_connection(('127.0.0.1',4445),3) as connection:
        connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        file=connection.makefile('rwb');json.loads(file.readline())
        for request in ({'execute':'qmp_capabilities'}, {'execute':'input-send-event','arguments':{'events':events}}):
            file.write(json.dumps(request).encode()+b'\n');file.flush()
            while True:
                reply=json.loads(file.readline())
                if 'error' in reply:raise RuntimeError(reply)
                if 'return' in reply:break

def main():
    def prepare(disk):
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={WIDTH}\nheight={HEIGHT}\nscale=100\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'latency',prepare);report={'author':'mio','tag':TAG,'width':WIDTH,'height':HEIGHT,'scale':100,'samples':{}}
    report['sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                      for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym')}
    test_disk=OUT/'sanddata-std-128-latency.img'
    report['user_sha256']={name:hashlib.sha256(compiler.file_content(test_disk,name)).hexdigest()
                          for name in ('BIN/SHELL.SCX','APPS/SETTINGS.SCX')}
    try:
        q.key('ret');time.sleep(2);symbols=q.symbols()
        def stats():
            return {name:t.word(symbols[name]) for name in ('wm_frames','wm_ticks_total','wm_ticks_max','pf_used')}
        def position():return t.word(symbols['wm_cursor_x']),t.word(symbols['wm_cursor_y'])
        floor=[]
        for _ in range(5):
            start=time.perf_counter();position();floor.append((time.perf_counter()-start)*1000)
        report['poll_floor_ms']=round(statistics.median(floor),2)
        def samples(name):
            latencies=[];initial=stats()
            for index in range(24):
                x=t.word(symbols['mx']);y=t.word(symbols['my']);delta=6 if index%2==0 else -6
                started=time.perf_counter()
                event([{'type':'rel','data':{'axis':'x','value':delta}}])
                deadline=time.perf_counter()+3
                while position()!=(x+delta,y):
                    if time.perf_counter()>deadline:raise AssertionError(('cursor stalled',name,index))
                latencies.append((time.perf_counter()-started)*1000)
            ordered=sorted(latencies);final=stats()
            report['samples'][name]=dict(median_ms=round(statistics.median(latencies),2),p95_ms=round(ordered[int(len(ordered)*.95)-1],2),
                max_ms=round(max(latencies),2),host_samples_ms=[round(n,2) for n in latencies],
                frames=final['wm_frames']-initial['wm_frames'],composition_ticks=final['wm_ticks_total']-initial['wm_ticks_total'],max_frame_tick=final['wm_ticks_max'])
            (OUT/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
            q.shot(name)
        samples('01-desktop-pointer')
        # 开始菜单启动真实新版Shell。此处仍使用同一Windows双盘VM。
        t.point(30,HEIGHT-18);t.click()
        for _ in range(7):q.key('down')
        q.key('ret');time.sleep(1);assert len(t.windows())==1
        samples('02-shell-pointer')
        window=t.windows()[0];t.point(window['x']+70,window['y']+12)
        event([{'type':'btn','data':{'button':'left','down':True}}]);time.sleep(.2)
        samples('03-shell-drag')
        event([{'type':'btn','data':{'button':'left','down':False}}]);time.sleep(.2)
        q.text('run APPS/SETTINGS.SCX\n')
        deadline=time.monotonic()+40
        while len(t.windows())!=2 and time.monotonic()<deadline:time.sleep(.2)
        assert len(t.windows())==2
        time.sleep(1)
        before=stats();time.sleep(3);after=stats()
        report['idle_settings_frames_3s']=after['wm_frames']-before['wm_frames']
        report['idle_settings_composition_ticks_3s']=after['wm_ticks_total']-before['wm_ticks_total']
        samples('04-settings-pointer')
        report['queue_overflow']={name:t.word(symbols[name]) for name in ('keyboard_overflow','event_overflow')}
        assert all(value==0 for value in report['queue_overflow'].values())
        report['status']='PASS';report['limits']='本次指定分辨率/100% std/128MB Windows QEMU TCG本机；宿主轮询包含HMP开销，10ms客体PIT刻度；不代替全部多缩放/所有组件矩阵'
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)

if __name__=='__main__':main()
