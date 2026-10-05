#!/usr/bin/env python3
"""mio：当前Lens的真实G2编译及完整像素/候选回滚。独立测试副本。

所有操作走HMP/QMP，只读现场/页表；
失败文件比较整幅原图及Fit缓存，不把只读几个像素当作完整回滚。
"""
import hashlib,json,struct,sys,time,zipfile
from array import array
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
OUT=ROOT/'build/m8-lens'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def reference_fit(raw,sw,sh,dw,dh):
    """使用宿主无限精度整数核对每个像素，不采用客体过滤结果。

    图像坐标按像素中心投影至Q8，并夹到首末中心；四邻域先乘
    alpha再按权重求和。此参考允许大整数，不复制客体u32溢出的
    偶然行为，也不用Pillow的不同滤波/舍入规则放松像素判等。
    """
    pixels=array('I');pixels.frombytes(raw);assert sys.byteorder=='little'
    horizontal=[max(0,min((sw-1)*256,((2*x+1)*sw*128)//dw-128)) for x in range(dw)]
    result=array('I')
    for y in range(dh):
        fy=max(0,min((sh-1)*256,((2*y+1)*sh*128)//dh-128));ay=fy%256
        for fx in horizontal:
            ax=fx%256;alpha=0;channels=[0,0,0]
            for yy,wy in ((fy//256,256-ay),(min(fy//256+1,sh-1),ay)):
                for xx,wx in ((fx//256,256-ax),(min(fx//256+1,sw-1),ax)):
                    color=pixels[yy*sw+xx];weight=wx*wy*(color>>24);alpha+=weight
                    for index,shift in enumerate((16,8,0)):channels[index]+=weight*((color>>shift)&255)
            value=0 if not alpha else ((alpha+32768)//65536)<<24
            if alpha:
                for total,shift in zip(channels,(16,8,0)):value|=((total+alpha//2)//alpha)<<shift
            result.append(value)
    return result.tobytes()


def composite(src,dst):
    alpha=src>>24
    return 0xFF000000|sum(((((src>>shift)&255)*alpha+((dst>>shift)&255)*(255-alpha)+127)//255)<<shift for shift in (16,8,0))


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    source=ROOT/'user/lens.c'
    files=('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img',
        'user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','user/lens.c','kernel/font16.txt')
    inputs={name:sha(ROOT/name) for name in files};verifier=sha(Path(__file__))
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    width,height=1920,1080
    body=b''.join(struct.pack('<I',((160 if (x//80+y//60)&1 else 255)<<24)
        |((x&255)<<16)|((y&255)<<8)|((x+y)&255)) for y in range(height) for x in range(width))
    good=struct.pack('<8s6I',b'SCB2MIO\0',width,height,2,0,len(body),0x004F494D)+body
    old=struct.pack('<8s4I',b'SCB1MIO\0',7,5,1,0x004F494D)+bytes(range(35))
    bad_flags=bytearray(good);struct.pack_into('<I',bad_flags,20,1)
    bad_dib=bytearray(54);bad_dib[:2]=b'BM';struct.pack_into('<I',bad_dib,10,54)
    struct.pack_into('<I',bad_dib,14,0xFFFFFFFF);struct.pack_into('<iiHHI',bad_dib,18,3,2,1,24,0)
    # 坏头/短正文无需各复制一份8MB大图，否则夹具本身会占满64MB
    # 测试盘。最后槽位坏值仍使用完整SCB1正文，确实走完逐行候选。
    tiny=struct.pack('<8s6I',b'SCB2MIO\0',2,2,2,0,16,0x004F494D)+struct.pack('<4I',0xFF112233,0x80123456,0x000000FF,0xFFCCDDEE)
    fixtures={'GOOD.SCB':good,'OLD.SCB':old,'BADOLD.SCB':old[:-1]+b'\xff',
        'FLAGS.SCB':bytes(bad_flags[:32]),'SHORT.SCB':good[:33],'TAIL.SCB':tiny+b'\0','DIB.BMP':bytes(bad_dib),'TINY.SCB':tiny}
    for name,sw,sh in (('THIN.SCB',1,1080),('WIDE.SCB',1920,1)):
        data=b''.join(struct.pack('<I',0xFF000000|((x+y)&255)) for y in range(sh) for x in range(sw))
        fixtures[name]=struct.pack('<8s6I',b'SCB2MIO\0',sw,sh,2,0,len(data),0x004F494D)+data
    bmp_expected={}
    for name,bits,top in (('BOTTOM.BMP',24,False),('TOP.BMP',32,True)):
        w,h=7,5;stride=(w*(bits//8)+3)&~3
        pixels=[0xFF000000|(x*31<<16)|(y*47<<8)|(x+y*7) for y in range(h) for x in range(w)]
        data=bytearray()
        for y in (range(h) if top else range(h-1,-1,-1)):
            row=bytearray()
            for x in range(w):row+=struct.pack('<I',pixels[y*w+x])[:bits//8] if bits==24 else struct.pack('<I',pixels[y*w+x]&0xFFFFFF)
            row+=bytes(stride-len(row));data+=row
        header=bytearray(54);header[:2]=b'BM';struct.pack_into('<I',header,2,54+len(data));struct.pack_into('<I',header,10,54)
        struct.pack_into('<IiiHHI',header,14,40,w,-h if top else h,1,bits,0)
        fixtures[name]=bytes(header+data);bmp_expected[name]=b''.join(struct.pack('<I',p) for p in pixels)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/lens.c',source.read_bytes())
        for name in ('NUI.inc','IMAGE.inc'):compiler.disk_put(disk,'SYS/SRC/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        for name,blob in fixtures.items():compiler.disk_put(disk,'HOME/'+name,blob)
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'lens-core',prepare)
    disk=OUT/'sanddata-std-128-lens-core.img';checks=[];transients=[]
    try:
        t.open_shell(True);v.idle()
        native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/lens.c','HOME/LEN.SCX',300);v.idle()
        mapping=compiler.await_file(disk,'HOME/LEN.SCX.map')
        (OUT/'lens-native.scx').write_bytes(native);(OUT/'lens-native.map').write_bytes(mapping)
        addresses=theme.native_symbols(mapping);baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/LEN.SCX HOME/GOOD.SCB\n');w=seed.wait(theme.window,'真正G2 Lens')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name):return v.user_bytes(w,addresses[name],100 if name=='status' else 64).split(b'\0')[0]
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'Lens完整客户帧',120)
        def frame():
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=next(row for row in t.windows() if row['handle']==w['handle'])
                    raw=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4)
                    canvas=q.memory(win['canvas'],len(raw))
                    if raw==canvas:return raw
                    transients.append(dict(source_sha256=hashlib.sha256(raw).hexdigest(),canvas_sha256=hashlib.sha256(canvas).hexdigest(),registers=q.hmp('info registers')))
                time.sleep(.12)
            raise AssertionError('未取得实际完整提交帧')
        def check_original():
            raw=frame();pixels=array('I');pixels.frombytes(raw);source_pixels=array('I');source_pixels.frombytes(body)
            roles=struct.unpack('<32I',v.user_bytes(w,addresses['ui_theme'],128));light=0xFF000000|roles[8];dark=0xFF000000|roles[9]
            left,top,vw,vh=(number(name) for name in ('viewport_x','viewport_y','viewport_w','viewport_h'))
            stride=number('ui_width');panx,pany=number('pan_x'),number('pan_y')
            assert vw<width and vh<height,'大图核心例应真实裁剪而非放大概览'
            expected=array('I');actual=array('I')
            for y in range(vh):
                for x in range(vw):
                    background=light if (x//16+y//16)&1 else dark
                    expected.append(composite(source_pixels[(pany+y)*width+panx+x],background))
                actual.extend(pixels[(top+y)*stride+left:(top+y)*stride+left+vw])
            assert actual.tobytes()==expected.tobytes(),'100%实际像素不等于原图裁剪及主题棋盘alpha合成'
            return dict(width=vw,height=vh,pan_x=panx,pan_y=pany,sha256=hashlib.sha256(actual.tobytes()).hexdigest())
        def state():
            with t.stable_frame():
                return dict(filename=text('filename').decode(),metadata={name:number(name) for name in
                    ('image_w','image_h','cache_w','cache_h','loaded','scb_version','fit','pan_x','pan_y','preview','fit_pixels','fit_w','fit_h')},
                    image=v.user_bytes(w,number('preview'),number('image_w')*number('image_h')*4),
                    filtered=v.user_bytes(w,number('fit_pixels'),number('fit_w')*number('fit_h')*4) if number('fit_pixels') else b'')
        def open_path(path):
            counter=number('lens_operations');q.key('f1');seed.wait(lambda:number('ui_modal')==2,'真实Open输入框')
            q.text(path+'\n');seed.wait(lambda:number('lens_operations')>counter,'候选读取完整结束',180);present()
        seed.wait(lambda:number('loaded')==1 and number('ui_frames')>=2,'1920×1080完整读取/过滤/提交',180)
        assert number('image_w')==number('cache_w')==width and number('image_h')==number('cache_h')==height
        assert state()['image']==body and number('scb_version')==2
        assert number('fit_pixels') and number('fit_w')>0 and number('fit_h')>0,'128MB首组应实际生成Fit过滤缓存'
        initial=state();reference=reference_fit(body,width,height,number('fit_w'),number('fit_h'))
        assert initial['filtered']==reference,'整幅整数alpha双线性Fit与宿主无限精度参考不一致'
        frame();q.shot('01-full-resolution-source-fit');checks.append('完整1920×1080 ARGB缓存8294400B逐字节一致；整幅整数alpha双线性Fit与宿主无限精度参考逐字节一致')
        q.key('1');seed.wait(lambda:number('fit')==0,'原始物理像素倍率');present()
        win=next(win for win in t.windows() if win['handle']==w['handle'])
        start_x=win['x']+1+number('viewport_x')+number('viewport_w')//2
        start_y=win['y']+win['h']-win['ch']-1+number('viewport_y')+number('viewport_h')//2
        t.point(start_x,start_y);q.button(True);seed.wait(lambda:number('dragging')==1,'实际图像拖动按下')
        t.point(start_x-60,start_y-40);q.button(False)
        seed.wait(lambda:number('pan_x')==60 and number('pan_y')==40,'真实60×40物理像素平移');present()
        original=check_original()
        q.shot('02-original-pixels-panned')
        before=state();pages=t.word(kernel['pf_used'])
        counter=number('lens_operations');q.key('f1');seed.wait(lambda:number('ui_modal')==2,'实际Open准备取消')
        q.key('esc');seed.wait(lambda:number('lens_operations')>counter,'真正取消Open');present()
        assert state()==before and t.word(kernel['pf_used'])==pages,'取消Open改变原图或泄漏页'
        for name in ('BADOLD.SCB','FLAGS.SCB','SHORT.SCB','TAIL.SCB','DIB.BMP','MISSING.SCB'):
            open_path('HOME/'+name);assert state()==before,'坏文件破坏完整图片/过滤缓存/倍率/平移：'+name
            assert t.word(kernel['pf_used'])==pages,'失败候选页没有归还：'+name
        open_path('HOME/'+('a'*60));assert state()==before and text('status')==b'Path exceeds 63 bytes'
        assert t.word(kernel['pf_used'])==pages
        q.shot('03-complete-rollback');checks.append('坏尾槽/flags/短尾/多尾/DIB回绕/缺文件/超长路径完整保留上次8294400B图像与Fit缓存/倍率/平移，候选页归还')
        for name in ('TINY.SCB','THIN.SCB','WIDE.SCB'):
            open_path('HOME/'+name);blob=fixtures[name];sw,sh=struct.unpack_from('<2I',blob,8)
            current=state();assert current['image']==blob[32:] and number('fit_w')>=1 and number('fit_h')>=1
            assert current['filtered']==reference_fit(blob[32:],sw,sh,number('fit_w'),number('fit_h'))
            frame();q.shot('04-'+name.split('.')[0].lower())
        checks.append('取消Open保留整图/模式/页；透明不可见蓝色不渗入边缘，1×1080/1920×1均显示至少1物理像素且整幅Fit过滤严格相等')
        for name,expected in bmp_expected.items():
            open_path('HOME/'+name);assert state()['image']==expected and number('image_w')==7 and number('image_h')==5
            q.shot('04-'+name.split('.')[0].lower())
        open_path('HOME/OLD.SCB');palette=struct.unpack('<256I',v.user_bytes(w,addresses['palette_rgb'],1024))
        assert state()['image']==b''.join(struct.pack('<I',0xFF000000|palette[p]) for p in old[24:])
        q.shot('05-old-scb1');checks.append('BMP24正高度/行填充与BMP32负高度/保留字节、旧SCB1原DAC全部原图字节一致')
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'原图/过滤/私有帧/画布/页表严格回收')
        q.shot('06-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and verifier==sha(Path(__file__))
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,
            native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),checks=checks,overflow=overflow,
            original_pixels=original,fit_sha256=hashlib.sha256(reference).hexdigest(),transient_snapshots=transients,
            limits='当前源码真实G2核心/1024×768100%；十九窗口矩阵/150%物理1px/OOM继续补测；PNG/JPG/WebP未实现')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
