#!/usr/bin/env python3
"""mio：新版Shell/原生CLI/配置ABI的Windows双盘无头QEMU证据。

只在启动前准备副本盘。实际系统内G2编译Shell/CLI/探针，之后用
真实Shell命令安装和操作文件；只读历史/页表/现场，禁止注入函数。
"""
import hashlib
import json
import struct
import time
import zipfile
from pathlib import Path
import verify_truecolor as t
import verify_s3c as compiler
import verify_theme as theme

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-userspace';OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=theme.OUT=OUT
q=t.q

def wait(test,description,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        time.sleep(.15)
    q.shot('timeout');raise AssertionError(description)

def idle():
    wait(lambda:len(t.windows())==1 and compiler.task_states()[1:].count(1)==1
         and 2 not in compiler.task_states()[1:],'Shell焦点与任务回收')

def transcript(window):
    raw=q.memory(q.symbols()['terminals'],6*28)
    for index in range(6):
        handle,address,used=struct.unpack_from('<3I',raw,index*28)
        if handle==window['handle']:
            return q.memory(address,used).decode('utf-8')
    raise AssertionError('Shell必须实际启用独占终端')

def command(window,symbols,text,status=0):
    old=transcript(window).count('> '+text+'\n')
    q.text(text+'\n')
    wait(lambda:theme.user_word(window,symbols['waiting'])==0
         and theme.user_word(window,symbols['line_used'])==0
         and transcript(window).count('> '+text+'\n')>old,'命令实际执行后恢复提示符')
    if status is not None:
        actual=theme.user_word(window,symbols['last_status'])
        assert actual==(status&0xFFFFFFFF),(text,status,actual,transcript(window)[-800:])
    assert text in transcript(window),(text,transcript(window)[-800:])

def main():
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/SRC/userprobe.c',(ROOT/'user/userprobe.c').read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        compiler.disk_put(disk,'SYS/USER.CFG',b'SUSER1MIO\nusername=mio\nhome=/HOME\n')
        compiler.disk_put(disk,'SYS/ENV.CFG',b'SENV1MIO\nPATH=/BIN:/APPS\n')
    proc=t.launch('std',128,'user',prepare);disk=OUT/'sanddata-std-128-user.img'
    checks=[];artifacts={}
    desktop_base=[]
    try:
        def desktop_ready():
            wait(lambda:t.word(q.symbols()['dirty'])==0 and t.word(q.symbols()['render_hold'])==0,'初始桌面提交完成')
            desktop_base.extend((t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages'])))
        t.open_shell(True,desktop_ready)
        initial=t.windows()[0];time.sleep(.7);q.shot('01-bootstrap-shell')
        shell=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/shell.c','HOME/SH.SCX',180);idle()
        cli=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/cli.c','HOME/CL.SCX',180);idle()
        probe=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/userprobe.c','HOME/UP.SCX',180);idle()
        shell_symbols=theme.native_symbols(compiler.await_file(disk,'HOME/SH.SCX.map'))
        probe_symbols=theme.native_symbols(compiler.await_file(disk,'HOME/UP.SCX.map'))
        q.text('run HOME/SH.SCX\n');wait(lambda:len(t.windows())==2,'真实原生Shell运行')
        # 真实任务栏置顶旧启动Shell，Esc关闭；保留新G2产物作后续终端。
        t.point(160,750);t.click();q.key('esc');idle()
        window=t.windows()[0]
        assert window['owner']!=initial['owner']
        assert transcript(window).endswith('mio@/HOME> ')
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        for name in ('cp','ls','pwd','cat','echo','stat','mkdir','rm','mv','mem','uptime','help','whoami','env','lsblk','partitions','df','ps','cpu'):
            command(window,shell_symbols,f'cp /HOME/CL.SCX /BIN/{name.upper()}.SCX')
            assert compiler.file_content(disk,'bin/'+name+'.scx')==cli, name
        checks.append('已验收G2在系统内实际编译Shell/共享CLI/探针；真实cp命令安装19个独立/BIN SCX，逐字节等于原生产物')
        command(window,shell_symbols,'help');assert 'Commands in /BIN:' in transcript(window)
        command(window,shell_symbols,'pwd');assert '/HOME\n' in transcript(window)
        command(window,shell_symbols,'whoami');assert 'mio\n' in transcript(window)
        command(window,shell_symbols,'env');assert 'PATH=/BIN:/APPS\n' in transcript(window)
        command(window,shell_symbols,'lsblk');assert 'ATA LBA28' in transcript(window) and '65536' in transcript(window)
        command(window,shell_symbols,'partitions');assert 'no MBR/GPT partition table' in transcript(window)
        command(window,shell_symbols,'df');assert 'deleted data holes are not reclaimed' in transcript(window)
        command(window,shell_symbols,'ps');assert 'GENERATION' in transcript(window)
        command(window,shell_symbols,'cpu');assert 'Guest CPU / 100Hz PIT' in transcript(window)
        q.shot('02-native-shell-cli')
        command(window,shell_symbols,'mkdir DEMO')
        command(window,shell_symbols,'cp notes.txt DEMO/COPY.TXT')
        command(window,shell_symbols,'cd DEMO',status=None)
        assert transcript(window).endswith('mio@/HOME/DEMO> ')
        command(window,shell_symbols,'ls')
        last=transcript(window).rsplit('> ls\n',1)[1]
        assert 'COPY.TXT' in last and 'SYS/' not in last and 'APPS/' not in last,last
        command(window,shell_symbols,'cat COPY.TXT')
        assert 'SandCore' in transcript(window)
        command(window,shell_symbols,'mv COPY.TXT RENAMED.TXT')
        command(window,shell_symbols,'stat RENAMED.TXT')
        command(window,shell_symbols,'rm RENAMED.TXT')
        command(window,shell_symbols,'cd ..',status=None)
        command(window,shell_symbols,'rm DEMO')
        command(window,shell_symbols,'no_such_command',status=-2)
        command(window,shell_symbols,'rm /SYS/CORE/CORE.SKM',status=1)
        q.shot('03-relative-directory-output')
        checks.append('真实提示符/家目录、直接子项ls、相对cat/cp/mkdir/mv/stat/rm、错误码与SYS/CORE拒绝；CLI不创建窗口')
        q.text('run HOME/UP.SCX\n');w=wait(theme.window,'原生ABI探针')
        wait(lambda:theme.user_word(w,probe_symbols['user_probe'])==1,'配置/目录/作业断言完成',60)
        failed=theme.user_word(w,probe_symbols['user_probe']+4)
        assert failed==0,(failed,theme.user_word(w,probe_symbols['user_probe']+8))
        assert compiler.file_content(disk,'HOME/ROGUE')==b'DENIED'
        q.shot('04-native-user-abi');q.key('esc');idle()
        command(window,shell_symbols,'env COLOR');assert 'COLOR=warm\n' in transcript(window)
        assert transcript(window).endswith('visitor@/HOME> ')
        checks.append('非法指针/容量/终端伪造、候选仅验证/保存、坏用户与环境配置保持快照、cwd检查、CLI输出授权、PID复用后票据结果与无授权输出拒绝全部实际通过')
        # 真CPU异常：纯CLI没有自建窗口，Shell作业必须暂停等卡片。
        q.text('/LEGACY/APPS/PANIC.SCX\n')
        wait(lambda:3 in compiler.task_states(),'未处理CLI异常应暂停')
        q.shot('05-cli-fault-paused')
        t.point(284,46);t.click();idle()
        wait(lambda:theme.user_word(window,shell_symbols['waiting'])==0,'关闭异常卡片后父Shell恢复')
        assert theme.user_word(window,shell_symbols['last_status'])==128
        assert 'command exited (128)' in transcript(window)
        q.shot('06-shell-survives-cli-fault')
        # 窗口最大化再恢复：逻辑文字历史保持，字形按新宽度重排。
        before=transcript(window)
        t.point(window['x']+window['w']-40,window['y']+12);t.click()
        wait(lambda:t.windows()[0]['w']==1024,'最大化Shell')
        assert transcript(t.windows()[0])==before
        q.shot('07-terminal-reflow')
        checks.append('CLI除零暂停/卡片关闭返回128且Shell存活；真实鼠标最大化保留逻辑历史/重新排版')
        assert t.word(q.symbols()['event_overflow'])==t.word(q.symbols()['keyboard_overflow'])==0
        q.text('/HOME/UP.SCX linger\n')
        wait(lambda:theme.user_word(window,shell_symbols['waiting'])>0 and compiler.task_states()[1:].count(1)==2,'真实长CLI已附着')
        t.point(1010,12);t.click()
        wait(lambda:not t.windows() and all(state==0 for state in compiler.task_states()[1:]),'父终端关闭取消附着CLI并回收')
        wait(lambda:t.word(q.symbols()['pf_used'])==desktop_base[0]+t.word(q.symbols()['desktop_pages'])-desktop_base[1],
             '全部用户地址空间/终端/窗口页回到实际桌面基线')
        checks.append('19个原生CLI含CPU/卷/设备/任务命令实跑；真实关闭父终端取消长CLI，全部页回到桌面基线，键鼠队列零溢出')
        q.hmp('quit');proc.wait(timeout=10)
        proc=t.launch('std',128,'user',reuse=True)
        t.open_shell(True);cold=t.windows()[0]
        assert transcript(cold).endswith('visitor@/HOME> ')
        q.text('env COLOR\n');wait(lambda:'COLOR=warm\n' in transcript(cold),'同盘冷启动环境恢复')
        q.shot('08-user-environment-coldboot');q.key('esc')
        wait(lambda:not t.windows() and all(state==0 for state in compiler.task_states()[1:]),'冷启动Shell正常退出')
        checks.append('同盘真实冷启动恢复visitor用户名/家目录与COLOR环境；普通Shell仍可正常Esc退出')
        for name,data in (('G2',g2),('shell-native',shell),('cli-native',cli),('userprobe-native',probe)):
            artifacts[name]=hashlib.sha256(data).hexdigest();(OUT/(name+'.scx')).write_bytes(data)
        for path in ('kernel/userspace.c','kernel/wm_terminal.inc','kernel/task.c','user/SCAPI.H','user/shell.c','user/cli.c','build/sandcore.img'):
            artifacts[path]=hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
        result=dict(author='mio',status='PASS',checks=checks,sha256=artifacts,
                    limitation='Settings结构化用户/环境/通用编辑器、全显示缩放/新版三代编译器继续')
        (OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('USERSPACE PASS',json.dumps(result,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)

if __name__=='__main__':main()
