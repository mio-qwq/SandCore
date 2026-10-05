#!/usr/bin/env python3
"""mio：真实Open点击与排队首字同时到达时，路径不能丢首字。

坐标先由实际鼠标移动到按钮，再在同一QMP输入批次发送左键按下
和字母h，后续OME/QUEUE.SCB经HMP输入。全部输入来自真实PS/2
设备，不写客体内存/寄存器、不调用内部函数。暂停的QEMU拒绝
input-send-event，首轮验证器失败保留历史，改为运行中真实输入批。
若外层先GETKEY再处理Open，h被丢弃，OME路径就不可能成功打开。
"""
import hashlib,json,struct
from pathlib import Path
import verify_canvas as v

ROOT=v.ROOT;STAGE=ROOT/'build/m8-canvas';OUT=STAGE/'action-queue'
t=v.t;q=v.q;compiler=v.compiler;theme=v.theme


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


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
        # 同一次QMP请求送入点击及字符两个设备事件；不是逐次请求
        # 等路径框出现后再送首字。不能在暂停VM中发送input-send-event，
        # 也不用改ui_action/ui_modal/键盘队列来伪造首字保留。
        q.qmp([{'type':'btn','data':{'down':True,'button':'left'}},
               {'type':'key','data':{'down':True,'key':{'type':'qcode','data':'h'}}},
               {'type':'key','data':{'down':False,'key':{'type':'qcode','data':'h'}}},
               {'type':'btn','data':{'down':False,'button':'left'}}])
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
            checks=['同一次QMP送按钮按下沿与首字，后续路径经HMP；完整指定SCB2逐字节一致','退出严格回收/键鼠队列零溢出'],overflow=overflow,
            limits='Canvas 1024/100 Open动作帧首字专测，不代替全部组件输入矩阵')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
