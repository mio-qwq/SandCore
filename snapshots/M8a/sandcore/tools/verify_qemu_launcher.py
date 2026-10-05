#!/usr/bin/env python3
"""mio：实际执行两份batch，无头双盘验证auto/whpx/tcg真实启动。

运行文件先冻结到私有目录，开发盘和用户试玩盘从不挂入VM。
不是用Python重写启动参数替代batch：cmd实际执行冻结run.bat，
HMP/QMP正常输入、完整欢迎/Shell截图和真实进程命令分别留证。
"""
import argparse
import json
import shutil
import socket
import subprocess
import time
import traceback
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='ACTUAL_BATCH_HEADLESS_DUAL_DISK',cases={})
    for label,source in [('development',ROOT/'run.bat'),('private-copy',ROOT.parent/'temp miotest/run.bat')]:
        batch=source.read_bytes();assert all(b<128 for b in batch),'batch must be ASCII'
        for mode in ('auto','tcg','whpx'):
            for port in (4444,4445):
                try:connection=socket.create_connection(('127.0.0.1',port),.2)
                except OSError:continue
                connection.close();raise RuntimeError(f'{port} occupied; leave existing VM alone')
            target=out/(label+'-'+mode);target.mkdir();phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=target
            destination=target/'build' if label=='development' else target;destination.mkdir(exist_ok=True)
            shutil.copy2(ROOT/'build/sandcore.img',destination/'sandcore.img')
            shutil.copy2(ROOT/'build/sanddata.img',destination/'sanddata.img')
            disk=destination/'sanddata.img';phase.DISK=disk
            c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
            c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
            script=target/'run.bat';script.write_bytes(batch)
            command=['cmd.exe','/d','/c',str(script),mode,'headless']
            item=dict(batch_sha256=phase.sha(batch),command=command,checks=[])
            with (target/'launcher.log').open('wb') as output:
                proc=subprocess.Popen(command,cwd=target,stdout=output,stderr=output,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                # cmd先做能力探测再启动QEMU；不能立刻向尚未监听
                # 的HMP发pmemsave，把正常的启动间隙误报为客体失败。
                deadline=time.monotonic()+30
                while True:
                    if proc.poll() is not None:raise RuntimeError(('batch exited before monitors',proc.returncode))
                    try:
                        for port in (4444,4445):
                            with socket.create_connection(('127.0.0.1',port),.2):pass
                        break
                    except OSError:
                        if time.monotonic()>=deadline:raise TimeoutError('batch monitor readiness')
                        time.sleep(.05)
                phase.wait(lambda:t.word(kernel['boot_stage'])==2,'batch-welcome',90);q.shot('01-welcome')
                t.open_shell(True);phase.idle();q.shot('02-shell')
                # 此观察只读本次cmd的直接子进程，证明真正运行哪个
                # QEMU与加速参数，不把探测成功当作客体已启动。
                query=f"Get-CimInstance Win32_Process -Filter 'ParentProcessId={proc.pid}' | Select-Object ProcessId,Name,CommandLine | ConvertTo-Json -Compress"
                inspected=subprocess.check_output(['powershell.exe','-NoProfile','-Command',query],text=True).strip()
                rows=json.loads(inspected);rows=rows if isinstance(rows,list) else [rows]
                processes=[row for row in rows if str(row['Name']).startswith('qemu-system-')]
                assert len(processes)==1,rows
                actual=processes[0];text=actual['CommandLine']
                assert 'if=floppy' in text and 'if=ide' in text and 'sandcore.img' in text and 'sanddata.img' in text
                assert '-smp 1' in text and '-cpu qemu32' in text and '-display none' in text
                if mode=='whpx':assert '-accel whpx' in text and '-accel tcg' not in text
                if mode=='tcg':assert '-accel tcg' in text and '-accel whpx' not in text
                assert not phase.faults()
                item.update(process=actual,checks=['实际batch/双盘/单vCPU/无头','真实欢迎/开始菜单Shell键鼠','未出现三环异常'])
                q.hmp('quit');proc.wait(timeout=10);assert proc.returncode==0
                item.update(status='PASS',exit_code=proc.returncode)
                report['cases'][label+'-'+mode]=item
                (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            except Exception as error:
                item.update(status='FAIL',error=repr(error));report['cases'][label+'-'+mode]=item
                report.update(status='FAIL',traceback=traceback.format_exc())
                if proc.poll() is None:q.shot('failure')
                (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
            finally:
                if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    report.update(status='PASS',limitations='两份启动器六种实际启动/欢迎/鼠标Shell；非完整游戏或旧设备矩阵')
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('BATCH QEMU PASS',len(report['cases']),flush=True)


if __name__=='__main__':main()
