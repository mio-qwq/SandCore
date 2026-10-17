"""保存实际PIO对照和GUI10终态；不改写原GUI09的RUNNING状态。"""
from pathlib import Path
import gzip
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'evidence/m10a2-pio-gui10'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_bytes())
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def main():
    OUT.mkdir(parents=True,exist_ok=False)
    files={}
    def preserve(source,name,compress=False):
        source=ROOT/source;target=OUT/name;target.parent.mkdir(parents=True,exist_ok=True)
        if compress:target.write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        else:shutil.copyfile(source,target)
        files[name]=dict(source=str(source.relative_to(ROOT)),source_sha256=sha(source),archive_sha256=sha(target),bytes=source.stat().st_size)
    old=read(ROOT/'build/m10a2-p6-gui-run-09/p6-gui.json')
    new=read(ROOT/'build/m10a2-p6-gui-run-10/p6-gui.json')
    assert old['status']=='RUNNING'
    assert new['status']=='P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
    assert new['current_inputs_unchanged'] and new['source_disk_unchanged'] and not new['changed_input_output_paths']
    final=next(c for c in new['cases'] if c['name']=='p6-gui-single-final-owner-account')
    assert final['status']=='PASS' and final['physical_delta']==0 and final['unexplained_delta']==0 and not final['unexpected']
    measured=[]
    for n,r in ((9,old),(10,new)):
        case=next(c for c in r['cases'] if c['name']==f'p6-cli-gui{n}-pio-read-measured')
        warm=next(c for c in r['cases'] if c['name']=='warm-existing-async-desktop-resources')
        assert case['status']=='PASS' and case['exit_code']==0 and '697678241 230424 /HOME/WELCOME.SCB' in case['output']
        assert warm['status']=='PASS' and warm['complete_existing_desktop_resources']
        base=f'build/m10a2-p6-gui-run-{n:02}'
        before=read(ROOT/base/'pio-measured-before.json');after=read(ROOT/base/'pio-measured-after.json')
        assert before['Pid']==after['Pid']==r['qemu_pid']
        measured.append(dict(gui=n,command_case=case,resource_wait_seconds=warm['wall_seconds'],cpu_before=before,cpu_after=after,cpu_observation_delta=after['CpuSeconds']-before['CpuSeconds']))
        preserve(f'{base}/p6-gui.json',f'gui{n:02}/p6-gui.json.gz',True)
        for name in ('pio-measured-before.json','pio-measured-after.json'):preserve(f'{base}/{name}',f'gui{n:02}/{name}')
        stdout=Path(case['stdout_path'])
        preserve(str(stdout.relative_to(ROOT)),f'gui{n:02}/read-command.stdout.bin')
    preserve('build/m10a2-main-pio-01/p6-unified-host-15.json','host15.json.gz',True)
    preserve('build/m10a2-p6-final-09/finalization.json','final09.json.gz',True)
    preserve('build/m10a2-p6-final-09/inspection-summary.json','inspection-summary.json')
    for source in ('kernel/ata.c','kernel/ata.h','drafts/m10a2-ata-pio/ata-before.c','drafts/m10a2-ata-pio/HOST15.json','drafts/m10a2-ata-pio/BASELINE.json','drafts/m10a2-ata-pio/COPY.json'):
        preserve(source,'source/'+source)
    names=('installer-default-layout','installer-detail','installer-exit-code','files-home-drop','files-home-exit-code','debug-bp-added','debug-bp-zero','debug-f5-second','debug-target-exit','debug-exit-code','settings-1024-preview','settings-mode-query-settled','settings-1920-confirmed','terminal-shell-exit','all-apps-closed')
    for name in names:preserve(f'build/m10a2-p6-gui-run-10/p6-gui10-{name}.png','screenshots/'+name+'.png')
    for name in ('installed-data.rdt','files2-stop.log','files2-cancel-attempt.log'):
        preserve('build/m10a2-p6-gui-run-10/'+name,'files/'+name)
    installed=OUT/'files/installed-data.rdt'
    assert installed.stat().st_size==73 and sha(installed)=='e01e31d9117841935f2a0cadbf022efccd4ec7ec30982e6807c1c32363e74ff4'
    ps=next(c for c in new['cases'] if c['name']=='p6-cli-gui10-normal-app-exit-process-list')
    assert ps['status']=='PASS' and not any(x in ps['output'] for x in ('TERMINAL2','DEBUG2','FILES2','INSTALLER2','SETTINGS2','CALC2'))
    save(OUT/'review.json',dict(status='DECLARED_PIO_AND_GUI10_FACTS_REVIEW',full_M10a2=False,
        original_gui09_status_preserved=old['status'],gui10_outer_status=new['status'],
        process_exit=dict(driver_exit_code=0,qemu_pid=10232,qemu_no_longer_running=True,source='actual exec64591 completion and Get-Process'),
        measured=measured,final_owner={k:final[k] for k in ('status','before_physical_pages','after_physical_pages','physical_delta','attributed_physical_delta','unexplained_delta','unexpected','actual_pool_deltas','actual_font_page_delta','actual_desktop_page_delta')},
        input_preservation=dict(current_inputs_unchanged=True,source_disk_unchanged=True),
        gui_facts=dict(installer=dict(bytes=73,source_revision=974,target_revision=1506,durable=1,error=0,ownership_verified=True,source_retained=True,normal_exit=0),
            debugger=dict(f5_continue_twice=True,f4_pause=True,published_modules=7,published_symbols=1765,f9_entry_and_paused_address=True,breakpoint_table_counts=[2,1,0],target_normal_exit=0,debugger_normal_exit=0),
            display=dict(actual_modes=[[1920,1080,150],[1024,768,150],[1920,1080,150]],confirmed_after_reopen=True),terminal=dict(shell_exit=0,window_closed_by_actual_x=True,process_list=ps['output'])),
        unresolved=['Files drag to desktop ended before confirmation for both SYSTEM and user-owned source; original GUI06 cancel313 not proven fixed','Settings .C31 cleanup remains unverified','Terminal latest prompt visibility after accumulated output needs focused review','Installer result column off default viewport requires review','Remaining full M10a2 contract'],
        bounds=['Specific 230424B read and cold-resource phase; not whole boot time, FPS or whole-disk throughput','CPU observation intervals include external control and short idle','Successful durable write/readback is not power-fault injection','No source disk or user four-file playground overwritten'],files=files))
    (OUT/'README.md').write_text('''# 主核PIO与GUI10实际结果

Windows WHPX，1920×1080 / 150%，普通UID/GID1000。主核单扇区数据阶段改REP INSW/OUTSW；loader原字节、错误/超时与FLUSH次序保留，用户程序和库原字节复用native08。

同一230424B文件POSIX校验697678241不变；宿主命令3.188→0.437秒，客体PIT0.59→0.31秒。相同冷资源完成阶段177.593→26.109秒。CPU原观察区间另存；不称整机帧率或全部开机时间。

真实Installer预检→安装73B，DURABLE1/错误0/权限核实，独立回读摘要一致、源保留、正常exit0。Debug2 F5继续、F4暂停后7模块1765符号，F9添加断点，真实表2→1→0，再次F5继续，目标及Debug正常exit0。真实Settings切到1024×768/150%，Keep后关闭再开确认预览0，最后恢复1920×1080/150%及预览0。Shell退出0后实际X关闭Terminal，ps核实应用全退出。

测试SID注销、原SID恢复后的同暂停owner终账13948→13948、全部池/字体/桌面增量0，未解释0；driver实际退出0，QEMU关闭，全部输入与原盘保持。原GUI09外层RUNNING仍原样保留。

Files本轮两次拖到桌面均未出现确认，之后正常退出0/日志空；不声明原GUI06取消313修复。Settings临时文件清理、Terminal末尾可见性、Installer默认结果列及完整M10a2其余合同继续。review.json列出精确原报告与归档摘要；gzip可还原原始JSON，不改原事实。
''',encoding='utf-8')
    print(json.dumps(dict(status='ARCHIVED_ACTUAL_FACTS',files=len(files),bytes=sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file()),review_sha256=sha(OUT/'review.json')),ensure_ascii=False))

if __name__=='__main__':main()
