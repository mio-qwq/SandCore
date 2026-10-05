#!/usr/bin/env python3
"""mio：同一真实G2 Files的十九显示组合、浮层及窗口操作。

输入全部经过HMP/QMP。只读等待已提交客户帧，与内核画布完整
比较；不对像素放容差、不改客体选择/尺寸/队列。每例独立副本盘。
"""
import hashlib,json,struct,sys,time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
import verify_canvas as v
import verify_files as seed

t=v.t;q=v.q;compiler=v.compiler;theme=v.theme
STAGE=ROOT/'build/m8-files';OUT=STAGE/'matrix'
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,vga,native,mapping,inputs):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';directory=OUT/tag
    directory.mkdir(parents=True,exist_ok=True);assert not (directory/'results.json').exists()
    v.OUT=directory;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping)
    bar=32*scale//100 if vga=='std' else 16;verifier=sha(Path(__file__));checks=[];transients=[]
    def prepare(disk):
        compiler.disk_put(disk,'HOME/FIL.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
        for i in range(40):compiler.disk_put(disk,f'HOME/FTEST/F{i:02}.TXT',f'{i}\n'.encode())
        compiler.disk_put(disk,'HOME/FTEST/沙核.SCB',b'UNCHANGED')
    proc=t.launch(vga,128,'files-matrix',prepare);w=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际桌面')
        assert (t.word(kernel['gfx_width']),t.word(kernel['gfx_height']))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click()
        seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();base=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/FIL.SCX HOME/FTEST\n');w=seed.wait(theme.window,'同一G2 Files真实启动')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name):return v.user_bytes(w,addresses[name],64).split(b'\0')[0]
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'实际布局提交')
        def click(x,y,right=False):
            win=current();ratio=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def layout():
            if number('small_window'):return
            rows=number('visible_rows');assert rows>=1,'默认/普通调整窗口须显示文件行'
            assert number('list_y')>=number('body_y') and number('list_y')+rows*number('row_height')<=number('navigation_y')-8,'列表覆盖分页按钮'
            assert number('navigation_y')+22<=number('UI_H')-(22 if number('ui_compact') else 30),'分页覆盖页脚'
        def frame(label):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                seed.wait(lambda:t.word(kernel['dirty'])==0,'合成结束')
                with t.stable_frame():
                    win=current();length=win['cw']*win['ch']*4
                    pixels=v.user_bytes(w,number('ui_pixels'),length);canvas=q.memory(win['canvas'],length)
                    if pixels==canvas:
                        assert all(pixels[(r*win['cw']+win['cw']-1)*4+3]==255 for r in range(win['ch']))
                        assert all(pixels[((win['ch']-1)*win['cw']+c)*4+3]==255 for c in range(win['cw']))
                        assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096),'最大私有帧末页越界'
                        break
                    transients.append(dict(label=label,source_sha256=hashlib.sha256(pixels).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest(),registers=q.hmp('info registers')))
                time.sleep(.12)
            else:raise AssertionError('未取得完整相等的实际客户帧：'+label)
            q.shot(label);checks.append(label)
        seed.wait(lambda:number('ui_frames')>=2,'Files首帧/列表就绪');layout();frame('01-responsive-files')
        assert number('count')==41 and text('cwd')==b'HOME/FTEST'
        old=number('selection');click(120,number('navigation_y')+10)
        seed.wait(lambda:number('selection')!=old,'实际Next分页');assert number('selection')>old;present();frame('02-page-selected')
        click(number('list_left')+50,number('list_y')+number('row_height')//2,True)
        seed.wait(lambda:number('file_context')==1,'实际文件右键浮层');present()
        assert number('context_x')>=0 and number('context_y')>=0
        assert number('context_x')+number('context_width')<=number('UI_W') and number('context_y')+number('context_height')<=number('UI_H')
        if number('UI_H')<220:assert number('context_columns')==2,'矮窗口应重排两列'
        frame('03-context-inside-client')
        selection=number('selection');columns=number('context_columns')
        click(number('context_x')+16+(5%columns)*(number('context_button_width')+8),number('context_y')+16+(5//columns)*number('context_step'))
        seed.wait(lambda:number('ui_modal')==2,'真实右键Rename路径框');frame('04-context-rename-dialog')
        counter=number('file_operations');q.key('esc');seed.wait(lambda:number('file_operations')>counter,'实际取消Rename')
        assert number('selection')==selection and number('count')==41;present()
        click(number('list_left')+50,number('list_y')+number('row_height')//2,True)
        seed.wait(lambda:number('file_context')==1,'再次打开浮层')
        geometry=tuple(current()[k] for k in ('x','y','w','h','cw','ch'))
        # 点在真实右下抓取区。只有按下/松开、没有位移，不能把
        # 抓取区内的落点当成新的右下角而偷偷缩小原窗口。
        click(number('UI_W')-4,number('UI_H')-4)
        seed.wait(lambda:number('file_context')==0,'菜单外点击仅关闭');assert number('ui_modal')==0 and number('count')==41
        assert tuple(current()[k] for k in ('x','y','w','h','cw','ch'))==geometry,'原地点击抓取区改变窗口几何'
        present();frame('05-menu-closed-without-mutation')
        if vga=='std':
            def extra():
                raw=q.memory(kernel['extras'],6*56)
                return next(row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]==w['handle'])
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            win=current();old=(win['x'],win['y'],win['w'],win['h'])
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'鼠标最大化');present();layout();frame('06-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and tuple(current()[k] for k in ('x','y','w','h'))==old,'精确窗口还原');present();layout();frame('07-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'真实最小化')
            available=width-240*scale//100;bw=max(40*scale//100,min(140*scale//100,available//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click()
            seed.wait(lambda:extra()[3]==0 and number('ui_focus')==1,'任务栏恢复');present();frame('08-taskbar-restored')
            win=current();title=win['h']-win['ch']-1
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            target_w=min(width-win['x'],340*scale//100);target_h=min(height-bar-win['y'],240*scale//100)
            # 抓取点距真实末像素各3px；提交边界须加回该偏移。
            # 鼠标实际增量=目标尺寸-原尺寸，不能沿用旧错误公式。
            t.point(win['x']+target_w-4,win['y']+target_h-4);q.button(False)
            seed.wait(lambda:current()['cw']!=win['cw'] or current()['ch']!=win['ch'],'真实改变尺寸');present();layout();frame('09-resized-files')
            assert (current()['w'],current()['h'])==(target_w,target_h),'尺寸增量不等于真实鼠标位移'
            win=current();t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+92,win['y']+title+36);q.button(False)
            seed.wait(lambda:current()['w']==96 and number('small_window')==1,'真实最小客户区');present();frame('09b-minimum-window')
            click(number('UI_W')//2,number('UI_H')//2)
            seed.wait(lambda:extra()[4]==1 and current()['w']==width and number('small_window')==0,'客户区加号实际放大')
            present();layout();frame('09c-enlarged-from-minimum');title_button(0)
        else:
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==base[0]+t.word(kernel['desktop_pages'])-base[1],'Files所有私有帧/画布/页表严格回收')
        q.shot('10-reclaimed');checks.append('10-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values()),overflow
        assert sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
                    width=width,height=height,scale=scale,theme=style,vga=vga,checks=checks,overflow=overflow,transient_snapshots=transients)
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        return report
    except Exception:
        if proc.poll() is None:
            if w is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames',
                        'ui_compact','body_y','list_y','navigation_y','row_height','visible_rows','small_window','file_context')}
                    state['window']=current();state['registers']=q.hmp('info registers')
                    (directory/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs}
    native=(STAGE/'files-native.scx').read_bytes();mapping=(STAGE/'files-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['files']['scx_sha256']
    assert hashlib.sha256(mapping).hexdigest()==core['native']['files']['map_sha256']
    cases=[(320,200,100,'AURORA','cirrus')]
    cases.extend((w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200))
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std')))
    reports=[];verifier=sha(Path(__file__))
    for width,height,scale,style,vga in cases:
        tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';existing=OUT/tag/'results.json'
        print('开始Files '+tag,flush=True)
        if existing.exists():
            result=json.loads(existing.read_text(encoding='utf-8'));assert result['status']=='PASS' and result['inputs_sha256']==inputs and result['verifier_sha256']==verifier
        else:result=run_case(width,height,scale,style,vga,native,mapping,inputs)
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        reports.append(result);print('通过Files '+tag,flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,limits='Files十九显示组合/布局/浮层/实际窗口/完整像素与回收；不代替其它组件和整个M8')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
