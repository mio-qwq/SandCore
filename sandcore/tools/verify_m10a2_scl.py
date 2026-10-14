#!/usr/bin/env python3
"""一次Windows双盘VM验证SCL引导调用/保护/回收，复用M10a1只读基盘。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from m10_guest_boot import unique_symbols, wait_first_desktop, physical_word
from mkfs_m9 import Record, read_image, fold
from mkfs_m10 import mapped, release_views, parents, write_image, check_main, digest_payload

ROOT = Path(__file__).resolve().parents[1]

def prepare(image, build):
    core = (build/'fs/SYS/CORE/CORE.SKM').read_bytes()
    check_main(core)
    additions = {'SYS/CORE/CORE.SKM':core,
                 'HOME/M10A2/BASE.SCL':(build/'BASE.SCL').read_bytes(),
                 'HOME/M10A2/UPPER.SCL':(build/'UPPER.SCL').read_bytes(),
                 'HOME/M10A2/SCLPROBE.SCX':(build/'SCLPROBE.SCX').read_bytes()}
    temporary = image.with_suffix('.prepared.tmp')
    with ExitStack() as stack:
        disk = mapped(stack,image)
        records = read_image(disk,False)
        stack.callback(lambda: release_views(records))
        if struct.unpack_from('<I',disk,20)[0]!=5:
            raise ValueError('本切片只使用已有M10a1 v5基盘')
        root = struct.unpack_from('<iiI',disk,32)
        commit,counter = struct.unpack_from('<II',disk,52)
        counter=max(counter,max(r.generation for r in records.values()))
        original={key:(digest_payload(r.payload),r.uid,r.gid,r.mode,r.generation)
                  for key,r in records.items()}
        def generation():
            nonlocal counter
            if counter==0xffffffff:
                raise ValueError('对象代数耗尽')
            counter+=1
            return counter
        for name,body in additions.items():
            uid=-1 if name.startswith('SYS/') else 1000
            records[fold(name)]=Record(name,body,uid,uid,23,generation())
        parents(records,generation)
        if commit==0xffffffff:
            raise ValueError('目录代数耗尽')
        directory,count,_=write_image(temporary,records,256,root,commit+1,counter)
        with ExitStack() as checked_stack:
            checked=read_image(mapped(checked_stack,temporary),False)
            checked_stack.callback(lambda: release_views(checked))
            for key,record in records.items():
                r=checked[key]
                if (digest_payload(r.payload),r.uid,r.gid,r.mode,r.generation)!=(
                    digest_payload(record.payload),record.uid,record.gid,record.mode,record.generation):
                    raise AssertionError('准备盘回读失败：'+key)
            for key,before in original.items():
                if key in additions:
                    continue
                r=checked[key]
                if (digest_payload(r.payload),r.uid,r.gid,r.mode,r.generation)!=before:
                    raise AssertionError('既有用户/系统对象改变：'+key)
    # 原始基盘始终只读；这里只替换启动器为本轮创建的独立副本。
    os.replace(temporary,image)
    return dict(controlled_paths=sorted(additions),preserved_other_objects=len(original)-1,
                directory_sectors=directory,entries=count,
                host_bootstrap_libraries=True,guest_sccc_compiled=False,
                installed_core_payload_sha256=core[72:104].hex())

def stable_pages(vm,symbols):
    previous=None;equal=0;observations=[]
    deadline=time.monotonic()+6
    while time.monotonic()<deadline:
        value=physical_word(vm,symbols['pf_used'])
        observations.append(value)
        equal=equal+1 if value==previous else 0
        if equal==4:
            return value,observations
        previous=value;time.sleep(.03)
    raise TimeoutError('未观察到稳定物理页账')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--build',type=Path,default=ROOT/'build/m10a2-bootstrap')
    parser.add_argument('--data',type=Path,default=ROOT/'build/m10a1/sanddata.img')
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    args=parser.parse_args()
    build=args.build.resolve(strict=True);data=args.data.resolve(strict=True)
    symbols=unique_symbols(build/'core.sym')
    if 'pf_used' not in symbols:
        raise ValueError('缺少当前核心的唯一物理页计数符号')
    source_sha=sha(data)
    report=dict(status='RUNNING',scope='M10A2_SCL_BOOTSTRAP_ONLY',cases=[],
                source_data_sha256=source_sha,guest_sccc_compiled=False,
                core_sha256=sha(build/'fs/SYS/CORE/CORE.SKM'),
                build_report_sha256=sha(build/'bootstrap-build-01.json'),
                screenshots=[],unrelated_m10a1_evidence='REUSED_NOT_RERUN')
    def persist():
        (args.out/'scl.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with QemuSession(build/'sandcore.img',data,args.out,args.accel,True,audio=False,
                     memory=256,data_prepare=lambda image:prepare(image,build)) as vm:
        guest=Guest(vm)
        try:
            wait_first_desktop(guest,symbols,report)
            guest.connect()
            command='/HOME/M10A2/SCLPROBE.SCX'
            for mode,expected,marker in (('',0,b'SCL_BOOTSTRAP_PASS'),
                                          ('orphan',0,b'ORPHAN modules-live-at-exit'),
                                          ('fault',14,b'FAULT modules-live-before-page-fault')):
                before,_=stable_pages(vm,symbols)
                code,output,wall,cpu=guest.command(command+(' '+mode if mode else ''),timeout=90)
                after,observations=stable_pages(vm,symbols)
                okay=code==expected and marker in output and b'FAIL ' not in output
                # 首次正常执行预热PID历史；后两项必须精确恢复该暖基点。
                if mode:
                    okay=okay and before==after
                report['cases'].append(dict(name=mode or 'normal',status='PASS' if okay else 'FAIL',
                    exit_code=code,expected_exit=expected,output=output.decode('utf-8','replace'),
                    wall_seconds=wall,qemu_cpu_seconds=cpu,before_physical_pages=before,
                    after_physical_pages=after,stable_observations=observations))
                persist();print((mode or 'normal')+(': PASS' if okay else ': FAIL'),flush=True)
                if not okay:
                    raise AssertionError('实际SCL用例失败：'+(mode or 'normal'))
            # 原始M10a1旧SCX程序与新内核真实读写调用保持可用。
            code,output,wall,cpu=guest.command('ls /HOME/M10A2',timeout=30)
            okay=code==0 and b'BASE.SCL' in output and b'UPPER.SCL' in output
            report['cases'].append(dict(name='existing-SCX-ls',status='PASS' if okay else 'FAIL',
                exit_code=code,output=output.decode('utf-8','replace'),wall_seconds=wall,qemu_cpu_seconds=cpu))
            if not okay:
                raise AssertionError('既有SCX ls不能读取新库目录')
            vm.hmp('sendkey f1');time.sleep(.3)
            picture=vm.out/'desktop.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status']='DECLARED_SCL_BOOTSTRAP_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,
                          message=str(error),traceback=traceback.format_exc()))
            try:
                picture=vm.out/'failure.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture_error:
                report['screenshot_error']=repr(capture_error)
            raise
        finally:
            guest.close()
            report['source_disk_unchanged']=sha(data)==source_sha
            persist()
    print(report['status'],flush=True)

if __name__=='__main__':
    main()
