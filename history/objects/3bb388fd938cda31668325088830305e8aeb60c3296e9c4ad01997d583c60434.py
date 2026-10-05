#!/usr/bin/env python3
"""mio：Canvas实际G2编译、真彩笔画、撤销及文件失败回滚证据。

只在开机前准备测试副本；运行期间键盘走HMP、鼠标走QMP，磁盘
写入必须来自普通三环程序。页表/堆只读，不把修改客体内存当输入。
打开失败比较完整655392B文档和655360B撤销，而非仅看状态栏。
成功报告不覆盖，重新运行用--tag，失败截图保留在各自目录。
"""
import hashlib
import json
import struct
import time
import zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-canvas'
q=t.q
PIXELS=512*320
BYTES=32+PIXELS*4


def sha(blob):return hashlib.sha256(blob).hexdigest()


def bind():
    OUT.mkdir(parents=True,exist_ok=True)
    t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT


def wait(test,label,seconds=50):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        assert 3 not in compiler.task_states(),label+' / 普通三环异常'
        time.sleep(.12)
    q.shot('timeout');raise AssertionError(label)


def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1
         and 2 not in compiler.task_states()[1:],'所有临时进程实际回收')
    symbols=q.symbols()
    def prompt():
        raw=q.memory(symbols['terminals'],6*28)
        for index in range(6):
            handle,text,used=struct.unpack_from('<3I',raw,index*28)
            if handle==t.windows()[0]['handle'] and used:
                return q.memory(text,used).endswith(b'> ') and t.word(symbols['dirty'])==0
        return False
    wait(prompt,'Shell真实提示符和历史区初始化完成')


def user_bytes(window,address,count):
    """按真实CR3读私有区；连续物理页合并读取，跨洞重新起段。

    大作品不能每页两次HMP查表，耗时会掩盖应用行为。先只读各个
    PDE/PTE整表，再按实际相邻页合并pmemsave；不假定堆永久连续。
    """
    pd=t.word(q.symbols()['tasks']+window['owner']*168)
    directory=struct.unpack('<1024I',q.memory(pd,4096));tables={};runs=[]
    while count:
        index=address>>22;pde=directory[index]
        assert pde&5==5,('缺用户PDE',hex(address))
        if index not in tables:tables[index]=struct.unpack('<1024I',q.memory(pde&~4095,4096))
        pte=tables[index][(address>>12)&1023]
        assert pte&5==5,('缺用户PTE',hex(address))
        physical=(pte&~4095)+(address&4095);n=min(count,4096-(address&4095))
        if runs and runs[-1][0]+runs[-1][1]==physical:runs[-1][1]+=n
        else:runs.append([physical,n])
        count-=n;address+=n
    return b''.join(q.memory(a,n) for a,n in runs)


