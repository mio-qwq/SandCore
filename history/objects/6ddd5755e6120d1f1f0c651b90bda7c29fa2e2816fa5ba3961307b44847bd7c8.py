#!/usr/bin/env python3
"""mio：实际目标自然退出、普通EXEC复用PID后的身份与独占回收。

先真正Continue，让目标收到Esc返回23，再由Shell实际运行另一
SCX复用空槽。所有变化来自公开键鼠/系统调用；只读任务代数与
旧88B快照，不修改内核槽或伪造退出码，旧DEBUG owner仍由核保护。
"""
import hashlib,json,struct,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-debugger-draft';OUT=STAGE/'lifecycle'
ZERO='''/* mio：普通Shell的独立任务，原目标结束后真实复用PID。
 * 原调试器不能把它的窗口/寄存器/内存当已结束的拥有目标。 */
#include "SCAPI.H"
int main(void){int w=sc_open("Independent reused task / mio",260,100);if(w<0)return 1;sc_text(w,8,12,"Shell owned / mio",PAL_UI_TEXT);wait_escape();return 7;}
'''.encode()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'debugger-native.scx').read_bytes();mapping=(STAGE/'debugger-native.map').read_bytes();target=(STAGE/'target-native.scx').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['debugger']['scx_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native']['debugger']['map_sha256']
    assert hashlib.sha256(target).hexdigest()==core['native']['target']['scx_sha256'];(OUT/'zero.c').write_bytes(ZERO)
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'HOME/ZERO.C',ZERO)
        compiler.disk_put(disk,'HOME/DBG.SCX',native);compiler.disk_put(disk,'HOME/TARGET.SCX',target)
        compiler.disk_put(disk,'HOME/TARGET.SCX.map',(STAGE/'target-native.map').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping);proc=t.launch('std',128,'debugger-lifecycle',prepare);disk=OUT/'sanddata-std-128-debugger-lifecycle.img'
    observations=[];w=None
    try:
        t.open_shell(True);v.idle();zero=compiler.compile_native(disk,'BIN/G2.SCX','HOME/ZERO.C','HOME/ZERO.SCX',300);v.idle()
        zero_map=compiler.await_file(disk,'HOME/ZERO.SCX.map');(OUT/'zero-native.scx').write_bytes(zero);(OUT/'zero-native.map').write_bytes(zero_map)
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/DBG.SCX HOME/TARGET.SCX\n');w=seed.wait(theme.window,'实际G2图形调试器')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name,n=64):return v.user_bytes(w,addresses[name],n).split(b'\0')[0]
        def generation(pid):return struct.unpack_from('<I',q.memory(kernel['cpu_generation'],8*4),pid*4)[0]
        def focus(win):
            handles=sorted(row['handle'] for row in t.windows());bw=max(40,min(140,(1024-240)//len(handles)))
            t.point(116+bw*handles.index(win['handle'])+bw//2,752);t.click()
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'自然退出状态实际整帧',180)
        def record(label):
            with t.stable_frame():
                row=dict(case=label,target_pid=number('target_pid'),target_generation=number('target_generation'),paused=number('paused'),
                    have_context=number('have_context'),memory_valid=number('memory_valid'),status=text('status').decode(),
                    task_states=compiler.task_states(),slot_generation=generation(pid),context_sha256=hashlib.sha256(v.user_bytes(w,addresses['context'],88)).hexdigest())
                observations.append(row);return row
        seed.wait(lambda:number('ui_frames')>=2 and number('paused')==number('have_context')==1,'真实首指令前暂停')
        pid=number('target_pid');expected_generation=number('target_generation');initial=v.user_bytes(w,addresses['context'],88)
        assert pid==3 and generation(pid)==expected_generation;record('before-actual-continue')
        q.key('f5');seed.wait(lambda:len(t.windows())==3 and compiler.file_content(disk,'HOME/DEBUG.OK')==b'LIVE','真实Continue已经运行目标代码')
        target_window=next(row for row in t.windows() if row['owner']==pid);focus(target_window);q.key('esc')
        seed.wait(lambda:len(t.windows())==2 and number('target_pid')==0xFFFFFFFF,'目标Esc自然返回23且调试身份结束')
        assert not number('paused') and not number('memory_valid') and number('have_context')==1
        assert text('status')==b'Target exited 23' and v.user_bytes(w,addresses['context'],88)==initial
        assert compiler.task_states()[pid]==0;focus(w);q.key('r');present();record('natural-exit-23');q.shot('01-natural-exit-retains-last-real-context')
        shell=next(row for row in t.windows() if row['owner']==1);focus(shell);q.text('run HOME/ZERO.SCX\n')
        reused=seed.wait(lambda:next((row for row in t.windows() if row['owner']==pid),None),'普通Shell EXEC实际复用原PID')
        assert generation(pid)==expected_generation+1 and compiler.task_states()[pid]==1
        focus(w);seed.wait(lambda:number('ui_focus')==1,'任务栏实际返回原调试器');present()
        assert number('target_pid')==0xFFFFFFFF and not number('memory_valid') and text('status')==b'Target exited 23'
        assert v.user_bytes(w,addresses['context'],88)==initial;record('same-pid-new-generation');q.shot('02-reused-pid-is-not-old-debug-target')
        # 对结束会话再次打开Memory，仍不得读取同PID独立任务映像；
        # 如果误认原目标，400000映射会返回成功。真正owner权限仍由核
        # 决定，这个应用检查仅使界面明确失败，不扩大DEBUG权限。
        q.key('m');seed.wait(lambda:number('ui_modal')==1,'结束会话的实际Memory框');q.text('00400000\n')
        seed.wait(lambda:number('ui_modal')==0 and number('memory_mode')==1,'实际结束会话地址输入')
        present();assert number('target_pid')==0xFFFFFFFF and not number('memory_valid')
        assert compiler.task_states()[pid]==1 and v.user_bytes(w,addresses['context'],88)==initial
        record('no-memory-from-independent-reuse');q.shot('03-ended-session-cannot-read-reused-task')
        focus(reused);q.key('esc');seed.wait(lambda:len(t.windows())==2 and compiler.task_states()[pid]==0,'独立复用任务自然返回7')
        focus(w);q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'自然目标/独立任务/调试器全页严格回收')
        q.shot('04-all-naturally-exited-and-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            helper_source_sha256=hashlib.sha256(ZERO).hexdigest(),helper_native_sha256=hashlib.sha256(zero).hexdigest(),helper_map_sha256=hashlib.sha256(zero_map).hexdigest(),observations=observations,overflow=overflow,
            limits='实际目标自然返回23、Shell普通EXEC复用代数及结束会话不读新任务、严格回收；并非在STATUS两调用间强制造复用，不代替其它组件或全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
