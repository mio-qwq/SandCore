#!/usr/bin/env python3
"""mio：第二阶段的新输入/新输出，Windows双盘QEMU真实编译入口。

复用已记录的HMP/QMP只读工具，不复用历史PASS。测试目录显式指定，
启动前复制两张盘，夹具只写副本；运行中只通过键鼠交互驱动程序。
新增终端私有字段后按当前ELF实际terminal_t长度读取提示符，不能
继续用旧28B步距把另一终端的状态拼成“已就绪”。
"""
import argparse
import hashlib
import json
import shutil
import struct
import time
import traceback
import zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme

ROOT = Path(__file__).resolve().parent.parent
q = t.q
OUT = None
DISK = None


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def faults():
    raw = q.memory(q.symbols()['fault_cards'],8*24)
    return [list(struct.unpack_from('<6I',raw,index*24)) for index in range(1,8)
            if struct.unpack_from('<I',raw,index*24)[0]]


def wait(condition, label, seconds=45):
    deadline = time.monotonic()+seconds
    while time.monotonic()<deadline:
        result = condition()
        if result:
            return result
        assert not faults(), (label, faults())
        time.sleep(.15)
    q.shot('timeout-'+label.replace(' ','-')[:40])
    raise AssertionError(label)


def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1
         and 2 not in compiler.task_states()[1:], 'compiler-reclaimed')
    # 此结构是内核私有状态，SCAPI的win_t/task_t和MONITOR缓冲并未扩大。
    address = q.symbols()['terminals']
    window = t.windows()[0]
    def prompt():
        for index in range(6):
            raw = q.memory(address+index*52,12)
            handle,text,used = struct.unpack('<3I',raw)
            if handle==window['handle'] and used:
                return q.memory(text,used).endswith(b'> ') and t.word(q.symbols()['dirty'])==0
        return False
    wait(prompt,'shell-prompt')


def compile_source(driver,source,output,seconds=300):
    idle()
    start = time.monotonic()
    blob = compiler.compile_native(DISK,driver,source,output,seconds)
    idle()
    mapping = compiler.await_file(DISK,output+'.map',30)
    return blob,mapping,round(time.monotonic()-start,3)


def main():
    global OUT,DISK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True)
    parser.add_argument('--action',choices=('boot','native'),default='boot')
    args = parser.parse_args()
    OUT = ROOT/args.out
    OUT.mkdir(parents=True,exist_ok=False)
    t.OUT=q.OUT=compiler.OUT=theme.OUT=OUT
    compiler.v.windows=t.windows
    # 同时复制软盘，后续源码重建不改变正在运行VM的引导输入。
    shutil.copy2(ROOT/'build/sandcore.img',OUT/'sandcore.img')
    inputs = {name:sha((ROOT/name).read_bytes()) for name in
              ('build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym',
               'user/SCAPI.H','user/s3c.c','user/COMPILEUI.inc','kernel/font16.txt')}
    (OUT/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n',encoding='utf-8')
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        old=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert sha(old)=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/OLD.SCX',old)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'phase2',prepare,floppy=(OUT/'sandcore.img').as_posix())
    DISK=OUT/'sanddata-std-128-phase2.img'
    checks=[]
    report=dict(author='mio',status='RUNNING',scope=args.action,inputs=inputs,checks=checks)
    try:
        wait(lambda:t.word(q.symbols()['gfx_width'])==1024,'native-mode')
        time.sleep(3)
        assert t.word(q.symbols()['boot_stage'])==2, ('boot skipped key gate',t.word(q.symbols()['boot_key']))
        q.shot('01-native-welcome')
        t.open_shell(True)
        assert t.word(q.symbols()['boot_stage'])==3 and t.word(q.symbols()['boot_key'])==10
        idle()
        q.shot('02-desktop-shell')
        checks.append('当前双盘原生欢迎/实际按键进入桌面/鼠标开始菜单启动Shell/完整提示符')
        if args.action=='native':
            generations=[]
            driver='BIN/OLD.SCX'
            for generation in range(1,4):
                path='BIN/G'+str(generation)+'.SCX'
                blob,mapping,seconds=compile_source(driver,'SYS/SRC/s3c.c',path,600)
                (OUT/('g'+str(generation)+'.scx')).write_bytes(blob)
                (OUT/('g'+str(generation)+'.map')).write_bytes(mapping)
                generations.append(dict(driver=driver,output=path,sha256=sha(blob),seconds=seconds))
                driver=path
            assert (OUT/'g2.scx').read_bytes()==(OUT/'g3.scx').read_bytes(),'G2/G3不同，不能宣称自举收敛'
            report['generations']=generations
            checks.append('历史已验收编译器实际生成新版G1，G1生成G2/G2生成G3；G2/G3逐字节相等')
        assert not faults()
        report.update(status='PASS',limitations='仅本报告scope；其它完整M8目标继续')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('PHASE2 PASS',args.action,checks,flush=True)
    except Exception as error:
        report.update(status='FAIL',error=str(error),traceback=traceback.format_exc())
        if proc.poll() is None:
            q.shot('failure')
            report['registers']=q.hmp('info registers')
            report['faults']=faults()
        (OUT/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:
            q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':
    main()
