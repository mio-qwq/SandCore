#!/usr/bin/env python3
"""mio：整版优化前后分离宿主CPU、客体CPU和合成成本。

测试始终使用自己的双盘副本和明确指定的内核符号。鼠标输入来自
持续QMP真实相对事件；不写客体状态，不用伪造帧或进度获得结果。
CPU时间取Windows进程计费，客体分类取原CPUINFO旁表的PIT采样。
报告只叫MEASURED：成本数据不自动成为整个M8功能或流畅度PASS。
"""
import argparse
import ctypes
import hashlib
import json
import shutil
import socket
import struct
import subprocess
import time
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler

ROOT = Path(__file__).resolve().parent.parent
q = t.q
OUT = None
SYM = None
PROC = None


def process_cpu():
    # FILETIME以100ns为单位。单核100%表示一秒墙钟消耗一秒进程CPU，
    # 不能把它误报为宿主所有核心均饱和；两边统计口径分别保存。
    values = [ctypes.c_ulonglong() for _ in range(4)]
    api = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    api.argtypes = [ctypes.c_void_p]+[ctypes.c_void_p]*4
    if not api(int(PROC._handle), *(ctypes.byref(value) for value in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return (values[2].value+values[3].value)/10_000_000


def counters():
    names = ('sc_ticks','cpu_total','cpu_idle','cpu_kernel','cpu_user','cpu_graphics',
             'wm_frames','wm_ticks_total','wm_ticks_max','wm_partial_frames',
             'render_copy_ticks','keyboard_overflow','event_overflow','pf_used')
    result = {}
    q.hmp('stop')
    try:
        # 每次暂停取同代旁表，不把前后两个tick拼成无法相加的分类。
        for name in names:
            if name in SYM:
                result[name] = t.word(SYM[name])
    finally:
        q.hmp('cont')
    return result


class InputStream:
    def __enter__(self):
        self.socket = socket.create_connection(('127.0.0.1',4445),3)
        self.socket.settimeout(3)
        self.file = self.socket.makefile('rwb')
        json.loads(self.file.readline())
        self.command({'execute':'qmp_capabilities'})
        return self

    def command(self,value):
        self.file.write(json.dumps(value).encode()+b'\n');self.file.flush()
        while True:
            answer=json.loads(self.file.readline())
            if 'error' in answer:raise RuntimeError(answer)
            if 'return' in answer:return answer['return']

    def move(self,dx,dy):
        self.command({'execute':'input-send-event','arguments':{'events':[
            {'type':'rel','data':{'axis':'x','value':dx}},
            {'type':'rel','data':{'axis':'y','value':dy}}]}})

    def __exit__(self,*args):
        self.file.close();self.socket.close()


def measure(label,seconds,move=False):
    time.sleep(.5)
    first = counters()
    cpu = process_cpu();start=time.monotonic();events=0
    with InputStream() as stream:
        while time.monotonic()-start<seconds:
            elapsed=time.monotonic()-start
            if move:
                # 在桌面下方留白/客户区内来回移动，不点按钮、不拖窗。
                # 每秒60组事件，并保存实际发出数量而非把目标频率当事实。
                phase=int(elapsed*2)&1
                stream.move(8 if phase==0 else -8,2 if phase==0 else -2)
                events+=1
            target=start+(events/60 if move else elapsed+.02)
            time.sleep(max(0,min(.02,target-time.monotonic())))
    wall=time.monotonic()-start;cost=process_cpu()-cpu
    last=counters()
    delta={name:(last[name]-value)&0xffffffff for name,value in first.items()}
    total=delta['cpu_total']
    assert total==delta['cpu_idle']+delta['cpu_kernel']+delta['cpu_user'],delta
    result=dict(label=label,wall_seconds=round(wall,4),host_cpu_seconds=round(cost,4),
                host_one_core_percent=round(100*cost/wall,2),mouse_event_groups=events,
                guest_delta=delta,guest_first=first,guest_last=last,
                guest_busy_percent=round(100*(total-delta['cpu_idle'])/total,2) if total else None,
                wm_frames_per_host_second=round(delta['wm_frames']/wall,3))
    q.shot(label)
    print(json.dumps(result,ensure_ascii=True),flush=True)
    (OUT/(label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return result


def wait(test,label,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        assert 3 not in compiler.task_states(),label+' / ring3 fault'
        time.sleep(.1)
    q.shot('timeout-'+label);raise AssertionError(label)


def run():
    global OUT,SYM,PROC
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True)
    parser.add_argument('--inputs',default='build')
    parser.add_argument('--seconds',type=float,default=6)
    args=parser.parse_args()
    OUT=ROOT/args.out;OUT.mkdir(parents=True,exist_ok=False)
    source=ROOT/args.inputs
    t.OUT=q.OUT=compiler.OUT=OUT
    SYM=t.table_symbols(source/'kernel.sym');q.symbols=lambda:SYM
    for port in (4444,4445):
        try:connection=socket.create_connection(('127.0.0.1',port),.2)
        except OSError:continue
        connection.close();raise RuntimeError(str(port)+' is occupied; leave unrelated VM alone')
    for name in ('sandcore.img','sanddata.img','kernel.sym','kernel.elf'):
        shutil.copy2(source/name,OUT/name)
    compiler.disk_put(OUT/'sanddata.img','SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
    compiler.disk_put(OUT/'sanddata.img','SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    command=[r'C:\Program Files\qemu\qemu-system-i386.exe','-accel','tcg','-m','128','-vga','std',
             '-drive','format=raw,if=floppy,file='+str(OUT/'sandcore.img'),
             '-drive','format=raw,if=ide,file='+str(OUT/'sanddata.img'),'-display','none',
             '-monitor','tcp:127.0.0.1:4444,server,nowait','-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    report=dict(author='mio',status='RUNNING',qemu_command=command,samples=[],
                inputs={name:hashlib.sha256((source/name).read_bytes()).hexdigest()
                        for name in ('sandcore.img','sanddata.img','kernel.sym','kernel.elf')},
                limits='1024x768/100%/Aurora/TCG/128MB; PIT 100Hz snapshots and read-only HMP observation; host percent is one core')
    (OUT/'qemu-command.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
    PROC=subprocess.Popen(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        wait(lambda:t.word(SYM['boot_stage'])==2,'welcome-ready')
        q.key('ret');wait(lambda:t.word(SYM['boot_stage'])==3,'desktop-ready');time.sleep(1)
        t.point(800,680)
        report['samples'].append(measure('01-desktop-idle',args.seconds))
        report['samples'].append(measure('02-desktop-mouse-60hz',args.seconds,True))
        t.open_shell(True)
        wait(lambda:len(t.windows())==1,'shell')
        report['samples'].append(measure('03-shell-idle',args.seconds))
        for index,name in enumerate(('race','mines','world')):
            q.text('run LEGACY/'+name+'.scx\n')
            window=wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,name)
            time.sleep(2)
            t.point(window['x']+window['w']//2,window['y']+window['h']//2)
            report['samples'].append(measure(f'{index+4:02}-legacy-'+name,args.seconds))
            if name=='race':
                report['samples'].append(measure('04b-legacy-race-mouse-60hz',args.seconds,True))
            # 真实点击窗口X结束任务，避免ESC是否被应用消费干扰资源回收观察。
            t.point(window['x']+window['w']-12,window['y']+12);t.click()
            wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1,'reclaim-'+name)
        report['status']='MEASURED'
        (OUT/'measurements.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    except Exception as error:
        report.update(status='FAIL',error=repr(error))
        q.shot('failure')
        (OUT/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if PROC.poll() is None:q.hmp('quit');PROC.wait(timeout=10)


if __name__=='__main__':run()
