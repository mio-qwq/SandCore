#!/usr/bin/env python3
"""mio：G2 系统内生成 M7 全部应用，HMP/QMP 驱动实际图形功能。

读取原生 SCCC .map 的 OBJECT 地址，再沿目标真实页表检查变量。
这种只读诊断不使用宿主 ELF 的地址猜测，也不改写游戏/GUI 状态。
构建与运行始终在 Windows 双盘 QEMU，磁盘副本不污染用户工作盘。
"""
import hashlib,json,shutil,subprocess,time,socket,struct
from pathlib import Path
import verify_m6 as v
import verify_s3c as c
from verify_m7_base import file_content,task_states
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m7-graphics'; OUT.mkdir(parents=True,exist_ok=True)
v.OUT=c.OUT=OUT
maps={}

def wait(predicate,message,timeout=25):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if predicate(): return
        if 3 in task_states(): v.shot('unexpected-fault'); raise AssertionError(message+' / exception')
        time.sleep(.2)
    v.shot('timeout'); raise AssertionError(message)

def mapped(pid,address,n):
    raw=v.memory(v.symbols()['tasks']+pid*168,4); pd=struct.unpack('<I',raw)[0]
    result=bytearray()
    while n:
        pde=struct.unpack('<I',v.memory(pd+(address>>22)*4,4))[0]; assert pde&1
        pte=struct.unpack('<I',v.memory((pde&0xfffff000)+((address>>12)&1023)*4,4))[0]; assert pte&1
        take=min(n,4096-(address&4095))
        result+=v.memory((pte&0xfffff000)+(address&4095),take)
        address+=take; n-=take
    return bytes(result)

def value(app,pid,name,n=4):
    data=mapped(pid,maps[app][name],n)
    return struct.unpack('<i',data)[0] if n==4 else data

def focus(window):
    # 同一 stable handle 的任务按钮按 handle 顺序排列，不猜当前 z 序。
    ordered=sorted(v.windows(),key=lambda item:item['handle'])
    index=next(i for i,w in enumerate(ordered) if w['handle']==window['handle'])
    v.point(54+index*30,192); v.click()
    wait(lambda:v.windows()[-1]['handle']==window['handle'],'taskbar focus')

def launch(app,args=''):
    v.text('run apps/'+app+'.scx'+(' '+args if args else '')+'\n')
    wait(lambda:len(v.windows())>=2 and v.windows()[-1]['owner']!=1,'window '+app)
    time.sleep(1)
    return v.windows()[-1]

def close():
    v.key('esc'); wait(lambda:len(v.windows())==1,'return Shell'); time.sleep(.3)

