#!/usr/bin/env python3
"""mio：经真实三环调试接口暂停Studio，实际复用编译器PID。

普通协调程序拥有DBGEXEC目标，只以已有DEBUG暂停/继续；不改
目标私有字段、任务槽或代数。编译器真实退出，再EXEC返回0的
普通程序占同一槽，恢复Studio必须拒绝把它的0认作编译成功。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'pid-reuse'
COORD='''/* mio：真实普通三环协调程序，公开DEBUG只控制自己创建的
 * Studio。编译器PID来自CPUINFO的新槽，不从宿主写入。目标暂停
 * 后编译器照常完成，再运行另一个返回0的程序真实复用那个槽。
 * 只有按R才恢复Studio，便于验收只读保存暂停/复用的完整事实。 */
#include "SCAPI.H"
volatile u32 coordination[16];
int main(void)
{
    u32 initial[SC_CPU_WORDS],cpu[SC_CPU_WORDS];
    int win=sc_open("PID reuse / mio",240,100);
    if(win<0)return 1;
    sc_text(win,8,12,"Compiler PID reuse / R resume",PAL_UI_TEXT);
    if(sc_cpu(initial))return 2;
    int target=sc_dbgexec("HOME/IDE.SCX HOME/LARGE.C");
    if(target<0)return 3;
    coordination[1]=target;
    if(sc_debug(target,2,0,0,0))return 4;
    coordination[0]=1;
    int child=-1;
    while(child<0){
        if(sc_key()==27)return 5;
        if(sc_cpu(cpu))return 6;
        for(int i=1;i<8;i++)if(i!=target&&cpu[16+i*6+1]==1&&cpu[16+i*6+2]!=initial[16+i*6+2]){
            child=i;coordination[2]=child;coordination[3]=cpu[16+i*6+2];break;
        }
        if(child<0)sc_yield();
    }
    if(sc_debug(target,7,0,0,0))return 7;
    coordination[0]=2;
    int state;
    do{state=sc_status(child);sc_yield();}while(state==0x40000000||state==0x40000001);
    coordination[4]=state;
    do{sc_cpu(cpu);sc_yield();}while(cpu[16+child*6+1]!=0);
    int next=sc_exec("HOME/ZERO.SCX");coordination[5]=next;
    if(next<0)return 8;
    do{state=sc_status(next);sc_yield();}while(state==0x40000000||state==0x40000001);
    sc_cpu(cpu);coordination[6]=cpu[16+next*6+2];coordination[7]=state;coordination[0]=3;
    sc_window(win,2);
    while(sc_key()!='r')sc_yield();
    coordination[8]=sc_debug(target,2,0,0,0);coordination[0]=4;
    while(sc_key()!=27)sc_yield();
    return 0;
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
    # 单函数树/总代码仍在C.md容量内，真实解析/生成留出协调程序
    # 观察RUN新槽并暂停拥有目标的时间，不能用改task.state造暂停。
    large=(''.join('int f'+str(i)+'(void){int a=0;'+('a+=1;'*60)+'return a;}\n' for i in range(50))+'int main(void){return f0();}\n').encode()
    assert len(large)<65536
    (OUT/'coord.c').write_text(COORD,encoding='utf-8');(OUT/'large.c').write_bytes(large)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'BIN/S3C.SCX',g2)
        compiler.disk_put(disk,'HOME/IDE.SCX',native);compiler.disk_put(disk,'HOME/LARGE.C',large)
        compiler.disk_put(disk,'SYS/SRC/coord.c',COORD.encode());compiler.disk_put(disk,'HOME/ZERO.C',b'int main(void){return 0;}\n')
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'studio-pid',prepare);disk=OUT/'sanddata-std-128-studio-pid.img'
    try:
        t.open_shell(True);v.idle();artifacts={}
        for source,target in (('SYS/SRC/coord.c','HOME/COORD.SCX'),('HOME/ZERO.C','HOME/ZERO.SCX')):
            blob=compiler.compile_native(disk,'BIN/G2.SCX',source,target,300);v.idle();maps=compiler.await_file(disk,target+'.map')
            label='coord' if 'COORD' in target else 'zero';(OUT/(label+'-native.scx')).write_bytes(blob);(OUT/(label+'-native.map')).write_bytes(maps)
            artifacts[label]=dict(scx_sha256=hashlib.sha256(blob).hexdigest(),map_sha256=hashlib.sha256(maps).hexdigest())
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']));q.text('run HOME/COORD.SCX\n')
        owner=seed.wait(lambda:next((w for w in t.windows() if w['owner']==2),None),'真实三环协调窗口')
        owner_addresses=theme.native_symbols((OUT/'coord-native.map').read_bytes());ide_addresses=theme.native_symbols(mapping)
        def coordination():return list(struct.unpack('<16I',v.user_bytes(owner,owner_addresses['coordination'],64)))
        seed.wait(lambda:coordination()[0]==1,'公开DBGEXEC/继续完成')
        target=seed.wait(lambda:next((w for w in t.windows() if w['owner']==coordination()[1]),None),'实际受调试Studio窗口')
        def number(name):return theme.user_word(target,ide_addresses[name])
        seed.wait(lambda:number('ui_frames')>=2,'实际Studio首帧');q.shot('01-debug-owned-studio')
        q.key('f5');seed.wait(lambda:coordination()[0]>=2,'公开CPU观察编译器/真实DEBUG暂停Studio',180)
        seed.wait(lambda:coordination()[0]==3,'编译器真正退出、返回0程序实际复用PID',300)
        values=coordination();assert values[2]==values[5] and values[4]==values[7]==0
        assert values[6]!=values[3] and compiler.task_states()[values[1]]==3
        output=compiler.await_file(disk,'HOME/LARGE.SCX');assert output[:8]==b'SCX1MIO\0'
        observation=dict(coordinator=values,studio_compiler_pid=number('compiler_pid'),studio_compiler_generation=number('compiler_generation'),
            studio_compiled=number('compiled'),task_states=compiler.task_states(),registers=q.hmp('info registers'))
        (OUT/'actual-paused-pid-reuse.json').write_text(json.dumps(observation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        q.shot('02-real-compiler-pid-reused-while-studio-paused');q.key('r')
        seed.wait(lambda:coordination()[0]==4 and coordination()[8]==0,'公开DEBUG实际恢复Studio')
        seed.wait(lambda:number('compiler_pid')==0xFFFFFFFF,'Studio检测实际代数变化',120)
        assert not number('compiled') and v.user_bytes(target,ide_addresses['status'],64).split(b'\0')[0]==b'Compiler identity lost / build again'
        q.shot('03-reused-zero-exit-rejected')
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'协调/调试目标/编译器/复用任务全部回收')
        q.shot('04-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native=artifacts,observation=observation,overflow=overflow,
            checks=['普通三环DBGEXEC拥有Studio，CPUINFO发现本次编译任务后DEBUG真实暂停','真实编译器返回0并回收，同一PID另一真实SCX也返回0，代数实际递增；恢复Studio必须拒绝此0','全部页/目标/队列严格回收'],
            limits='独立Studio草稿实际PID复用专测，非全M8或编译中编辑测试')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
