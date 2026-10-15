#!/usr/bin/env python3
"""Run a unified host build in an isolated P5 candidate and preserve provenance."""
import argparse,hashlib,json,subprocess,time,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def reference_path(value):
    # 客体报告在Windows保存绝对路径，WSL构建只把同一C盘路径映射回来。
    if len(value)>3 and value[1:3]==':\\':
        value='/mnt/'+value[0].lower()+'/'+value[3:].replace('\\','/')
    return Path(value).resolve()

def snapshot(stage):
    result={}
    # 真实构建也读取字体、许可及第三方固定源；全部进入前后摘要闭包。
    for tree in ('kernel','user','tools','boot','assets','third_party'):
        for p in sorted((stage/tree).rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                result[p.relative_to(stage).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
    for name in ('Makefile','m10a2.mk','m9.mk','m10.mk','core.ld'):
        p=stage/name;result[name]=hashlib.sha256(p.read_bytes()).hexdigest()
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--tag',required=True)
    parser.add_argument('--core',action='store_true')
    parser.add_argument('--native-generation-report',type=Path)
    parser.add_argument('--shell-host-image',type=Path)
    args=parser.parse_args();stage=args.stage.resolve()
    if not stage.is_relative_to((ROOT/'build').resolve()) or not (stage/'build-view.json').is_file():parser.error('a reviewed isolated build view is required')
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.tag):parser.error('invalid tag')
    output=stage/'reports'/args.tag
    if output.exists():parser.error('do not overwrite a prior report')
    output.mkdir(parents=True)
    before=snapshot(stage)
    issued=None
    if bool(args.native_generation_report)!=bool(args.shell_host_image):
        parser.error('native generation report and exact host image are required together')
    if args.shell_host_image:
        native=json.loads(args.native_generation_report.read_text(encoding='utf-8'))
        image=args.shell_host_image.resolve()
        item=native.get('native_artifacts',{}).get('SCSHELL.SCX',{})
        actual=hashlib.sha256(image.read_bytes()).hexdigest()
        generation_origin=None
        if item.get('generated_this_vm') is not True:
            origin=reference_path(item.get('generation_origin_report',''))
            if not origin.is_relative_to((ROOT/'build').resolve()):raise ValueError('host origin outside build')
            original=json.loads(origin.read_text(encoding='utf-8'))
            produced=original.get('native_artifacts',{}).get('SCSHELL.SCX',{})
            if (hashlib.sha256(origin.read_bytes()).hexdigest()!=item.get('generation_origin_report_sha256')
                or original.get('status')!='P5_NATIVE_GENERATION_PASS' or produced.get('generated_this_vm') is not True
                or produced.get('sha256')!=actual or reference_path(produced.get('path',''))!=image
                or not native.get('reused_generation',{}).get('source_compile_closures',{}).get('SCSHELL.SCX')):
                raise ValueError('reused host lacks its exact actual generation origin')
            generation_origin=dict(report=str(origin),sha256=item['generation_origin_report_sha256'])
        runtime_changes={name:dict(before=native['source_snapshot'].get(name),after=before.get(name))
            for name in set(native['source_snapshot'])|set(before) if native['source_snapshot'].get(name)!=before.get(name)}
        if (native.get('status')!='P5_NATIVE_GENERATION_PASS' or native.get('current_inputs_unchanged') is not True
            or native.get('source_disk_unchanged') is not True
            or any(not name.startswith('kernel/') for name in runtime_changes)
            or any(before.get(name)!=value for name,value in native['source_input_sha256'].items())
            or item.get('sha256')!=actual or item.get('bytes')!=image.stat().st_size
            or (item.get('generated_this_vm') is not True and not generation_origin) or not args.core
            or not image.is_relative_to((ROOT/'build').resolve())):
            raise ValueError('native host must be the exact guest-generated file for this candidate')
        # 原生成会话的CORE/ELF仍需可追溯；只保存这几份小产物，绝不复制磁盘。
        prior=output/'prior-bootstrap';prior.mkdir()
        for name in ('core.elf','core.sym','core.bin','shell_host_issue.h','fs/SYS/CORE/CORE.SKM'):
            source=stage/'build/p5-unified-check'/name
            target=prior/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        header=stage/'build/p5-unified-check/shell_host_issue.h'
        subprocess.run(['python3',str(stage/'tools/mk_shell_issue.py'),'--host',str(image),'--out',str(header)],check=True)
        issued=dict(path=str(image),bytes=image.stat().st_size,sha256=actual,
            actual_host_generation_origin=generation_origin,
            generation_to_runtime_kernel_changes=runtime_changes,
            generation_report_sha256=hashlib.sha256(args.native_generation_report.read_bytes()).hexdigest(),
            header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),same_generated_bytes_selected=True)
    target='m10a2-shell-bootstrap' if args.core else 'm10a2-shell-artifacts'
    command=['make','-j2','BUILD=build/p5-unified-check','M10A2_SHELL_DIR=build/p5-unified-check','M9_BUILD=1','M10_BUILD=1',target]
    if issued:command.insert(-1,'SHELL_HOST_IMAGE='+str(args.shell_host_image.resolve()))
    started=time.monotonic()
    with (output/'build.log').open('wb') as log:
        done=subprocess.run(command,cwd=stage,stdout=log,stderr=subprocess.STDOUT)
    after=snapshot(stage)
    result=dict(status='HOST_CANDIDATE_BUILD_PASS' if done.returncode==0 else 'HOST_CANDIDATE_BUILD_FAILED',
        exit_code=done.returncode,command=command,seconds=time.monotonic()-started,
        driver_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        issued_native_host=issued,
        inputs_sha256=before,changed_source_files=sorted(k for k in set(before)|set(after) if before.get(k)!=after.get(k)),
        executes_guest=False,active_kernel_replaced=False,outputs=[])
    for name in ('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL','SCSHELL.SCL','SCFILEWORK.SCX','SCSHELL.SCX','fs/SYS/CORE/CORE.SKM'):
        p=stage/'build/p5-unified-check'/name
        if p.is_file():result['outputs'].append(dict(path=p.relative_to(stage).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    (output/'build.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # 保留完整日志文件，终端只送真正诊断和尾部，避免历史warning淹没本批失败。
    rows=(output/'build.log').read_text(encoding='utf-8',errors='replace').splitlines()
    diagnostics=[row for row in rows if 'error:' in row or 'No rule to make target' in row]
    print('\n'.join(diagnostics[:20]+rows[-18:]))
    print('REPORT',output/'build.json','EXIT',done.returncode,'SOURCE_CHANGED',result['changed_source_files'])
    raise SystemExit(done.returncode or bool(result['changed_source_files']))
if __name__=='__main__':main()
