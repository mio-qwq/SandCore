#!/usr/bin/env python3
"""统一构建本轮平台与应用并保存守恒来源；不构建封存游戏或生成数据盘。"""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
import stage_m10a2_sdk as sdk
ROOT=Path(__file__).resolve().parents[1]
SUFFIX={'.c','.h','.inc','.asm','.json','.ld','.py','.mk','.scw','.rsp','.rdt','.sh'}
OUTPUTS=('sandcore.img','fs/SYS/CORE/CORE.SKM','core.sym','core.elf','core.bin',
 'fs/bin/sccc.scx','fs/bin/sh.scx','fs/bin/nano.scx','fs/SYS/CORE/IMAGE.SCX',
 'fs/SYS/UI/UAX15_1.SCU','SCLD.SCX','SCSHELL.SCX','SCFILEWORK.SCX',
 'SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL','SCSHELL.SCL','SCGFX.SCL','SCTERM.SCL','SCSOUND.SCL',
 'FILES2.SCX','NOTES2.SCX','DEBUG2.SCX','CALC2.SCX','PALETTE2.SCX','MONITOR2.SCX','STUDIO2.SCX',
 'CANVAS2.SCX','LENS2.SCX','SETTINGS2.SCX','INSTALLER2.SCX','TERMINAL2.SCX','SOUND2.SCX','SOUNDWORK.SCX','SESSION2.SCX','LOGIN2.SCX',
 'shell_host_issue.h','login_program_issue.h','login_resource_issue.h')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def paths():
    # SDK源码是本次归档输入；不把它们全称为GCC实际编译单元。
    # 实际编译依赖另从.d收集，封存游戏在这里仅只读记录、从不构建。
    result=set()
    for folder in ('kernel','user','boot','modules','tools','examples/m10a2-sdk'):
        result.update(p.resolve() for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix.lower() in SUFFIX)
    for pattern in ('*.mk','Makefile'):result.update(p.resolve() for p in ROOT.glob(pattern))
    for folder in ('assets/ui','third_party/unicode15_1'):
        result.update(p.resolve() for p in (ROOT/folder).rglob('*') if p.is_file())
    result.update(p.resolve() for p in (ROOT/'third_party').glob('*SOURCES.json'))
    result.update((ROOT/name).resolve() for name in sdk.HEADERS.values())
    result.update((ROOT/'user/stdlib'/name).resolve() for name in sdk.STANDARD)
    return result
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tag',required=True)
    p.add_argument('--build',type=Path,default=ROOT/'build/m10a2-main-studio-01');args=p.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):raise ValueError('invalid tag')
    build=args.build.resolve()
    if not build.is_relative_to(ROOT/'build'):raise ValueError('build必须在本工作区build内')
    build.mkdir(parents=True,exist_ok=True);log=build/(args.tag+'.log');report_path=build/(args.tag+'.json')
    if log.exists() or report_path.exists():raise FileExistsError('不能覆盖历史构建凭证')
    before={p:sha(p) for p in paths()}
    rel=build.relative_to(ROOT).as_posix()
    targets=[rel+'/sandcore.img',rel+'/fs/SYS/CORE/CORE.SKM',rel+'/fs/bin/sccc.scx',
        rel+'/fs/bin/sh.scx',rel+'/fs/bin/nano.scx','m10a2-shell-artifacts','m10a2-app-artifacts',
        'm10a2-object-artifacts','m10a2-ui-resources']
    command=['make','-j4','BUILD='+rel,'M9_BUILD=1','M10_BUILD=1',*targets]
    started=time.monotonic()
    with log.open('xb') as stream:code=subprocess.call(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    actual=set()
    for dep in build.rglob('*.d'):
        for row in dep.read_text(encoding='utf-8').replace('\\\n',' ').splitlines():
            if ':' not in row:continue
            for name in row.split(':',1)[1].split():
                path=(ROOT/name).resolve()
                if path.is_file() and path.is_relative_to(ROOT) and not path.is_relative_to(build):actual.add(path)
    changed=sorted(p.relative_to(ROOT).as_posix() for p,value in before.items() if not p.is_file() or sha(p)!=value)
    added=paths()-set(before)
    # 已声明源码在构建中出现新文件同样记为改变，不用构建后的hash掩盖它。
    changed.extend(sorted(p.relative_to(ROOT).as_posix() for p in added))
    inputs=set(before)|actual
    report=dict(scope='M10A2_P6_PLATFORM_APPLICATIONS_UNIFIED_HOST_BUILD',exit_code=code,
        command=command,wall_seconds=time.monotonic()-started,host_only=True,guest_compile=False,guest_runtime=False,
        source_files_modified_during_build=changed,source_freeze_before_sha256={p.relative_to(ROOT).as_posix():v for p,v in sorted(before.items())},
        inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p),
            role='COMPILER_DEPENDENCY' if p in actual else 'SOURCE_SDK_OR_BUILD_ARCHIVE_INPUT') for p in sorted(inputs)],outputs=[],
        build_directory=rel,note='HOST compiler/object packing is not guest provenance. No data disk or frozen game target was built.')
    missing=[]
    for name in OUTPUTS:
        path=build/name
        if path.is_file():report['outputs'].append(dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,sha256=sha(path)))
        else:missing.append(name)
    report['missing_outputs']=missing
    if changed or missing:report['exit_code']=report['exit_code'] or 1
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('P6_UNIFIED_HOST_BUILD',report['exit_code'],'outputs',len(report['outputs']),'changed',len(changed),'missing',missing)
    return report['exit_code']
if __name__=='__main__':raise SystemExit(main())
