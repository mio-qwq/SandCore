#!/usr/bin/env python3
"""mio：真实Open点击与排队首字同时到达时，路径不能丢首字。

实际鼠标按下Open后，只读观察按钮动作已登记而外层画纸仍在绘制。
运行中经HMP送h，再只读证明外层还未返回、窗口键队首确有h，
才继续OME/QUEUE.SCB。这样专测“动作帧已有排队首字”，而不是把
字母先于PS/2鼠标包到达误当应用丢字。所有失败设置留历史。
若外层先GETKEY再处理Open，h被丢弃，OME路径就不可能成功打开。
"""
import hashlib,json,re,socket,struct,time
from pathlib import Path
import verify_canvas as v

ROOT=v.ROOT;STAGE=ROOT/'build/m8-canvas';OUT=STAGE/'action-queue'
t=v.t;q=v.q;compiler=v.compiler;theme=v.theme


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def mouse_down_now():
    # 用同一公开QMP协议发送实际按下，但不固定睡120ms；这次需要
    # 只读捕获应用绘制中的位置，不是缩短短点击测试的物理边沿。
    with socket.create_connection(('127.0.0.1',4445),3) as s:
        f=s.makefile('rwb');json.loads(f.readline())
        for value in ({'execute':'qmp_capabilities'},{'execute':'input-send-event','arguments':{'events':[{'type':'btn','data':{'down':True,'button':'left'}}]}}):
            f.write(json.dumps(value).encode()+b'\n');f.flush()
            while True:
                response=json.loads(f.readline())
                assert 'error' not in response,response
                if 'return' in response:break


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs}
    native=(STAGE/'canvas-native.scx').read_bytes();mapping=(STAGE/'canvas-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['canvas']['scx_sha256']
    verifier=sha(Path(__file__));v.OUT=OUT;v.bind();symbols=q.symbols();addresses=theme.native_symbols(mapping)
    image=struct.pack('<8s6I',b'SCB2MIO\0',512,320,2,0,v.PIXELS*4,0x004F494D)+struct.pack('<I',0xFF335577)*v.PIXELS
    def prepare(disk):
        compiler.disk_put(disk,'HOME/CAN.SCX',native);compiler.disk_put(disk,'HOME/QUEUE.SCB',image)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'canvas-queue',prepare)
    try:
        t.open_shell(True);v.idle();base=(t.word(symbols['pf_used']),t.word(symbols['desktop_pages']))
        q.text('run HOME/CAN.SCX\n');w=v.wait(theme.window,'真实G2 Canvas')
        def number(name):return theme.user_word(w,addresses[name])
        v.wait(lambda:number('ui_frames')>=2,'Canvas初始化/真实首帧')
        title=w['h']-w['ch']-1
        t.point(w['x']+1+26,w['y']+title+number('tools_y')+10)
        functions=sorted((int(p[0],16),p[1]) for row in mapping.decode().splitlines()[1:] if len(p:=row.split())>=3 and p[2]!='OBJECT')
        def painting():
            registers=q.hmp('info registers');match=re.search(r'\bEIP=([0-9A-Fa-f]+)',registers)
            eip=int(match[1],16);function=next((name for address,name in reversed(functions) if address<=eip),'')
            return number('ui_action')==1 and number('ui_modal')==0 and t.word(symbols['current'])==w['owner'] and function in ('draw_paper','image_over'),registers
        mouse_down_now();deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            q.hmp('stop');ready,registers=painting()
            if ready:break
            q.hmp('cont');time.sleep(.003)
        else:raise AssertionError('未捕获外层动作已经登记的画纸绘制；不能假称首字专测PASS')
        # 此时停在外层画纸中；恢复运行，再用实际键盘送h。必须再读
        # 到窗口队首h且仍在同一外层绘制，才能证明它先于GETKEY决策。
        q.hmp('cont');q.hmp('sendkey h 40');time.sleep(.015);q.hmp('stop')
        ready,registers=painting();raw=q.memory(symbols['wins'],t.word(symbols['nwins'])*80)
        row=next(raw[i:i+80] for i in range(0,len(raw),80) if struct.unpack_from('<I',raw,i+8)[0]==w['handle'])
        head,tail=row[68],row[69]
        queued=bool(head!=tail and row[52+tail]==ord('h'))
        observation=dict(ui_action=number('ui_action'),ui_modal=number('ui_modal'),current=t.word(symbols['current']),
                         registers=registers,window_queue_head=head,window_queue_tail=tail,window_queue_first=row[52+tail])
        (OUT/'queued-before-action-decision.json').write_text(json.dumps(observation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert ready and queued,'没有只读观察到外层动作帧的窗口队首h，不能把模态打开后输入冒充此测试'
        q.hmp('cont');q.button(False)
        v.wait(lambda:number('ui_modal')==2,'Open按钮真实打开路径框')
        q.text('OME/QUEUE.SCB\n')
        v.wait(lambda:number('canvas_operations')>=1 and number('ui_modal')==0,'包含排队首字的完整路径真实读取')
        filename=v.user_bytes(w,addresses['filename'],64).split(b'\0')[0]
        assert filename==b'hOME/QUEUE.SCB',('首字未进入路径框',filename)
        assert v.user_bytes(w,number('document'),v.BYTES)==image,'成功读取必须是完整指定SCB2'
        q.shot('01-open-click-keeps-queued-first-letter')
        q.key('esc');v.idle()
        v.wait(lambda:t.word(symbols['pf_used'])==base[0]+t.word(symbols['desktop_pages'])-base[1],'排队输入测试完整页回收')
        q.shot('02-reclaimed')
        overflow={name:t.word(symbols[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values()),overflow
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),
            observation=observation,checks=['实际Open动作登记/外层画纸绘制中，窗口队首h只读证据；后续HMP路径与完整SCB2逐字节一致','退出严格回收/键鼠队列零溢出'],overflow=overflow,
            limits='Canvas 1024/100 Open动作帧首字专测，不代替全部组件输入矩阵')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
