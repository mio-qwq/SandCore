#!/usr/bin/env python3
"""一个Windows双盘VM验证公开SC32 API与客体原生编译，GUI证据单列。"""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import struct
import time
import traceback

from scserial import QemuSession,sha
from verify_m9 import Guest,png_from_ppm
from m10_guest_boot import unique_symbols,wait_first_desktop
from verify_m10a2_scl import prepare,stable_pages
from verify_m10a2_dynamic import SOURCE_NAMES
from scl_format import validate
from scd_format import validate_scx

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--build-report',type=Path,required=True)
    parser.add_argument('--data',type=Path,default=ROOT/'build/m10a1/sanddata.img')
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    args=parser.parse_args();build=ROOT/'build/m10a2-sc32-bootstrap'
    proof=json.loads(args.build_report.read_text(encoding='utf-8'))
    if proof['exit_code'] or proof['source_files_modified_during_build']:
        raise ValueError('构建失败或期间源码发生变化')
    for item in proof['outputs']:
        if sha(ROOT/item['path'])!=item['sha256']:raise ValueError('产物不匹配构建记录')
    prefix='/TMP/S32-'+secrets.token_hex(3);package='/HOME/M10P'
    compiler=prefix+'/SCCC0.SCX'
    extra={'SYS/LIB/SCLDR.SCL':(build/'SCLDR.SCL').read_bytes(),
        'SYS/LIB/SC32API.SCL':(build/'SC32API.SCL').read_bytes(),
        compiler[1:]:(build/'fs/bin/sccc.scx').read_bytes(),
        package[1:]+'/SC32PROBE.SCX':(build/'SC32PROBE.SCX').read_bytes(),
        package[1:]+'/SC32GUARD.SCL':(build/'SC32GUARD.SCL').read_bytes()}
    names=set(SOURCE_NAMES)|{'SC32API.H','SC32GUI.H'}
    for folder in ('sc32','guiapi'):
        for path in (ROOT/'user'/folder).glob('*'):
            if path.is_file() and path.suffix.lower() in ('.c','.h','.json'):
                names.add(path.relative_to(ROOT/'user').as_posix())
    hashes={}
    for name in sorted(names):
        path=ROOT/'user'/name;blob=path.read_bytes()
        extra[prefix[1:]+'/SRC/'+name]=blob;hashes['user/'+name]=sha(path)
        if path.suffix.upper()=='.H':extra['SYS/INC/'+name]=blob
    for name in ('SC32PROBE.C','SC32PROBE.json','SC32GUARD.C','SC32GUARD.json'):
        path=ROOT/'tests/m10a2'/name;extra[prefix[1:]+'/'+name]=path.read_bytes()
        hashes['tests/m10a2/'+name]=sha(path)
    original=sha(args.data);symbols=unique_symbols(build/'core.sym')
    report=dict(status='RUNNING',scope='SC32_PUBLIC_API_NATIVE_BUILD_AND_FILE_FOUNDATION',
        cases=[],source_data_sha256=original,input_sha256=hashes,
        build_report_sha256=sha(args.build_report),installed_libraries_source='HOST_GCC_BOOTSTRAP',
        core_sha256=sha(build/'fs/SYS/CORE/CORE.SKM'),screenshots=[],
        gui_behavior_verified=False,full_Phase2=False,full_M10a2=False)
    def persist():
        (args.out/'sc32.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with QemuSession(build/'sandcore.img',args.data,args.out,args.accel,True,audio=False,memory=256,
        data_prepare=lambda image:prepare(image,build,extra,runtime_fixtures=False)) as vm:
        guest=Guest(vm)
        def run(name,command,expected=0,contains=None,timeout=180):
            code,output,wall,cpu=guest.command(command,timeout=timeout)
            good=code==expected and (contains is None or contains in output)
            report['cases'].append(dict(name=name,status='PASS' if good else 'FAIL',command=command,
                exit_code=code,expected_exit=expected,output=output.decode('utf-8','replace'),
                wall_seconds=wall,qemu_cpu_seconds=cpu));persist()
            print(name+(': PASS' if good else ': FAIL'),flush=True)
            if not good:raise AssertionError(name)
        def check(name,good,**details):
            report['cases'].append(dict(name=name,status='PASS' if good else 'FAIL',**details));persist()
            print(name+(': PASS' if good else ': FAIL'),flush=True)
            if not good:raise AssertionError(name)
        try:
            wait_first_desktop(guest,symbols,report);guest.connect()
            run('public-API-host-app-real-calls',package+'/SC32PROBE.SCX',contains=b'SC32_FOUNDATION_PASS')
            run('native-output-directory','mkdir '+package+'/NATIVE')
            run('native-plugin-copy','cp '+package+'/SC32GUARD.SCL '+package+'/NATIVE/SC32GUARD.SCL')
            app=package+'/NATIVE/SC32PROBE.SCX'
            run('guest-SCCC-public-header-app',compiler+' '+prefix+'/SC32PROBE.C '+app+
                ' --dynamic '+prefix+'/SC32PROBE.json',contains=b'SCCC OK SCX DYNAMIC')
            blob=guest.get_bytes(app,'SC32PROBE-native.scx');validate_scx(blob)
            report['native_application']=dict(bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest(),guest_compiled=True)
            run('native-caller-to-GCC-library-ABI',app,contains=b'SC32_FOUNDATION_PASS')
            target=package+'/NATIVE/SC32API.SCL'
            run('guest-SCCC-builds-SC32-library',compiler+' '+prefix+'/SRC/sc32/SC32API.C '+target+
                ' --scl '+prefix+'/SRC/sc32/SC32API.json',timeout=240)
            library=guest.get_bytes(target,'SC32API-native.scl');tables=validate(library)
            check('native-library-all-declared-exports',len(tables['exports'])==85 and len(tables['imports'])==5,
                bytes=len(library),sha256=hashlib.sha256(library).hexdigest(),executed_this_generated_file=False)
            before,_=stable_pages(vm,symbols)
            run('warm-public-API-exit',app,contains=b'SC32_FOUNDATION_PASS')
            after,observations=stable_pages(vm,symbols)
            check('warm-exit-physical-page-recovery',before==after,before=before,after=after,observations=observations)
            run('legacy-SCX-after-new-STD-and-file','ls '+package,contains=b'SC32PROBE.SCX')
            vm.hmp('sendkey f1');time.sleep(.3)
            picture=vm.out/'desktop.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture));report['status']='DECLARED_SC32_FOUNDATION_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,
                message=str(error),traceback=traceback.format_exc()))
            try:
                picture=vm.out/'failure.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture:report['capture_error']=repr(capture)
            raise
        finally:
            guest.close();report['source_disk_unchanged']=sha(args.data)==original
            report['current_inputs_unchanged']=all(sha(ROOT/name)==digest for name,digest in hashes.items());persist()
    print(report['status'],flush=True)

if __name__=='__main__':main()
