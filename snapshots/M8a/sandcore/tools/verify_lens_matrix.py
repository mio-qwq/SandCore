#!/usr/bin/env python3
"""mio：同一真实G2 Lens的十九显示组合、物理图片倍率与窗口操作。

各例使用独立IDE副本和if=floppy引导；HMP/QMP实际输入。只读完整
源图/过滤/客户帧/页表，不向应用写假状态。源码/核改变须另建阶段，
已成功的组合和报告不能覆盖，进行中画帧只恢复后重新取完整快照。
"""
import hashlib,json,struct,time
from array import array
from pathlib import Path
import verify_files as seed
import verify_lens as reference

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-lens';OUT=STAGE/'matrix'
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
MODES=((640,480),(800,600),(1024,768),(1280,720),(1920,1080))


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(width,height,scale,style,vga,native,mapping,inputs,photo):
    tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';directory=OUT/tag
    directory.mkdir(parents=True,exist_ok=True);assert not (directory/'results.json').exists()
    v.OUT=directory;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping)
    bar=32*scale//100 if vga=='std' else 16;verifier=sha(Path(__file__));checks=[];transients=[]
    iw,ih=struct.unpack_from('<2I',photo,8);assert photo[:8]==b'SCB2MIO\0' and len(photo)==32+iw*ih*4
    def prepare(disk):
        compiler.disk_put(disk,'HOME/LEN.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={width}\nheight={height}\nscale={scale}\n'.encode())
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/f'build/fs/SYS/THEMES/{style}.CFG').read_bytes())
    proc=t.launch(vga,128,'lens-matrix',prepare);w=None
    try:
        q.key('ret');seed.wait(lambda:not t.windows() and t.word(kernel['dirty'])==0,'实际桌面')
        assert (t.word(kernel['gfx_width']),t.word(kernel['gfx_height']))==(width,height)
        t.point(30 if vga=='std' else 16,height-bar//2);t.click();seed.wait(lambda:t.word(kernel['menu_open'])!=0,'开始菜单')
        for _ in range(7):q.key('down')
        q.key('ret');v.idle();base=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/LEN.SCX SYS/WALL/TIDAL.SCB\n');w=seed.wait(theme.window,'同一真实G2 Lens启动')
        def number(name):return theme.user_word(w,addresses[name])
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'实际Lens布局提交',180)
        def click(x,y,right=False):
            win=current();ratio=number('ui_scale');title=win['h']-win['ch']-1
            t.point(win['x']+1+x*ratio//100,win['y']+title+y*ratio//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def layout():
            # ui_frames是提交计数，不锁住下一次draw。不能在多次
            # 运行中HMP读取之间混合“上一帧完成”和“下一帧临时
            # viewport=0”。同一次CPU暂停必须同时取得实际几何、
            # 私有帧/内核画布和完整布局。只有完整相等且布局有效
            # 的真实快照才通过；恢复后重采，15秒后仍坏则失败。
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    fields={name:number(name) for name in ('ui_width','ui_height','ui_scale','UI_W','UI_H','small_window','ui_compact',
                        'viewport_x','viewport_y','viewport_w','viewport_h','work_y','work_h')}
                    win=current();same_geometry=(fields['ui_width'],fields['ui_height'])==(win['cw'],win['ch'])
                    small=fields['UI_W']<276 or fields['UI_H']<148
                    vx,vy,vw,vh=(fields[name] for name in ('viewport_x','viewport_y','viewport_w','viewport_h'))
                    geometry_ok=same_geometry and bool(fields['small_window'])==small
                    bounds_ok=small or (vw>0 and vh>0 and vx>=0 and vy>=fields['work_y']*fields['ui_scale']//100
                        and vx+vw<=fields['ui_width'] and vy+vh<=fields['ui_height']
                        and fields['work_y']+fields['work_h']<=fields['UI_H']-(22 if fields['ui_compact'] else 30)-12)
                    if geometry_ok and bounds_ok:
                        count=win['cw']*win['ch']*4
                        private=v.user_bytes(w,number('ui_pixels'),count);canvas=q.memory(win['canvas'],count)
                        if private==canvas:return
                        fields.update(source_sha256=hashlib.sha256(private).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest())
                    transients.append(dict(label='layout-producer',fields=fields,window=win,registers=q.hmp('info registers')))
                time.sleep(.12)
            raise AssertionError('没有取得几何/视口/完整帧同时有效的实际快照')
        def frame(label):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=current();pixels=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4)
                    canvas=q.memory(win['canvas'],len(pixels))
                    if pixels==canvas:
                        assert all(pixels[(r*win['cw']+win['cw']-1)*4+3]==255 for r in range(win['ch']))
                        assert all(pixels[((win['ch']-1)*win['cw']+c)*4+3]==255 for c in range(win['cw']))
                        assert v.user_bytes(w,number('ui_pixels')+1920*1080*4-4096,4096)==bytes(4096),'最大私有帧末页越界'
                        break
                    transients.append(dict(label=label,source_sha256=hashlib.sha256(pixels).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest(),registers=q.hmp('info registers')))
                time.sleep(.12)
            else:raise AssertionError('未取得完整提交帧：'+label)
            q.shot(label);checks.append(label);return pixels
        def original(raw):
            # 照片为完全不透明SCB2，100%每个物理像素必须等于原图
            # 的精确裁剪。字体/按钮的150%只影响视口大小，不改变倍率。
            assert not number('fit');source=array('I');source.frombytes(photo[32:]);dest=array('I');dest.frombytes(raw)
            vw,vh=number('viewport_w'),number('viewport_h');ww,hh=min(iw,vw),min(ih,vh)
            xx=number('viewport_x')+(vw-ww)//2;yy=number('viewport_y')+(vh-hh)//2
            stride=number('ui_width');px,py=number('pan_x'),number('pan_y')
            assert 0<=px<=iw-ww and 0<=py<=ih-hh
            for y in range(hh):
                assert dest[(yy+y)*stride+xx:(yy+y)*stride+xx+ww]==source[(py+y)*iw+px:(py+y)*iw+px+ww],'100%倍率随UI缩放改变或平移裁剪错误'
        seed.wait(lambda:number('loaded')==1 and number('ui_frames')>=2,'完整照片/过滤/首帧',180);layout()
        with t.stable_frame():assert v.user_bytes(w,number('preview'),iw*ih*4)==photo[32:],'高清完整缓存不是原图'
        assert number('cache_w')==iw and number('cache_h')==ih and number('fit')==1
        fw,fh=number('fit_w'),number('fit_h');pointer=number('fit_pixels');assert pointer and fw and fh
        with t.stable_frame():filtered=v.user_bytes(w,pointer,fw*fh*4)
        assert filtered==reference.reference_fit(photo[32:],iw,ih,fw,fh),'当前视口整幅Fit不等于整数参考'
        frame('01-native-photo-fit')
        # 真实鼠标hover只刷新表面，完整缓存指针/字节不应改变。
        click(170,16);present();assert number('fit_pixels')==pointer
        with t.stable_frame():assert v.user_bytes(w,pointer,len(filtered))==filtered
        # 工具条100%是第三项，位置从实际换行规则重建，不借用
        # 客体ui_action伪造点击或只检查模式字段而跳过画面。
        bx=16;by=38 if number('ui_compact') else 70
        for label in ('Open','Fit','100%'):
            bw=len(label)*8+20
            if bx+bw>number('UI_W')-16:bx=16;by+=26 if number('ui_compact') else 36
            if label=='100%':break
            bx+=bw+8
        click(bx+bw//2,by+10);seed.wait(lambda:number('fit')==0,'鼠标100%按钮');present();original(frame('02-original-physical-pixels'))
        q.key('c');present();win=current();vx,vy,vw,vh=(number(name) for name in ('viewport_x','viewport_y','viewport_w','viewport_h'))
        assert number('pan_x')==max(0,(iw-vw)//2) and number('pan_y')==max(0,(ih-vh)//2),'Center未真正居中'
        start_x=win['x']+1+vx+vw//2;start_y=win['y']+win['h']-win['ch']-1+vy+vh//2
        # 150%特意选逻辑取整不变的相邻物理点，不能靠跨过两个
        # 像素才算成功。其它缩放也使用同样1px真实QMP位移。
        if scale==150 and vga=='std':
            while ((start_x-win['x']-1)*100//scale)!=((start_x-win['x']-2)*100//scale):start_x+=1
            title=win['h']-win['ch']-1
            while ((start_y-win['y']-title)*100//scale)!=((start_y-win['y']-title-1)*100//scale):start_y+=1
        t.point(start_x,start_y);q.button(True);seed.wait(lambda:number('dragging')==1,'真实照片拖动按下')
        px,py=number('pan_x'),number('pan_y');logical_before=[number('ui_x'),number('ui_y')];q.move(-1,-1)
        seed.wait(lambda:number('pan_x')==px+1 and number('pan_y')==py+1,'真实1px物理平移',30)
        q.button(False);present();original(frame('03-one-physical-pixel-pan'))
        assert number('pan_x')==px+1 and number('pan_y')==py+1
        logical_after=[number('ui_x'),number('ui_y')]
        if scale==150 and vga=='std':assert logical_after==logical_before,'150%例必须覆盖逻辑坐标未变的1px物理位移'
        click(number('work_x')+number('work_w')//2,number('work_y')+number('work_h')//2,True)
        seed.wait(lambda:number('fit')==1,'图片视口实际右键Fit');present();frame('04-right-button-fit')
        geometry=tuple(current()[key] for key in ('x','y','w','h','cw','ch'))
        click(number('UI_W')-4,number('UI_H')-4)
        assert tuple(current()[key] for key in ('x','y','w','h','cw','ch'))==geometry,'原地抓取点击改变窗口'
        if vga=='std':
            def extra():
                raw=q.memory(kernel['extras'],6*56)
                return next(row for i in range(6) if (row:=struct.unpack_from('<14I',raw,i*56))[0]==w['handle'])
            def title_button(index):
                win=current();box=24*scale//100;title=win['h']-win['ch']-1
                t.point(win['x']+win['w']-box*(index+1)+box//2,win['y']+title//2);t.click()
            old=tuple(current()[key] for key in ('x','y','w','h'))
            title_button(1);seed.wait(lambda:extra()[4]==1 and current()['w']==width,'实际最大化');present();layout();frame('05-maximized')
            title_button(1);seed.wait(lambda:extra()[4]==0 and tuple(current()[key] for key in ('x','y','w','h'))==old,'精确还原');present();layout();frame('06-restored')
            title_button(2);seed.wait(lambda:extra()[3]==1,'真实最小化')
            available=width-240*scale//100;bw=max(40*scale//100,min(140*scale//100,available//2))
            t.point(116*scale//100+bw+bw//2,height-bar//2);t.click();seed.wait(lambda:extra()[3]==0 and number('ui_focus')==1,'任务栏恢复')
            present();frame('07-taskbar-restored')
            win=current();title=win['h']-win['ch']-1
            target_w=min(width-win['x'],340*scale//100);target_h=min(height-bar-win['y'],240*scale//100)
            t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+target_w-4,win['y']+target_h-4);q.button(False)
            seed.wait(lambda:(current()['w'],current()['h'])==(target_w,target_h),'实际按偏移调整大小');present();layout();frame('08-resized-photo')
            win=current();t.point(win['x']+win['w']-4,win['y']+win['h']-4);q.button(True)
            t.point(win['x']+92,win['y']+title+36);q.button(False)
            seed.wait(lambda:current()['w']==96 and number('small_window')==1,'实际极小窗口');present();frame('09-minimum-window')
            with t.stable_frame():assert v.user_bytes(w,number('preview'),iw*ih*4)==photo[32:],'极小窗口清空高清图片'
            click(number('UI_W')//2,number('UI_H')//2);seed.wait(lambda:extra()[4]==1 and number('small_window')==0,'加号实际放大')
            present();layout();frame('10-enlarged-photo');title_button(0)
        else:
            win=current();t.point(win['x']+win['w']-6,win['y']+6);t.click()
        v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==base[0]+t.word(kernel['desktop_pages'])-base[1],'原图/过滤/私有帧/画布/页表严格回收')
        q.shot('11-reclaimed');checks.append('11-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,
            native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            width=width,height=height,scale=scale,theme=style,vga=vga,checks=checks,overflow=overflow,
            one_pixel_pan=dict(physical_delta=[-1,-1],pan_before=[px,py],pan_after=[px+1,py+1],logical_before=logical_before,logical_after=logical_after),
            fit_dimensions=[fw,fh],fit_sha256=hashlib.sha256(filtered).hexdigest(),transient_snapshots=transients)
        (directory/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return report
    except Exception:
        if proc.poll() is None:
            if w is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames','small_window','fit','pan_x','pan_y','dragging','work_y','work_h','viewport_x','viewport_y','viewport_w','viewport_h')}
                    state.update(window=current(),registers=q.hmp('info registers'))
                    (directory/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=dict(core['inputs_sha256'])
    for name in ('build/fs/SYS/WALL/TIDAL.SCB','build/fs/SYS/THEMES/AURORA.CFG','build/fs/SYS/THEMES/CLASSIC.CFG','tools/verify_lens.py'):inputs[name]=sha(ROOT/name)
    assert inputs=={name:sha(ROOT/name) for name in inputs}
    native=(STAGE/'lens-native.scx').read_bytes();mapping=(STAGE/'lens-native.map').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native_sha256'] and hashlib.sha256(mapping).hexdigest()==core['native_map_sha256']
    photo=(ROOT/'build/fs/SYS/WALL/TIDAL.SCB').read_bytes();cases=[(320,200,100,'AURORA','cirrus')]
    cases.extend((w,h,s,'AURORA','std') for w,h in MODES for s in (100,150,200))
    cases.extend(((640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std')))
    reports=[];verifier=sha(Path(__file__))
    for width,height,scale,style,vga in cases:
        tag=f'{width}-{height}-{scale}-{style.lower()}-{vga}';existing=OUT/tag/'results.json';print('开始Lens '+tag,flush=True)
        if existing.exists():
            result=json.loads(existing.read_text(encoding='utf-8'));assert result['status']=='PASS' and result['inputs_sha256']==inputs and result['verifier_sha256']==verifier
        else:result=run_case(width,height,scale,style,vga,native,mapping,inputs,photo)
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        reports.append(result);print('通过Lens '+tag,flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=reports,limits='Lens十九组合/高清原图/整幅Fit/物理100%/1px平移/窗口/完整帧和回收；不代替其它组件、压缩解码、全部M8和游戏光追')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
