#!/usr/bin/env python3
"""mio：Files的原生编译、真实目录交互/修改与文件分派。

测试盘只在启动前准备；运行后的mkdir/rename/remove/编译产物
都来自普通三环程序。只读实际页表/私有变量定位所画行，不修改
选择序号或调用客体函数。成功报告保留，--tag生成另一独立副本。
"""
import argparse,hashlib,json,re,struct,sys,time,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
import verify_canvas as v

t=v.t;q=v.q;compiler=v.compiler;theme=v.theme
OUT=ROOT/'build/m8-files'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def wait(test,label,seconds=50):
    # 调试目标暂停也是state=3，不能把合法首指令暂停误当异常。
    # 真正异常由公开的故障卡片事实检查，仍不允许测试中出现异常卡。
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        cards=q.memory(q.symbols()['fault_cards'],8*24)
        assert not any(struct.unpack_from('<I',cards,i*24)[0] for i in range(8)),label+' / 实际异常卡'
        time.sleep(.12)
    q.shot('timeout');raise AssertionError(label)


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tag')
    args=parser.parse_args()
    if args.tag:
        assert re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',args.tag)
        OUT=OUT/args.tag
    OUT.mkdir(parents=True,exist_ok=True)
    assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    v.OUT=OUT;v.bind()
    inputs={name:sha(ROOT/name) for name in ('build/sandcore.img','build/sanddata.img','build/kernel.elf',
        'build/kernel.sym','user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','user/files.c','user/ide.c','user/debugger.c','user/lens.c','kernel/font16.txt')}
    verifier=sha(Path(__file__))
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    target=b'#include "SCAPI.H"\nint main(void){int w=sc_open("Files target",220,100);if(w<0)return 1;sc_text(w,8,12,"FILES EXEC / mio",PAL_UI_TEXT);wait_escape();return 0;}\n'
    pixels=struct.pack('<8s6I',b'SCB2MIO\0',64,32,2,0,64*32*4,0x004F494D)+struct.pack('<I',0x80224488)*(64*32)
    fixture={'DATA.TXT':b'Files UTF-8 / mio\n','PICTURE.SCB':pixels,'UNKNOWN.DAT':b'\0UNCHANGED\xff',
        '沙核'+('a'*35)+'.TXT':'沙核\n'.encode(),'NEST/LEAF.TXT':b'child, not current row\n'}
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for name in ('files.c','ide.c','debugger.c','lens.c','NUI.inc','IMAGE.inc'):
            compiler.disk_put(disk,'SYS/SRC/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'HOME/TARGET.C',target)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        for name,blob in fixture.items():compiler.disk_put(disk,'HOME/FTEST/'+name,blob)
        for i in range(70):compiler.disk_put(disk,f'HOME/FTEST/F{i:02}.TXT',f'{i}\n'.encode())
    proc=t.launch('std',128,'files',prepare);disk=OUT/'sanddata-std-128-files.img'
    artifacts={};checks=[];w=None;addresses=None
    try:
        t.open_shell(True);v.idle();kernel=q.symbols()
        for name,source,output in (('files','SYS/SRC/files.c','HOME/FIL.SCX'),('ide','SYS/SRC/ide.c','APPS/IDE.SCX'),
                                  ('debugger','SYS/SRC/debugger.c','APPS/DEBUGGER.SCX'),('lens','SYS/SRC/lens.c','APPS/LENS.SCX'),
                                  ('target','HOME/TARGET.C','HOME/FTEST/RUN.SCX')):
            blob=compiler.compile_native(disk,'BIN/G2.SCX',source,output,240);v.idle()
            mapping=compiler.await_file(disk,output+'.map')
            (OUT/(name+'-native.scx')).write_bytes(blob);(OUT/(name+'-native.map')).write_bytes(mapping)
            artifacts[name]=dict(scx_sha256=hashlib.sha256(blob).hexdigest(),map_sha256=hashlib.sha256(mapping).hexdigest())
        addresses=theme.native_symbols((OUT/'files-native.map').read_bytes())
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/FIL.SCX HOME/FTEST\n');w=wait(theme.window,'真实G2 Files')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name,n=64):return v.user_bytes(w,addresses[name],n).split(b'\0')[0]
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            frames=number('ui_frames');wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'Files实际提交')
        def click(x,y,right=False):
            win=current();scale=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*scale//100,win['y']+title+y*scale//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def names():
            raw=v.user_bytes(w,addresses['names'],512*64)
            return [raw[i*64:(i+1)*64].split(b'\0')[0] for i in range(number('count'))]
        def select(name,right=False):
            index=names().index(name.encode());rows=number('visible_rows');assert rows>0
            while number('selection')//rows!=index//rows:
                old=number('selection');click(120 if old<index else 40,number('navigation_y')+10)
                wait(lambda:number('selection')!=old,'实际鼠标分页')
            click(number('list_left')+64,number('list_y')+(index%rows)*number('row_height')+number('row_height')//2,right)
            wait(lambda:number('selection')==index,'实际鼠标选中完整名字')
        def operation(key,next_path=None,answer=None):
            count=number('file_operations');q.key(key)
            if next_path is not None:
                wait(lambda:number('ui_modal')==2,'路径框完成输入准备');q.text(next_path+'\n')
            if answer is not None:
                wait(lambda:number('ui_modal')==3,'实际删除确认');q.key(answer)
            wait(lambda:number('file_operations')>count,'文件调用/列表刷新真实结束');present()
        wait(lambda:number('ui_frames')>=2,'Files初始化/布局')
        actual=names();assert b'NEST' in actual and b'LEAF.TXT' not in actual and number('count')==78
        long_name='沙核'+('a'*35)+'.TXT';assert long_name.encode() in actual,'真实UTF-8名称被视觉省略破坏'
        q.shot('01-direct-children-utf8')
        for index,path in enumerate((b'',b'HOME',b'SYS',b'APPS',b'BIN')):
            click(40,number('body_y')+20+index*36)
            wait(lambda:text('cwd')==path,'真实鼠标侧栏导航'+path.decode());present()
        click(40,number('body_y')+56);wait(lambda:text('cwd')==b'HOME','侧栏回HOME');present()
        select('FTEST');q.key('ret');wait(lambda:text('cwd')==b'HOME/FTEST','实际进入测试目录');present()
        select('NEST');q.key('ret');wait(lambda:text('cwd')==b'HOME/FTEST/NEST','进入真实子目录');present()
        assert names()==[b'LEAF.TXT'],'直接子项投影内容错误'
        q.key('backspace');wait(lambda:text('cwd')==b'HOME/FTEST','上级导航');present()
        select('F69.TXT');assert names()[number('selection')]==b'F69.TXT';q.shot('02-last-page')
        select('DATA.TXT',True);wait(lambda:number('file_context')==1,'真实右键浮层');q.shot('03-right-menu')
        old_files=compiler.file_content(disk,'HOME/FTEST/DATA.TXT')
        # 点菜单之外、落在背景删除按钮上，必须仅关闭菜单。
        click(530,80);wait(lambda:number('file_context')==0,'菜单外点击只关闭浮层')
        assert number('ui_modal')==0 and compiler.file_content(disk,'HOME/FTEST/DATA.TXT')==old_files,'浮层点击穿透'
        operation('n','HOME/FTEST/NEW');assert b'NEW' in names();q.shot('04-directory-created')
        select('NEW');operation('f2','HOME/FTEST/RENAMED');assert b'RENAMED' in names() and b'NEW' not in names()
        selection=number('selection');operation('x',answer='esc');assert number('selection')==selection and b'RENAMED' in names()
        operation('x',answer='y');assert b'RENAMED' not in names();q.shot('05-rename-delete-cancel')
        too_long='HOME/'+('a'*60);assert len(too_long)>63
        operation('n',too_long);assert text('status')==b'Path exceeds 63 bytes'
        assert compiler.file_content(disk,too_long[:63]) is None,'超长输入被截断创建成另一个名字'
        operation('n','SYS/CORE/GUI');assert text('status')==b'Core path protected by kernel'
        assert compiler.file_content(disk,'SYS/CORE/GUI') is None
        select('DATA.TXT');operation('f2','SYS/CORE/DATA.TXT')
        assert text('status')==b'Core path protected by kernel' and compiler.file_content(disk,'HOME/FTEST/DATA.TXT')==old_files
        q.shot('06-long-path-core-protected')
        select('UNKNOWN.DAT');q.key('ret');present();assert len(t.windows())==2
        assert text('status')==b'Binary file; use an application' and compiler.file_content(disk,'HOME/FTEST/UNKNOWN.DAT')==fixture['UNKNOWN.DAT']
        checks.append('直接子目录/UTF-8真实名字/多页鼠标选择，菜单外点击不穿透；mkdir/rename/delete取消与成功、超长输入完整拒绝、核心路径保护和未知二进制保留')
        for name,kind in (('DATA.TXT','ide'),('PICTURE.SCB','lens'),('RUN.SCX','target')):
            select(name);q.key('ret');wait(lambda:len(t.windows())==3,'真实Files分派'+kind)
            child=t.windows()[-1];child_addresses=theme.native_symbols((OUT/(kind+'-native.map')).read_bytes())
            if kind in ('ide','lens'):wait(lambda:theme.user_word(child,child_addresses['ui_frames'])>=2,'子程序初始化/真实显示')
            if kind in ('ide','lens'):
                assert v.user_bytes(child,child_addresses['filename'],64).split(b'\0')[0]==('HOME/FTEST/'+name).encode(),'分派实际文件路径被改写'
            q.shot('07-dispatch-'+kind);q.key('esc');wait(lambda:len(t.windows())==2,'子程序真实退出');present()
        select('RUN.SCX');q.key('d');wait(lambda:len(t.windows())==3,'SCX实际送图形调试器')
        debugger=t.windows()[-1];debug_addresses=theme.native_symbols((OUT/'debugger-native.map').read_bytes())
        wait(lambda:theme.user_word(debugger,debug_addresses['paused'])==1 and theme.user_word(debugger,debug_addresses['have_context'])==1,'目标首指令前真实暂停/现场')
        assert v.user_bytes(debugger,debug_addresses['target_path'],64).split(b'\0')[0]==b'HOME/FTEST/RUN.SCX'
        q.shot('08-dispatch-debugger');q.key('esc');wait(lambda:len(t.windows())==2,'调试器与专属目标实际退出');present()
        checks.append('同批真实G2 Studio/Lens/独立SCX/Debug实际分派运行；Debug目标首指令前暂停并取得真实现场，子程序退出后返回Files')
        q.key('esc');v.idle()
        wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'Files及全部子程序/调试目标严格页回收')
        q.shot('09-all-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values()),overflow
        assert inputs=={name:sha(ROOT/name) for name in inputs},'源/核/默认盘在运行期间改变'
        assert sha(Path(__file__))==verifier,'测试器在运行期间改变'
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native=artifacts,checks=checks,overflow=overflow,
                    limits='Files1024/100核心与同批实际G2子程序；十九显示/窗口矩阵另测，压缩图像和全部M8尚未完成')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
