#!/usr/bin/env python3
"""一个Windows双盘VM验证自动动态SCX、系统根与原生编译接线。"""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import struct
import time
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from m10_guest_boot import unique_symbols, wait_first_desktop
from verify_m10a2_scl import prepare, stable_pages
from scl_format import validate
from scd_format import validate_scx

ROOT=Path(__file__).resolve().parents[1]
SOURCE_NAMES=('sccc.c','s3c.c','SCAPI.H','SCTYPES.H','SCIO.H','COMPILECLI.inc','s3c_lex.inc',
    's3c_pp.inc','s3c_parse.inc','s3c_std_parse.inc','s3c_emit.inc','s3c_std_emit.inc','s3c_scl.inc','runtime/SCLFMT.H',
    'runtime/scl_format.c','runtime/SCDYN.H','runtime/scd_format.c','runtime/SCLDR.H',
    'runtime/SCLPRIVATE.H','runtime/SCLROOT.C','runtime/SCLROOT.json','runtime/scldr.c',
    'runtime/scl_platform.c','runtime/scl_application.c','runtime/SCLJUMPPRIVATE.H','runtime/scl_termination.c')

def manifest(name,dependencies,imports,library=False):
    item=dict(name=name,dependencies=[dict(name=n,abi_major=1,min_minor=0,min_version=1) for n in dependencies],
        imports=[dict(name=n,dependency=d,kind=k,size=s) for n,d,k,s in imports])
    if library:item.update(abi_major=1,abi_minor=0,library_version=1,init='root_check_init',fini=None,
        exports=[dict(name='root_seen',kind='data'),dict(name='root_raise_fault',kind='function'),
                 dict(name='root_spin',kind='function')])
    else:item['kind']='application'
    return (json.dumps(item,indent=2)+'\n').encode()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--data',type=Path,default=ROOT/'build/m10a1/sanddata.img')
    parser.add_argument('--build-report',type=Path,required=True)
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    args=parser.parse_args();build=ROOT/'build/m10a2-dynamic-bootstrap'
    build_report=json.loads(args.build_report.read_text(encoding='utf-8'))
    if build_report['exit_code']:raise ValueError('指定构建未通过')
    for item in build_report['outputs']:
        if sha(ROOT/item['path'])!=item['sha256']:raise ValueError('产物与构建记录不同')
    prefix='/TMP/DY-'+secrets.token_hex(3);package='/HOME/M10D'
    compiler=prefix+'/SCCC0.SCX';symbol=unique_symbols(build/'core.sym')
    additions={'SYS/LIB/SCLDR.SCL':(build/'SCLDR.SCL').read_bytes(),
        compiler[1:]:(build/'fs/bin/sccc.scx').read_bytes()}
    input_hashes={}
    for name in SOURCE_NAMES:
        blob=(ROOT/'user'/name).read_bytes();additions[prefix[1:]+'/SRC/'+name]=blob
        input_hashes['user/'+name]=hashlib.sha256(blob).hexdigest()
    for name in ('DYNAPP.C','DYNAPP.json'):
        blob=(ROOT/'tests/m10a2'/name).read_bytes();additions[prefix[1:]+'/'+name]=blob
        input_hashes['tests/m10a2/'+name]=hashlib.sha256(blob).hexdigest()
    for directory in ('HOST','NATIVE'):
        for name in ('BASE.SCL','UPPER.SCL'):
            additions[package[1:]+'/'+directory+'/'+name]=(ROOT/'evidence/m10a2-native'/name).read_bytes()
    app=(build/'DYNAPP.SCX').read_bytes();parsed=validate_scx(app)
    additions[package[1:]+'/HOST/DYNAPP.SCX']=app
    # 用户包里的同名坏根不能成为正式启动来源。
    additions[package[1:]+'/HOST/SCLDR.SCL']=b'not a trusted root'
    bad=bytearray(app);meta=36+parsed['metadata_rva'];deps=struct.unpack_from('<I',bad,meta+32)[0]
    struct.pack_into('<I',bad,meta+deps+4,2)
    validate_scx(bytes(bad));additions[package[1:]+'/HOST/BADVER.SCX']=bytes(bad)
    missing=app.replace(b'UPPER.SCL\0',b'ZPPER.SCL\0')
    validate_scx(missing);additions[package[1:]+'/HOST/MISSING.SCX']=missing
    bad=bytearray(app);relocs=struct.unpack_from('<I',bad,meta+48)[0]
    struct.pack_into('<I',bad,meta+relocs,0)
    additions[package[1:]+'/HOST/BADCRT.SCX']=bytes(bad)
    header='#include "SRC/runtime/SCLDR.H"\n'
    check=(header+'#include "SRC/SCAPI.H"\nextern volatile int base_value;\nint root_seen;\n'
        'int root_check_init(void){ScRuntimeStats s;s.size=sizeof(s);'
        'root_seen=ScApplicationRuntime()!=0 && ScRuntimeQuery(ScApplicationRuntime(),&s)==SC_SCL_BUSY;'
        'return root_seen?0:1;}\n'
        'static void fault_entry(u32 *context){sc_exit((int)context[12]);}\n'
        'int root_raise_fault(void){if(sc_handler(14,fault_entry))return 8;'
        '*(volatile unsigned char *)(void *)ScRuntimeQuery=0;return 9;}\n'
        'int root_spin(int value){if(base_value!=7)return 1;base_value=value;'
        'for(int i=0;i<5000;i++){if(base_value!=value)return 2;if(!(i%64))sc_yield();}'
        'return base_value==value?0:3;}\n').encode()
    query=(header+'extern int root_seen;\nint main(void){ScRuntimeStats s;s.size=sizeof(s);'
        'if(!root_seen || !ScApplicationRuntime())return 1;'
        'if(ScRuntimeQuery(ScApplicationRuntime(),&s))return 2;'
        'return s.modules==3 && s.pinned_modules==2?0:3;}\n').encode()
    fault=b'extern int root_raise_fault(void);\nint main(void){return root_raise_fault();}\n'
    root_imports=[('ScApplicationRuntime','SCLDR.SCL','function',1),('ScRuntimeQuery','SCLDR.SCL','function',1)]
    for name,blob in (
        ('ROOTCHECK.C',check),('DYNROOT.C',query),('DYNFAULT.C',fault),
        ('ROOTCHECK.json',manifest('ROOTCHK.SCL',['SCLDR.SCL','BASE.SCL'],
            root_imports+[('base_value','BASE.SCL','data',4)],True)),
        ('DYNROOT.json',manifest('DYNROOT.SCX',['SCLDR.SCL','ROOTCHK.SCL'],
            root_imports+[('root_seen','ROOTCHK.SCL','data',4)])),
        ('DYNFAULT.json',manifest('DYNFAULT.SCX',['ROOTCHK.SCL'],
            [('root_raise_fault','ROOTCHK.SCL','function',1)])),
        ('DYNONE.C',b'extern int root_spin(int);\nint main(void){return root_spin(11);}\n'),
        ('DYNTWO.C',b'extern int root_spin(int);\nint main(void){return root_spin(22);}\n'),
        ('DYNONE.json',manifest('DYNONE.SCX',['ROOTCHK.SCL'],[('root_spin','ROOTCHK.SCL','function',1)])),
        ('DYNTWO.json',manifest('DYNTWO.SCX',['ROOTCHK.SCL'],[('root_spin','ROOTCHK.SCL','function',1)]))):
        additions[prefix[1:]+'/'+name]=blob
    source_sha=sha(args.data)
    report=dict(status='RUNNING',scope='M10A2_AUTOMATIC_DYNAMIC_SCX_AND_NATIVE_TOOLCHAIN',cases=[],
        installed_root_source='HOST_GCC_BOOTSTRAP',source_data_sha256=source_sha,
        build_report_sha256=sha(args.build_report),input_sha256=input_hashes,
        core_sha256=sha(build/'fs/SYS/CORE/CORE.SKM'),root_sha256=sha(build/'SCLDR.SCL'),
        prefix=prefix,package=package,screenshots=[],full_M10a2=False)
    def persist():
        (args.out/'dynamic.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with QemuSession(build/'sandcore.img',args.data,args.out,args.accel,True,audio=False,memory=256,
        data_prepare=lambda image:prepare(image,build,additions,runtime_fixtures=False)) as vm:
        guest=Guest(vm)
        def run(name,command,expected=0,contains=None,timeout=160):
            actual,output,wall,cpu=guest.command(command,timeout=timeout)
            okay=actual==expected and (contains is None or contains in output)
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',command=command,
                exit_code=actual,expected_exit=expected,output=output.decode('utf-8','replace'),
                wall_seconds=wall,qemu_cpu_seconds=cpu));persist()
            print(name+(': PASS' if okay else ': FAIL'),flush=True)
            if not okay:raise AssertionError(name)
            return output
        def check(name,okay,**details):
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',**details));persist()
            print(name+(': PASS' if okay else ': FAIL'),flush=True)
            if not okay:raise AssertionError(name)
        try:
            wait_first_desktop(guest,symbol,report);guest.connect()
            run('automatic-host-SCX-and-fixed-system-root',package+'/HOST/DYNAPP.SCX')
            target=package+'/NATIVE/DYNAPP.SCX'
            run('guest-SCCC-dynamic-compile',compiler+' '+prefix+'/DYNAPP.C '+target+
                ' --dynamic '+prefix+'/DYNAPP.json',contains=b'SCCC OK SCX DYNAMIC')
            native=guest.get_bytes(target,'native-DYNAPP.scx');tables=validate_scx(native)
            report['native_application']=dict(sha256=hashlib.sha256(native).hexdigest(),bytes=len(native),
                guest_compiled=True,metadata=tables)
            run('automatic-native-SCX-real-import-calls',target)
            for stem,output,option in (('ROOTCHECK','ROOTCHK.SCL','--scl'),
                ('DYNROOT','DYNROOT.SCX','--dynamic'),('DYNFAULT','DYNFAULT.SCX','--dynamic'),
                ('DYNONE','DYNONE.SCX','--dynamic'),('DYNTWO','DYNTWO.SCX','--dynamic')):
                run('compile-'+stem,compiler+' '+prefix+'/'+stem+'.C '+package+'/NATIVE/'+output+
                    ' '+option+' '+prefix+'/'+stem+'.json')
            run('init-sees-shared-root-context',package+'/NATIVE/DYNROOT.SCX')
            pair=package+'/NATIVE/DYNONE.SCX & a=$!; '+package+'/NATIVE/DYNTWO.SCX & b=$!; '+\
                'wait $a; first=$?; wait $b; second=$?; echo M10D_ISOLATION:$first:$second; '+\
                'test $first -eq 0 && test $second -eq 0'
            run('two-concurrent-processes-private-library-data',pair,contains=b'M10D_ISOLATION:0:0')
            # 此源码由真正运行的SCCC编成独立库；来源与已安装宿主根分列。
            run('native-root-output-directory','mkdir '+prefix+'/BUILT')
            root_target=prefix+'/BUILT/SCLDR.SCL'
            run('guest-SCCC-builds-runtime-root',compiler+' '+prefix+'/SRC/runtime/SCLROOT.C '+root_target+
                ' --scl '+prefix+'/SRC/runtime/SCLROOT.json',timeout=240)
            root_blob=guest.get_bytes(root_target,'native-SCLDR.scl');root_tables=validate(root_blob)
            check('native-root-nine-exports-no-imports',len(root_tables['exports'])==9 and
                not root_tables['imports'] and not root_tables['dependencies'],bytes=len(root_blob),
                sha256=hashlib.sha256(root_blob).hexdigest(),executed_this_generated_file=False)
            compiler1=prefix+'/BUILT/SCCC1.SCX'
            run('guest-SCCC-self-build-G1',compiler+' '+prefix+'/SRC/sccc.c '+compiler1,timeout=240)
            report['guest_compiler_G1']=dict(sha256=hashlib.sha256(guest.get_bytes(compiler1,'SCCC-G1.scx')).hexdigest())
            run('G1-output-directory','mkdir '+package+'/G1')
            for name in ('BASE.SCL','UPPER.SCL'):
                run('G1-library-'+name,'cp '+package+'/NATIVE/'+name+' '+package+'/G1/'+name)
            generated=package+'/G1/DYNAPP.SCX'
            run('actual-G1-dynamic-compile',compiler1+' '+prefix+'/DYNAPP.C '+generated+
                ' --dynamic '+prefix+'/DYNAPP.json',contains=b'SCCC OK SCX DYNAMIC')
            run('automatic-G1-application-executes',generated)
            check('G0-G1-dynamic-bytes-identical',native==guest.get_bytes(generated,'G1-DYNAPP.scx'))
            before,_=stable_pages(vm,symbol)
            run('repeat-normal-exit',target)
            after,observations=stable_pages(vm,symbol)
            check('normal-exit-page-recovery',before==after,before=before,after=after,observations=observations)
            before,_=stable_pages(vm,symbol)
            run('root-code-readonly-real-page-fault',package+'/NATIVE/DYNFAULT.SCX',expected=14)
            after,observations=stable_pages(vm,symbol)
            check('abnormal-exit-page-recovery',before==after,before=before,after=after,observations=observations)
            run('incompatible-ABI-rejected',package+'/HOST/BADVER.SCX',expected=1,
                contains=b'SCL: dependency or symbol (-4)')
            run('missing-library-rejected',package+'/HOST/MISSING.SCX',expected=1,
                contains=b'SCL: dependency or symbol (-5)')
            run('patch-into-CRT-rejected',package+'/HOST/BADCRT.SCX',expected=1)
            run('SYSTEM-cannot-overwrite-system-root','cp '+compiler+' /SYS/LIB/SCLDR.SCL',expected=1)
            current=guest.get_bytes('/SYS/LIB/SCLDR.SCL','protected-root-after')
            check('protected-root-bytes-preserved',current==(build/'SCLDR.SCL').read_bytes())
            run('legacy-SCX-ls-still-runs','ls '+package+'/NATIVE',contains=b'DYNAPP.SCX')
            vm.hmp('sendkey f1');time.sleep(.3)
            picture=vm.out/'desktop.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture));report['status']='DECLARED_DYNAMIC_STARTUP_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,
                message=str(error),traceback=traceback.format_exc()))
            try:
                picture=vm.out/'failure.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture:report['capture_error']=repr(capture)
            raise
        finally:
            guest.close();report['source_disk_unchanged']=sha(args.data)==source_sha
            report['current_inputs_unchanged']=all(sha(ROOT/name)==digest for name,digest in input_hashes.items())
            persist()
    print(report['status'],flush=True)

if __name__=='__main__':main()
