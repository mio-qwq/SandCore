#!/usr/bin/env python3
"""mio：同一G2 Canvas的五档×三缩放、Classic、VGA及窗口交互。

每个组合只改未启动的测试盘副本，运行输入全走HMP/QMP。作品
与Undo逐字节比较，实际客户帧与内核画布比较，检查最大私有帧
末页未被越界写入。resize/minimize/maximize/restore由鼠标触发；
窗口几何改变不能借调用内核函数或改私有变量伪造。
"""
import hashlib,json,struct,time
from pathlib import Path
import verify_canvas as seed

ROOT=seed.ROOT
STAGE=ROOT/'build/m8-canvas'
OUT=STAGE/'matrix'
t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,native,mapping,inputs,vga='std'):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}'
    directory=OUT/tag;directory.mkdir(parents=True,exist_ok=True)
    assert not (directory/'results.json').exists(),'组合成功报告不可覆盖'
    seed.OUT=directory;seed.bind();symbols=q.symbols();addresses=theme.native_symbols(mapping)
    bar=32*scale//100 if vga=='std' else 16;checks=[];start=time.monotonic();verifier=sha(Path(__file__));transients=[]
    def number(name):return t.word(symbols[name])
    def prepare(disk):
        compiler.disk_put(disk,'HOME/CAN.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch(vga,128,'canvas-matrix',prepare);base=None;w=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and number('dirty')==0,'纯桌面提交')
        assert (number('gfx_width'),number('gfx_height'))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click()
        seed.wait(lambda:number('menu_open')!=0,'实际开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');seed.idle();base=(number('pf_used'),number('desktop_pages'))
        q.text('run HOME/CAN.SCX\n');w=seed.wait(theme.window,'同一真实G2 Canvas')
        def value(name):return theme.user_word(w,addresses[name])
        def current():return [win for win in t.windows() if win['handle']==w['handle']][0]
        def present():
            frames=value('ui_frames');seed.wait(lambda:value('ui_frames')>frames and number('dirty')==0,'真实布局/提交')
        def click(x,y):
            win=current();ratio=value('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100);t.click()
        def body():return seed.user_bytes(w,value('document'),seed.BYTES)
        def undo():return seed.user_bytes(w,value('undo_pixels'),seed.PIXELS*4)
        def frame(label):
            # dirty=0只表示内核合成完成，不表示用户下一帧已画完。
            # 用户私有后台在绘制中可以与上一提交画布不同，这是
            # FRAME32的正常生产/提交合同。只读暂停采样若遇不同，
            # 保存摘要/真实EIP并恢复CPU，等待一次完整逐字节相等；
            # 不改客体dirty/后台，不对像素放容差，也不忽略任何列。
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                seed.wait(lambda:number('dirty')==0,'合成完成')
                with t.stable_frame():
                    win=current();length=win['cw']*win['ch']*4
                    pixels=seed.user_bytes(w,value('ui_pixels'),length);canvas=q.memory(win['canvas'],length)
                    if pixels==canvas:
                        # UI_W/H是取整布局单位，150%可能余一个物理
                        # 像素。底图必须连余数列/行也铺满。
                        assert all(pixels[(row*win['cw']+win['cw']-1)*4+3]==255 for row in range(win['ch'])),'客户区余数列未铺完整底图'
                        assert all(pixels[(win['ch']-1)*win['cw']*4+col*4+3]==255 for col in range(win['cw'])),'客户区余数行未铺完整底图'
                        assert seed.user_bytes(w,value('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096),'私有帧末页被越界写入'
                        break
                    transients.append(dict(label=label,ui_frames=value('ui_frames'),
                        source_sha256=hashlib.sha256(pixels).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest(),
                        registers=q.hmp('info registers'),current_task=number('current')))
                time.sleep(.12)
            else:raise AssertionError('15秒内未取得完整相等的实际提交帧：'+label)
            q.shot(label);checks.append(label)
        seed.wait(lambda:value('ui_frames')>=2,'完成文档/撤销/首帧初始化')
        assert value('ui_scale')==(scale if vga=='std' else 50)
        assert value('paper_w')>0 and value('paper_h')>0,'默认窗口应可画'
        blank=body();frame('01-responsive-blank')
        compact=value('ui_compact');click(210 if compact else 274,value('tools_y')+10)
        seed.wait(lambda:value('menu_open')==1,'鼠标工具菜单真实开启')
        frame('02-tools');click(value('menu_x')+24,value('menu_y')+8+(26 if compact else 36)+10)
        seed.wait(lambda:value('brush')==6 and value('menu_open')==0,'鼠标改变画笔半径')
        # 右键事件仍来自QMP，不能把直接赋值menu_open算右键通过。
        x=value('paper_x')+value('paper_w')//2;y=value('paper_y')+value('paper_h')//2
        click(x,y);seed.wait(lambda:value('dirty')==1,'鼠标笔画')
        painted=body();assert painted!=blank and undo()==blank[32:]
        q.key('u');present();assert body()==blank,'Undo作品精确恢复'
        q.key('u');present();assert body()==painted,'Redo作品精确恢复'
        frame('03-stroke-undo-redo')
        old_color=value('color_rgb');click(210 if compact else 274,value('tools_y')+10)
        seed.wait(lambda:value('menu_open')==1,'颜色工具重新打开')
        click(value('menu_x')+24,value('menu_y')+8+2*(26 if compact else 36)+10)
        seed.wait(lambda:value('ui_modal')==1,'RGB输入框');q.text('XYZ12Q\n')
        seed.wait(lambda:value('ui_modal')==0 and value('menu_open')==0,'坏RGB输入返回')
        assert value('color_rgb')==old_color and body()==painted,'坏颜色不能改画笔/作品'
        previous_undo=undo()
        for answer in ('esc','y'):
            click(210 if compact else 274,value('tools_y')+10)
            seed.wait(lambda:value('menu_open')==1,'清空工具菜单')
            click(value('menu_x')+24,value('menu_y')+18)
            seed.wait(lambda:value('ui_modal')==3,'实际清空确认')
            q.key(answer);seed.wait(lambda:value('ui_modal')==0 and value('menu_open')==0,'清空确认返回');present()
            if answer=='esc':assert body()==painted and undo()==previous_undo,'取消清空破坏作品/Undo'
            else:assert body()==blank and undo()==painted[32:],'清空应保留原作品到Undo'
        q.key('u');present();assert body()==painted,'撤销清空应精确恢复作品'
        frame('03b-color-rejected-clear-cancel-undo')
        q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
        q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}])
        seed.wait(lambda:value('menu_open')==1,'真实右键工具浮层');present();frame('04-right-menu')
        q.key('esc');seed.wait(lambda:value('menu_open')==0,'Esc只关闭工具浮层')
        before=body();old_undo=undo()
        # 初始Open键及紧跟首字实际排队；路径框通过真实键盘接收HOME。
        counter=value('canvas_operations');q.key('o')
        seed.wait(lambda:value('ui_modal')==2,'紧凑/普通路径框就绪');q.text('HOME/TRUE.SCB')
        present();frame('05-path-dialog');q.key('esc')
        seed.wait(lambda:value('canvas_operations')>counter,'取消路径框真实返回')
        assert body()==before and undo()==old_undo and value('dirty')==1,'取消路径框改变作品/Undo'
        if vga=='std':
            def extra():
                raw=q.memory(symbols['extras'],6*56)
                for i in range(6):
                    row=struct.unpack_from('<14I',raw,i*56)
                    if row[0]==w['handle']:return row
                raise AssertionError('窗口扩展丢失')
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            old=current();geometry=(old['x'],old['y'],old['w'],old['h'])
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'实际鼠标最大化')
            present();assert body()==before and undo()==old_undo;frame('06-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and (current()['x'],current()['y'],current()['w'],current()['h'])==geometry,'鼠标还原精确几何')
            present();frame('07-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'鼠标最小化')
            assert body()==before and undo()==old_undo
            available=width-240*scale//100;bw=max(40*scale//100,min(140*scale//100,available//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click()
            seed.wait(lambda:extra()[3]==0 and value('ui_focus')==1,'实际任务栏恢复聚焦')
            present();frame('08-taskbar-restored')
            win=current();title=win['h']-win['ch']-1
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            target_w=min(width-win['x'],max(340*scale//100,96))
            target_h=min(height-bar-win['y'],max(220*scale//100,title+40))
            t.point(win['x']+target_w-1,win['y']+target_h-1);q.button(False)
            seed.wait(lambda:current()['cw']!=win['cw'] or current()['ch']!=win['ch'],'鼠标改变真实客户区尺寸')
            present();assert body()==before and undo()==old_undo,'resize不得重采样回写作品/Undo'
            frame('09-resized-paper')
            win=current();box=24*scale//100
            t.point(win['x']+win['w']-box//2,win['y']+(win['h']-win['ch']-1)//2);t.click()
        else:
            # VGA保留历史窗口交互路径，关闭坐标按13px标题栏，不把
            # 未提供的高分辨率三按钮误当VGA支持的操作。
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        seed.idle();seed.wait(lambda:number('pf_used')==base[0]+number('desktop_pages')-base[1],'严格回收所有私有文档/Undo/画布/页表')
        q.shot('10-reclaimed');checks.append('10-reclaimed')
        overflow={name:number(name) for name in ('keyboard_overflow','event_overflow')}
        assert not any(overflow.values()),overflow
        assert sha(Path(__file__))==verifier,'组合期间验证器源改变，不能登记PASS'
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,width=width,height=height,scale=scale,theme=style,vga=vga,
                    native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
                    checks=checks,baseline_pages=base[0],reclaimed_pages=number('pf_used'),
                    baseline_desktop_pages=base[1],reclaimed_desktop_pages=number('desktop_pages'),overflow=overflow,
                    verifier_sha256=verifier,transient_snapshots=transients,elapsed_host_seconds=round(time.monotonic()-start,2))
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        return report
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    assert not (OUT/'results.json').exists(),'整组成功报告不可覆盖'
    OUT.mkdir(parents=True,exist_ok=True)
    source=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert source['status']=='PASS'
    verifier=sha(Path(__file__))
    inputs=source['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs},'当前核/应用/测试器不是已通过种子'
    native=(STAGE/'canvas-native.scx').read_bytes();mapping=(STAGE/'canvas-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==source['native']['canvas']['scx_sha256']
    assert hashlib.sha256(mapping).hexdigest()==source['native']['canvas']['map_sha256']
    cases=[(w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200)]
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std'),(320,200,100,'AURORA','cirrus')))
    reports=[]
    for width,height,scale,style,vga in cases:
        tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';existing=OUT/tag/'results.json'
        if existing.exists():
            report=json.loads(existing.read_text(encoding='utf-8'))
            assert report['status']=='PASS' and report['inputs_sha256']==inputs
        else:
            print('开始Canvas '+tag,flush=True)
            report=run_case(width,height,scale,style,native,mapping,inputs,vga)
        assert inputs=={name:sha(ROOT/name) for name in inputs},'矩阵期间输入改变'
        assert sha(Path(__file__))==verifier,'矩阵期间验证器源改变'
        reports.append(report);print('通过Canvas '+tag,flush=True)
    previous=STAGE/'matrix-verifier-before-frame-readiness.py'
    summary=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,
                 earlier_verifier_sha256=sha(previous) if previous.exists() else None,
                 limits='Canvas十九组合及原生窗口交互/像素/回收；不代替其它组件、图片压缩格式、游戏/实时光追或整个M8')
    (OUT/'results.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False),flush=True)


if __name__=='__main__':
    import argparse,re
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tag',help='独立复测目录')
    args=parser.parse_args()
    if args.tag:
        assert re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',args.tag)
        OUT=OUT/args.tag
    main()
