#!/usr/bin/env python3
"""mio：Studio草稿的真实G2编译、编辑/索引/文件和编译资格验证。

测试盘只在启动前注入夹具；运行中用HMP/QMP输入，只读页表与
实际私有状态。草稿成功不会覆盖正式源码，不把宿主语法检查视作
原生执行。失败留盘/截图/状态，另建阶段重测，不改成功报告。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
OUT=ROOT/'build/m8-studio-draft'
SOURCE=ROOT/'build/m8-next/ide.c'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    names=('build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym',
        'user/SCAPI.H','user/NUI.inc','user/api.h','user/debugger.c','user/assembler.c','kernel/font16.txt','build/m8-next/ide.c')
    inputs={name:sha(ROOT/name) for name in names};verifier=sha(Path(__file__))
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    target=b'#include "SCAPI.H"\nint main(void){int w=sc_open("Studio target",240,100);if(w<0)return 1;sc_write("HOME/STUDIO.OK","PASS",4);sc_text(w,8,12,"Made in Studio / mio",PAL_UI_TEXT);wait_escape();return 0;}\n'
    assembly=b'[bits 32]\n[org 0x400000]\nmov eax, 0\nmov ebx, 0\nint 0x7c\n'
    edit='//沙核abc\nint main(void){return 0;}\n'.encode()
    rows=(b'// row\n'*10000)[:64999]+b'\n';assert len(rows)==65000
    fixtures={'EDIT.C':edit,'TARGET.C':target,'FAIL.C':b'int main(void){return missing_name;}\n',
        'TEST.ASM':assembly,'BAD.ASM':b'intended_invalid_instruction\n','BINARY.C':b'old\0new',
        'MAX.C':b'\n'*65535,'OVER.C':b'\n'*65536,'ROWS.C':rows,'LONG.C':b'//'+b'a'*64997+b'\n'}
    def prepare(disk):
        # Studio本次Build也执行验收过的实际G2，而非仅由宿主打包
        # 同名编译器。Assembler和Debugger再由它编译本轮源码。
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'BIN/S3C.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/ide.c',SOURCE.read_bytes())
        for name in ('NUI.inc','api.h','debugger.c','assembler.c'):compiler.disk_put(disk,'SYS/SRC/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'SYS/CORE/LOCK.C',b'// readonly core\n')
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        for name,blob in fixtures.items():compiler.disk_put(disk,'HOME/'+name,blob)
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'studio-core',prepare)
    disk=OUT/'sanddata-std-128-studio-core.img';w=None;addresses=None;checks=[];artifacts={}
    try:
        t.open_shell(True);v.idle()
        for name,source,path in (('ide','SYS/SRC/ide.c','HOME/IDE.SCX'),('asm','SYS/SRC/assembler.c','BIN/ASM.SCX'),
            ('debugger','SYS/SRC/debugger.c','APPS/DEBUGGER.SCX')):
            blob=compiler.compile_native(disk,'BIN/G2.SCX',source,path,300);v.idle()
            mapping=compiler.await_file(disk,path+'.map');(OUT/(name+'-native.scx')).write_bytes(blob);(OUT/(name+'-native.map')).write_bytes(mapping)
            artifacts[name]=dict(scx_sha256=hashlib.sha256(blob).hexdigest(),map_sha256=hashlib.sha256(mapping).hexdigest())
        addresses=theme.native_symbols((OUT/'ide-native.map').read_bytes())
        debug_addresses=theme.native_symbols((OUT/'debugger-native.map').read_bytes())
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/IDE.SCX HOME/EDIT.C\n');w=seed.wait(theme.window,'真正G2 Studio')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name,n=64):return v.user_bytes(w,addresses[name],n).split(b'\0')[0]
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'Studio实际帧提交',120)
        def click(x,y,right=False):
            win=current();scale=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*scale//100,win['y']+title+y*scale//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def raw():return v.user_bytes(w,addresses['source'],number('used')+1)
        def index(expected=None):
            with t.stable_frame():
                blob=raw();assert blob[-1:]==b'\0'
                if expected is not None:assert blob[:-1]==expected,'编辑后的完整源码字节不同'
                expected_starts=[0]+[i+1 for i,c in enumerate(blob[:-1]) if c==10]
                count=number('line_count');assert count==len(expected_starts)
                actual=list(struct.unpack('<'+str(count)+'I',v.user_bytes(w,addresses['line_starts'],count*4)))
                assert actual==expected_starts,'增量行索引与全文参考不一致'
                cursor=number('cursor');assert 0<=cursor<=len(blob)-1
                assert cursor==len(blob)-1 or blob[cursor]&192!=128,'光标落入UTF-8续字节'
                return blob[:-1]
        def state():
            with t.stable_frame():
                return dict(source=v.user_bytes(w,addresses['source'],65536),
                    index=v.user_bytes(w,addresses['line_starts'],65536*4),filename=text('filename'),output=text('output'),
                    fields={name:number(name) for name in ('used','cursor','first_line','left_column','dirty','compiled','document_revision','line_count','diagnostic_mode')})
        def operation(key,path=None,cancel=False):
            counter=number('studio_operations');dirty=number('dirty');q.key(key)
            if key in ('f1','f3'):
                if dirty:seed.wait(lambda:number('ui_modal')==3,'真实未保存确认');q.key('y')
                seed.wait(lambda:number('ui_modal')==2,'实际完整路径框')
                if cancel:q.key('esc')
                else:q.text(path+'\n')
            seed.wait(lambda:number('studio_operations')>counter,'Studio实际操作结束',180);present()
        seed.wait(lambda:number('ui_frames')>=2,'首帧真实提交');index(edit);q.shot('01-utf8-source')
        # F8移动文首，再三次右移：两条ASCII和一个完整沙字。退格
        # 应删沙的三字节而非其中一字节，汉字核仍为完整原编码。
        q.key('f8');q.key('right');q.key('right');q.key('right');q.key('backspace');present()
        expected='//核abc\nint main(void){return 0;}\n'.encode();index(expected);assert number('cursor')==2
        q.key('x');q.key('tab');q.key('ret');present();expected=expected[:2]+b'x    \n'+expected[2:];index(expected)
        q.key('backspace');present();expected=expected[:7]+expected[8:];index(expected)
        operation('f2');assert compiler.file_content(disk,'HOME/EDIT.C')==expected and number('dirty')==0
        # 鼠标在实际编辑行定位；行号/屏幕列绝不能作为源码字节值。
        click(69+2*8,number('editor_y')+10);seed.wait(lambda:number('cursor')==2,'鼠标实际列定位')
        click(100,number('editor_y')+10,True);seed.wait(lambda:number('edit_menu')==1,'真实右键编辑浮层')
        q.shot('02-edit-index-mouse-menu')
        before=state();click(80,80);seed.wait(lambda:number('edit_menu')==0,'外部点击只关闭菜单')
        assert state()==before and number('ui_modal')==0,'浮层点击穿透保存/Open或移动光标'
        for path,message in (('HOME/BINARY.C',b'Binary file rejected'),('HOME/OVER.C',b'Source exceeds 65535 bytes'),
            ('HOME/MISSING.C',b'File not found'),('HOME/'+('a'*60),b'Path exceeds 63 bytes')):
            before=state();operation('f1',path);assert state()==before and text('status')==message
        before=state();operation('f1',cancel=True);assert state()==before and text('status')==b'Cancelled'
        operation('f1','HOME/ROWS.C');index(rows)
        first=number('first_line');click(number('editor_right')+10,number('editor_y')+number('editor_rows')*20-2)
        seed.wait(lambda:number('first_line')==first+1,'真实向下滚动按钮');present();assert number('first_line')==first+1 and number('cursor')==0
        q.shot('03-scroll-does-not-snap-to-caret')
        operation('f1','HOME/MAX.C');index(fixtures['MAX.C']);assert number('line_count')==65536
        q.key('f9');present();assert number('cursor')==65535
        q.key('x');present();index(fixtures['MAX.C']);assert text('status')==b'Source capacity reached'
        q.key('backspace');present();index(b'\n'*65534);q.key('ret');present();index(fixtures['MAX.C'])
        operation('f2');assert compiler.file_content(disk,'HOME/MAX.C')==fixtures['MAX.C']
        q.shot('04-65535-capacity-and-all-line-offsets')
        operation('f1','SYS/CORE/LOCK.C');q.key('x');present();operation('f2')
        assert text('status')==b'Core file protected by kernel' and number('dirty')==1
        assert compiler.file_content(disk,'SYS/CORE/LOCK.C')==b'// readonly core\n'
        before=state();counter=number('studio_operations');q.key('f1');seed.wait(lambda:number('ui_modal')==3,'真实丢弃确认');q.key('esc')
        seed.wait(lambda:number('studio_operations')>counter,'取消未保存文档确认');present();assert state()==before
        q.shot('05-core-protection-and-confirm-cancel')
        operation('f1','HOME/TARGET.C');operation('f5')
        seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF and number('compiled')==1,'内部真正C编译成功',300);present()
        target_scx=compiler.await_file(disk,'HOME/TARGET.SCX');assert target_scx[:8]==b'SCX1MIO\0'
        (OUT/'studio-built-target.scx').write_bytes(target_scx);(OUT/'studio-built-target.map').write_bytes(compiler.await_file(disk,'HOME/TARGET.SCX.map'))
        q.shot('06-internal-c-build-and-log');operation('f6')
        seed.wait(lambda:len(t.windows())==3,'Studio生成的程序真正运行');assert compiler.await_file(disk,'HOME/STUDIO.OK',expected=b'PASS')==b'PASS'
        q.shot('07-generated-c-program-runs');q.key('esc');seed.wait(lambda:len(t.windows())==2,'生成程序真实退出');present()
        operation('f7');seed.wait(lambda:len(t.windows())==3,'Studio实际启动Debugger');debug=t.windows()[-1]
        seed.wait(lambda:theme.user_word(debug,debug_addresses['paused'])==1 and theme.user_word(debug,debug_addresses['have_context'])==1,'Debugger目标首指令前真实现场')
        assert v.user_bytes(debug,debug_addresses['target_path'],64).split(b'\0')[0]==b'HOME/TARGET.SCX'
        q.shot('08-generated-c-debugger');q.key('esc');seed.wait(lambda:len(t.windows())==2,'调试器及目标完整退出');present()
        operation('f1','HOME/FAIL.C');operation('f5');seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF,'错误C编译真正返回',300);present()
        assert not number('compiled');operation('f6');assert len(t.windows())==2 and text('status')==b'Build current source first (F5)'
        q.shot('09-failed-c-cannot-run')
        operation('f1','HOME/TEST.ASM');operation('f5');seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF,'内部真正ASM编译返回',180);present()
        assert number('compiled')==1,'ASM --ide不能成功返回'
        asm_scx=compiler.await_file(disk,'HOME/TEST.SCX');assert asm_scx[:8]==b'SCX1MIO\0';(OUT/'studio-built-asm.scx').write_bytes(asm_scx)
        operation('f6');seed.wait(lambda:len(t.windows())==2 and compiler.task_states()[1:].count(1)==2,'ASM真正运行退出');present()
        q.shot('10-internal-asm-build-run')
        operation('f1','HOME/BAD.ASM');operation('f5');seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF,'错误ASM真正返回',180);present();assert not number('compiled')
        # New明确提交一个完整身份，Save才在磁盘产生文件；取消和
        # 超限身份都不能创建63B同前缀文件，测试盘不修改内核限制。
        operation('f3','HOME/NEW.C');index(b'');assert number('dirty')==1
        q.text('// new\n');present();index(b'// new\n');operation('f2');assert compiler.file_content(disk,'HOME/NEW.C')==b'// new\n'
        before=state();operation('f3','HOME/'+('b'*60));assert state()==before and text('status')==b'Path exceeds 63 bytes'
        assert compiler.file_content(disk,('HOME/'+('b'*60))[:63]) is None
        checks.extend(('完整UTF-8移动删除/任意插入/Tab/LF/增量索引/鼠标定位与手动滚动','NUL/超限/缺失/超长路径/取消完整回滚、65535B和65536行极限、CORE -5保护',
            '同一真实G2内部C成功/错误/实际Run与Debug、真实G2 SandAsm --ide成功/错误/运行、新文档身份与保存'))
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'Studio/编译器/产物/调试器全部实际页回收')
        q.shot('11-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native=artifacts,checks=checks,overflow=overflow,
            limits='独立Studio草稿1024/100核心，正式源码未替换；十九尺寸/排队首字/编译中编辑与PID复用/长行耗时继续补测')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:
            if w is not None and addresses is not None:
                with t.stable_frame():
                    state={name:theme.user_word(w,addresses[name]) for name in ('used','cursor','first_line','left_column','dirty','compiled','compiler_pid','line_count','editor_rows','ui_modal','studio_operations')}
                    state.update(registers=q.hmp('info registers'))
                    (OUT/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
