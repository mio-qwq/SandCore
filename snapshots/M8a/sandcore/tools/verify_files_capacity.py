#!/usr/bin/env python3
"""mio：真实Files达到512个直接子项，不以缓冲声明代替上机证据。

启动前准备独立满盘；之后删菜单/建目录/取消/删除/再利用空槽均
由G2 Files经普通系统调用完成。菜单已解析的内存快照仍可使用，
删除测试盘上的菜单不会影响默认开发盘。根目录投影先有511项，
移走SYS内一项重复目录来源，再在根下补一项才真正达到512项。
"""
import hashlib,json,struct,time
from pathlib import Path
import verify_files as seed

v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-files';OUT=STAGE/'capacity'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists(),'成功证据不可覆盖'
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs}
    verifier=sha(Path(__file__));native=(STAGE/'files-native.scx').read_bytes()
    mapping=(STAGE/'files-native.map').read_bytes();addresses=theme.native_symbols(mapping)
    assert hashlib.sha256(native).hexdigest()==core['native']['files']['scx_sha256']
    shell=(ROOT/'build/m8-userspace/shell-native.scx').read_bytes()
    assert hashlib.sha256(shell).hexdigest()=='5516ca0db9cd600e184d4fa58f8a02d3af0300addc82f96ae33abd386a227bc3'
    (OUT/'historical-shell-native.scx').write_bytes(shell)
    fixtures={f'F{i:03}.TXT':f'capacity {i} / mio\n'.encode() for i in range(508)}
    def prepare(disk):
        # 构造测试盘只在QEMU启动前。四项启动资源+508项作品恰512
        # 条底层记录，但SYS/CORE与SYS/MENU共投影一个SYS，故只有
        # 511个直接子项；不能把底层目录记录数冒充GUI实际行数。
        records=dict(fixtures)
        records.update({'BIN/SHELL.SCX':shell,'FIL.SCX':native,
            'SYS/CORE/CORE.SKM':(ROOT/'build/fs/SYS/CORE/CORE.SKM').read_bytes(),
            'SYS/MENU.CFG':(ROOT/'build/fs/SYS/MENU.CFG').read_bytes()})
        assert len(records)==512
        raw=bytearray(64*1024*1024);raw[:9]=b'SANDFSMIO'
        struct.pack_into('<4I',raw,12,512,80,4,len(raw)//512)
        lba=81
        for index,(name,blob) in enumerate(sorted(records.items())):
            struct.pack_into('<64sII',raw,512+index*72,name.encode(),lba,len(blob))
            raw[lba*512:lba*512+len(blob)]=blob;lba+=(len(blob)+511)//512
        disk.write_bytes(raw)
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'files-capacity',prepare)
    disk=OUT/'sanddata-std-128-files-capacity.img';checks=[]
    try:
        t.open_shell(True);v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        assert t.word(kernel['nfiles'])==512
        q.text('run FIL.SCX\n');w=seed.wait(theme.window,'真正G2 Files')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name):return v.user_bytes(w,addresses[name],64).split(b'\0')[0]
        def names():
            raw=v.user_bytes(w,addresses['names'],512*64)
            return [raw[i*64:(i+1)*64].split(b'\0')[0] for i in range(number('count'))]
        def present():
            frames=number('ui_frames');seed.wait(lambda:number('ui_frames')>frames and t.word(kernel['dirty'])==0,'实际提交完成')
        def click(x,y):
            win=next(win for win in t.windows() if win['handle']==w['handle']);scale=number('ui_scale')
            t.point(win['x']+1+x*scale//100,win['y']+win['h']-win['ch']-1+y*scale//100);t.click()
        def select(name):
            index=names().index(name.encode());rows=number('visible_rows');assert rows>0
            while number('selection')//rows!=index//rows:
                old=number('selection');click(120 if old<index else 40,number('navigation_y')+10)
                seed.wait(lambda:number('selection')!=old,'实际鼠标分页')
            click(number('list_left')+64,number('list_y')+(index%rows)*number('row_height')+number('row_height')//2)
            seed.wait(lambda:number('selection')==index,'实际鼠标选中末页文件')
        def operation(key,path=None,answer=None):
            counter=number('file_operations');q.key(key)
            if path is not None:
                seed.wait(lambda:number('ui_modal')==2,'完整路径框');q.text(path+'\n')
            if answer is not None:
                seed.wait(lambda:number('ui_modal')==3,'真实删除确认');q.key(answer)
            seed.wait(lambda:number('file_operations')>counter,'真实文件调用及列表刷新');present()
        seed.wait(lambda:number('ui_frames')>=2,'完整根列表')
        assert text('cwd')==b'' and number('count')==511
        operation('n','OVERFLOW');assert number('count')==511 and t.word(kernel['nfiles'])==512
        assert compiler.file_content(disk,'OVERFLOW') is None and text('status').startswith(b'Operation failed:')
        q.key('3');seed.wait(lambda:text('cwd')==b'SYS','进入SYS');present();select('MENU.CFG')
        operation('x',answer='y');assert compiler.file_content(disk,'SYS/MENU.CFG') is None
        q.key('1');seed.wait(lambda:text('cwd')==b'','回根目录');present()
        operation('n','ROOTCAP');assert number('count')==512 and t.word(kernel['nfiles'])==512
        actual=names();assert len(actual)==len(set(actual))==512
        assert set(actual)=={name.encode() for name in fixtures}|{b'BIN',b'SYS',b'FIL.SCX',b'ROOTCAP'}
        q.shot('01-real-512-direct-children');checks.append('512个真实直接子项及完整名称，满盘新建明确拒绝')
        select('ROOTCAP');assert number('selection')==511,'新增的第512项必须真实可达'
        operation('x',answer='esc');assert number('count')==512 and names()[number('selection')]==b'ROOTCAP'
        q.shot('02-last-row-delete-cancel')
        operation('x',answer='y');assert number('count')==511 and t.word(kernel['nfiles'])==511
        operation('n','REUSED');assert number('count')==512 and t.word(kernel['nfiles'])==512
        select('F507.TXT');assert names()[number('selection')]==b'F507.TXT'
        q.shot('03-slot-reused-last-file-reached')
        for name,blob in fixtures.items():assert compiler.file_content(disk,name)==blob,name
        checks.append('鼠标分页到第512项、删除取消保留、真实删除/空槽再利用；508份作品完整字节未改')
        q.key('esc');v.idle()
        seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'512项Files严格回收')
        q.shot('04-reclaimed')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,
            native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),
            historical_shell_sha256=hashlib.sha256(shell).hexdigest(),checks=checks,overflow=overflow,
            limits='独立满容量测试盘/1024×768100%；默认开发盘不改，完整显示矩阵与其他组件独立验证')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
