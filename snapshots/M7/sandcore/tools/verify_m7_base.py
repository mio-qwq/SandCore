#!/usr/bin/env python3
"""mio：真实 CPU 异常、注册恢复、INT3/TF 与整帧提交的 Windows QEMU 证据。

磁盘始终用副本，进程始终是本脚本创建并 finally 关闭的 Windows QEMU。
HMP sendkey 驱动三环程序，QMP 操作卡片，pmemsave 核对状态与页数。
检查实际目录中的 PASS 文件，避免把窗口上的一句文字当作路径完成证据。
"""
import json
import socket
import shutil
import struct
import subprocess
import time
from pathlib import Path
import verify_m6 as v

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m7-base'
OUT.mkdir(parents=True,exist_ok=True)
v.OUT=OUT

def task_states():
    data=v.memory(v.symbols()['tasks'],8*168)
    return [struct.unpack_from('<I',data,i*168+20)[0] for i in range(8)]

def file_content(disk,path):
    data=disk.read_bytes()
    count=struct.unpack_from('<I',data,12)[0]
    for i in range(count):
        v4=struct.unpack_from('<I',data,20)[0]==4
        name,start,size=struct.unpack_from('<64sII' if v4 else '<32sII',data,512+i*(72 if v4 else 40))
        if name.split(b'\0',1)[0].decode()==path:
            return data[start*512:start*512+size]
    return None

def launch(command):
    v.text('run '+command+'\n')
    time.sleep(.25)

def wait_file(disk,path):
    # ATA PIO 写回 32 个目录扇区可能跨多个抢占周期；等待真实持久化
    # 结果，不用固定 .25s 假设磁盘操作已经完成，15s 到期才报告失败。
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        if file_content(disk,path)==b'PASS': return True
        time.sleep(.2)
    return False

def run():
    try:
        test=socket.create_connection(('127.0.0.1',4444),.3)
    except OSError:
        pass
    else:
        test.close(); raise RuntimeError('4444 已占用；不操作他人的 QEMU')
    data=OUT/'sanddata-test.img'
    shutil.copy2(ROOT/'build/sanddata.img',data)
    command=[r'C:\Program Files\qemu\qemu-system-i386.exe',
             '-drive','format=raw,if=floppy,file=build/sandcore.img',
             '-drive','format=raw,if=ide,file='+data.as_posix(),
             '-display','none','-monitor','tcp:127.0.0.1:4444,server,nowait',
             '-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    (OUT/'qemu-command.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
    proc=subprocess.Popen(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    checks=[]
    try:
        time.sleep(5)
        assert proc.poll() is None
        v.shot('01-boot-expanded'); v.key('ret'); time.sleep(1)
        assert not v.windows()
        loaded=struct.unpack('<I',v.memory(v.symbols()['modules_loaded'],4))[0]
        failed=struct.unpack('<I',v.memory(v.symbols()['modules_failed'],4))[0]
        callback=struct.unpack('<I',v.memory(v.symbols()['scene_callback'],4))[0]
        assert loaded==1 and failed==0 and 0x1800000<=callback<0x1900000,'CORE.SKM 必须实际重定位并安装回调'
        v.icon('bin/shell.scx'); v.click()
        baseline=struct.unpack('<I',v.memory(v.symbols()['pf_used'],4))[0]
        for index,mode,vector in [(2,'div',0),(3,'ud',6),(4,'page',14)]:
            launch('apps/probe.scx '+mode)
            assert task_states().count(3)==1,mode+' 应暂停一个任务'
            tick=struct.unpack('<I',v.memory(v.symbols()['sc_ticks'],4))[0]
            time.sleep(.15)
            assert struct.unpack('<I',v.memory(v.symbols()['sc_ticks'],4))[0]>tick,'暂停时 PIT 必须继续'
            cards=v.memory(v.symbols()['fault_cards'],8*24)
            assert any(struct.unpack_from('<II',cards,i*24)==(1,vector) for i in range(1,8))
            v.shot(f'{index:02}-fault-{mode}')
            v.point(284,47); v.click(); time.sleep(.2)
            assert task_states().count(3)==0 and len(v.windows())==1
            assert struct.unpack('<I',v.memory(v.symbols()['pf_used'],4))[0]==baseline,'关闭异常卡片必须回收资源'
            checks.append(mode+'：真实异常暂停、桌面继续、关闭杀掉并回收：通过')
        launch('apps/probe.scx handler')
        assert wait_file(data,'home/handled.ok'),'处理器必须实际恢复执行'
        v.shot('05-registered-handler')
        v.key('esc'); time.sleep(.2)
        launch('apps/probe.scx debug')
        assert wait_file(data,'home/debug.ok'),'INT3/TF/继续/读取/退出链必须完成'
        v.shot('06-real-breakpoint-step')
        v.key('esc'); time.sleep(.2)
        launch('apps/probe.scx frame')
        first=v.windows()[-1]['digest']; v.shot('07-frame-a'); time.sleep(.2)
        assert v.windows()[-1]['digest']!=first,'连续帧必须变化'
        v.shot('08-frame-b'); v.key('esc'); time.sleep(.2)
        assert struct.unpack('<I',v.memory(v.symbols()['pf_used'],4))[0]==baseline
        launch('apps/probe.scx fs')
        success=wait_file(data,'home/fs.ok'); v.shot('09-filesystem-protection')
        assert success,'目录/规范化/核心保护必须在三环实测'
        v.key('esc'); time.sleep(.2)
        launch('apps/probe.scx font')
        success=wait_file(data,'home/font.ok'); v.shot('10-phoenix-unicode')
        assert success,'字体 API 必须逐位等于用户 SCF，且拒绝非法编码/映射'
        v.key('esc'); time.sleep(.2)
        checks+=['注册处理器修改 EIP 并恢复：通过','真实 INT3/TF 断点、单步、继续、内存读取：通过',
                 '整帧提交、连续帧变化、让出与资源回收：通过',
                 'CORE.SKM 实际装载重定位/场景回调：通过',
                 'v4 子目录/空目录/规范化/重命名/删除/SYS/CORE 三环保护：通过',
                 '统一凤凰字体：UTF-8/Unicode 字形逐位对照/缺字/非法编码/地址/owner：通过']
        (OUT/'results.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS',checks,flush=True)
    finally:
        if proc.poll() is None:
            v.hmp('quit'); proc.wait(timeout=5)

if __name__=='__main__': run()
