#!/usr/bin/env python3
"""mio：同一G2/核/窗口/源程序，测NUI逐行指针写入的实际成本。

旧片段取已冻结局部合成包，当前片段取当前源码。两份普通三环
程序除NUI内容外完全相同，交替跑三轮200PITtick，每帧仍填满
真实1024模式客户区并提交ARGB。不降低像素数、不用HOST代码作
原生性能证据。结果只代表此填色+提交负载，不代表所有GUI帧率。
"""
import hashlib,json,statistics,struct,zipfile
from pathlib import Path
import verify_canvas as v

ROOT=v.ROOT;OUT=ROOT/'build/m8-canvas/row-cost';t=v.t;q=v.q;compiler=v.compiler;theme=v.theme
SOURCE='''/* mio：两份探针使用完全同一源码，只有私有NUI片段不同。
 * 不渲染文字/菜单，避免其它布局改动影响填色成本；两个颜色取
 * 既有主题角色，每帧完整覆盖同一实际客户区，并走相同FRAME32。
 * 首帧/分配结束才置ready，不能把初始化算入其中一种实现的耗时。 */
#include "SCAPI.H"
#include "NUI.inc"
static volatile u32 bench_state,bench_loops,bench_start,bench_end,bench_last;
int main(void){
    if(ui_open("NUI row cost / mio")<0)return 1;
    ui_physical_rgb(0,0,ui_width,ui_height,ui_role(SC_THEME_PAPER));ui_present();bench_state=1;
    for(;;){
        int key=sc_key();if(key==27)return 0;
        /* 使用客体PIT计时，不把宿主发送命令/截屏时间当CPU吞吐。
         * 完成最后一整帧后再停止，报告真实超出的tick而非固定填200。 */
        if(key=='r'&&bench_state!=2){bench_loops=0;bench_start=sc_tick();bench_state=2;}
        if(bench_state==2){
            bench_last=ui_role((bench_loops&1)?SC_THEME_FACE:SC_THEME_PAPER);
            ui_physical_rgb(0,0,ui_width,ui_height,bench_last);ui_present();bench_loops++;
            bench_end=sc_tick();if(bench_end-bench_start>=200)bench_state=3;
        }
        sc_yield();
    }
}
'''.encode('utf-8')


def sha(blob):return hashlib.sha256(blob).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    v.OUT=OUT;v.bind();archive_path=ROOT/'build/M8-damage-evidence.zip'
    assert sha(archive_path.read_bytes())=='15d34f56a440bef7905eeaa673d20b96a5c9048f4fb6556d1a91d70e4a9ff390'
    with zipfile.ZipFile(archive_path) as archive:old=archive.read('sandcore/user/NUI.inc')
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert sha(g2)=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    new=(ROOT/'user/NUI.inc').read_bytes();header=(ROOT/'user/SCAPI.H').read_bytes()
    inputs={name:sha((ROOT/name).read_bytes()) for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img','user/SCAPI.H','user/NUI.inc','kernel/font16.txt')}
    verifier=sha(Path(__file__).read_bytes())
    for name,data in (('source.c',SOURCE),('old-NUI.inc',old),('current-NUI.inc',new),('SCAPI.H',header)):(OUT/name).write_bytes(data)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for name,nui in (('OLD',old),('NEW',new)):
            for leaf,data in (('P.C',SOURCE),('NUI.inc',nui),('SCAPI.H',header)):compiler.disk_put(disk,'SYS/BENCH/'+name+'/'+leaf,data)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'row-cost',prepare);disk=OUT/'sanddata-std-128-row-cost.img';samples=[];native={}
    try:
        t.open_shell(True);v.idle();kernel=q.symbols()
        for name in ('OLD','NEW'):
            code=compiler.compile_native(disk,'BIN/G2.SCX','SYS/BENCH/'+name+'/P.C','HOME/'+name+'.SCX',240);v.idle()
            mapping=compiler.await_file(disk,'HOME/'+name+'.SCX.map')
            (OUT/(name.lower()+'-native.scx')).write_bytes(code);(OUT/(name.lower()+'-native.map')).write_bytes(mapping)
            native[name]=dict(scx_sha256=sha(code),map_sha256=sha(mapping))
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        for round_number in range(3):
            for name in ('OLD','NEW'):
                q.text('run HOME/'+name+'.SCX\n');w=v.wait(theme.window,'真实G2 '+name+' NUI')
                symbols=theme.native_symbols((OUT/(name.lower()+'-native.map')).read_bytes())
                def number(field):return theme.user_word(w,symbols[field])
                v.wait(lambda:number('bench_state')==1,'相同窗口/私有帧初始化')
                q.key('r');v.wait(lambda:number('bench_state')==3 and t.word(kernel['dirty'])==0,'完整200tick实际负载结束')
                with t.stable_frame():
                    width,height=w['cw'],w['ch'];size=width*height*4
                    expected=struct.pack('<I',0xFF000000|(number('bench_last')&0xFFFFFF))*(width*height)
                    assert v.user_bytes(w,number('ui_pixels'),size)==q.memory(w['canvas'],size)==expected,'实际源/提交完整像素错误'
                    assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096)
                    ticks=number('bench_end')-number('bench_start');loops=number('bench_loops');assert ticks>=200 and loops>0
                    samples.append(dict(version=name,round=round_number+1,width=width,height=height,bytes_per_frame=size,loops=loops,pit_ticks=ticks,
                                        frames_per_200_ticks=loops*200/ticks,frames_per_guest_second=loops*100/ticks))
                q.shot(f'{name.lower()}-{round_number+1}-full-pixels');q.key('esc');v.idle()
                v.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'两种NUI实际负载严格回收')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values()),overflow
        assert inputs=={name:sha((ROOT/name).read_bytes()) for name in inputs} and verifier==sha(Path(__file__).read_bytes())
        medians={name:statistics.median(row['frames_per_200_ticks'] for row in samples if row['version']==name) for name in ('OLD','NEW')}
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,source_sha256=sha(SOURCE),
                    old_archive_sha256=sha(archive_path.read_bytes()),old_nui_sha256=sha(old),current_nui_sha256=sha(new),native=native,samples=samples,
                    median_frames_per_200_ticks=medians,ratio_new_over_old=medians['NEW']/medians['OLD'],overflow=overflow,
                    limits='Windows QEMU TCG/128MB/1024/100，同一G2普通三环全客户区填色+FRAME32负载；不是所有GUI60Hz或游戏/光追性能')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
