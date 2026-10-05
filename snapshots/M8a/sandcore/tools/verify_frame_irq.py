#!/usr/bin/env python3
"""mio：系统内 G2 编译、大帧真实输入、整帧原字节和中途关闭回收。

单独 Windows 无头 QEMU，软盘 if=floppy 加 IDE 测试副本。所有启动、
键入、点击和关闭都走真实 HMP/QMP 输入。--baseline 只记录旧实现；
正式运行还要求复制期间实际 PIT 样本非零，不以源码 STI 推断成功。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme

ROOT=Path(__file__).resolve().parent.parent
BASELINE='--baseline' in sys.argv
OUT=ROOT/'build/m8-frame-irq'/('baseline' if BASELINE else 'irq-enabled')
OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT
q=t.q

def wait(test,label,seconds=40):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        assert 3 not in compiler.task_states(),label+'：三环异常'
        time.sleep(.08)
    q.shot('timeout');raise AssertionError(label)

def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1 and 2 not in compiler.task_states()[1:],'退出与全部任务页回收')

def user_read(w,address,count):
    """逐表验证再合并相邻物理页，不能假设 8MB 用户堆物理连续。"""
    pd=t.word(q.symbols()['tasks']+w['owner']*168)
    blocks=[];last_table=-1;entries=None
    while count:
        index=address>>22
        if index!=last_table:
            pde=t.word(pd+index*4);assert pde&5==5
            entries=struct.unpack('<1024I',q.memory(pde&~4095,4096));last_table=index
        pte=entries[(address>>12)&1023];assert pte&5==5
        physical=(pte&~4095)+(address&4095);n=min(count,4096-(address&4095))
        if blocks and blocks[-1][0]+blocks[-1][1]==physical:blocks[-1][1]+=n
        else:blocks.append([physical,n])
        address+=n;count-=n
    return b''.join(q.memory(p,n) for p,n in blocks)

def main():
    inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('build/sandcore.img','build/kernel.elf','build/kernel.sym','user/SCAPI.H','user/frameirqprobe.c',
             'kernel/task.h','kernel/task.c','kernel/io.h','kernel/wm.h','kernel/wm.c','kernel/wm_native.inc')}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/irqprobe.c',(ROOT/'user/frameirqprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1920\nheight=1080\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'frame-irq',prepare)
    disk=OUT/'sanddata-std-128-frame-irq.img';report={'author':'mio','status':'BASELINE' if BASELINE else 'PASS','inputs_sha256':inputs,'cases':[]}
    try:
        q.key('ret');wait(lambda:t.word(q.symbols()['gfx_width'])==1920,'真实1080p启动')
        t.point(30,1062);t.click()
        for _ in range(7):q.key('down')
        q.key('ret');wait(lambda:len(t.windows())==1,'真实开始菜单启动Shell')
        native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/irqprobe.c','HOME/IRQ.SCX',180);idle()
        map_bytes=compiler.await_file(disk,'HOME/IRQ.SCX.map');symbols=theme.native_symbols(map_bytes)
        (OUT/'irq-native.scx').write_bytes(native);(OUT/'irq-native.map').write_bytes(map_bytes)
        report['native_sha256']=hashlib.sha256(native).hexdigest()
        if not BASELINE:
            reference=json.loads((ROOT/'build/m8-frame-irq/baseline/results.json').read_text(encoding='utf-8'))
            assert report['native_sha256']==reference['native_sha256'],'对照必须使用同一实际G2用户程序'
        for indexed in (False,True):
            baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
            copies_before=t.word(q.symbols()['render_copy_ticks']) if 'render_copy_ticks' in q.symbols() else None
            q.text('run HOME/IRQ.SCX'+(' indexed' if indexed else '')+'\n')
            w=wait(theme.window,'大帧窗口创建')
            wait(lambda:theme.user_word(w,symbols['irq_probe'])==1,'探针完成校验并开始复制')
            typed='sandcorem8liveinput0123456789'
            started=time.monotonic();q.text(typed)
            title=w['h']-w['ch']-1
            t.point(w['x']+64,w['y']+title+180)
            for _ in range(6):t.click()
            q.key('ret');wait(lambda:theme.user_word(w,symbols['irq_probe']+15*4)==1,'Enter真实封存结果')
            values=list(struct.unpack('<16I',user_read(w,symbols['irq_probe'],64)))
            actual=user_read(w,symbols['irq_text'],128).split(b'\0')[0].decode()
            assert values[1]==0,values
            wait(lambda:t.word(q.symbols()['dirty'])==0,'最后完整画面真正提交')
            with t.stable_frame():
                source=user_read(w,values[11],values[4]);canvas=q.memory(w['canvas'],values[4])
                assert source==canvas,'完整用户帧和内核画布存在差异'
            suffix='indexed' if indexed else 'argb'
            q.shot('01-'+suffix+'-sealed')
            before=struct.unpack('<64I',user_read(w,symbols['irq_cpu_before'],256));after=struct.unpack('<64I',user_read(w,symbols['irq_cpu_after'],256))
            delta=[(after[i]-before[i])&0xFFFFFFFF for i in range(3,8)]
            assert delta[0]==sum(delta[1:4]),'CPU分类未封闭'
            copies_after=t.word(q.symbols()['render_copy_ticks']) if 'render_copy_ticks' in q.symbols() else None
            sampled=None if copies_before is None else (copies_after-copies_before)&0xFFFFFFFF
            overflow={name:t.word(q.symbols()[name]) for name in ('keyboard_overflow','event_overflow')}
            row=dict(format=suffix,bytes=values[4],frames=values[3],typed=actual,expected=typed,presses=values[5],releases=values[6],
                     copy_pit_samples=sampled,cpu_delta=dict(zip(('total','idle','kernel','user','graphics'),delta)),
                     queue_overflow=overflow,whole_canvas_sha256=hashlib.sha256(canvas).hexdigest(),host_input_seconds=round(time.monotonic()-started,3))
            report['cases'].append(row)
            if not BASELINE:
                assert actual==typed and values[5]==6 and values[6]==6,row
                # 1.8MB索引复制可能短于10ms且与PIT相位锁定，不能因
                # 没采中就推断IF=0。ARGB大帧必须实际采中；两条路径
                # 都另验证键鼠完整、逐字节画布和活动关闭回收。
                assert sampled is not None
                if not indexed:assert sampled>0,'ARGB复制期间未观测到真实PIT样本'
                assert not any(overflow.values()),overflow
            q.key('esc');idle()
            wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],'正常退出严格回收')
            # 正在不断提交时，X 必须经主循环在整帧完成后关闭；不能
            # 为追求即时关闭而释放复制还在写的物理页或调用者私有页表。
            q.text('run HOME/IRQ.SCX'+(' indexed' if indexed else '')+'\n');w=wait(theme.window,'中途关闭重开探针')
            wait(lambda:theme.user_word(w,symbols['irq_probe'])==1,'关闭前正提交大帧')
            t.point(w['x']+w['w']-12,w['y']+12);t.click();idle()
            wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],'X中途关闭严格回收')
            q.shot('02-'+suffix+'-closed')
            row['normal_and_active_close_reclaimed']=True
        assert inputs=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs},'验证输入被改动'
        report['limits']='std/128MB/1920×1080/100% 本机TCG；7,324,200B ARGB(6.99MiB)和1,831,050B索引(1.75MiB)整帧。PIT 10ms采样，不称逐周期耗时或完整M8已丝滑。'
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)

if __name__=='__main__':main()
