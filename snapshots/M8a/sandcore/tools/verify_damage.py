#!/usr/bin/env python3
"""mio：真实三环改变小块/透明度/圆角边、鼠标拖动，PCI整景等价检查。

F经既有窗口恢复调用触发完整合成，几何/画布不变。测试器只读状态、
页表/PCI，不写dirty、不强制调用内核函数。比较排除会真实走秒的任务栏，
光标留在同一位置并参与比较，残影不能靠掩掉光标区域躲过。
"""
import hashlib,json,struct,time,zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-damage';OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT;q=t.q
def wait(test,label,seconds=40):
    limit=time.monotonic()+seconds
    while time.monotonic()<limit:
        value=test()
        if value:return value
        assert 3 not in compiler.task_states(),label+' / 三环异常'
        time.sleep(.1)
    q.shot('timeout');raise AssertionError(label)
def values(w,address):
    pd=t.word(q.symbols()['tasks']+w['owner']*168);raw=b'';left=48
    while left:
        n=min(left,4096-(address&4095));raw+=q.memory(t.physical(pd,address),n);address+=n;left-=n
    return list(struct.unpack('<12I',raw))
def main():
    assert not (OUT/'results.json').exists(),'已有成功证据不能覆盖；重测使用--tag'
    inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
        ('build/sandcore.img','build/kernel.elf','build/kernel.sym','kernel/gfx.c','kernel/gfx.h','kernel/wm.c','kernel/wm.h',
         'kernel/wm_native.inc','kernel/wm_terminal.inc','kernel/desktop.c','kernel/main.c','user/SCAPI.H','user/damageprobe.c')}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/damageprobe.c',(ROOT/'user/damageprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'damage',prepare);disk=OUT/'sanddata-std-128-damage.img';cases=[]
    try:
        t.open_shell(True)
        native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/damageprobe.c','HOME/DAMAGE.SCX',180)
        wait(lambda:len(t.windows())==1 and 2 not in compiler.task_states()[1:],'编译真实退出')
        mapping=compiler.await_file(disk,'HOME/DAMAGE.SCX.map');address=theme.native_symbols(mapping)['damage_probe']
        (OUT/'damage-native.scx').write_bytes(native);(OUT/'damage-native.map').write_bytes(mapping)
        base=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/DAMAGE.SCX\n')
        w=wait(lambda:t.windows()[-1] if len(t.windows())==3 else None,'两层真实窗口')
        wait(lambda:values(w,address)[0]==1,'探针初始化')
        t.point(w['x']+w['w']-90,w['y']+150)
        symbols=q.symbols();bar=32;screen=t.word(symbols['framebuffer'])
        def settled():wait(lambda:t.word(symbols['dirty'])==0 and t.word(symbols['cursor_dirty'])==0,'实际场景与指针提交')
        def picture():
            settled()
            with t.stable_frame():return q.memory(screen,1024*(768-bar)*4)
        def compare(label):
            partial=picture();count=values(w,address)[9];q.key('f')
            wait(lambda:values(w,address)[9]>count,'实际F完整重画');full=picture()
            assert partial==full,label+' / 局部与同一完整场景PCI不同'
            q.shot(label);cases.append(label)
        compare('01-initial-full')
        for key in ('1','2','3','4'):
            previous=values(w,address)[2];before=t.word(symbols['wm_partial_frames'])
            q.key(key);wait(lambda:values(w,address)[2]>previous,'真实改帧'+key);settled()
            assert t.word(symbols['wm_partial_frames'])>before,'必须真的走局部合成'
            compare('02-frame-'+key)
        current=[x for x in t.windows() if x['handle']==w['handle']][0]
        t.point(current['x']+80,current['y']+12);q.button(True)
        wait(lambda:t.word(symbols['dirty'])==0,'开始拖动完整帧')
        for index in range(5):
            old=t.word(symbols['wm_partial_frames']);q.move(12,5)
            wait(lambda:t.word(symbols['wm_partial_frames'])>old,'真实拖动局部路径')
        q.button(False);settled();compare('03-drag-no-trails')
        q.key('esc');wait(lambda:len(t.windows())==1 and 2 not in compiler.task_states()[1:],'退出两层窗口')
        wait(lambda:t.word(symbols['pf_used'])==base[0]+t.word(symbols['desktop_pages'])-base[1],'画布/堆/页表完整回收')
        q.shot('04-reclaimed')
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,native_sha256=hashlib.sha256(native).hexdigest(),cases=cases,
            partial_frames=t.word(symbols['wm_partial_frames']),partial_pixels=t.word(symbols['wm_partial_pixels']),
            overflow={n:t.word(symbols[n]) for n in ('keyboard_overflow','event_overflow')},
            limits='std/128MB/1024/100% Aurora；全部非任务栏PCI含光标逐字节局部=真实F完整重画；15显示组合/Classic/VGA与全部组件继续')
        assert all(v==0 for v in report['overflow'].values())
        assert inputs=={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in inputs}
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
if __name__=='__main__':
    import argparse,re
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag',help='新的独立复测子目录；默认首轮目录保留给阶段矩阵')
    args=parser.parse_args()
    if args.tag:
        assert re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',args.tag),'标签只允许字母/数字/下划线/连字符'
        OUT=OUT/args.tag;OUT.mkdir(parents=True,exist_ok=True)
        t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT
    main()
