#!/usr/bin/env python3
"""mio：Lens草稿的真实G2编译及完整像素/候选回滚。独立测试副本。

运行草稿不改变默认发布源码。所有操作走HMP/QMP，只读现场/页表；
失败文件比较整幅原图及Fit缓存，不把只读几个像素当作完整回滚。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
OUT=ROOT/'build/m8-lens-draft'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    source=ROOT/'build/m8-next/lens.c'
    files=('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img',
        'user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','build/m8-next/lens.c','kernel/font16.txt')
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
    tiny=struct.pack('<8s6I',b'SCB2MIO\0',2,2,2,0,16,0x004F494D)+struct.pack('<4I',0xFF112233,0x80123456,0x00000000,0xFFCCDDEE)
    fixtures={'GOOD.SCB':good,'OLD.SCB':old,'BADOLD.SCB':old[:-1]+b'\xff',
        'FLAGS.SCB':bytes(bad_flags[:32]),'SHORT.SCB':good[:33],'TAIL.SCB':tiny+b'\0','DIB.BMP':bytes(bad_dib)}
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
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'lens-draft',prepare)
    disk=OUT/'sanddata-std-128-lens-draft.img';checks=[]
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
        def state():
            with t.stable_frame():
                return dict(filename=text('filename').decode(),metadata={name:number(name) for name in
                    ('image_w','image_h','cache_w','cache_h','loaded','scb_version','fit','pan_x','pan_y','preview','fit_pixels','fit_w','fit_h')},
                    image=v.user_bytes(w,number('preview'),number('image_w')*number('image_h')*4),
                    filtered=v.user_bytes(w,number('fit_pixels'),number('fit_w')*number('fit_h')*4) if number('fit_pixels') else b'')
        def open_path(path):
            counter=number('lens_operations');q.key('ctrl-a');seed.wait(lambda:number('ui_modal')==2,'真实Open输入框')
            q.text(path+'\n');seed.wait(lambda:number('lens_operations')>counter,'候选读取完整结束',180);present()
        seed.wait(lambda:number('loaded')==1 and number('ui_frames')>=2,'1920×1080完整读取/过滤/提交',180)
        assert number('image_w')==number('cache_w')==width and number('image_h')==number('cache_h')==height
        assert state()['image']==body and number('scb_version')==2
        assert number('fit_pixels') and number('fit_w')>0 and number('fit_h')>0,'128MB首组应实际生成Fit过滤缓存'
        q.shot('01-full-resolution-source-fit');checks.append('完整1920×1080 ARGB缓存8294400B逐字节一致，真实Fit缓存已生成')
        q.key('1');seed.wait(lambda:number('fit')==0,'原始物理像素倍率');present()
        win=next(win for win in t.windows() if win['handle']==w['handle'])
        start_x=win['x']+1+number('viewport_x')+number('viewport_w')//2
        start_y=win['y']+win['h']-win['ch']-1+number('viewport_y')+number('viewport_h')//2
        t.point(start_x,start_y);q.button(True);seed.wait(lambda:number('dragging')==1,'实际图像拖动按下')
        t.point(start_x-60,start_y-40);q.button(False)
        seed.wait(lambda:number('pan_x')==60 and number('pan_y')==40,'真实60×40物理像素平移');present()
        q.shot('02-original-pixels-panned')
        before=state();pages=t.word(kernel['pf_used'])
        for name in ('BADOLD.SCB','FLAGS.SCB','SHORT.SCB','TAIL.SCB','DIB.BMP','MISSING.SCB'):
            open_path('HOME/'+name);assert state()==before,'坏文件破坏完整图片/过滤缓存/倍率/平移：'+name
            assert t.word(kernel['pf_used'])==pages,'失败候选页没有归还：'+name
        open_path('HOME/'+('a'*60));assert state()==before and text('status')==b'Path exceeds 63 bytes'
        assert t.word(kernel['pf_used'])==pages
        q.shot('03-complete-rollback');checks.append('坏尾槽/flags/短尾/多尾/DIB回绕/缺文件/超长路径完整保留上次8294400B图像与Fit缓存/倍率/平移，候选页归还')
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
            limits='草稿独立真实G2核心/1024×768100%；尚未替换默认Lens，双线性全字节/100%实际提交像素与十九窗口矩阵需继续补测；PNG/JPG/WebP未实现')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
