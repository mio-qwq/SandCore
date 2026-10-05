#!/usr/bin/env python3
"""mio：SCB2/SCX图标/真实图片/LEGACY的独立双盘运行证据。

只在关机测试盘注入测试资源；开机后通过Shell输入编译/执行，HMP和
QMP输入真实驱动，内存只读取结果及画布。每个原生应用由M7已验收G2
生成；新版SCCC也先由G2实际编译，再用它原生封装内置图标。
"""
import hashlib
import json
import struct
import time
import zipfile
from pathlib import Path
from PIL import Image
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-images';OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT
q=t.q

def idle():
    theme.wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1
               and 2 not in compiler.task_states()[1:],'Shell焦点与僵尸回收')

def baseline():return (t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))

def cache_snapshot(path):
    # 私有缓存布局仅用于只读诊断，不是公共ABI。校验实际缓存原尺寸
    # 与像素，不仅凭菜单标签推测图标来源；SCX位图偏移必须非0。
    raw=q.memory(q.symbols()['cache'],32*96)
    for index in range(32):
        name,offset,length,touch,pixels,w,h,pages,used=struct.unpack_from('<64s8I',raw,index*96)
        if used and name.split(b'\0',1)[0].decode().upper()==path.upper():
            assert pixels and 1<=w<=128 and 1<=h<=128
            return offset,w,h,hashlib.sha256(q.memory(pixels,w*h*4)).hexdigest()
    return None

