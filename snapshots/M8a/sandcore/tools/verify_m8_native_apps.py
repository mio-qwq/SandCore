#!/usr/bin/env python3
"""mio：当前G2真正编译正式用户组件，保留每份SCX/map/log来源。

G2只能来自本轮三代收敛报告，不拿宿主GCC产物替代失败组件。所有
源码和头文件由SandFS读入，键盘提交完整命令，输出从真实IDE盘读取。
本轮只证明原生编译；游戏可玩流程、布局和性能仍须独立运行证据。
"""
import argparse
import hashlib
import json
import os
import shutil
import time
import traceback
from pathlib import Path
import verify_m8_phase2 as phase

ROOT=phase.ROOT
q=phase.q
t=phase.t
compiler=phase.compiler


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True)
    parser.add_argument('--compiler-stage',required=True)
    parser.add_argument('--apps',default='race,world,lumen,mines,monitor,lens,canvas,files,ide,debugger,note,palette,calc,settings,shell,assembler')
    parser.add_argument('--source-overlay',help='只向本轮私有盘安装冻结源码：JSON的文件名映射到实际文件路径')
    parser.add_argument('--accel',choices=('tcg','whpx','auto'),help='显式选择并记录本轮加速；不指定时保留既有环境/default TCG')
    args=parser.parse_args()
    if args.accel:os.environ['SANDCORE_QEMU_ACCEL']=args.accel
    out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    stage=ROOT/args.compiler_stage
    proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='native'
    g2=(stage/'g2.scx').read_bytes()
    assert g2==(stage/'g3.scx').read_bytes()
    apps=args.apps.split(',')
    for app in apps:assert (ROOT/'user'/f'{app}.c').is_file(),app
    # 对照优化不能拿不同编译器/不同共享库的历史SCX直接比速度。
    # overlay只替本轮私有盘的指定源码，让冻结旧应用由同一G2重新
    # 编译；当前工作区源码、开发数据盘和用户旧镜像均不改写。
    source_files={path.name:path.read_bytes() for path in sorted((ROOT/'build/fs/SYS/SRC').iterdir()) if path.is_file()}
    overlay={};overlay_paths={}
    if args.source_overlay:
        specification=json.loads((ROOT/args.source_overlay).read_text(encoding='utf-8'))
        assert isinstance(specification,dict) and specification,'空/无效源码覆盖表'
        names={name.casefold():name for name in source_files}
        for requested,filename in specification.items():
            assert requested==Path(requested).name and requested.casefold() in names,'只能覆盖既有源文件名'
            actual=names[requested.casefold()];path=(ROOT/filename).resolve()
            assert path.is_file(),str(path)
            overlay[actual]=path.read_bytes();overlay_paths[actual]=str(path)
        source_files.update(overlay)
    # 恢复旧源码时，复制工具可能保留早于build/fs的修改时间，make
    # 因而不触发复制。实际盘内源码必须逐字节等于当前源（或显式
    # overlay），不能只记录根目录SHA后把旧安装副本误称为新编译。
    # 此检查在启动QEMU前完成；失败先刷新构建，不制造运行证据。
    stale=[]
    for name,blob in source_files.items():
        current_source=ROOT/'user'/name
        if name not in overlay and current_source.is_file() and current_source.read_bytes()!=blob:stale.append(name)
    assert not stale,'盘内源码与工作区不一致，须重新构建: '+', '.join(stale)
    shutil.copy2(ROOT/'build/sandcore.img',out/'sandcore.img')
    phase.OUT=t.OUT=q.OUT=compiler.OUT=phase.theme.OUT=out
    compiler.v.windows=t.windows
    inputs={name:phase.sha((ROOT/name).read_bytes()) for name in
            ('build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym','user/SCAPI.H','kernel/font16.txt')}
    # 冻结核与只读符号，后台构建不能把新符号混进旧VM观察结果。
    kernel=q.symbols();q.symbols=lambda:kernel
    shutil.copy2(ROOT/'build/kernel.elf',out/'kernel.elf')
    shutil.copy2(ROOT/'build/kernel.sym',out/'kernel.sym')
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for name,blob in overlay.items():compiler.disk_put(disk,'SYS/SRC/'+name,blob)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'native-apps',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-native-apps.img'
    report=dict(author='mio',status='RUNNING',scope='NATIVE_COMPILE_ONLY',inputs=inputs,
                requested_accel=os.environ.get('SANDCORE_QEMU_ACCEL','tcg'),
                g2_sha256=phase.sha(g2),artifacts={},source_inputs={},source_overlay=overlay_paths)
    frozen=out/'source-inputs';frozen.mkdir()
    for name,blob in source_files.items():
        report['source_inputs'][name]=phase.sha(blob);(frozen/name).write_bytes(blob)
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome',180)
        t.open_shell(True);phase.idle()
        for app in apps:
            print('COMPILING',app,flush=True)
            source='SYS/SRC/'+app+'.c';output='HOME/'+app.upper()+'.SCX'
            blob,mapping,seconds=phase.compile_source('BIN/G2.SCX',source,output,600)
            (out/(app+'.scx')).write_bytes(blob);(out/(app+'.map')).write_bytes(mapping)
            log=compiler.file_content(phase.DISK,output+'.log')
            (out/(app+'.log')).write_bytes(log)
            actual_source=next(blob for name,blob in source_files.items() if name.casefold()==(app+'.c').casefold())
            report['artifacts'][app]=dict(source_sha256=phase.sha(actual_source),
                                        output=output,sha256=phase.sha(blob),map_sha256=phase.sha(mapping),bytes=len(blob),seconds=seconds)
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print('NATIVE OK',app,len(blob),seconds,flush=True)
        report.update(status='PASS',limitations='仅实际系统内原生编译；功能、可玩流程、性能和审美未由此报告判定')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('NATIVE APPS PASS',len(apps),flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:
            q.shot('failure');report['faults']=phase.faults();report['registers']=q.hmp('info registers')
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