def run():
    try:s=socket.create_connection(('127.0.0.1',4444),.3)
    except OSError:pass
    else:s.close();raise RuntimeError('4444 occupied')
    disk=OUT/'sanddata-test.img'
    # 未改变的原生机器码可复用；同时核对它生成时盘上的源码、API、
    # UI 和 G2 本体，避免每修一个输入断言就重编所有相同应用。
    cache={}
    if disk.exists():
        same_compiler=file_content(disk,'HOME/S3C2.SCX')==file_content(ROOT/'build/m7-compiler/sanddata-test.img','HOME/S3C2.SCX')
        same_api=file_content(disk,'SYS/INC/SCAPI.H')==(ROOT/'user/SCAPI.H').read_bytes()
        same_ui=file_content(disk,'SYS/SRC/UI.inc')==(ROOT/'user/UI.inc').read_bytes()
        for app in ['files','ide','debugger','lumen','race','world','probe']:
            log=file_content(disk,'apps/'+app+'.scx.log')
            if same_compiler and same_api and (same_ui or app=='probe') and log and log.startswith(b'SCCC OK') and file_content(disk,'SYS/SRC/'+app+'.c')==(ROOT/'user'/str(app+'.c')).read_bytes():
                cache[app]=(file_content(disk,'apps/'+app+'.scx'),file_content(disk,'apps/'+app+'.scx.map'))
    shutil.copy2(ROOT/'build/m7-compiler/sanddata-test.img',disk)
    for app,(blob,symbol_map) in cache.items():
        c.disk_put(disk,'apps/'+app+'.scx',blob)
        c.disk_put(disk,'apps/'+app+'.scx.map',symbol_map)
        c.disk_put(disk,'apps/'+app+'.scx.log',b'SCCC OK verified native cache')
    for name in ['UI.inc','files.c','ide.c','debugger.c','lumen.c','race.c','world.c','probe.c']:
        c.disk_put(disk,'SYS/SRC/'+name,(ROOT/'user'/name).read_bytes())
    c.disk_put(disk,'SYS/INC/SCAPI.H',(ROOT/'user/SCAPI.H').read_bytes())
    c.disk_put(disk,'HOME/UI.C',b'#include "SCAPI.H"\nint main(void) { return 7; }\n')
    c.disk_put(disk,'HOME/FAIL.C',b'int main(void) { return undefined_name; }\n')
    cmd=[r'C:\Program Files\qemu\qemu-system-i386.exe','-drive','format=raw,if=floppy,file=build/sandcore.img',
         '-drive','format=raw,if=ide,file='+disk.as_posix(),'-display','none',
         '-monitor','tcp:127.0.0.1:4444,server,nowait','-qmp','tcp:127.0.0.1:4445,server,nowait','-no-reboot']
    (OUT/'qemu-command.json').write_text(json.dumps(cmd,indent=2),encoding='utf-8')
    proc=subprocess.Popen(cmd,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    results={}
    try:
        time.sleep(5);v.key('ret');time.sleep(1);v.icon('bin/shell.scx');v.click()
        baseline=struct.unpack('<I',v.memory(v.symbols()['pf_used'],4))[0]
        for app in ['files','ide','debugger','lumen','race','world','probe']:
            blob=cache[app][0] if app in cache else c.compile_native(disk,'HOME/S3C2.SCX','SYS/SRC/'+app+'.c','apps/'+app+'.scx',180)
            (OUT/(app+'.scx')).write_bytes(blob)
            raw=file_content(disk,'apps/'+app+'.scx.map')
            maps[app]={}
            for line in raw.decode().splitlines()[1:]:
                parts=line.split()
                if len(parts)>=3 and parts[2]=='OBJECT': maps[app][parts[1]]=int(parts[0],16)
            results[app+'_native_sha256']=hashlib.sha256(blob).hexdigest()
        (OUT/'native-provenance.json').write_text(json.dumps({app:dict(source_sha256=hashlib.sha256((ROOT/'user'/str(app+'.c')).read_bytes()).hexdigest(),scx_sha256=results[app+'_native_sha256'],compiler_G2_sha256=hashlib.sha256(file_content(disk,'HOME/S3C2.SCX')).hexdigest(),reused_identical_verified_native=app in cache) for app in maps},indent=2),encoding='utf-8')
        # 原生编译的汉字探针，不能只验证宿主 GCC 启动版本。
        win=launch('probe','font');wait(lambda:file_content(disk,'home/font.ok')==b'PASS','native Chinese')
        v.shot('01-native-unicode');close();results['native_font_contract']='PASS'
        # 六段真实短片、连续帧、暂停及重播。
        win=launch('lumen');pid=win['owner']
        for chapter in range(6):
            # 第一幕标题按时间分行揭示，等待整句出现再取构图证据。
            # 其余章节 .8s 已跨过转场，既可看清主角也不会跳到下一幕。
            v.key(str(chapter+1));time.sleep(3.7 if chapter==0 else .8);v.shot(f'film-{chapter+1}')
        v.key('2');time.sleep(.8)
        before=v.windows()[-1]['digest'];time.sleep(.8);assert before!=v.windows()[-1]['digest']
        v.key('spc');time.sleep(.6);before=v.windows()[-1]['digest'];time.sleep(.8)
        assert before==v.windows()[-1]['digest'] and value('lumen',pid,'paused')==1
        v.shot('film-paused');v.key('r');wait(lambda:value('lumen',pid,'paused')==0,'replay');close()
        results['film_6_chapters_frames_pause_replay']='PASS'
        # 赛车真实连续输入：加速/里程、转向/刹车和重置。
        win=launch('race');pid=win['owner'];before=v.windows()[-1]['digest']
        v.hmp('sendkey up 2000');time.sleep(2.2)
        assert value('race',pid,'distance')>0 and value('race',pid,'speed')>0
        assert before!=v.windows()[-1]['digest'];v.shot('race-driving')
        # 转向单独在重置后的直线/零速状态测量，排除弯道自然侧移
        # 和正好卡在方向限幅处的干扰；等待实际输入被消费。
        v.key('r');wait(lambda:value('race',pid,'distance')==0,'reset before steering')
        v.hmp('sendkey right 1000');wait(lambda:value('race',pid,'steer')>0,'right steering input')
        time.sleep(1.2)
        v.hmp('sendkey down 900');time.sleep(1);v.shot('race-steer-brake')
        v.key('r');wait(lambda:value('race',pid,'distance')==0,'race restart');close()
        results['race_input_frames_restart']='PASS'
        # 沙盒读取的是程序自己的体素字节；输入必须实际破坏/放置。
        win=launch('world');pid=win['owner'];time.sleep(2)
        wait(lambda:value('world',pid,'target')>=0,'reachable voxel')
        initial=value('world',pid,'world',6912);v.shot('world-before')
        v.key('q');wait(lambda:value('world',pid,'world',6912)!=initial,'break voxel')
        broken=value('world',pid,'world',6912)
        # 塔只有一层厚，拆掉中心块后穿洞看到远处；重新瞄准邻块，
        # 不能把无支撑/超出六格距离的放置当成功。
        v.key('down');v.key('down');v.key('down');time.sleep(1)
        wait(lambda:value('world',pid,'target')>=0,'aim neighbouring voxel')
        v.key('2');time.sleep(.4);v.key('e')
        wait(lambda:value('world',pid,'world',6912)!=broken,'place voxel');v.shot('world-build')
        v.key('f2');wait(lambda:file_content(disk,'HOME/WORLD/CHUNK.SCW') is not None,'world save',35)
        saved=file_content(disk,'HOME/WORLD/CHUNK.SCW');assert len(saved)==6944 and saved[:8]==b'SBOX1MIO'
        v.shot('world-saved');close()
        win=launch('world');pid=win['owner']
        assert value('world',pid,'world',6912)==saved[16:6928],'disk world restores after new process'
        v.shot('world-restored');close();results['world_break_place_disk_restore']='PASS'
        # 文件管理器创建、导航、重命名及删除空目录。
        win=launch('files','HOME');pid=win['owner'];v.key('n');v.text('GUI-DIR\n')
        wait(lambda:value('files',pid,'count')>0 and value('files',pid,'status',18).startswith(b'Directory created'),'mkdir GUI')
        wait(lambda:file_content(disk,'HOME/GUI-DIR')==b'','directory persisted')
        count=value('files',pid,'count');names=value('files',pid,'names',192*64)
        index=next(i for i in range(count) if names[i*64:(i+1)*64].split(b'\0')[0]==b'GUI-DIR')
        for _ in range(index):v.key('down')
        v.shot('files-created');v.key('ret');wait(lambda:value('files',pid,'cwd',64).split(b'\0')[0]==b'HOME/GUI-DIR','enter directory')
        v.shot('files-subdirectory');v.key('backspace')
        count=value('files',pid,'count');names=value('files',pid,'names',192*64)
        index=next(i for i in range(count) if names[i*64:(i+1)*64].split(b'\0')[0]==b'GUI-DIR')
        for _ in range(index):v.key('down')
        v.key('f2')
        # 路径对话框初始为完整旧名，仅替换末尾 DIR 为 REN。
        v.text('\b\b\bREN\n');wait(lambda:file_content(disk,'HOME/GUI-REN')==b'','GUI rename')
        v.shot('files-renamed');v.key('x');v.key('y');wait(lambda:file_content(disk,'HOME/GUI-REN') is None,'GUI delete')
        v.shot('files-deleted');close();results['files_mkdir_navigation_rename_delete']='PASS'
        # IDE 任意位置插入/退格原样恢复、保存、内部编译、错误诊断和 ASM。
        win=launch('ide','HOME/UI.C');pid=win['owner'];original=file_content(disk,'HOME/UI.C')
        v.key('right');v.text('Z');v.key('backspace');v.key('f2')
        wait(lambda:file_content(disk,'HOME/UI.C')==original,'IDE source save');v.shot('ide-editor')
        v.key('f5');wait(lambda:value('ide',pid,'compiled')==1,'IDE C build',70)
        assert file_content(disk,'HOME/UI.SCX')[:8]==b'SCX1MIO\0';v.shot('ide-build-c')
        v.key('f6');time.sleep(1);assert 3 not in task_states();v.shot('ide-run-c');close()
        win=launch('ide','HOME/FAIL.C');pid=win['owner'];v.key('f5')
        wait(lambda:value('ide',pid,'diagnostic_mode')==1,'IDE error diagnostics',70)
        assert value('ide',pid,'compiled')==0 and file_content(disk,'HOME/FAIL.SCX')==b'UNCHANGED'
        v.shot('ide-error');close()
        win=launch('ide','home/demo.asm');pid=win['owner'];v.key('f5')
        wait(lambda:value('ide',pid,'compiled')==1,'IDE ASM build',70);v.shot('ide-build-asm');close()
        results['ide_edit_save_c_compile_run_error_asm']='PASS'
        # 调试器 GUI 真实断点/TF/内存/继续/暂停，目标是独立私有地址空间。
        win=launch('debugger','apps/probe.scx target');pid=win['owner']
        target=value('debugger',pid,'target_pid');initial_eip=struct.unpack_from('<I',value('debugger',pid,'context',88),56)[0]
        v.shot('debug-before-first');v.key('b');v.key('ret');v.key('f5')
        wait(lambda:task_states()[target]==3,'GUI breakpoint')
        wait(lambda:struct.unpack_from('<I',value('debugger',pid,'context',88),48)[0]==3,'INT3 context')
        v.shot('debug-breakpoint');v.key('f8')
        wait(lambda:struct.unpack_from('<I',value('debugger',pid,'context',88),56)[0]!=initial_eip,'TF changes EIP')
        v.shot('debug-step');v.key('m');v.key('ret');v.shot('debug-memory');v.key('u');v.key('f5')
        wait(lambda:len(v.windows())==3,'debug target window');focus(win);v.key('f4')
        wait(lambda:task_states()[target]==3,'GUI pause running target');v.shot('debug-paused')
        close();assert task_states()[target] not in [1,3];results['debugger_break_step_memory_continue_pause_cleanup']='PASS'
        assert struct.unpack('<I',v.memory(v.symbols()['pf_used'],4))[0]==baseline,'all native applications reclaim pages'
        (OUT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS native graphics and GUI matrix',flush=True)
    finally:
        if proc.poll() is None:v.hmp('quit');proc.wait(timeout=5)
if __name__=='__main__':run()
