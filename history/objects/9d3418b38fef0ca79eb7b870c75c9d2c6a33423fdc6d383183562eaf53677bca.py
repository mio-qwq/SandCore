#!/usr/bin/env python3
"""mio：同一真实G2 Monitor的十九显示组合/全行鼠标/窗口验收。

先通过正常Space冻结本进程采样，随后完整快照/历史保持不变；
系统PIT仍继续。每次箭头坐标用同次暂停、全客户帧相等且布局
有效的私有读数，避免拼接生产中的临时0行。输入都走HMP/QMP。
"""
import hashlib,json,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
STAGE=ROOT/'build/m8-monitor-draft';OUT=STAGE/'matrix'
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,vga,native,mapping,inputs):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';directory=OUT/tag
    directory.mkdir(parents=True,exist_ok=True);assert not (directory/'results.json').exists()
    v.OUT=directory;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping)
    verifier=sha(Path(__file__));bar=32*scale//100 if vga=='std' else 16;shots=[];transients=[];coverage={}
    def prepare(disk):
        compiler.disk_put(disk,'HOME/MON.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch(vga,128,'monitor-matrix',prepare);w=None;frozen=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际桌面')
        assert (t.word(kernel['gfx_width']),t.word(kernel['gfx_height']))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/MON.SCX\n');w=seed.wait(theme.window,'同一真实G2 Monitor')
        def number(name):return theme.user_word(w,addresses[name])
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'Monitor真实提交帧',180)
        def click(x,y,right=False):
            win=current();ratio=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def frozen_state():
            return {name:v.user_bytes(w,addresses[name],count*4) for name,count in
                (('snapshot',48),('storage',32),('cpu',64),('before',64),('history',120),('memory_history',120),('memory_history_valid',120))}
        def frame(label=None):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=current();fields={name:number(name) for name in ('ui_width','ui_height','UI_W','UI_H','small_window','ui_compact',
                        'monitor_y','monitor_bottom','task_y','visible_tasks','volume_rows','metric_rows','first_task','first_volume','first_metric',
                        'view','frozen','monitor_menu','menu_x','menu_y','menu_width','menu_height','menu_columns','menu_button','menu_step','monitor_operations')}
                    small=fields['UI_W']<276 or fields['UI_H']<172
                    valid=bool(fields['small_window'])==small
                    if not small:
                        heading=18 if fields['ui_compact'] else 28
                        if fields['view'] in (0,1,2):
                            row_name=('visible_tasks','volume_rows','metric_rows')[fields['view']];rows=fields[row_name]
                            y=fields['task_y'] if fields['view']==0 else fields['monitor_y']+heading
                            valid=bool(valid and 1<=rows<=8 and y+rows*22+2<=fields['monitor_bottom'])
                        valid=bool(valid and fields['monitor_y']>=0 and fields['monitor_y']<fields['monitor_bottom']
                            and fields['monitor_bottom']<=fields['UI_H']-(22 if fields['ui_compact'] else 30))
                    if fields['monitor_menu']:
                        valid=bool(valid and not small and fields['menu_x']>=0 and fields['menu_y']>=0
                            and fields['menu_x']+fields['menu_width']<=fields['UI_W'] and fields['menu_y']+fields['menu_height']<=fields['UI_H'])
                    if valid and (fields['ui_width'],fields['ui_height'])==(win['cw'],win['ch']):
                        private=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4);canvas=q.memory(win['canvas'],len(private))
                        if private==canvas:
                            assert fields['frozen']==1 and frozen_state()==frozen
                            assert all(private[(r*win['cw']+win['cw']-1)*4+3]==255 for r in range(win['ch']))
                            assert all(private[((win['ch']-1)*win['cw']+c)*4+3]==255 for c in range(win['cw']))
                            assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096)
                            break
                    transients.append(dict(label=label,fields=fields,window=win,registers=q.hmp('info registers')))
                time.sleep(.12)
            else:raise AssertionError('未取得完整且布局合法的实际Monitor帧：'+str(label))
            if label:q.shot(label);shots.append(label)
            return fields
        def menu(item,label=None):
            layout=frame();click(60,layout['monitor_y']+10,True);seed.wait(lambda:number('monitor_menu')==1,'实际右键菜单')
            present();layout=frame(label);column=layout['menu_columns']
            click(layout['menu_x']+8+(item%column)*(layout['menu_button']+8)+layout['menu_button']//2,
                layout['menu_y']+8+(item//column)*layout['menu_step']+10)
            seed.wait(lambda:number('monitor_menu')==0 and number('view')==item,'实际鼠标页面');present();return frame()
        def arrow(down,layout):
            view=layout['view'];rows=layout[('visible_tasks','volume_rows','metric_rows')[view]]
            heading=18 if layout['ui_compact'] else 28;y=layout['task_y'] if view==0 else layout['monitor_y']+heading
            h=rows*22;button=8 if h<32 else 16
            click(layout['UI_W']-30,y+(h-button+3 if down else 3))
        def browse(view,label):
            layout=frame();assert layout['view']==view
            first_name=('first_task','first_volume','first_metric')[view];row_name=('visible_tasks','volume_rows','metric_rows')[view]
            seen=set(range(layout[first_name],layout[first_name]+layout[row_name]))
            while layout[first_name]+layout[row_name]<8:
                previous=layout[first_name];arrow(True,layout);seed.wait(lambda:number(first_name)==previous+1,'真实鼠标下一行')
                present();layout=frame();seen.update(range(layout[first_name],layout[first_name]+layout[row_name]))
            assert seen==set(range(8));coverage[label]=sorted(seen);frame(label)
            arrow(False,layout);present();assert number(first_name)==max(0,layout[first_name]-1)
        seed.wait(lambda:number('samples')>=1 and number('cpu_valid')==1,'实际CPU采样',180)
        q.key('spc');seed.wait(lambda:number('frozen')==1,'原键盘Freeze');present()
        with t.stable_frame():frozen=frozen_state()
        frame('01-native-frozen-valid-layout');browse(0,'02-mouse-all-eight-task-slots')
        menu(1,'03-responsive-context-menu');browse(1,'04-mouse-all-eight-volume-fields')
        menu(2);browse(2,'05-mouse-all-eight-cpu-memory-fields')
        menu(3);frame('06-cpu-history-clipped');menu(4);frame('07-ram-history-clipped')
        menu(1);layout=frame();click(60,layout['monitor_y']+10,True);seed.wait(lambda:number('monitor_menu')==1,'浮层实际打开');present();layout=frame()
        # 对于最矮两列菜单，按钮中部可能已被菜单盖住。点击
        # Overview上边沿内2像素，仍是原按钮命中但在菜单上方；
        # 核对真实菜单矩形，而非凭猜测说“外点不穿透”。
        x=18;y=40 if layout['ui_compact'] else 72
        assert not (layout['menu_x']<=x<layout['menu_x']+layout['menu_width'] and layout['menu_y']<=y<layout['menu_y']+layout['menu_height'])
        before=tuple(number(name) for name in ('view','frozen','first_task','first_volume','first_metric','monitor_operations'))
        click(x,y);seed.wait(lambda:number('monitor_menu')==0,'实际外点只关浮层');present()
        assert tuple(number(name) for name in ('view','frozen','first_task','first_volume','first_metric','monitor_operations'))==before
        frame('08-menu-dismiss-no-overview-through')
        geometry=tuple(current()[name] for name in ('x','y','w','h','cw','ch'));click(number('UI_W')-4,number('UI_H')-4)
        assert tuple(current()[name] for name in ('x','y','w','h','cw','ch'))==geometry
        if vga=='std':
            def extra():
                raw=q.memory(kernel['extras'],6*56)
                return next(row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]==w['handle'])
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            old=tuple(current()[name] for name in ('x','y','w','h'))
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'真实最大化');present();frame('09-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and tuple(current()[name] for name in ('x','y','w','h'))==old,'精确还原');present();frame('10-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'真实最小化')
            bw=max(40*scale//100,min(140*scale//100,(width-240*scale//100)//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click();seed.wait(lambda:extra()[3]==0 and number('ui_focus')==1,'任务栏恢复')
            present();frame('11-taskbar-restored')
            win=current();title=win['h']-win['ch']-1;tw=min(width-win['x'],340*scale//100);th=min(height-bar-win['y'],240*scale//100)
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True);t.point(win['x']+tw-4,win['y']+th-4);q.button(False)
            seed.wait(lambda:(current()['w'],current()['h'])==(tw,th),'真实调整大小');present();frame('12-resized-keeps-all-data')
            win=current();t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True);t.point(win['x']+92,win['y']+title+36);q.button(False)
            seed.wait(lambda:current()['w']==96 and number('small_window')==1,'真实极小窗口');present();frame('13-minimum-keeps-frozen-snapshots')
            click(number('UI_W')//2,number('UI_H')//2);seed.wait(lambda:extra()[4]==1 and number('small_window')==0,'加号真正放大')
            present();frame('14-enlarged');title_button(0)
        else:
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'Monitor私有帧/历史/页表严格回收')
        q.shot('15-all-pages-reclaimed');shots.append('15-all-pages-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and verifier==sha(Path(__file__))
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            width=width,height=height,scale=scale,theme=style,vga=vga,checks=shots,coverage=coverage,overflow=overflow,
            frozen_sha256={name:hashlib.sha256(blob).hexdigest() for name,blob in frozen.items()},transient_snapshots=transients)
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return report
    except Exception:
        if proc.poll() is None:
            if w is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames','monitor_y','monitor_bottom','task_y','visible_tasks','volume_rows','metric_rows','small_window','view','frozen','monitor_menu')}
                    state.update(window=current(),registers=q.hmp('info registers'));(directory/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS';inputs=dict(core['inputs_sha256'])
    for name in ('build/fs/SYS/THEMES/AURORA.CFG','build/fs/SYS/THEMES/CLASSIC.CFG'):inputs[name]=sha(ROOT/name)
    assert inputs=={name:sha(ROOT/name) for name in inputs}
    native=(STAGE/'monitor-native.scx').read_bytes();mapping=(STAGE/'monitor-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['monitor']['scx_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native']['monitor']['map_sha256']
    cases=[(320,200,100,'AURORA','cirrus')]+[(w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200)]
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std')))
    reports=[]
    for width,height,scale,style,vga in cases:
        print(f'开始Monitor {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
        reports.append(run_case(width,height,scale,style,vga,native,mapping,inputs));print(f'通过Monitor {width}-{height}-{scale}-{style.lower()}-{vga}',flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,
        limits='独立Monitor十九组合/全字段鼠标/冻结数据/图线边界/浮层/窗口/完整帧/页回收，不代替正式预装与其它组件或完整M8')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Monitor十九组合PASS',flush=True)


if __name__=='__main__':main()
