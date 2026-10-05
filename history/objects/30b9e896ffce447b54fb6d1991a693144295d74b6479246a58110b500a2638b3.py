#!/usr/bin/env python3
"""mio：同一真实G2 Studio草稿的十九显示/菜单/滚动/窗口验证。

只在核心PASS且字节输入完全相同时运行；运行中不写私有状态。
截取完整帧、源码和索引；帧计数并不锁下一次draw，布局与实际
内核画布在同一次只读暂停里核对，重采生产中的帧而不放宽边界。
"""
import hashlib,json,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-studio-draft';OUT=STAGE/'matrix'
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,vga,native,mapping,inputs):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';directory=OUT/tag
    directory.mkdir(parents=True,exist_ok=True);assert not (directory/'results.json').exists()
    v.OUT=directory;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping)
    fixture=('//沙核abc\n'*1200).encode();expected_index=[0]+[i+1 for i,c in enumerate(fixture) if c==10]
    verifier=sha(Path(__file__));bar=32*scale//100 if vga=='std' else 16;shots=[];transients=[]
    def prepare(disk):
        compiler.disk_put(disk,'HOME/IDE.SCX',native);compiler.disk_put(disk,'HOME/VIEW.C',fixture)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch(vga,128,'studio-matrix',prepare);w=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际桌面')
        assert (t.word(kernel['gfx_width']),t.word(kernel['gfx_height']))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/IDE.SCX HOME/VIEW.C\n');w=seed.wait(theme.window,'同一实际G2 Studio')
        def number(name):return theme.user_word(w,addresses[name])
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            previous=number('ui_frames');seed.wait(lambda:number('ui_frames')>previous and t.word(kernel['dirty'])==0,'Studio真实提交帧',180)
        def click(x,y,right=False):
            win=current();ratio=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def frame(label):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=current();fields={name:number(name) for name in ('ui_width','ui_height','UI_W','UI_H','small_window',
                        'editor_y','editor_rows','editor_bottom','editor_columns','editor_right','ui_compact','edit_menu',
                        'menu_x','menu_y','menu_width','menu_height','first_line','left_column','cursor','used','line_count')}
                    geometry=(fields['ui_width'],fields['ui_height'])==(win['cw'],win['ch'])
                    small=fields['UI_W']<276 or fields['UI_H']<160
                    bounds=bool(fields['small_window'])==small and (small or fields['editor_rows']>=1
                        and fields['editor_y']+fields['editor_rows']*20+8<=fields['editor_bottom']
                        and fields['editor_bottom']<=fields['UI_H']-(22 if fields['ui_compact'] else 30)
                        and 69<fields['editor_right']<=fields['UI_W']-16 and fields['editor_columns']>=1)
                    if fields['edit_menu']:bounds=bool(bounds and not small and fields['menu_x']>=0 and fields['menu_y']>=0
                        and fields['menu_x']+fields['menu_width']<=fields['UI_W'] and fields['menu_y']+fields['menu_height']<=fields['UI_H'])
                    if geometry and bounds:
                        private=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4);canvas=q.memory(win['canvas'],len(private))
                        if private==canvas:
                            assert all(private[(r*win['cw']+win['cw']-1)*4+3]==255 for r in range(win['ch']))
                            assert all(private[((win['ch']-1)*win['cw']+c)*4+3]==255 for c in range(win['cw']))
                            assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096)
                            assert v.user_bytes(w,addresses['source'],len(fixture)+1)==fixture+b'\0'
                            assert fields['line_count']==len(expected_index)
                            assert list(struct.unpack('<'+str(len(expected_index))+'I',v.user_bytes(w,addresses['line_starts'],len(expected_index)*4)))==expected_index
                            assert fields['used']==len(fixture) and not number('dirty');break
                        fields['private_sha256']=hashlib.sha256(private).hexdigest();fields['canvas_sha256']=hashlib.sha256(canvas).hexdigest()
                    transients.append(dict(label=label,fields=fields,window=win,registers=q.hmp('info registers')))
                time.sleep(.12)
            else:raise AssertionError('未取得完整且边界合法的实际Studio帧：'+label)
            q.shot(label);shots.append(label);return fields
        seed.wait(lambda:number('ui_frames')>=2,'Studio首帧/源/索引',180);layout=frame('01-native-source')
        click(69+6*8,layout['editor_y']+10);seed.wait(lambda:number('cursor')==8,'实际鼠标UTF-8列定位');present();layout=frame('02-mouse-caret-utf8')
        # draw开头会暂时清editor_rows，逐个pmemsave并不组成原子
        # 布局：1080大画布可能读到rows=0，误点成上箭头而不是下
        # 箭头。使用前一帧同次只读暂停核对过的完整布局，不延长
        # 实际点击、也不修改应用状态或放宽first_line==1断言。
        click(layout['editor_right']+10,layout['editor_y']+layout['editor_rows']*20-2)
        seed.wait(lambda:number('first_line')==1,'真实滚动箭头');present();assert number('cursor')==8;layout=frame('03-manual-scroll-keeps-caret')
        click(120,layout['editor_y']+10,True);seed.wait(lambda:number('edit_menu')==1,'真实右键浮层');present();frame('04-responsive-right-menu')
        before=[number(name) for name in ('cursor','first_line','left_column','document_revision','studio_operations')]
        click(40,38 if number('ui_compact') else 70)
        seed.wait(lambda:number('edit_menu')==0,'菜单外背景Open只撤浮层');present()
        assert [number(name) for name in ('cursor','first_line','left_column','document_revision','studio_operations')]==before and not number('ui_modal')
        frame('05-menu-dismiss-no-background-action')
        geometry=tuple(current()[name] for name in ('x','y','w','h','cw','ch'))
        click(number('UI_W')-4,number('UI_H')-4);assert tuple(current()[name] for name in ('x','y','w','h','cw','ch'))==geometry
        if vga=='std':
            def extra():
                raw=q.memory(kernel['extras'],6*56)
                return next(row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]==w['handle'])
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            old=tuple(current()[name] for name in ('x','y','w','h'))
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'真实最大化');present();frame('06-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and tuple(current()[name] for name in ('x','y','w','h'))==old,'精确还原');present();frame('07-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'真实最小化')
            bw=max(40*scale//100,min(140*scale//100,(width-240*scale//100)//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click();seed.wait(lambda:extra()[3]==0 and number('ui_focus')==1,'实际任务栏恢复')
            present();frame('08-taskbar-restored')
            win=current();title=win['h']-win['ch']-1
            tw=min(width-win['x'],340*scale//100);th=min(height-bar-win['y'],240*scale//100)
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+tw-4,win['y']+th-4);q.button(False)
            seed.wait(lambda:(current()['w'],current()['h'])==(tw,th),'真实调整大小');present();frame('09-resized-source')
            win=current();t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+92,win['y']+title+36);q.button(False)
            seed.wait(lambda:current()['w']==96 and number('small_window')==1,'真实极小窗口');present();frame('10-minimum-keeps-document')
            click(number('UI_W')//2,number('UI_H')//2);seed.wait(lambda:extra()[4]==1 and number('small_window')==0,'加号真正放大')
            present();frame('11-enlarged-source');title_button(0)
        else:
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'Studio行索引/帧/页表严格回收')
        q.shot('12-all-reclaimed');shots.append('12-all-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            width=width,height=height,scale=scale,theme=style,vga=vga,checks=shots,overflow=overflow,transient_snapshots=transients)
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return report
    except Exception:
        if proc.poll() is None:
            if w is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames','editor_y','editor_rows','editor_columns','editor_bottom','small_window','cursor','first_line','edit_menu','ui_modal')}
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
    native=(STAGE/'ide-native.scx').read_bytes();mapping=(STAGE/'ide-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['ide']['scx_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native']['ide']['map_sha256']
    cases=[(320,200,100,'AURORA','cirrus')]+[(w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200)]
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std')))
    reports=[]
    for width,height,scale,style,vga in cases:
        print(f'开始Studio {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
        reports.append(run_case(width,height,scale,style,vga,native,mapping,inputs))
        print(f'通过Studio {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,
        limits='独立Studio草稿十九组合/UTF-8鼠标/手动滚动/浮层/窗口/完整帧/源/索引/回收，不代替编译竞态、成本、其它组件或完整M8')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Studio十九组合PASS',flush=True)


if __name__=='__main__':main()
