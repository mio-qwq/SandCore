#!/usr/bin/env python3
"""mio：同一真实G2产物的十五显示组合及Classic局部合成矩阵。

为什么不在每个模式重新编译：此处验证显示合成，应用字节必须相同。
种子来自verify_damage的真实系统内编译报告，先核对源码、SCX和符号。
每个组合启动独立Windows双盘QEMU，配置只写未启动的测试盘副本。
拖动比较发生在仍按住左键时；释放按钮可能触发完整重画，不能拿
释放后的漂亮截图代替拖动局部路径的显存证明。读PCI只排除走秒的
任务栏，光标/全部窗口/桌面都参与逐字节比较，没有遮罩或像素容差。
"""
import hashlib
import json
import struct
import time
from pathlib import Path
import verify_damage as seed

ROOT=seed.ROOT
STAGE=ROOT/'build/m8-damage'
OUT=STAGE/'matrix'
t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,native,address,inputs):
    directory=OUT/f'{width}-{height}-{scale}-{style.lower()}'
    assert not (directory/'results.json').exists(),'成功组合不得覆盖'
    directory.mkdir(parents=True,exist_ok=True)
    # 各模块共享旧监视器工具，但必须明确绑定本组合目录。否则截图和
    # 只读内存转储可能落入种子目录，破坏精确来源与不可覆盖的证据。
    t.OUT=t.q.OUT=compiler.OUT=theme.OUT=seed.OUT=directory
    symbols=q.symbols();bar=32*scale//100
    def number(name):return t.word(symbols[name])
    def wait(test,label):return seed.wait(test,label,50)
    def prepare(disk):
        compiler.disk_put(disk,'HOME/DAMAGE.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',
            f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode('ascii'))
        compiler.disk_put(disk,'SYS/THEME.CFG',
            (ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch('std',128,'damage-matrix',prepare)
    cases=[];start=time.monotonic();baseline=None;early_baseline=None
    try:
        q.key('ret');wait(lambda:not t.windows() and number('dirty')==0,'纯桌面实际就绪')
        assert (number('gfx_width'),number('gfx_height'),number('scale_percent'))==(width,height,scale)
        # 开始菜单位置取本模式的实际任务栏；旧open_shell固定750，
        # 在480行模式会把指针推到屏幕边缘而点不到开始按钮。
        t.point(30,height-bar//2);t.click()
        wait(lambda:number('menu_open')!=0,'真实开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');wait(lambda:len(t.windows())==1,'菜单启动真实Shell')
        early_baseline=(number('pf_used'),number('desktop_pages'))
        def shell_ready():
            # WINCREATE先建立窗口，随后TERMINAL CONTROL才申请4页
            # 历史。窗口出现不等于应用完成初始化；尤其1080首帧
            # 抢占可能把这两步隔开。必须实际读到最终提示符并提交
            # 完整画面，再取回收基线，不能给终值“补四页”放松断言。
            raw=q.memory(symbols['terminals'],6*28)
            for index in range(6):
                handle,text,used=struct.unpack_from('<3I',raw,index*28)
                if handle==t.windows()[0]['handle'] and used:
                    return q.memory(text,used).endswith(b'> ') and number('dirty')==0
            return False
        wait(shell_ready,'Shell终端已初始化并提交真实提示符')
        with t.stable_frame():baseline=(number('pf_used'),number('desktop_pages'))
        q.text('run HOME/DAMAGE.SCX\n')
        w=wait(lambda:t.windows()[-1] if len(t.windows())==3 else None,'两层窗口')
        wait(lambda:seed.values(w,address)[0]==1,'同一原生探针初始化')
        t.point(w['x']+w['w']-90,w['y']+150)
        framebuffer=number('framebuffer')
        def settled():
            wait(lambda:number('dirty')==0 and number('cursor_dirty')==0,'场景及光标提交完毕')
        def picture():
            settled()
            with t.stable_frame():return q.memory(framebuffer,width*(height-bar)*4)
        def compare(label):
            partial=picture();counter=seed.values(w,address)[9]
            geometry=[(v['handle'],v['x'],v['y'],v['w'],v['h']) for v in t.windows()]
            q.key('f');wait(lambda:seed.values(w,address)[9]>counter,'F请求真实完整合成')
            full=picture()
            assert geometry==[(v['handle'],v['x'],v['y'],v['w'],v['h']) for v in t.windows()],label+' / F改变几何'
            assert partial==full,label+' / 完整非任务栏PCI逐字节不等'
            assert seed.values(w,address)[1]==0,'程序报告调用失败'
            q.shot(label);cases.append(label)
        compare('01-initial')
        for key in ('1','2','3','4'):
            old=seed.values(w,address)[2];before=number('wm_partial_frames')
            q.key(key);wait(lambda:seed.values(w,address)[2]>old,'真实改帧'+key);settled()
            assert number('wm_partial_frames')>before,'改帧必须实际进入局部路径'
            compare('02-frame-'+key)
        current=[v for v in t.windows() if v['handle']==w['handle']][0]
        t.point(current['x']+80,current['y']+12);q.button(True);settled()
        # 640宽窗口不能向右移动，但高度仍有拖动空间。每步同时提交
        # x/y位移，验证实际几何至少一轴改变，不能仅凭光标移动计数。
        before=number('wm_partial_frames')
        previous=(current['x'],current['y'])
        for _ in range(4):q.move(12,5);settled()
        current=[v for v in t.windows() if v['handle']==w['handle']][0]
        assert (current['x'],current['y'])!=previous,'真实窗口未移动'
        assert number('wm_partial_frames')>before,'拖动必须实际进入局部路径'
        compare('03-drag-held-no-trails')
        q.button(False);settled();compare('04-drag-released')
        q.key('esc');wait(lambda:len(t.windows())==1 and 2 not in compiler.task_states()[1:],'关闭全部探针窗口')
        wait(lambda:number('pf_used')==baseline[0]+number('desktop_pages')-baseline[1],
             '私有源帧/画布/页表完整回收')
        q.shot('05-reclaimed')
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,width=width,height=height,scale=scale,theme=style,
            cases=cases,native_sha256=hashlib.sha256(native).hexdigest(),
            compared_pci_bytes=width*(height-bar)*4,comparison='逐字节，全非任务栏，含光标；拖动按住/释放均比较',
            partial_frames=number('wm_partial_frames'),partial_pixels=number('wm_partial_pixels'),
            overflow={name:number(name) for name in ('keyboard_overflow','event_overflow')},
            baseline_pages=baseline[0],baseline_desktop_pages=baseline[1],
            early_baseline_pages=early_baseline[0],early_desktop_pages=early_baseline[1],
            reclaimed_pages=number('pf_used'),reclaimed_desktop_pages=number('desktop_pages'),
            verifier_sha256=sha(Path(__file__)),
            elapsed_host_seconds=round(time.monotonic()-start,2))
        assert all(value==0 for value in report['overflow'].values())
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        return report
    except Exception:
        if proc.poll() is None:
            diagnostic=dict(baseline=baseline,early_baseline=early_baseline,pf_used=number('pf_used'),
                            desktop_pages=number('desktop_pages'),windows=t.windows(),task_states=compiler.task_states())
            (directory/'failure-diagnostic.json').write_text(json.dumps(diagnostic,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    assert not (OUT/'results.json').exists(),'整组成功报告不得覆盖'
    OUT.mkdir(parents=True,exist_ok=True)
    source=json.loads((STAGE/'results.json').read_text(encoding='utf-8'))
    assert source['status']=='PASS'
    inputs={name:sha(ROOT/name) for name in source['inputs_sha256']}
    assert inputs==source['inputs_sha256'],'种子与当前核/源文件不同'
    native=(STAGE/'damage-native.scx').read_bytes()
    assert hashlib.sha256(native).hexdigest()==source['native_sha256']
    mapping=(STAGE/'damage-native.map').read_bytes()
    address=theme.native_symbols(mapping)['damage_probe']
    reports=[]
    matrix=[(w,h,s,'AURORA') for w,h in MODES for s in (100,150,200)]
    matrix.extend(((640,480,200,'CLASSIC'),(1024,768,150,'CLASSIC'),(1920,1080,200,'CLASSIC')))
    for case in matrix:
        tag=f'{case[0]}-{case[1]}-{case[2]}-{case[3].lower()}'
        existing=OUT/tag/'results.json'
        # 中断后允许跳过同核、同应用的已经成功组合；失败组合仍重跑。
        # 总输入锁保证跨次运行没有借用另一个内核的旧PASS。
        if existing.exists():
            report=json.loads(existing.read_text(encoding='utf-8'))
            assert report['status']=='PASS' and report['inputs_sha256']==inputs
        else:
            print('开始局部合成 '+tag,flush=True)
            report=run_case(*case,native,address,inputs)
        reports.append(report)
        assert inputs=={name:sha(ROOT/name) for name in inputs},'矩阵运行期间输入变化'
        print('通过局部合成 '+tag,flush=True)
    snapshot=STAGE/'matrix-verifier-before-readiness.py'
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,native_sha256=source['native_sha256'],
        native_map_sha256=hashlib.sha256(mapping).hexdigest(),cases=reports,
        earlier_verifier_sha256=sha(snapshot) if snapshot.exists() else None,
        limits='五档×三缩放Aurora及三个Classic组合的合成等价/拖动/回收；不代替全组件布局、游戏画质或整个M8验收')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':
    import argparse,re
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag',help='新的独立复测目录；默认matrix不覆盖已有成功报告')
    args=parser.parse_args()
    if args.tag:
        assert re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',args.tag),'标签只允许字母/数字/下划线/连字符'
        OUT=STAGE/args.tag
    main()