def main():
    bind();assert not (OUT/'results.json').exists(),'成功报告不可覆盖，请用--tag'
    names=('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img',
           'user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','user/canvas.c','user/lens.c','user/pathprobe.c',
           'kernel/font16.txt','tools/verify_canvas.py')
    inputs={name:sha((ROOT/name).read_bytes()) for name in names}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert sha(g2)=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    header=struct.pack('<8s6I',b'SCB2MIO\0',512,320,2,0,PIXELS*4,0x004F494D)
    # 测试作品的RGB/alpha是文档内容，不是绕过GFX角色表新增UI色号。
    good=header+b''.join(struct.pack('<I',((96 if (x//64+y//40)&1 else 255)<<24)
        |((x*255//511)<<16)|((y*255//319)<<8)|((x+y)&255)) for y in range(320) for x in range(512))
    old=struct.pack('<8s4I',b'SCB1MIO\0',512,320,1,0x004F494D)+bytes((x//16+y//10)%105 for y in range(320) for x in range(512))
    bad_old=old[:-1]+b'\xff';bad_flags=bytearray(good);struct.pack_into('<I',bad_flags,20,1)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for name in ('canvas.c','lens.c','pathprobe.c','NUI.inc','IMAGE.inc'):
            compiler.disk_put(disk,'SYS/SRC/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        for name,blob in (('GOOD.SCB',good),('OLD.SCB',old),('BADOLD.SCB',bad_old),
                          ('BADFLAG.SCB',bad_flags),('SHORT.SCB',good[:-4]),('沙核.SCB',good)):
            compiler.disk_put(disk,'HOME/'+name,blob)
    proc=t.launch('std',128,'canvas',prepare);disk=OUT/'sanddata-std-128-canvas.img'
    checks=[];artifacts={};w=None;native_symbols=None
    try:
        t.open_shell(True);idle()
        for source,output in (('canvas','CAN'),('lens','LENS'),('pathprobe','PATH')):
            native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/'+source+'.c','HOME/'+output+'.SCX',240);idle()
            mapping=compiler.await_file(disk,'HOME/'+output+'.SCX.map')
            (OUT/(source+'-native.scx')).write_bytes(native);(OUT/(source+'-native.map')).write_bytes(mapping)
            artifacts[source]=dict(scx_sha256=sha(native),map_sha256=sha(mapping))
        native_symbols=theme.native_symbols((OUT/'canvas-native.map').read_bytes())
        symbols=q.symbols()
        def number(name):return theme.user_word(w,native_symbols[name])
        def text(name,n=96):return user_bytes(w,native_symbols[name],n).split(b'\0')[0]
        def present():
            frames=number('ui_frames')
            wait(lambda:number('ui_frames')>frames and t.word(symbols['dirty'])==0,'真实客户帧提交')
        def click(x,y):
            current=[v for v in t.windows() if v['handle']==w['handle']][0]
            scale=number('ui_scale');title=current['h']-current['ch']-1
            t.point(current['x']+1+x*scale//100,current['y']+title+y*scale//100);t.click()
        def stroke_at(offset=0):
            x=number('paper_x')+number('paper_w')//5;y=number('paper_y')+number('paper_h')//2+offset
            current=[v for v in t.windows() if v['handle']==w['handle']][0]
            title=current['h']-current['ch']-1;scale=number('ui_scale')
            t.point(current['x']+1+x*scale//100,current['y']+title+y*scale//100)
            q.button(True);time.sleep(.25)
            for _ in range(5):q.move(22,5);time.sleep(.12)
            q.button(False);wait(lambda:number('dirty')==1 and number('drawing')==0,'完整连续笔画结束');present()
        def state():
            with t.stable_frame():
                return (user_bytes(w,number('document'),BYTES),user_bytes(w,number('undo_pixels'),PIXELS*4),
                        number('dirty'),number('have_undo'),text('filename',64))
        def operation(key,path,confirm=None):
            counter=number('canvas_operations');q.key(key)
            wait(lambda:number('ui_modal')==2,'路径框真实输入就绪');q.text(path+'\n')
            if confirm is not None:
                wait(lambda:number('ui_modal')==3,'合法候选完成后才询问丢弃');q.key(confirm)
            wait(lambda:number('canvas_operations')>counter,'文件读取/写入/回滚实际结束',70);present()
        def reclaim(base):
            idle();wait(lambda:t.word(symbols['pf_used'])==base[0]+t.word(symbols['desktop_pages'])-base[1],
                        '作品/撤销/私有帧/画布/页表严格回收')
        baseline=(t.word(symbols['pf_used']),t.word(symbols['desktop_pages']))
        q.text('run HOME/CAN.SCX\n');w=wait(theme.window,'真实原生Canvas')
        wait(lambda:number('ui_frames')>=2 and number('document')!=0,'画板完成初始化')
        blank=state()[0];assert blank[:32]==header
        click(274,80);wait(lambda:number('menu_open')==1,'鼠标工具菜单')
        click(number('menu_x')+24,number('menu_y')+8+72+12)
        wait(lambda:number('ui_modal')==1,'真实RGB输入框');q.text('3055CC\n')
        wait(lambda:number('color_rgb')==0x3055CC and number('menu_open')==0,'六位RGB生效')
        stroke_at();painted=state();assert painted[0]!=blank and painted[1]==blank[32:] and painted[3]==1
        words=struct.unpack('<'+str(PIXELS)+'I',painted[0][32:])
        assert 0xFF3055CC in words and len(set(words))>3,'真实圆笔中心/软边颜色'
        q.key('u');present();assert state()[0]==blank,'撤销整条连续笔画'
        q.key('u');present();assert state()[0]==painted[0],'重做原笔画逐字节一致'
        q.shot('01-rgb-soft-stroke-undo-redo');checks.append('鼠标工具/RGB3055CC/连续圆形软边笔画/完整Undo与Redo逐字节')
        for index,path in enumerate(('HOME/BADOLD.SCB','HOME/BADFLAG.SCB','HOME/SHORT.SCB','HOME/MISSING.SCB')):
            before=state();pages=t.word(symbols['pf_used']);operation('o',path)
            assert state()==before,path+' / 失败改变作品或撤销状态'
            assert t.word(symbols['pf_used'])==pages,'坏候选泄漏私有页'
            q.shot('02-rejected-'+str(index))
        checks.append('坏SCB1最后槽位/坏SCB2flags/短正文/不存在路径，完整作品与Undo/dirty/have_undo/filename不变且候选页回收')
        protected=compiler.file_content(disk,'SYS/CORE/CORE.SKM');before=state()
        operation('s','SYS/CORE/CORE.SKM')
        assert state()==before and compiler.file_content(disk,'SYS/CORE/CORE.SKM')==protected,'核心保护保存不能破坏文件/作品'
        q.shot('03-protected-save-kept');checks.append('核心路径强制保护，保存失败保留作品/Undo/dirty/filename和原CORE.SKM')
        operation('s','HOME/TRUE.SCB');saved=compiler.file_content(disk,'HOME/TRUE.SCB')
        assert saved==painted[0] and number('dirty')==0 and text('filename',64)==b'HOME/TRUE.SCB'
        (OUT/'paint-saved.scb').write_bytes(saved);q.shot('04-scb2-saved');stroke_at(-18)
        before=state();pages=t.word(symbols['pf_used']);operation('o','HOME/GOOD.SCB','esc')
        assert state()==before and t.word(symbols['pf_used'])==pages,'合法候选取消必须保留完整作品/Undo并释放候选'
        operation('o','HOME/GOOD.SCB','y');assert state()[0]==good and number('dirty')==number('have_undo')==0
        q.shot('05-rgba-reopened');checks.append('SCB2保存655392B精确头/正文，透明ARGB候选取消与提交、成功打开明确清Undo')
        operation('o','HOME/OLD.SCB');palette=struct.unpack('<256I',user_bytes(w,native_symbols['document_palette'],1024))
        expected=header+b''.join(struct.pack('<I',0xFF000000|palette[value]) for value in old[24:])
        assert state()[0]==expected,'旧SCB1每个槽位必须按原DAC转换'
        q.shot('06-old-scb1-converted');checks.append('旧512×320 SCB1全部像素按原DAC转ARGB，主题不改作品')
        q.key('o');wait(lambda:number('ui_modal')==2,'中文文件浏览器')
        table=user_bytes(w,native_symbols['names'],512*64)
        directory_names=[table[i*64:(i+1)*64].split(b'\0')[0] for i in range(512)]
        target=directory_names.index('沙核.SCB'.encode());rows=(number('UI_H')-200)//24
        assert rows>0
        # 首页能显示的真实行数按共用路径框公式推导；翻页通过实际Next。
        h=number('UI_H')-48;list_y=124;nav_y=12+h-76;rows=(nav_y-4-list_y)//24
        first=0
        while target>=first+rows:
            x=(number('UI_W')-(680 if number('UI_W')>720 else number('UI_W')-32))//2
            click(x+112,nav_y+10);first+=rows
        width=680 if number('UI_W')>720 else number('UI_W')-32;x=(number('UI_W')-width)//2
        click(x+80,list_y+(target-first)*24+8);counter=number('canvas_operations');q.key('ret')
        wait(lambda:number('canvas_operations')>counter,'中文路径真正打开');present()
        assert text('filename',64)=='HOME/沙核.SCB'.encode() and state()[0]==good
        q.shot('07-phoenix-unicode-path');checks.append('实际鼠标目录选择中文沙核.SCB，真实UTF-8文件名与字库呈现')
        stroke_at();q.key('esc');wait(lambda:number('ui_modal')==3,'未保存关闭确认');q.key('esc')
        wait(lambda:number('ui_modal')==0,'取消关闭回到作品');assert len(t.windows())==2 and number('dirty')==1
        q.key('esc');wait(lambda:number('ui_modal')==3,'再次关闭确认');q.key('y');reclaim(baseline)
        q.shot('08-canvas-reclaimed');checks.append('未保存关闭取消保留任务，确认退出严格回收所有文档/Undo/窗口/页表')
        lens_symbols=theme.native_symbols((OUT/'lens-native.map').read_bytes())
        baseline=(t.word(symbols['pf_used']),t.word(symbols['desktop_pages']))
        q.text('run HOME/LENS.SCX HOME/TRUE.SCB\n');w=wait(theme.window,'Lens实际重新读取画板文件')
        wait(lambda:theme.user_word(w,lens_symbols['loaded'])==1 and theme.user_word(w,lens_symbols['ui_frames'])>=2,'Lens真实SCB2读取与提交')
        assert theme.user_word(w,lens_symbols['scb_version'])==2
        assert user_bytes(w,lens_symbols['preview'],PIXELS*4)==saved[32:],'Lens完整预览必须等于画板保存ARGB'
        q.shot('09-lens-reopens-scb2');q.key('esc');reclaim(baseline)
        checks.append('实际G2 Lens重新打开Canvas保存的SCB2，512×320缓存逐字节等于作品')
        path_symbols=theme.native_symbols((OUT/'pathprobe-native.map').read_bytes())
        baseline=(t.word(symbols['pf_used']),t.word(symbols['desktop_pages']))
        q.text('run HOME/PATH.SCX\n');w=wait(theme.window,'路径UTF-8普通三环探针')
        wait(lambda:theme.user_word(w,path_symbols['ui_modal'])==2,'UTF-8路径框就绪');q.key('right');q.key('backspace')
        wait(lambda:user_bytes(w,path_symbols['path_probe_text'],64).split(b'\0')[0]=='HOME/沙'.encode(),'整汉字标量退格')
        q.shot('10-unicode-path-backspace');q.key('esc')
        wait(lambda:theme.user_word(w,path_symbols['path_probe_stage'])==2,'取消路径框正常返回');q.key('esc');reclaim(baseline)
        q.shot('11-all-reclaimed');checks.append('共用路径框Right取消初始全选、Backspace一次删除核的完整UTF-8标量、取消/退出严格回收')
        overflow={name:t.word(symbols[name]) for name in ('keyboard_overflow','event_overflow')}
        assert not any(overflow.values()),overflow
        assert inputs=={name:sha((ROOT/name).read_bytes()) for name in names},'验证期间输入改变'
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,g2_sha256=sha(g2),native=artifacts,
                    checks=checks,overflow=overflow,saved_sha256=sha(saved),
                    limits='std/128MB/1024×768/100% Aurora功能验证；五档×三档/Classic/VGA/窗口操作与共用Settings回归继续，不宣称完整M8通过')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':
    import argparse,re
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tag',help='新独立复测目录，不覆盖成功报告')
    args=parser.parse_args()
    if args.tag:
        assert re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',args.tag),'标签只允许字母/数字/下划线/连字符'
        OUT=OUT/args.tag
    main()
