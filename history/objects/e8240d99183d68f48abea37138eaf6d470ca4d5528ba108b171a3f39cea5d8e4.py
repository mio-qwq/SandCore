#!/usr/bin/env python3
"""mio：相同G2/核/窗口/源码的Studio真实长文档绘制成本对照。

旧源码来自已冻结Files包；新版为当前草稿。普通测试外层包含
完整编辑器，只把原main重命名后以相同公开API打开真实源文件，
持续draw+完整FRAME32。使用真实PIT、完整客户帧，不降低分辨率，
不以宿主代码/循环计数替代原生执行；数值只代表这项绘制负载。
"""
import hashlib,json,statistics,struct,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'cost'
SOURCE='''/* mio：两份普通三环外层源码完全相同，IDE.inc分别为
 * 冻结旧Studio或当前草稿。仍调用其真实open_file/UTF-8光标/
 * draw/整帧提交，不抽取一个孤立索引函数冒充用户界面性能。
 * 强制连续完整重画只用于测成本，正式编辑器仍事件节流。 */
#define main studio_original_main
#include "IDE.inc"
#undef main
static volatile u32 bench_state,bench_loops,bench_start,bench_end;
int main(void)
{
    if(ui_open("Studio cost / mio")<0)return 1;
    char args[128];sc_args(args,sizeof(args));
    if(!open_file(args))return 2;
    cursor=used;ui_pointer();draw();ui_present();bench_state=1;
    for(;;){
        int key=sc_key();if(key==27)return 0;
        if(key=='r'&&bench_state!=2){bench_loops=0;bench_start=sc_tick();bench_state=2;}
        if(bench_state==2){
            draw();ui_present();bench_loops++;bench_end=sc_tick();
            if(bench_end-bench_start>=200)bench_state=3;
        }
        sc_yield();
    }
}
'''.encode()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    archive_path=ROOT/'build/M8-files-evidence.zip'
    assert sha(archive_path)=='0db50bbbd44ca4e642560b563523eaf7f1049cbcd172558570cf535a5794fc9d'
    with zipfile.ZipFile(archive_path) as archive:
        old=archive.read('sandcore/user/ide.c')
        for name in ('build/sandcore.img','user/SCAPI.H','user/NUI.inc','kernel/font16.txt'):
            assert hashlib.sha256(archive.read('sandcore/'+name)).hexdigest()==sha(ROOT/name),name
    new=(ROOT/'build/m8-next/ide.c').read_bytes()
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    # 同一65000B分别表现“65000行/最后空行”和“单条超长注释”；
    # 两者都真实显示末尾光标，揭示反复扫描行与扫描显示列的差别。
    fixtures={'ROWS.C':b'\n'*65000,'LONG.C':b'//'+b'a'*64998}
    for name,data in (('bench.c',SOURCE),('old-IDE.inc',old),('new-IDE.inc',new)):(OUT/name).write_bytes(data)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for version,ide in (('OLD',old),('NEW',new)):
            for leaf,data in (('P.C',SOURCE),('IDE.inc',ide),('SCAPI.H',(ROOT/'user/SCAPI.H').read_bytes()),('NUI.inc',(ROOT/'user/NUI.inc').read_bytes())):
                compiler.disk_put(disk,'SYS/BENCH/'+version+'/'+leaf,data)
        for name,data in fixtures.items():compiler.disk_put(disk,'HOME/'+name,data)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'studio-cost',prepare);disk=OUT/'sanddata-std-128-studio-cost.img'
    artifacts={};samples=[]
    try:
        t.open_shell(True);v.idle()
        for version in ('OLD','NEW'):
            native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/BENCH/'+version+'/P.C','HOME/'+version+'.SCX',300);v.idle()
            mapping=compiler.await_file(disk,'HOME/'+version+'.SCX.map');(OUT/(version.lower()+'-native.scx')).write_bytes(native);(OUT/(version.lower()+'-native.map')).write_bytes(mapping)
            artifacts[version]=dict(scx_sha256=hashlib.sha256(native).hexdigest(),map_sha256=hashlib.sha256(mapping).hexdigest())
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        for fixture,data in fixtures.items():
            for round_number in range(3):
                for version in ('OLD','NEW'):
                    q.text('run HOME/'+version+'.SCX HOME/'+fixture+'\n');w=seed.wait(theme.window,'实际G2 '+version+' '+fixture)
                    addresses=theme.native_symbols((OUT/(version.lower()+'-native.map')).read_bytes())
                    def number(name):return theme.user_word(w,addresses[name])
                    seed.wait(lambda:number('bench_state')==1,'真实源/末尾光标/完整初帧',180)
                    assert number('used')==number('cursor')==len(data)
                    q.key('r');seed.wait(lambda:number('bench_state')==3 and t.word(kernel['dirty'])==0,'实际至少200PITtick整帧成本',180)
                    with t.stable_frame():
                        win=next(row for row in t.windows() if row['handle']==w['handle']);size=win['cw']*win['ch']*4
                        frame=v.user_bytes(w,number('ui_pixels'),size);assert frame==q.memory(win['canvas'],size)
                        assert v.user_bytes(w,addresses['source'],len(data)+1)==data+b'\0'
                        assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096)
                        ticks=number('bench_end')-number('bench_start');loops=number('bench_loops');assert ticks>=200 and loops>0
                        if version=='NEW':
                            starts=[0]+[i+1 for i,c in enumerate(data) if c==10];assert number('line_count')==len(starts)
                            assert list(struct.unpack('<'+str(len(starts))+'I',v.user_bytes(w,addresses['line_starts'],len(starts)*4)))==starts
                        samples.append(dict(version=version,fixture=fixture,round=round_number+1,width=win['cw'],height=win['ch'],bytes_per_frame=size,
                            loops=loops,pit_ticks=ticks,frames_per_200_ticks=loops*200/ticks,frames_per_guest_second=loops*100/ticks,source_bytes=len(data),source_sha256=hashlib.sha256(data).hexdigest(),
                            first_line=number('first_line'),left_column=number('left_column'),editor_rows=number('editor_rows'),editor_columns=number('editor_columns'),frame_sha256=hashlib.sha256(frame).hexdigest()))
                    q.shot(f'{fixture.lower()}-{version.lower()}-{round_number+1}');q.key('esc');v.idle()
                    seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'两版本负载严格回收')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        medians={fixture:{version:statistics.median(row['frames_per_200_ticks'] for row in samples if row['fixture']==fixture and row['version']==version) for version in ('OLD','NEW')} for fixture in fixtures}
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,old_archive_sha256=sha(archive_path),old_source_sha256=hashlib.sha256(old).hexdigest(),new_source_sha256=hashlib.sha256(new).hexdigest(),
            native=artifacts,samples=samples,median_frames_per_200_ticks=medians,ratio_new_over_old={fixture:row['NEW']/row['OLD'] for fixture,row in medians.items()},overflow=overflow,
            limits='同一历史G2/7a03638a核/1024×768100%/128MB Windows QEMU TCG，真实65000B末尾光标/完整Studio draw+FRAME32；新旧菜单/行选择/滚动样式也不同，不是孤立索引指令带宽，不代表全GUI输入延迟/固定60Hz或游戏光追')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
