#!/usr/bin/env python3
"""mio：同一G2产物在新旧核的吞吐、旧YIELD、双忙任务与回收对照。

--baseline只记录，不断言优化。每阶段目录禁止覆盖报告；原生产物
仅在参考阶段通过实际键盘命令生成，后阶段复制完全相同的SCX。
所有结果只读，实际按键/点击/关闭和全页回收验证保持真实输入。
"""
import hashlib,json,statistics,struct,sys,time,zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme
ROOT=Path(__file__).resolve().parent.parent
BASELINE='--baseline' in sys.argv
OUT=ROOT/'build/m8-idle-yield'/('baseline' if BASELINE else 'optimized')
if '--tag' in sys.argv:
    tag=sys.argv[sys.argv.index('--tag')+1]
    assert tag and all(c.isalnum() or c in '-_' for c in tag),'标签仅字母数字/连字符/下划线'
    assert tag not in ('baseline','optimized'),'重测使用新标签，不覆盖阶段'
    OUT=ROOT/'build/m8-idle-yield'/tag
OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT;q=t.q

def wait(test,label,seconds=60):
    limit=time.monotonic()+seconds
    while time.monotonic()<limit:
        value=test()
        if value:return value
        assert 3 not in compiler.task_states(),label+' / 三环异常'
        time.sleep(.12)
    q.shot('timeout');raise AssertionError(label)
def values(w,address):
    pd=t.word(q.symbols()['tasks']+w['owner']*168)
    # 探针旁表即便跨页也逐表读，不假定下一用户页的物理帧连续。
    # 160B布局固定，但链接器之后移动符号不能让测试读到别人的页。
    raw=b'';count=160
    while count:
        n=min(count,4096-(address&4095));raw+=q.memory(t.physical(pd,address),n)
        address+=n;count-=n
    return list(struct.unpack('<40I',raw))
def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1 and 2 not in compiler.task_states()[1:],'实际关闭与僵尸回收')
def main():
    assert not (OUT/'results.json').exists(),'已有阶段结果不得覆盖'
    inputs={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in
        ('build/sandcore.img','build/kernel.elf','build/kernel.sym','kernel/task.h','kernel/task.c','kernel/main.c','user/SCAPI.H','user/schedprobe.c')}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as z:g2=z.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/schedprobe.c',(ROOT/'user/schedprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        if not BASELINE:
            for name in ('sched-native.scx','sched-native.map'):
                compiler.disk_put(disk,'HOME/SCHED.SCX'+('.map' if name.endswith('.map') else ''),(OUT.parent/'baseline'/name).read_bytes())
    proc=t.launch('std',128,'idle-yield',prepare);disk=OUT/'sanddata-std-128-idle-yield.img'
    try:
        t.open_shell(True)
        if BASELINE:
            native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/schedprobe.c','HOME/SCHED.SCX',180);idle()
            mapping=compiler.await_file(disk,'HOME/SCHED.SCX.map')
        else:
            native=(OUT.parent/'baseline/sched-native.scx').read_bytes();mapping=(OUT.parent/'baseline/sched-native.map').read_bytes()
        (OUT/'sched-native.scx').write_bytes(native);(OUT/'sched-native.map').write_bytes(mapping)
        symbols=theme.native_symbols(mapping);address=symbols['sched_probe']
        resources=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/SCHED.SCX\n');w=wait(theme.window,'真实计算探针')
        rows=wait(lambda:values(w,address) if values(w,address)[0]==2 else None,'三次旧YIELD与三次计数计算完成',100)
        assert rows[1]==0,rows
        samples=[dict(zip(('total','idle','kernel','user','batches'),rows[8+i*5:13+i*5])) for i in range(6)]
        for row in samples:assert row['total']==row['idle']+row['kernel']+row['user']
        q.key('a');wait(lambda:values(w,address)[6]==1,'真实A键')
        t.point(w['x']+100,w['y']+180);t.click();wait(lambda:values(w,address)[7]==1,'真实客户区点击')
        q.shot('01-same-native-compute');q.key('esc');idle()
        wait(lambda:t.word(q.symbols()['pf_used'])==resources[0]+t.word(q.symbols()['desktop_pages'])-resources[1],'单探针完整回收')
        q.text('run HOME/SCHED.SCX pair\n')
        pair=wait(lambda:t.windows()[1:] if len(t.windows())==3 else None,'真实两个忙任务')
        parent=pair[0];child=pair[1]
        wait(lambda:values(parent,address)[0]==4,'双任务父计算完成')
        child_before=values(child,address)[4];time.sleep(.4);child_after=values(child,address)[4]
        assert child_after>child_before,'另一个忙任务必须继续前进'
        pair_result=values(parent,address);assert pair_result[1]==0 and pair_result[12]>0,pair_result
        q.shot('02-two-busy-tasks')
        # X分别关闭，不假设当前焦点/ESC会退出后台程序。内核任务
        # 槽与所有私有地址空间必须随后真的回到原基线。
        for window in (child,parent):
            t.point(window['x']+window['w']-12,window['y']+12);t.click()
            wait(lambda:all(x['handle']!=window['handle'] for x in t.windows()),'鼠标X关闭实际任务')
        idle();wait(lambda:t.word(q.symbols()['pf_used'])==resources[0]+t.word(q.symbols()['desktop_pages'])-resources[1],'双任务全页回收')
        q.shot('03-all-reclaimed')
        idle_pct=statistics.median(row['idle']*100/row['total'] for row in samples[:3])
        user_pct=statistics.median(row['user']*100/row['total'] for row in samples[3:])
        batches=statistics.median(row['batches']*200/row['total'] for row in samples[3:])
        report=dict(author='mio',status='BASELINE' if BASELINE else 'PASS',inputs_sha256=inputs,
            native_sha256=hashlib.sha256(native).hexdigest(),samples=samples,idle_percent=idle_pct,user_percent=user_pct,
            batches_per_200_ticks=batches,pair_parent_batches=pair_result[12],pair_child_progress=child_after-child_before,
            kernel_idle_dispatches=t.word(q.symbols()['idle_dispatches']) if 'idle_dispatches' in q.symbols() else None,
            overflow={n:t.word(q.symbols()[n]) for n in ('keyboard_overflow','event_overflow')},
            limits='std/128MB/1024x768/100% TCG；相同SCX三次200tick计算中位数，PIT状态采样；不代表1080p全场景FPS或全部M8完成')
        assert idle_pct>70 and all(v==0 for v in report['overflow'].values()),report
        if not BASELINE:
            old=json.loads((OUT.parent/'baseline/results.json').read_text(encoding='utf-8'))
            assert old['native_sha256']==report['native_sha256']
            assert report['kernel_idle_dispatches']>0 and user_pct>75
            assert batches>old['batches_per_200_ticks']*1.35,(old,report)
            report['throughput_ratio']=batches/old['batches_per_200_ticks']
        assert inputs=={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in inputs}
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)

if __name__=='__main__':main()
