#!/usr/bin/env python3
"""mio：同一真实G2调试器的十九布局、实际鼠标与拥有目标回收。

核心成功后复用其真实SCX和map，不复用旧版Debugger结果。每帧
只读暂停核对实际客户画布/全部88B现场/内核保存的原帧，暂停只是
取证手段；不从宿主改变目标寄存器、断点、应用模式或窗口大小。
"""
import hashlib,json,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-debugger-draft';OUT=STAGE/'matrix'
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,vga,native,mapping,target,inputs):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';directory=OUT/tag
    directory.mkdir(parents=True,exist_ok=True);assert not (directory/'results.json').exists()
    v.OUT=directory;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping)
    verifier=sha(Path(__file__));bar=32*scale//100 if vga=='std' else 16;shots=[];transients=[]
    def prepare(disk):
        compiler.disk_put(disk,'HOME/DBG.SCX',native);compiler.disk_put(disk,'HOME/TARGET.SCX',target)
        compiler.disk_put(disk,'HOME/TARGET.SCX.map',(STAGE/'target-native.map').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch(vga,128,'debugger-matrix',prepare);w=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际桌面')
        assert (t.word(kernel['gfx_width']),t.word(kernel['gfx_height']))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/DBG.SCX HOME/TARGET.SCX\n');w=seed.wait(theme.window,'同一真实G2调试器')
        def number(name):return theme.user_word(w,addresses[name])
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'调试器真实提交帧',180)
        def click(x,y,right=False):
            win=current();ratio=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def toolbar(index):
            # 控件中心来自共享工具条的公开绘制规则，命中之后还需
            # 真实模式/模态/内存结果佐证，不能只以算对坐标算通过。
            labels=('Step','Run','Pause','Restart','Break','Unbreak','Memory','Code','Regs')
            x=16;y=38 if number('ui_compact') else 70;step=26 if number('ui_compact') else 36
            for i,label in enumerate(labels):
                bw=len(label)*8+20
                if x+bw>number('UI_W')-16:x=16;y+=step
                if i==index:click(x+bw//2,y+10);return
                x+=bw+8
        def panel_origin(layout):
            heading=28 if layout['wide_layout'] or not layout['ui_compact'] else 0
            padding=2 if layout['ui_compact'] else 8
            if layout['register_mode'] or layout['stack_mode']:
                x=layout['UI_W']-280 if layout['wide_layout'] else 16
                pw=264 if layout['wide_layout'] else layout['UI_W']-32
            else:x=16;pw=layout['UI_W']-312 if layout['wide_layout'] else layout['UI_W']-32
            y=layout['stack_y'] if layout['stack_mode'] else layout['debug_y']
            return x+8,y+heading+padding//2,pw-16
        def arrow(down,layout):
            # 与Studio相同，行数在draw生产中会临时为0。鼠标使用
            # 同次只读暂停取得并与整帧相符的几何，不把独立读数
            # 拼成一份未曾实际显示的布局；真实模式/结果仍另核对。
            rows=layout['stack_rows'] if layout['stack_mode'] else layout['register_rows'] if layout['register_mode'] else layout['debug_rows']
            x,y,pw=panel_origin(layout);hh=rows*20;bh=8 if hh<32 else 16
            click(x+pw-6,y+(hh-bh+3 if down else 3))
        def frame(label):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=current();fields={name:number(name) for name in ('ui_width','ui_height','UI_W','UI_H','small_window','debug_y','debug_rows',
                        'debug_bottom','ui_compact','wide_layout','debug_menu','menu_x','menu_y','menu_width','menu_height','register_mode',
                        'register_first','register_rows','stack_mode','stack_y','stack_rows','stack_first','stack_snapshot','stack_address','stack_mask','stack_event',
                        'memory_mode','memory_address','memory_valid','target_pid','target_generation','paused','have_context')}
                    small=fields['UI_W']<276 or fields['UI_H']<172;heading=28 if fields['wide_layout'] or not fields['ui_compact'] else 0
                    padding=2 if fields['ui_compact'] else 8
                    bounds=bool(fields['small_window'])==small and (small or fields['debug_rows']>=1
                        and fields['debug_y']+heading+fields['debug_rows']*20+padding<=fields['debug_bottom']
                        and fields['debug_bottom']<=fields['UI_H']-(22 if fields['ui_compact'] else 30))
                    if fields['debug_menu']:bounds=bool(bounds and not small and fields['menu_x']>=0 and fields['menu_y']>=0
                        and fields['menu_x']+fields['menu_width']<=fields['UI_W'] and fields['menu_y']+fields['menu_height']<=fields['UI_H'])
                    if not small and (fields['wide_layout'] or fields['stack_mode']):
                        bounds=bool(bounds and 1<=fields['stack_rows']<=32 and fields['stack_y']+heading+fields['stack_rows']*20+padding<=fields['debug_bottom'])
                    if not small and fields['wide_layout']:
                        bounds=bool(bounds and 1<=fields['register_rows']<=10
                            and fields['debug_y']+heading+fields['register_rows']*20+padding+8==fields['stack_y'])
                    if bounds and (fields['ui_width'],fields['ui_height'])==(win['cw'],win['ch']):
                        private=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4);canvas=q.memory(win['canvas'],len(private))
                        if private==canvas:
                            assert fields['paused']==fields['have_context']==1 and fields['target_pid']<8
                            assert v.user_bytes(w,addresses['context'],88)==initial_context
                            assert fields['stack_snapshot']==1 and fields['stack_address']==struct.unpack_from('<I',initial_context,68)[0]
                            assert fields['stack_event']==struct.unpack_from('<I',initial_context,84)[0] and fields['stack_mask']==initial_stack_mask
                            assert v.user_bytes(w,addresses['stack_values'],128)==initial_stack
                            saved=t.word(kernel['tasks']+fields['target_pid']*168+8)
                            assert q.memory(saved,76)==initial_context[:76]
                            assert all(private[(r*win['cw']+win['cw']-1)*4+3]==255 for r in range(win['ch']))
                            assert all(private[((win['ch']-1)*win['cw']+c)*4+3]==255 for c in range(win['cw']))
                            assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096)
                            break
                        fields.update(private_sha256=hashlib.sha256(private).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest())
                    transients.append(dict(label=label,fields=fields,window=win,registers=q.hmp('info registers')))
                time.sleep(.12)
            else:raise AssertionError('未取得完整且布局合法的实际Debugger帧：'+label)
            q.shot(label);shots.append(label);return fields
        seed.wait(lambda:number('ui_frames')>=2 and number('paused')==number('have_context')==1,'真实首指令暂停及初帧',180)
        initial_context=v.user_bytes(w,addresses['context'],88);assert struct.unpack_from('<I',initial_context,56)[0]==0x400000
        initial_stack=v.user_bytes(w,addresses['stack_values'],128);initial_stack_mask=number('stack_mask')
        # 首指令ESP=7FFFF0，真实PTE给出顶端16B而下一页无映射。
        # 不能把同缓冲旧值或补0作为其余28个有效栈槽。
        assert struct.unpack_from('<I',initial_context,68)[0]==0x7FFFF0 and initial_stack_mask==15
        assert initial_stack[:16]==v.user_bytes(dict(owner=number('target_pid')),0x7FFFF0,16)
        frame('01-native-real-paused-context')
        toolbar(8);seed.wait(lambda:number('register_mode')==1,'实际鼠标Regs');present();layout=frame('02-mouse-register-panel')
        old=number('register_first');arrow(True,layout);present()
        assert number('register_first')==(old+1 if layout['register_rows']<10 else old)
        arrow(False,layout);present();assert number('register_first')==old;frame('03-mouse-register-navigation')
        layout=frame('03a-before-stack-menu');click(60,layout['debug_y']+10,True)
        seed.wait(lambda:number('debug_menu')==1,'实际Stack右键入口');present();layout=frame('03b-stack-responsive-menu')
        columns=number('menu_columns');item=9
        click(layout['menu_x']+8+(item%columns)*(number('menu_button')+8)+number('menu_button')//2,
            layout['menu_y']+8+(item//columns)*number('menu_step')+10)
        seed.wait(lambda:number('stack_mode')==1 and not number('register_mode') and not number('debug_menu'),'真正鼠标Stack视图');present()
        layout=frame('03c-real-stack-right-bottom-or-narrow-page');old=number('stack_first');arrow(True,layout);present()
        assert number('stack_first')==min(old+1,32-layout['stack_rows'])
        layout=frame('03d-stack-next-real-slot');arrow(False,layout);present();assert number('stack_first')==old
        toolbar(6);seed.wait(lambda:number('ui_modal')==1,'实际Memory地址框');q.text('00400000\n')
        seed.wait(lambda:number('ui_modal')==0 and number('memory_mode')==number('memory_valid')==1 and not number('register_mode'),'实际地址解析/读取')
        present();assert v.user_bytes(w,addresses['memory_bytes'],128)==v.user_bytes(dict(owner=number('target_pid')),0x400000,128)
        layout=frame('04-mouse-memory-real-128-byte-cache')
        arrow(False,layout);seed.wait(lambda:number('memory_address')==0x3FFFF0,'鼠标前页地址');present();assert not number('memory_valid')
        arrow(True,layout);seed.wait(lambda:number('memory_address')==0x400000 and number('memory_valid')==1,'读失败后鼠标返回映射页');present()
        layout=frame('05-unmapped-page-keeps-return-navigation')
        click(120,layout['debug_y']+10,True);seed.wait(lambda:number('debug_menu')==1,'真实右键浮层');present();frame('06-responsive-right-menu')
        before=[number(name) for name in ('last_event','debug_operations','memory_address','register_mode','register_first')]
        click(40,38 if number('ui_compact') else 70);seed.wait(lambda:number('debug_menu')==0,'菜单外背景Step只撤浮层');present()
        assert [number(name) for name in ('last_event','debug_operations','memory_address','register_mode','register_first')]==before and not number('ui_modal')
        frame('07-menu-dismiss-without-stepping-target')
        geometry=tuple(current()[name] for name in ('x','y','w','h','cw','ch'))
        click(number('UI_W')-4,number('UI_H')-4);assert tuple(current()[name] for name in ('x','y','w','h','cw','ch'))==geometry
        if vga=='std':
            def extra():
                raw=q.memory(kernel['extras'],6*56)
                return next(row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]==w['handle'])
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            original=tuple(current()[name] for name in ('x','y','w','h'))
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'真实最大化');present();frame('08-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and tuple(current()[name] for name in ('x','y','w','h'))==original,'精确还原');present();frame('09-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'真实最小化')
            bw=max(40*scale//100,min(140*scale//100,(width-240*scale//100)//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click();seed.wait(lambda:extra()[3]==0 and number('ui_focus')==1,'真实任务栏恢复')
            present();frame('10-taskbar-restored')
            win=current();title=win['h']-win['ch']-1
            tw=min(width-win['x'],340*scale//100);th=min(height-bar-win['y'],240*scale//100)
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+tw-4,win['y']+th-4);q.button(False)
            seed.wait(lambda:(current()['w'],current()['h'])==(tw,th),'真实调整大小');present();frame('11-resized-real-target')
            win=current();t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+92,win['y']+title+36);q.button(False)
            seed.wait(lambda:current()['w']==96 and number('small_window')==1,'真实极小窗口');present();frame('12-minimum-preserves-real-context')
            click(number('UI_W')//2,number('UI_H')//2);seed.wait(lambda:extra()[4]==1 and number('small_window')==0,'加号实际放大')
            present();frame('13-enlarged-real-target');title_button(0)
        else:
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'Debugger/拥有目标/私有帧和页表严格回收')
        q.shot('14-all-owned-target-pages-reclaimed');shots.append('14-all-owned-target-pages-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            width=width,height=height,scale=scale,theme=style,vga=vga,checks=shots,overflow=overflow,context_sha256=hashlib.sha256(initial_context).hexdigest(),
            stack_sha256=hashlib.sha256(initial_stack).hexdigest(),stack_mask=initial_stack_mask,transient_snapshots=transients)
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return report
    except Exception:
        if proc.poll() is None:
            # 失败时同样保留每次完整只读观察。仅存最后一组行数
            # 无法区分画布提交、布局越界和正在绘制三种事实；
            # 诊断字段不能影响原成功门槛或从宿主修正客体状态。
            (directory/'transient-snapshots.json').write_text(json.dumps(transients,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            if w is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames','debug_y','debug_rows','debug_bottom',
                        'small_window','debug_menu','ui_modal','target_pid','target_generation','paused','have_context','register_mode','register_first','memory_mode','memory_address','memory_valid',
                        'wide_layout','ui_compact','register_rows','stack_mode','stack_y','stack_rows','stack_first','stack_snapshot','stack_address','stack_mask','stack_event')}
                    state.update(window=current(),registers=q.hmp('info registers'));(directory/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=dict(core['inputs_sha256'])
    for name in ('build/fs/SYS/THEMES/AURORA.CFG','build/fs/SYS/THEMES/CLASSIC.CFG'):inputs[name]=sha(ROOT/name)
    assert inputs=={name:sha(ROOT/name) for name in inputs}
    native=(STAGE/'debugger-native.scx').read_bytes();mapping=(STAGE/'debugger-native.map').read_bytes();target=(STAGE/'target-native.scx').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['debugger']['scx_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native']['debugger']['map_sha256']
    assert hashlib.sha256(target).hexdigest()==core['native']['target']['scx_sha256']
    cases=[(320,200,100,'AURORA','cirrus')]+[(w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200)]
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std')))
    reports=[]
    for width,height,scale,style,vga in cases:
        print(f'开始Debugger {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
        reports.append(run_case(width,height,scale,style,vga,native,mapping,target,inputs))
        print(f'通过Debugger {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,
        limits='独立Debugger十九组合/真正Regs与内存导航/浮层不单步穿透/窗口/整帧/真实暂停现场/严格回收，不代替首字、退出复用、联动或全M8')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Debugger十九组合PASS',flush=True)


if __name__=='__main__':main()
