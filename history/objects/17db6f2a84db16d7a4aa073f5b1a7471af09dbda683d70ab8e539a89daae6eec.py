#!/usr/bin/env python3
"""mio：Studio真实Open动作帧里的排队首字与完整文件身份。

只读cdecl调用链确认仍是main外层绘制，排除path_dialog在已经
决定打开后重画背景的假观测。运行中发送真实PS/2键盘，再冻结
观察队首h；不在暂停时注入键，也不修改UI/源码/任务状态。
"""
import hashlib,json,re,socket,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
from verify_lens_action_queue import mouse_down_now
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'action-queue'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'ide-native.scx').read_bytes();mapping=(STAGE/'ide-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['ide']['scx_sha256']
    before=(b'//'+b'A'*646+b'\n')*100;after='//沙核\nint main(void){return 0;}\n'.encode()
    def prepare(disk):
        compiler.disk_put(disk,'HOME/IDE.SCX',native);compiler.disk_put(disk,'HOME/BEFORE.C',before);compiler.disk_put(disk,'HOME/QUEUE.C',after)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping);proc=t.launch('std',128,'studio-queue',prepare)
    functions=sorted((int(p[0],16),p[1]) for row in mapping.decode().splitlines()[1:] if len(p:=row.split())>=3 and p[2]!='OBJECT')
    def function(address):return next((name for start,name in reversed(functions) if start<=address),'')
    try:
        t.open_shell(True);v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/IDE.SCX HOME/BEFORE.C\n');w=seed.wait(theme.window,'实际G2 Studio')
        def number(name):return theme.user_word(w,addresses[name])
        seed.wait(lambda:number('ui_frames')>=2,'源码及实际首帧');assert number('dirty')==number('studio_operations')==0
        def painting():
            registers=q.hmp('info registers');eip=int(re.search(r'\bEIP=([0-9a-fA-F]+)',registers)[1],16)
            ebp=int(re.search(r'\bEBP=([0-9a-fA-F]+)',registers)[1],16)
            frames=[dict(address=eip,function=function(eip))]
            user=t.word(kernel['current'])==w['owner'] and 'CPL=3' in registers
            if user:
                for _ in range(16):
                    if not 0x7E0000<=ebp<=0x7FFFF8:break
                    parent,address=struct.unpack('<2I',v.user_bytes(w,ebp,8));frames.append(dict(address=address,function=function(address)))
                    if parent<=ebp:break
                    ebp=parent
            names=[frame['function'] for frame in frames]
            ready=user and number('ui_action')==1 and number('ui_modal')==number('studio_operations')==0
            ready=bool(ready and 'draw' in names and 'main' in names and not any(name in names for name in ('path_dialog','confirm_discard','ui_confirm','ui_edit_path')))
            return ready,registers,frames
        win=next(row for row in t.windows() if row['handle']==w['handle']);title=win['h']-win['ch']-1
        t.point(win['x']+27,win['y']+title+(38 if number('ui_compact') else 70)+10);mouse_down_now()
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            q.hmp('stop');ready,registers,frames=painting()
            if ready:break
            q.hmp('cont');time.sleep(.003)
        else:raise AssertionError('未取得主循环外层Open动作绘制；不冒称排队输入PASS')
        q.hmp('cont');q.hmp('sendkey h 40');time.sleep(.015);q.hmp('stop');ready,registers,frames=painting()
        raw=q.memory(kernel['wins'],t.word(kernel['nwins'])*80)
        row=next(raw[i:i+80] for i in range(0,len(raw),80) if struct.unpack_from('<I',raw,i+8)[0]==w['handle'])
        head,tail=row[68],row[69];observation=dict(ui_action=number('ui_action'),ui_modal=number('ui_modal'),studio_operations=number('studio_operations'),
            current=t.word(kernel['current']),registers=registers,cdecl_frames=frames,window_queue_head=head,window_queue_tail=tail,window_queue_first=row[52+tail])
        (OUT/'queued-before-action-decision.json').write_text(json.dumps(observation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert ready and head!=tail and row[52+tail]==104,'首字尚未处于外层动作决策前的真实窗口队列'
        q.hmp('cont');q.button(False);seed.wait(lambda:number('ui_modal')==2,'Open实际路径框');q.text('OME/QUEUE.C\n')
        seed.wait(lambda:number('studio_operations')>=1 and number('ui_modal')==0,'排队首字完整提交')
        assert v.user_bytes(w,addresses['filename'],64).split(b'\0')[0]==b'hOME/QUEUE.C'
        assert v.user_bytes(w,addresses['source'],len(after)+1)==after+b'\0' and number('used')==len(after)
        expected=[0]+[i+1 for i,c in enumerate(after) if c==10];assert number('line_count')==len(expected)
        assert list(struct.unpack('<'+str(len(expected))+'I',v.user_bytes(w,addresses['line_starts'],len(expected)*4)))==expected
        old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'新源码实际画面');q.shot('01-real-open-keeps-queued-first-letter')
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'排队测试全部页回收')
        q.shot('02-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            observation=observation,overflow=overflow,checks=['只读真实cdecl链证明main外层绘制、Open动作已登记/模态未开/队首h','实际完整hOME/QUEUE.C、源码及行索引一致，严格回收/队列零溢出'],
            limits='Studio草稿1024/100真实动作帧首字专测，不代替其它组件或全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
