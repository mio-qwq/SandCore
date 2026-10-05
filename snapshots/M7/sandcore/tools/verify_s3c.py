#!/usr/bin/env python3
"""mio：SCCC 真实三环编译、生成程序运行与两代自编译验收。

每次创建数据盘副本；源码以 SandFS 文件读入，输出从运行后的磁盘
重新读取核对。生成一代编译器后实际 EXEC 它，再由它生成第二代。
这比“源码包含自己”或宿主编译器出包多一步真正的闭环证据。
"""
import hashlib,json,socket,shutil,struct,subprocess,time
from pathlib import Path
import verify_m6 as v
from verify_m7_base import file_content,task_states
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m7-compiler'
OUT.mkdir(parents=True,exist_ok=True)
v.OUT=OUT
commands=[]

def disk_put(disk,path,blob):
    raw=bytearray(disk.read_bytes()); count=struct.unpack_from('<I',raw,12)[0]
    assert struct.unpack_from('<I',raw,20)[0]==4 and count<192
    end=33; entry=count
    for i in range(count):
        name,start,size=struct.unpack_from('<64sII',raw,512+i*72)
        if name.split(b'\0')[0].decode().upper()==path.upper(): entry=i
        end=max(end,start+(size+511)//512)
    assert end*512+len(blob)<=len(raw)
    raw[end*512:end*512+len(blob)]=blob
    struct.pack_into('<64sII',raw,512+entry*72,path.encode(),end,len(blob))
    if entry==count: struct.pack_into('<I',raw,12,count+1)
    disk.write_bytes(raw)

def await_file(disk,path,timeout=90,expected=None):
    deadline=time.monotonic()+timeout; shot_at=time.monotonic()+4
    while time.monotonic()<deadline:
        blob=file_content(disk,path)
        if blob is not None and (expected is None or blob==expected): return blob
        if time.monotonic()>=shot_at:
            v.shot('progress-'+path.replace('/','-').replace('.','-'))
            shot_at=time.monotonic()+30
        if 3 in task_states():
            v.shot('fault-'+path.replace('/','-')); raise AssertionError('compiler/target fault: '+path)
        time.sleep(.25)
    v.shot('timeout'); raise AssertionError('timeout: '+path)

def compile_native(disk,compiler,source,output,timeout=90):
    # Shell 最多 63 字节；提交前核对并拍完整输入，不能凭末尾日志推测。
    command='run '+compiler+' '+source+' '+output
    assert len(command)<=63, 'command exceeds Shell line capacity'
    assert len(v.windows())==1 and task_states()[1:].count(1)==1, 'compiler still owns focus'
    v.text('clear\n'); v.text(command)
    v.shot('command-'+output.replace('/','-').replace('.','-'))
    commands.append(command)
    (OUT/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
    v.key('ret')
    log=await_file(disk,output+'.log',timeout)
    assert log.startswith(b'SCCC OK'),log.decode(errors='replace')
    blob=await_file(disk,output,timeout)
    assert blob[:8]==b'SCX1MIO\0'
    # 日志先于符号表写入/退出；先等待编译任务真正关闭，下一条 HMP
    # 命令才不会被仍聚焦的编译窗口吞掉前几个字母。
    deadline=time.monotonic()+30
    while len(v.windows())!=1 and time.monotonic()<deadline: time.sleep(.2)
    assert len(v.windows())==1, 'compiler must exit after writing symbols'
    time.sleep(.3)
    return blob

def run():
    try: s=socket.create_connection(('127.0.0.1',4444),.3)
    except OSError: pass
    else: s.close(); raise RuntimeError('4444 occupied; leave existing QEMU alone')
    # 旧轮次吞键失败的截图留在历史目录并注明原因，不混入本轮证据。
    for image in OUT.glob('progress-*.png'):
        history=OUT/'history'; history.mkdir(exist_ok=True)
        shutil.move(str(image),str(history/image.name))
    disk=OUT/'sanddata-test.img'; shutil.copy2(ROOT/'build/sanddata.img',disk)
    bad_sources=['#error intended\n', 'float value; int main(void) { return 0; }\n',
                 'int main(void) { return unknown_symbol; }\n', '#define ONE(x) x\nint main(void) { return ONE(1,2); }\n']
    for i,source in enumerate(bad_sources): disk_put(disk,f'HOME/BAD{i}.C',source.encode())
    disk_put(disk,'HOME/FAIL.SCX',b'UNCHANGED')
    disk_put(disk,'HOME/FEATURE.C',(ROOT/'assets/home/FEATURE.C').read_bytes())
    cmd=[r'C:\Program Files\qemu\qemu-system-i386.exe',
         '-drive','format=raw,if=floppy,file=build/sandcore.img',
         '-drive','format=raw,if=ide,file='+disk.as_posix(),'-display','none',
         '-monitor','tcp:127.0.0.1:4444,server,nowait','-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    (OUT/'qemu-command.json').write_text(json.dumps(cmd,indent=2),encoding='utf-8')
    proc=subprocess.Popen(cmd,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    results={}
    try:
        time.sleep(5); assert proc.poll() is None
        v.shot('01-compiler-boot'); v.key('ret'); time.sleep(1); v.icon('bin/shell.scx'); v.click()
        feature=compile_native(disk,'bin/s3c.scx','HOME/FEATURE.C','HOME/TEST.SCX')
        (OUT/'feature.scx').write_bytes(feature)
        v.shot('02-feature-compiled')
        v.text('run HOME/TEST.SCX\n')
        proof=await_file(disk,'HOME/C-RESULT',expected=b'PASS')
        v.shot('03-feature-executed'); assert proof==b'PASS',proof
        v.key('esc'); time.sleep(.3)
        results['C_semantics_18_groups']='PASS'
        for i in range(len(bad_sources)):
            command=f'run bin/s3c.scx HOME/BAD{i}.C HOME/FAIL.SCX'
            v.text('clear\n'); v.text(command); v.shot(f'negative-command-{i}'); v.key('ret')
            time.sleep(1)
            deadline=time.monotonic()+30
            while len(v.windows())!=1 and time.monotonic()<deadline: time.sleep(.2)
            assert len(v.windows())==1 and 3 not in task_states()
            log=file_content(disk,'HOME/FAIL.SCX.log')
            assert log and not log.startswith(b'SCCC OK'),log
            assert file_content(disk,'HOME/FAIL.SCX')==b'UNCHANGED','failure overwrote previous file'
            v.shot(f'negative-diagnostic-{i}')
        results['negative_diagnostics_preserve_output_4']='PASS'

        gen1=compile_native(disk,'bin/s3c.scx','SYS/SRC/s3c.c','HOME/S3C1.SCX',180)
        (OUT/'s3c-generation1.scx').write_bytes(gen1); v.shot('04-self-generation1')
        gen2=compile_native(disk,'HOME/S3C1.SCX','SYS/SRC/s3c.c','HOME/S3C2.SCX',180)
        (OUT/'s3c-generation2.scx').write_bytes(gen2); v.shot('05-self-generation2')
        assert gen1==gen2,'two native generations must converge byte-for-byte'
        gen3=compile_native(disk,'HOME/S3C2.SCX','SYS/SRC/s3c.c','HOME/S3C3.SCX',180)
        (OUT/'s3c-generation3.scx').write_bytes(gen3); v.shot('06-self-generation3')
        assert gen2==gen3,'second native generation must actually generate identical third generation'
        again=compile_native(disk,'HOME/S3C2.SCX','HOME/FEATURE.C','HOME/TEST2.SCX')
        assert again==feature,'self-compiled compiler must reproduce feature machine code'
        v.text('run HOME/TEST2.SCX HOME/C-RESULT2\n'); await_file(disk,'HOME/C-RESULT2',expected=b'PASS'); v.shot('07-self-compiler-output-runs')
        assert 3 not in task_states()
        results.update(self_generation1_sha256=hashlib.sha256(gen1).hexdigest(),
                       self_generation2_equal=True,self_generation3_sha256=hashlib.sha256(gen3).hexdigest(),
                       self_generation3_equal=True,second_generation_compiles_feature_equal=True,
                       feature_sha256=hashlib.sha256(feature).hexdigest())
        (OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print('PASS',results,flush=True)
    finally:
        if proc.poll() is None: v.hmp('quit'); proc.wait(timeout=5)
if __name__=='__main__': run()
