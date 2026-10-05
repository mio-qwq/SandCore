#!/usr/bin/env python3
"""mio：Memory按钮动作帧的真实排队首字，不能在GETKEY丢失。

只读cdecl链限定main外层draw，排除address_dialog背景重画；
真键盘4在模态打开之前已处于窗口队首，后续00010必须组成
400010。缓冲与目标实际128B逐字节核对，不从宿主改任何状态。
"""
import hashlib,json,re,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
from verify_lens_action_queue import mouse_down_now
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-debugger-draft';OUT=STAGE/'action-queue'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'debugger-native.scx').read_bytes();mapping=(STAGE/'debugger-native.map').read_bytes()
    target=(STAGE/'target-native.scx').read_bytes();assert hashlib.sha256(native).hexdigest()==core['native']['debugger']['scx_sha256']
    assert hashlib.sha256(mapping).hexdigest()==core['native']['debugger']['map_sha256'] and hashlib.sha256(target).hexdigest()==core['native']['target']['scx_sha256']
    def prepare(disk):
        compiler.disk_put(disk,'HOME/DBG.SCX',native);compiler.disk_put(disk,'HOME/TARGET.SCX',target)
        compiler.disk_put(disk,'HOME/TARGET.SCX.map',(STAGE/'target-native.map').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1920\nheight=1080\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping);proc=t.launch('std',128,'debugger-queue',prepare);w=None
    functions=sorted((int(p[0],16),p[1]) for row in mapping.decode().splitlines()[1:] if len(p:=row.split())>=3 and p[2]!='OBJECT')
    def function(address):return next((name for start,name in reversed(functions) if start<=address),'')
    try:
        # 基础open_shell的750坐标只适合1024×768。在1080p那是
        # 桌面Debug图标，不能把误开的旧应用当本批首字证据。
        # 从真实1080任务栏打开Shell，再登记正确任务/窗口基线。
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际1080桌面')
        t.point(30,1064);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'真实1080开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/DBG.SCX HOME/TARGET.SCX\n');w=seed.wait(theme.window,'真实G2调试器')
        def number(name):return theme.user_word(w,addresses[name])
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        seed.wait(lambda:number('ui_frames')>=2 and number('paused')==number('have_context')==1,'真实初始目标/帧')
        win=current();title=win['h']-win['ch']-1;t.point(win['x']+win['w']-36,win['y']+title//2);t.click()
        seed.wait(lambda:current()['w']==1920 and number('ui_width')==1918,'真实最大化让动作后绘制足够取证',180)
        previous=number('ui_frames');seed.wait(lambda:number('ui_frames')>previous and t.word(kernel['dirty'])==0,'最大化整帧',180)
        before_operations=number('debug_operations')
        def painting():
            registers=q.hmp('info registers');eip=int(re.search(r'\bEIP=([0-9a-fA-F]+)',registers)[1],16)
            ebp=int(re.search(r'\bEBP=([0-9a-fA-F]+)',registers)[1],16)
            frames=[dict(address=eip,function=function(eip))];user=t.word(kernel['current'])==w['owner'] and 'CPL=3' in registers
            if user:
                for _ in range(16):
                    if not 0x7E0000<=ebp<=0x7FFFF8:break
                    parent,address=struct.unpack('<2I',v.user_bytes(w,ebp,8));frames.append(dict(address=address,function=function(address)))
                    if parent<=ebp:break
                    ebp=parent
            names=[frame['function'] for frame in frames]
            ready=user and number('ui_action')==7 and number('ui_modal')==0 and number('debug_operations')==before_operations
            ready=bool(ready and 'draw' in names and 'main' in names and not any(name in names for name in ('address_dialog','ui_edit_text','ui_edit_value')))
            return ready,registers,frames
        # 1920/100九按钮单行。Memory位于前六项累计宽度之后。
        x=16+sum(len(label)*8+28 for label in ('Step','Run','Pause','Restart','Break','Unbreak'))+34
        win=current();title=win['h']-win['ch']-1;t.point(win['x']+1+x,win['y']+title+80)
        # 同一次真实输入批次包含点击和首字。先暂停是为了让两项
        # 宿主输入都已排入设备，再正常恢复IRQ/应用；不写窗口
        # 队列或任务状态。取证仍要求main外层draw里已经登记
        # Memory，并且窗口队首确实是4，才可称首字保留。
        q.hmp('stop');mouse_down_now();q.hmp('sendkey 4 40');q.hmp('cont')
        attempts=[];deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            q.hmp('stop');ready,registers,frames=painting()
            raw=q.memory(kernel['wins'],t.word(kernel['nwins'])*80)
            row=next(raw[i:i+80] for i in range(0,len(raw),80) if struct.unpack_from('<I',raw,i+8)[0]==w['handle'])
            head,tail=row[68],row[69]
            attempts.append(dict(ready=ready,ui_action=number('ui_action'),ui_modal=number('ui_modal'),
                debug_operations=number('debug_operations'),cdecl_frames=frames,registers=registers,head=head,tail=tail,first=row[52+tail]))
            if ready and head!=tail and row[52+tail]==52:break
            q.hmp('cont');time.sleep(.003)
        else:raise AssertionError('未取到main外层Memory动作绘制，不称排队首字PASS')
        (OUT/'capture-attempts.json').write_text(json.dumps(attempts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raw=q.memory(kernel['wins'],t.word(kernel['nwins'])*80)
        row=next(raw[i:i+80] for i in range(0,len(raw),80) if struct.unpack_from('<I',raw,i+8)[0]==w['handle'])
        head,tail=row[68],row[69];observation=dict(ui_action=number('ui_action'),ui_modal=number('ui_modal'),debug_operations=number('debug_operations'),
            current=t.word(kernel['current']),registers=registers,cdecl_frames=frames,window_queue_head=head,window_queue_tail=tail,window_queue_first=row[52+tail])
        (OUT/'queued-before-action-decision.json').write_text(json.dumps(observation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert ready and head!=tail and row[52+tail]==52,'窗口队首4尚未处于外层决策前，不能冒称通过'
        q.hmp('cont');q.button(False);seed.wait(lambda:number('ui_modal')==1,'实际Memory输入框');q.text('00010\n')
        seed.wait(lambda:number('ui_modal')==0 and number('debug_operations')>before_operations,'真实地址操作完成')
        assert number('memory_address')==0x400010 and number('memory_mode')==number('memory_valid')==1
        assert v.user_bytes(w,addresses['memory_bytes'],128)==v.user_bytes(dict(owner=number('target_pid')),0x400010,128)
        seed.wait(lambda:number('ui_frames')>previous and t.word(kernel['dirty'])==0,'实际地址结果完整帧',180);q.shot('01-real-memory-keeps-queued-first-digit')
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'调试器与拥有目标全部页严格回收')
        q.shot('02-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            observation=observation,overflow=overflow,checks=['实际main外层Memory已登记/模态未开/窗口队首4','实际400010完整地址及目标128B一致，严格回收/队列零溢出'],
            limits='独立调试器1920/100真实动作帧地址首字，不代替其它组件/布局/全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:
            if 'attempts' in locals():(OUT/'capture-attempts.json').write_text(json.dumps(attempts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