def main():
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as z:
        g2=z.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    tiny=b'#include "SCAPI.H"\nint main(void){int w=sc_open_rgb("Native icon",360,120);sc_fill_rgb(w,0,0,360,120,SC_RGB_WHITE);sc_text_rgb(w,16,32,"Native SCCC icon PASS",SC_RGB_INK);while(sc_key()!=27)sc_yield();return 0;}\n'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'HOME/TINY.C',tiny)
    proc=t.launch('std',128,'images',prepare);disk=OUT/'sanddata-std-128-images.img'
    artifacts={};checks=[]
    try:
        t.open_shell(True)
        assert t.word(q.symbols()['disk_sectors'])==131072
        assert t.word(q.symbols()['fs_disk_sectors'])==131072
        q.shot('01-aurora-icons-wallpaper')
        probe=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/imageprobe.c','HOME/IM.SCX',120)
        address=theme.native_symbols(compiler.await_file(disk,'HOME/IM.SCX.map'))['image_probe']
        idle();before=baseline();q.text('run HOME/IM.SCX\n')
        w=theme.wait(theme.window,'图像探针窗口')
        theme.wait(lambda:theme.user_word(w,address)==1,'图标坏容器探针完成')
        assert theme.user_word(w,address+4)==0, ('probe failed',theme.user_word(w,address+8))
        time.sleep(.8);q.shot('02-native-icon-boundaries');theme.reclaim(before);idle()
        embedded_icon=theme.wait(lambda:cache_snapshot('HOME/ICONHELLO.SCX'),'SCX内置资源真实缓存')
        fixed_icon=theme.wait(lambda:cache_snapshot('SYS/ICONS/AURORA/LENS.SCB'),'显式ARGB资源真实缓存')
        assert embedded_icon[:3]==(288,2,1) and fixed_icon[1:3]==(128,128)
        before=baseline();q.text('run HOME/IM.SCX classic\n')
        w=theme.wait(theme.window,'Classic身份验证窗口')
        theme.wait(lambda:theme.user_word(w,address)==2,'真实切Classic')
        assert theme.user_word(w,address+4)==0
        classic_icon=theme.wait(lambda:cache_snapshot('SYS/ICONS/CLASSIC/LENS.SCB'),'@LENS应取新主题资源')
        assert classic_icon[1:3]==(32,32)
        assert cache_snapshot('HOME/ICONHELLO.SCX')==embedded_icon
        assert cache_snapshot('SYS/ICONS/AURORA/LENS.SCB')==fixed_icon
        q.shot('02b-classic-icon-identities');theme.reclaim(before);idle()
        checks.append('G2探针拒绝全部SCX图标坏头；64MB后段/LEGACY可读；实际注册空/显式/@三种快捷方式，换Classic只有@改为32px，SCX内置与显式128px像素摘要保持')
        native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/s3c.c','HOME/C.SCX',240)
        idle()
        command='run HOME/C.SCX HOME/TINY.C HOME/EMBED.SCX SYS/ICONS/AURORA/LENS.SCB'
        assert len(command)<=255 and len(command.split(' ',2)[2])<=127
        q.text(command);q.shot('03-complete-native-icon-command');q.key('ret')
        log=compiler.await_file(disk,'HOME/EMBED.SCX.log',180);assert log.startswith(b'SCCC OK'),log
        embedded=compiler.await_file(disk,'HOME/EMBED.SCX',180)
        load=struct.unpack_from('<I',embedded,12)[0]
        assert struct.unpack_from('<I',embedded,24)[0]==2
        icon=(ROOT/'build/fs/SYS/ICONS/AURORA/LENS.SCB').read_bytes()
        assert embedded[36+load:]==icon
        idle();before=baseline();q.text('run HOME/EMBED.SCX\n')
        theme.wait(theme.window,'带图标原生SCX正常执行');time.sleep(.8)
        q.shot('04-native-embedded-app');theme.reclaim(before);idle()
        checks.append('新版SCCC由G2在系统内实际生成，再在系统内编译TINY并封装SCX bit1图标，尾部逐字节等于指定SCB2，真实加载执行/退出')
        lens=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/lens.c','HOME/LENS.SCX',180)
        maps=theme.native_symbols(compiler.await_file(disk,'HOME/LENS.SCX.map'));idle()
        for index,path in enumerate(('SYS/WALL/TIDAL.SCB','SYS/ICONS/AURORA/LENS.SCB')):
            before=baseline();q.text('run HOME/LENS.SCX '+path+'\n')
            w=theme.wait(theme.window,'原生Lens窗口')
            theme.wait(lambda:theme.user_word(w,maps['loaded'])==1,'SCB2图像应完整读取',60)
            assert theme.user_word(w,maps['scb_version'])==2
            assert theme.user_word(w,maps['bmp'])==0
            time.sleep(1);q.shot(f'05-lens-scb2-{index}');theme.reclaim(before);idle()
        checks.append('G2实际生成Lens，打开真实细沙RGB大图与透明图标；SCB2尺寸/原ARGB/透明棋盘显示，退出回收通过')
        before=baseline();q.text('run LEGACY/APPS/CALC.SCX\n')
        theme.wait(theme.window,'原字节Legacy Calculator应可启动');time.sleep(.8)
        q.shot('06-legacy-original-calc');theme.reclaim(before);idle()
        legacy=json.loads((ROOT/'legacy/M7/provenance.json').read_text())
        for item in legacy['runtime']:
            blob=compiler.file_content(disk,item['path'])
            assert blob and hashlib.sha256(blob).hexdigest()==item['sha256']
        checks.append('全部Legacy SCX与验收M7逐文件摘要一致；原Calculator实际启动/显示/退出')
        assert t.word(q.symbols()['event_overflow'])==t.word(q.symbols()['keyboard_overflow'])==0
        for name,data in [('G2',g2),('imageprobe',probe),('new-s3c-native',native),('embedded-native',embedded),('lens-native',lens)]:
            artifacts[name]=hashlib.sha256(data).hexdigest();(OUT/(name+'.scx')).write_bytes(data)
        for path in ('kernel/image.c','kernel/desktop.c','kernel/ata.c','kernel/fs.c','user/s3c_emit.inc','user/lens.c','build/sandcore.img'):
            artifacts[path]=hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
        result=dict(author='mio',status='PASS',checks=checks,sha256=artifacts,
                    limitation='PNG/JPG/WebP、全显示/缩放、全Legacy与完整M8发布矩阵还未完成；本轮不是新版三代收敛结论')
        (OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('IMAGE PASS',result,flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
if __name__=='__main__':main()
