#!/usr/bin/env python3
"""mio：实际编译期间修改/保存、切换文件与编译输出身份测试。

编译器和Studio均运行真正历史G2产物。只在开始前准备源文件，
运行中真实点击任务栏、键入/保存/Open；观察版本仅用于证明操作
确实发生在同一编译提交尚未结束时，不修改PID/版本/退出状态。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=v.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'revision'
DRIVER='''/* mio：普通三环编译事务驱动，专为确定性的并发编辑验收。
 * 先读取源的完整快照写独立HOME/SNAP.C，由实际历史G2生成输出；
 * 子编译器实际退出后仍等待本次提交满1200个PITtick再返回原码。
 * 这样CPU很快时也保留真实未完成事务供鼠标/保存/Open操作，而
 * 不修改Studio版本/任务状态/时钟，不拿驱动等待时间称G2速度。
 * 它只用于独立测试盘BIN/S3C.SCX，正式编译器和默认盘不改。 */
#include "SCAPI.H"
static char snapshot[65536];
volatile u32 driver_state[8];
int main(void)
{
    int started=sc_tick(),win=sc_open("Build transaction / mio",250,110);
    if(win<0)return 1;
    sc_text(win,8,12,"Actual G2 / delayed return",PAL_UI_TEXT);
    char args[128],command[128];sc_args(args,sizeof(args));char *p=args;
    char *input=token(&p),*output=token(&p);
    u32 info[2];if(sc_stat(input,info)||info[0]!=1||info[1]>=sizeof(snapshot))return 2;
    int n=sc_read(input,snapshot,info[1]);if(n!=(int)info[1])return 3;
    if(sc_write("HOME/SNAP.C",snapshot,n)!=n)return 4;
    driver_state[1]=info[1];driver_state[0]=1;
    copy(command,"BIN/G2.SCX HOME/SNAP.C ",sizeof(command));append(command,output,sizeof(command));
    int pid=sc_exec(command);if(pid<0)return 5;driver_state[2]=pid;
    int code;
    do{code=sc_status(pid);sc_yield();}while(code==0x40000000||code==0x40000001);
    driver_state[3]=code;driver_state[0]=2;
    while(sc_tick()-started<1200)sc_yield();
    driver_state[0]=3;return code;
}
'''


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'ide-native.scx').read_bytes();mapping=(STAGE/'ide-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['ide']['scx_sha256']
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    # 各函数分别低于8192 AST节点；总代码仍低于262000B。大量
    # 实际C运算让生成器保持运行，绝不修改计时或暂停编译器造竞态。
    large=(''.join('int f'+str(i)+'(void){int a=0;'+('a+=1;'*90)+'return a;}\n' for i in range(65))+'int main(void){return f0();}\n').encode()
    assert len(large)<65536
    other=b'int main(void){return 7;}\n'
    (OUT/'large.c').write_bytes(large)
    (OUT/'driver.c').write_text(DRIVER,encoding='utf-8')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'HOME/IDE.SCX',native)
        compiler.disk_put(disk,'SYS/SRC/driver.c',DRIVER.encode())
        compiler.disk_put(disk,'HOME/LARGE.C',large);compiler.disk_put(disk,'HOME/OTHER.C',other)
        compiler.disk_put(disk,'HOME/OTHER.SCX.log',b'WRONG OUTPUT LOG / preserve\n')
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping);proc=t.launch('std',128,'studio-revision',prepare)
    disk=OUT/'sanddata-std-128-studio-revision.img';w=None
    try:
        t.open_shell(True);v.idle()
        driver=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/driver.c','BIN/S3C.SCX',180);v.idle()
        driver_map=compiler.await_file(disk,'BIN/S3C.SCX.map');driver_addresses=theme.native_symbols(driver_map)
        (OUT/'driver-native.scx').write_bytes(driver);(OUT/'driver-native.map').write_bytes(driver_map)
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/IDE.SCX HOME/LARGE.C\n');w=seed.wait(theme.window,'真正G2 Studio')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name,n=64):return v.user_bytes(w,addresses[name],n).split(b'\0')[0]
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'Studio完整显示',180)
        def focus():
            # 鼠标先置于Studio任务栏，开始编译后无需从编译窗口标题
            # 多包移动；任务栏依创建handle排序，不用当前Z序下标。
            handles=sorted(win['handle'] for win in t.windows());rank=handles.index(w['handle'])
            bw=max(40,min(140,(1024-240)//len(handles)))
            t.point(116+bw*rank+bw//2,752);t.click();seed.wait(lambda:number('ui_focus')==1,'实际任务栏返回Studio')
        seed.wait(lambda:number('ui_frames')>=2,'Studio真实首帧');t.point(326,752)
        observations=[]
        for switch in (False,True):
            if switch:
                count=number('studio_operations');q.key('f1');seed.wait(lambda:number('ui_modal')==2,'重开编译文档');q.text('HOME/LARGE.C\n')
                seed.wait(lambda:number('studio_operations')>count,'完整源文档重开');present();t.point(326,752)
            version=number('document_revision');count=number('studio_operations');q.key('f5')
            seed.wait(lambda:number('studio_operations')>count and number('compiler_pid')<8,'真正提交内部编译',180)
            pid=number('compiler_pid');job=seed.wait(lambda:next((win for win in t.windows() if win['owner']==pid),None),'实际三环编译事务窗口')
            seed.wait(lambda:theme.user_word(job,driver_addresses['driver_state'])>=1,'源快照真实提交给G2')
            assert compiler.file_content(disk,'HOME/SNAP.C')==large
            focus();assert number('compiler_pid')==pid,'真实编辑前编译已结束，不能冒充并发编辑测试'
            q.key('f9');q.key('spc');count=number('studio_operations');q.key('f2')
            seed.wait(lambda:number('studio_operations')>count and number('dirty')==0 and text('status')==b'Source saved','编译事务中实际修改又保存',180)
            assert number('compiler_pid')==pid and number('document_revision')!=version and number('build_revision')==version
            changed=v.user_bytes(w,addresses['source'],number('used'))
            assert changed==large+b' ' and compiler.file_content(disk,'HOME/LARGE.C')==changed
            if switch:
                count=number('studio_operations');q.key('f1');seed.wait(lambda:number('ui_modal')==2,'编译中打开另一文件');q.text('HOME/OTHER.C\n')
                seed.wait(lambda:number('studio_operations')>count,'切换文件真实完成');assert number('compiler_pid')==pid
                assert text('filename')==b'HOME/OTHER.C' and text('output')==b'HOME/OTHER.SCX' and text('build_output')==b'HOME/LARGE.SCX'
            with t.stable_frame():
                observation=dict(case='open-other-during-build' if switch else 'edit-save-during-build',compiler_pid=pid,build_revision=number('build_revision'),
                    document_revision=number('document_revision'),dirty=number('dirty'),filename=text('filename').decode(),output=text('output').decode(),build_output=text('build_output').decode(),
                    task_states=compiler.task_states(),driver_state=list(struct.unpack('<8I',v.user_bytes(job,driver_addresses['driver_state'],32))),registers=q.hmp('info registers'))
            assert observation['task_states'][pid]==1
            observations.append(observation);q.shot('01-'+observation['case'])
            seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF,'本次真正编译器实际结束',300);present()
            assert not number('compiled') and not number('dirty') and text('status')==b'Source changed / build again' and not number('diagnostic_mode')
            log=compiler.await_file(disk,'HOME/LARGE.SCX.log');assert log.startswith(b'SCCC OK')
            assert text('diagnostic_text',4096)==log[:4095].split(b'\0')[0],'编译中切换文档读取了另一输出日志'
            assert compiler.file_content(disk,'HOME/OTHER.SCX.log')==b'WRONG OUTPUT LOG / preserve\n'
            count=number('studio_operations');q.key('f6');seed.wait(lambda:number('studio_operations')>count,'拒绝运行旧版本');present()
            assert len(t.windows())==2 and text('status')==b'Build current source first (F5)'
            q.shot('02-'+observation['case']+'-old-output-rejected')
            if not switch:
                # 第二次测试恢复原文只走真实Backspace和Save，不在
                # 运行中写测试盘或私有source。尾空格仍是合法C源。
                q.key('backspace');count=number('studio_operations');q.key('f2');seed.wait(lambda:number('studio_operations')>count,'恢复原文实际保存')
                assert compiler.file_content(disk,'HOME/LARGE.C')==large
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'编译期间编辑测试所有页严格回收')
        q.shot('03-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            driver=dict(source_sha256=sha(OUT/'driver.c'),scx_sha256=hashlib.sha256(driver).hexdigest(),map_sha256=hashlib.sha256(driver_map).hexdigest()),
            observations=observations,overflow=overflow,checks=['普通三环驱动先提交完整原源快照、真正历史G2生成SCX，提交延迟返回1200tick；事务真实未完成时编辑再保存，旧成功输出必须拒绝Run','事务中Open另一文档，本次build_output日志仍完整正确，不抢走编辑界面','全部页严格回收/队列零溢出'],
            limits='Studio草稿编译提交版本与输出身份专测；普通测试驱动延迟返回不代表G2编译速度，默认编译器不改，不代替其它组件或全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:
            if w is not None:
                with t.stable_frame():
                    failure={name:number(name) for name in ('compiler_pid','compiler_generation','compiled','document_revision','build_revision','dirty','used','cursor','ui_modal','studio_operations')}
                    failure.update(registers=q.hmp('info registers'));(OUT/'failure-state.json').write_text(json.dumps(failure,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
