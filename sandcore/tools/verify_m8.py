#!/usr/bin/env python3
"""mio：M8 真机协议验证。副本盘、Windows QEMU、真实 HMP/QMP 输入。

不覆盖 M7 已验收证据；所有磁盘写入都落本轮副本。诊断内存只读，
不能把改写进程状态或直接调用内核函数算成界面测试。
"""
import json, shutil, struct, subprocess, time
from pathlib import Path
import verify_m6 as q

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/m8'
OUT.mkdir(parents=True, exist_ok=True)
q.OUT = OUT

def point(x,y):
    # PS/2相对包只有有符号8位delta；大屏不能用一次900px事件定位。
    # 分段走真实鼠标协议，每段读取当前位置，不改内核mx/my。
    sym=q.symbols()
    for _ in range(60):
        px=struct.unpack('<i',q.memory(sym['mx'],4))[0]
        py=struct.unpack('<i',q.memory(sym['my'],4))[0]
        if (px,py)==(x,y):return
        q.move(max(-100,min(100,x-px)),max(-100,min(100,y-py)))
    raise AssertionError(('mouse failed to reach',x,y,px,py))
q.point=point

def launch(vga='std', disk=None):
    data = OUT/'sanddata-test.img'
    staged=ROOT/'build/m8-runtime/sanddata.img'
    shutil.copy2(disk or (staged if staged.exists() else ROOT/'build/sanddata.img'), data)
    command = [r'C:\Program Files\qemu\qemu-system-i386.exe',
               '-drive','format=raw,if=floppy,file=build/sandcore.img',
               '-drive',f'format=raw,if=ide,file={data.as_posix()}',
               '-vga',vga,'-display','none',
               '-monitor','tcp:127.0.0.1:4444,server,nowait',
               '-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    (OUT/f'qemu-{vga}.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
    proc = subprocess.Popen(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(1)
    if proc.poll() is not None: raise RuntimeError('QEMU 未启动，检查端口')
    return proc

def smoke():
    results=[]
    for vga,expected in [('std',(1024,768)),('cirrus',(320,200))]:
        proc=launch(vga)
        try:
            q.key('ret'); time.sleep(1)
            sym=q.symbols()
            size=struct.unpack('<2i',q.memory(sym['gfx_width'],4)+q.memory(sym['gfx_height'],4))
            assert size==expected,(vga,size)
            q.shot(f'01-desktop-{vga}')
            assert not q.windows(),'桌面不得自动开 Shell'
            assert sym['__bss_end']<=0x200000,'静态内存越过保留区'
            results.append(f'{vga}: 实际桌面 {size[0]}x{size[1]}，BSS 边界通过')
        finally:
            try:q.hmp('quit')
            finally:proc.wait(timeout=10)
    (OUT/'foundation.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS',results)

def apps_smoke():
    proc=launch()
    try:
        q.key('ret');time.sleep(1)
        q.point(30,750);q.click();q.shot('02-start-menu')
        # 配置中前7项是日常原生工具；滚动键只导航菜单，ENTER真实EXEC。
        q.key('esc')
        for index,title in [(3,'Settings'),(4,'Lens'),(5,'Canvas'),(6,'Monitor'),(0,'FILES'),(1,'SC STUDIO'),(2,'SC DEBUG')]:
            q.point(30,750);q.click()
            for _ in range(index):q.key('down')
            q.key('ret');time.sleep(2)
            wins=q.windows();assert wins,('no window',title)
            states=struct.unpack('<8i',b''.join(q.memory(q.symbols()['tasks']+i*168+20,4) for i in range(8)))
            active=struct.unpack('<8i',b''.join(q.memory(q.symbols()['fault_cards']+i*24,4) for i in range(8)))
            assert not any(active),('unhandled fault',title,states,active)
            q.shot('03-app-'+title.replace(' ','-'))
            w=wins[-1];q.point(w['x']+w['w']-12,w['y']+8);q.click();time.sleep(.5)
            assert not q.windows(),('windows leaked',title)
    finally:
        try:q.hmp('quit')
        finally:proc.wait(timeout=10)

if __name__=='__main__':
    import sys
    apps_smoke() if 'apps' in sys.argv else smoke()
