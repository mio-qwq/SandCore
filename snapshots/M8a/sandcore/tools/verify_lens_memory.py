#!/usr/bin/env python3
"""mio：实际三环占页后打开8MB图片，验证Lens的OOM完整回滚。

最多两份由G2编译的普通程序只用公开ALLOC/MONITOR占用物理页，留约
4MB余量。不得修改页计数/内存上限或Lens状态，候选OOM也必须
真实经历分配与返回。关闭占页窗口后同一路径重新打开须成功。
"""
import hashlib,json,struct,time,zipfile
from pathlib import Path
import verify_files as seed

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-lens';OUT=STAGE/'memory'
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
SOURCE='''/* mio：普通三环内存压力探针，不修改内核或其它任务。
 * 每次从公开MONITOR读取真实空闲字节，保留4MB加页表余量，
 * 让Lens已有UI继续工作，但8MB候选不能分配成功。单个高端
 * 堆最多64MB，所以实例可能先触及虚址上限；另一实例继续。
 * 指针留在数组，不主动free；退出由已有生命周期严格回收。 */
#include "SCAPI.H"
volatile u32 reserve_state[8];
static void *held[32];
int main(void)
{
    u32 memory[48];int info[5];
    int win=sc_open_rgb("Memory reserve",160,90);
    if(win<0)return 1;
    sc_info(win,info);sc_fill_rgb(win,0,0,info[2],info[3],SC_RGB_PAPER);
    sc_text_rgb(win,8,8,"Memory reserve",SC_RGB_INK);
    sc_monitor(memory);reserve_state[2]=memory[4];reserve_state[0]=1;
    for(int i=0;i<32;i++){
        sc_monitor(memory);
        if(memory[4]<=4u*1024*1024+65536)break;
        u32 bytes=memory[4]-4u*1024*1024-65536;
        if(bytes>8u*1024*1024)bytes=8u*1024*1024;
        held[i]=sc_alloc(bytes);
        if(!held[i])break;
        reserve_state[1]+=bytes;reserve_state[4]++;
        sc_yield();
    }
    sc_monitor(memory);reserve_state[3]=memory[4];reserve_state[0]=2;
    while(sc_key()!=27)sc_yield();
    return 0;
}
'''


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    (OUT/'reserve.c').write_text(SOURCE,encoding='utf-8')
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    native=(STAGE/'lens-native.scx').read_bytes();mapping=(STAGE/'lens-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native_map_sha256']
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    image=struct.pack('<8s6I',b'SCB2MIO\0',1920,1080,2,0,1920*1080*4,0x004F494D)+struct.pack('<I',0xFF335577)*(1920*1080)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'SYS/SRC/reserve.c',SOURCE.encode())
        compiler.disk_put(disk,'HOME/LEN.SCX',native);compiler.disk_put(disk,'HOME/FULL.SCB',image)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'lens-memory',prepare);holders=[]
    disk=OUT/'sanddata-std-128-lens-memory.img';addresses=theme.native_symbols(mapping)
    try:
        t.open_shell(True);v.idle()
        helper=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/reserve.c','HOME/FILL.SCX',180);v.idle()
        helper_map=compiler.await_file(disk,'HOME/FILL.SCX.map');helper_addresses=theme.native_symbols(helper_map)
        (OUT/'reserve-native.scx').write_bytes(helper);(OUT/'reserve-native.map').write_bytes(helper_map)
        base=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        shell=t.windows()[0];q.text('run HOME/LEN.SCX HOME/FULL.SCB\n');w=seed.wait(theme.window,'实际G2 Lens大图')
        def number(name):return theme.user_word(w,addresses[name])
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'Lens实际完整帧',180)
        def state():
            with t.stable_frame():
                return dict(filename=v.user_bytes(w,addresses['filename'],64),metadata={name:number(name) for name in
                    ('image_w','image_h','cache_w','cache_h','loaded','scb_version','fit','pan_x','pan_y','preview','fit_pixels','fit_w','fit_h')},
                    image=v.user_bytes(w,number('preview'),1920*1080*4),filtered=v.user_bytes(w,number('fit_pixels'),number('fit_w')*number('fit_h')*4))
        def holder_state(win):return struct.unpack('<8I',v.user_bytes(win,helper_addresses['reserve_state'],32))
        def focus_tab(index,count):
            bw=max(40,min(140,(1024-240)//count));t.point(116+bw*index+bw//2,752);t.click()
        seed.wait(lambda:number('loaded')==1 and number('ui_frames')>=2,'大图原始缓存/过滤/显示',180)
        q.key('1');present();q.key('c');present();before=state()
        for _ in range(2):
            focus_tab(0,len(t.windows()));seed.wait(lambda:current_focus(shell),'真实Shell任务栏焦点')
            previous={win['handle'] for win in t.windows()};q.text('run HOME/FILL.SCX\n')
            holder=seed.wait(lambda:next((win for win in t.windows() if win['handle'] not in previous),None),'普通三环占页程序')
            seed.wait(lambda:holder_state(holder)[0]==2,'公开ALLOC实际占页完成',120);holders.append(holder)
            if holder_state(holder)[3]<=4*1024*1024+65536:break
        rows=[list(holder_state(win)) for win in holders]
        assert rows[-1][3]<8*1024*1024 and all(row[1]>0 for row in rows),'没有真实制造大图候选内存不足'
        focus_tab(1,len(t.windows()));seed.wait(lambda:number('ui_focus')==1,'实际返回Lens焦点');present()
        pages=t.word(kernel['pf_used']);counter=number('lens_operations')
        q.key('f1');seed.wait(lambda:number('ui_modal')==2,'内存压力下真实Open')
        q.text('HOME/FULL.SCB\n');seed.wait(lambda:number('lens_operations')>counter,'8MB候选真实OOM返回',180);present()
        assert state()==before,'内存不足破坏整幅原图/过滤缓存/倍率/平移'
        assert v.user_bytes(w,addresses['status'],100).split(b'\0')[0]==b'Not enough memory for image'
        assert t.word(kernel['pf_used'])==pages,'失败分配的候选/临时页表未归还'
        q.shot('01-real-oom-keeps-complete-image')
        # 三环占页程序正常退出，不能把宿主修改内存计数当释放。
        for holder in reversed(holders):
            raw=q.memory(kernel['wins'],t.word(kernel['nwins'])*80)
            handles=[struct.unpack_from('<I',raw,index+8)[0] for index in range(0,len(raw),80)]
            # wins按Z序移动，任务栏按稳定handle的升序排名；不能
            # 直接把Z序的下标当任务栏槽，否则Esc可能关掉Lens。
            handles.sort();focus_tab(handles.index(holder['handle']),len(handles))
            seed.wait(lambda:current_focus(holder),'占页窗口实际焦点');q.key('esc')
            seed.wait(lambda:not any(win['handle']==holder['handle'] for win in t.windows()),'占页任务真实退出')
        focus_tab(1,2);seed.wait(lambda:number('ui_focus')==1,'回到Lens');counter=number('lens_operations')
        q.key('f1');seed.wait(lambda:number('ui_modal')==2,'释放后重新Open')
        q.text('HOME/FULL.SCB\n');seed.wait(lambda:number('lens_operations')>counter,'释放后相同候选实际成功',180);present()
        assert number('fit')==1 and v.user_bytes(w,number('preview'),len(image)-32)==image[32:]
        q.shot('02-same-image-opens-after-release');q.key('esc');v.idle()
        seed.wait(lambda:t.word(kernel['pf_used'])==base[0]+t.word(kernel['desktop_pages'])-base[1],'两占页程序/Lens完整回收')
        q.shot('03-all-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=core['native_sha256'],native_map_sha256=core['native_map_sha256'],
            helper_source_sha256=sha(OUT/'reserve.c'),helper_native_sha256=hashlib.sha256(helper).hexdigest(),helper_map_sha256=hashlib.sha256(helper_map).hexdigest(),holders=rows,overflow=overflow,
            checks=['真实G2普通三环占用物理页；8MB候选OOM保留完整原图/过滤/文件身份/倍率/平移，候选临时页严格回滚','占页任务退出后相同路径实际成功；全部任务私有页/画布/页表回收'],limits='Lens1024/100真实候选OOM，不代替Fit缓存OOM/读取中关闭或全部M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def current_focus(win):
    raw=q.memory(q.symbols()['extras'],6*56)
    # 复核WM的实际top_visible事实；没有不存在的全局focused
    # 变量，也不以“符号缺失就通过”的退路冒充焦点断言。
    extras={row[0]:row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]}
    visible=[row for row in t.windows() if row['handle'] in extras and not extras[row['handle']][3]]
    return bool(visible) and visible[-1]['handle']==win['handle'] and not t.word(q.symbols()['menu_open']) and not t.word(q.symbols()['context_open'])


if __name__=='__main__':main()
