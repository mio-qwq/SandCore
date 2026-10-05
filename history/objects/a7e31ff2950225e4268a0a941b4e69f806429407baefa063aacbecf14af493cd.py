#!/usr/bin/env python3
"""mio：只读取证矩阵逐项布局采样的中间态，不修改应用来造PASS。

保持相同f230d4be/G2/1080输入。先取得真正完整提交且rows>0的
布局；再按原测试三个独立读取构造下箭头坐标，等待真实draw的
rows清零期间。记录取样/EIP/前一完整布局，不将它当应用故障。
"""
import hashlib,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'layout-observer'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'ide-native.scx').read_bytes();mapping=(STAGE/'ide-native.map').read_bytes();addresses=theme.native_symbols(mapping)
    assert hashlib.sha256(native).hexdigest()==core['native']['ide']['scx_sha256']
    def prepare(disk):
        compiler.disk_put(disk,'HOME/IDE.SCX',native);compiler.disk_put(disk,'HOME/VIEW.C',('//沙核abc\n'*1200).encode())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1920\nheight=1080\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'studio-layout-observer',prepare)
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'真正1080桌面')
        t.point(30,1064);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/IDE.SCX HOME/VIEW.C\n');w=seed.wait(theme.window,'同一G2 Studio')
        def number(name):return theme.user_word(w,addresses[name])
        seed.wait(lambda:number('ui_frames')>=2,'真实初帧');deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            with t.stable_frame():
                layout={name:number(name) for name in ('editor_y','editor_rows','editor_right','ui_width','ui_height')}
                win=next(row for row in t.windows() if row['handle']==w['handle'])
                if layout['editor_rows']>0 and (layout['ui_width'],layout['ui_height'])==(win['cw'],win['ch']):
                    private=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4)
                    if private==q.memory(win['canvas'],len(private)):break
            time.sleep(.05)
        else:raise AssertionError('没有真正完整帧，不能用于比较')
        down_top=layout['editor_y']+4+layout['editor_rows']*20-16
        deadline=time.monotonic()+30;samples=[]
        while time.monotonic()<deadline:
            raw=dict(right=number('editor_right'),y=number('editor_y'),rows=number('editor_rows'))
            raw['constructed_y']=raw['y']+raw['rows']*20-2
            samples.append(raw)
            if raw['rows']==0:
                with t.stable_frame():
                    raw['paused_rows']=number('editor_rows');raw['ui_frames']=number('ui_frames');raw['registers']=q.hmp('info registers')
                assert raw['constructed_y']<down_top,'观察没有改变下箭头位置'
                break
        else:raise AssertionError('本轮没有实际观察到清零，不将推测称取证成功')
        q.shot('01-real-layout-observer');q.key('esc');v.idle()
        seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'观察程序全部页回收')
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',scope='矩阵输入坐标的只读中间态取证',inputs_sha256=inputs,verifier_sha256=verifier,
            full_committed_layout=layout,actual_down_button_top=down_top,observation=raw,sample_count=len(samples),
            limits='实际rows=0只表示draw生产中的私有变量，不是已提交布局；证明独立读取会构造错误坐标，不代替新版矩阵十九组合或全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
