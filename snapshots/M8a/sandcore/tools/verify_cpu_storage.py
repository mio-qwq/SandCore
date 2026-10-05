#!/usr/bin/env python3
"""mio：真实三环CPU/存储/旧ABI、512项与旧32扇区冷启动验收。

G2实际编译Monitor/探针，所有运行目录改动通过系统调用发生。只读
现场、页表、磁盘和截图；副本盘准备允许离线封装，不注入运行内存。
"""
import hashlib,json,struct,time,zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-system';OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT;q=t.q

def wait(condition,description,seconds=40):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=condition()
        if value:return value
        assert 3 not in compiler.task_states(),description+' / 三环异常'
        time.sleep(.15)
    q.shot('timeout');raise AssertionError(description)

def values(window,address):
    pd=t.word(q.symbols()['tasks']+window['owner']*168)
    return list(struct.unpack('<20I',q.memory(t.physical(pd,address),80)))

def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1
         and 2 not in compiler.task_states()[1:],'关闭/编译退出回收')

def old32(disk):
    raw=disk.read_bytes();count,ds,version=struct.unpack_from('<III',raw,12)
    assert version==4 and count<=192
    files=[]
    for i in range(count):
        name,start,size=struct.unpack_from('<64sII',raw,512+i*72)
        files.append((name,None if not start else raw[start*512:start*512+size]))
    output=bytearray(len(raw));output[:512]=raw[:512];struct.pack_into('<I',output,16,32)
    at=33
    for i,(name,blob) in enumerate(files):
        struct.pack_into('<64sII',output,512+i*72,name,0 if blob is None else at,0 if blob is None else len(blob))
        if blob is not None:
            output[at*512:at*512+len(blob)]=blob;at+=(len(blob)+511)//512
    disk.write_bytes(output)

def fill(proc,disk,address,label):
    q.text('run HOME/SYS.SCX\n');w=wait(theme.window,'原生CPU/存储探针')
    wait(lambda:values(w,address)[0]==1,'空闲/真实三环计算采样')
    q.key('a');wait(lambda:values(w,address)[0]==2,'旧GETKEY忽略EBX仍消费')
    t.point(w['x']+80,w['y']+150);q.button(True)
    wait(lambda:values(w,address)[0]>=3,'旧POINTER忽略EDX仍消费')
    q.button(False)
    data=wait(lambda:values(w,address) if values(w,address)[0]==4 else None,'实际系统调用填满目录',180)
    assert data[1]==0,(label,data)
    expected=512 if label=='new80' else 192
    assert data[6]==data[7]==expected,(label,data)
    q.shot(label+'-full-directory')
    disk_count=struct.unpack_from('<I',disk.read_bytes(),12)[0]
    assert disk_count==expected,'运行时已写回真实目录表'
    q.hmp('quit');proc.wait(timeout=10)
    return data

def cold_cleanup(disk,tag,address,label):
    proc=t.launch('std',128,tag,reuse=True)
    try:
        t.open_shell(True);baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/SYS.SCX cleanup\n');w=wait(theme.window,'同盘冷启动清理探针')
        data=wait(lambda:values(w,address) if values(w,address)[0]==5 else None,'满盘读回/整组rename/完整list/清理',180)
        assert data[1]==0,(label,data)
        if label=='new80':assert data[8]>192,'新目录必须实际超过旧容量'
        q.shot(label+'-coldboot-cleanup');q.key('esc');idle()
        wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],
             '临时rename/目录列表/窗口/堆/页表完整回收')
        return data
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)

def main():
    # 运行时物理偏移依赖同批内核符号。先登记精确输入，结束再核对，
    # 不把上次构建的探针结果误归给本次内核或正在重建的符号表。
    inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('build/sandcore.img','build/kernel.elf','build/kernel.sym','user/SCAPI.H','user/sysprobe.c','user/monitor.c')}
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'system-new',prepare);disk=OUT/'sanddata-std-128-system-new.img'
    checks=[]
    try:
        t.open_shell(True)
        probe=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/sysprobe.c','HOME/SYS.SCX',180);idle()
        monitor=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/monitor.c','HOME/MON.SCX',180);idle()
        probe_map=compiler.await_file(disk,'HOME/SYS.SCX.map');address=theme.native_symbols(probe_map)['sys_probe']
        monitor_map=compiler.await_file(disk,'HOME/MON.SCX.map');ms=theme.native_symbols(monitor_map)
        for name,blob in (('sysprobe-native.scx',probe),('sysprobe-native.map',probe_map),('monitor-native.scx',monitor),('monitor-native.map',monitor_map)):(OUT/name).write_bytes(blob)
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/MON.SCX\n');w=wait(theme.window,'真实原生Monitor')
        wait(lambda:theme.user_word(w,ms['samples'])>=2,'真实CPU差分历史')
        q.shot('01-native-monitor-cpu')
        t.point(w['x']+1+160,w['y']+32+82);t.click()
        wait(lambda:theme.user_word(w,ms['view'])==1,'鼠标Volumes页面')
        submitted=theme.user_word(w,ms['ui_frames'])
        wait(lambda:theme.user_word(w,ms['ui_frames'])>submitted and t.word(q.symbols()['dirty'])==0,'卷页面实际提交显存')
        q.shot('02-native-monitor-volumes')
        t.point(w['x']+1+270,w['y']+32+82);t.click()
        wait(lambda:theme.user_word(w,ms['frozen'])==1,'鼠标冻结')
        frozen=theme.user_word(w,ms['samples']);time.sleep(1.5)
        assert theme.user_word(w,ms['samples'])==frozen
        t.click();wait(lambda:theme.user_word(w,ms['frozen'])==0,'鼠标恢复')
        q.key('esc');idle()
        wait(lambda:t.word(q.symbols()['pf_used'])==baseline[0]+t.word(q.symbols()['desktop_pages'])-baseline[1],'Monitor窗口/8MB私有画布回收')
        checks.append('已验收G2实际编译新版Monitor/系统探针；真实CPU历史、鼠标卷页面、Freeze/Resume和完整退出回收')
        data=fill(proc,disk,address,'new80');checks.append('真实空闲/计算负载采样、非法整块映射不写、旧48字输出护栏、旧输入忽略寄存器/消费与核心保护通过')
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    clean=cold_cleanup(disk,'system-new',address,'new80')
    checks.append('实际MKDIR填满512项/超量拒绝；同盘冷启动读回，超过192子项完整列出，整组rename与临时页严格回收')
    def old_prepare(old):
        prepare(old);compiler.disk_put(old,'HOME/SYS.SCX',probe);old32(old)
    proc=t.launch('std',128,'system-old32',old_prepare);old_disk=OUT/'sanddata-std-128-system-old32.img'
    try:
        t.open_shell(True);old_data=fill(proc,old_disk,address,'old32')
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    old_clean=cold_cleanup(old_disk,'system-old32',address,'old32')
    checks.append('历史v4/32扇区/192项盘按原位置读写，满盘/拒绝/同盘重启/list/rename/回收同样通过')
    assert inputs=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs},'验收期间构建输入发生变化'
    report=dict(author='mio',status='PASS',checks=checks,new80=data,new80_cold=clean,old32=old_data,old32_cold=old_clean,
                inputs_sha256=inputs,
                sha256={name:hashlib.sha256((OUT/name).read_bytes()).hexdigest() for name in ('sysprobe-native.scx','monitor-native.scx')},
                limitation='std/128MB/1024/100%；所有缩放/VGA、Settings全配置与完整M8仍待验收')
    (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
