#!/usr/bin/env python3
"""mio：只在副本盘注入模块/坏字体，验证开机真实加载和同源兜底。"""
import json,shutil,socket,struct,subprocess,time
from pathlib import Path
import verify_m6 as v
from verify_s3c import disk_put
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m7-modules';OUT.mkdir(parents=True,exist_ok=True);v.OUT=OUT

def run():
    try:s=socket.create_connection(('127.0.0.1',4444),.3)
    except OSError:pass
    else:s.close();raise RuntimeError('4444 occupied')
    disk=OUT/'sanddata-test.img';shutil.copy2(ROOT/'build/sanddata.img',disk)
    # 三字节真实 x86 零环 cdecl 入口：xor eax,eax / ret。
    valid=b'SKM1MIO\0'+struct.pack('<6I',0,3,0,0,1,0x004f494d)+b'\x31\xc0\xc3'
    disk_put(disk,'SYS/MOD/VALID.SKM',valid)
    invalid=bytearray((ROOT/'build/fs/SYS/CORE/CORE.SKM').read_bytes())
    size=struct.unpack_from('<I',invalid,12)[0];first=struct.unpack_from('<I',invalid,32+size)[0]
    struct.pack_into('<I',invalid,36+size,first+1) # 重定位字节重叠必须拒绝。
    disk_put(disk,'SYS/MOD/OVERLAP.SKM',invalid)
    bad=bytearray(valid);struct.pack_into('<I',bad,24,99);disk_put(disk,'SYS/MOD/BADABI.SKM',bad)
    font=bytearray((ROOT/'build/fs/sys/font.scf').read_bytes());font[12+4]=ord('X')
    disk_put(disk,'SYS/FONT.SCF',font)
    command=[r'C:\Program Files\qemu\qemu-system-i386.exe','-drive','format=raw,if=floppy,file=build/sandcore.img',
      '-drive','format=raw,if=ide,file='+disk.as_posix(),'-display','none','-monitor','tcp:127.0.0.1:4444,server,nowait',
      '-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    (OUT/'qemu-command.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
    proc=subprocess.Popen(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(5);v.key('ret');time.sleep(1)
        sym=v.symbols()
        loaded=struct.unpack('<I',v.memory(sym['modules_loaded'],4))[0]
        failed=struct.unpack('<I',v.memory(sym['modules_failed'],4))[0]
        assert (loaded,failed)==(2,2),'CORE 与 VALID 必须真实执行，坏重定位/ABI 拒绝'
        callback=struct.unpack('<I',v.memory(sym['scene_callback'],4))[0]
        assert 0x1800000<=callback<0x1900000
        assert struct.unpack('<I',v.memory(sym['zh16_active_n'],4))[0]==0,'坏字体不能部分激活'
        v.shot('01-modules-and-font-fallback');v.icon('bin/shell.scx');v.click();v.text('echo alive\n')
        assert len(v.windows())==1;v.shot('02-invalid-modules-desktop-alive')
        results={'valid_CORE_and_user_MOD':loaded,'invalid_overlap_and_ABI':failed,'bad_SCF_same_source_fallback':True,'desktop_alive':True}
        (OUT/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print('PASS modules/fallback',flush=True)
    finally:
        if proc.poll() is None:v.hmp('quit');proc.wait(timeout=5)
if __name__=='__main__':run()
