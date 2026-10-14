#!/usr/bin/env python3
"""一个Windows双盘VM完成新SCCC原生SCL与编译器自举的定向闭环。"""
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

ROOT=Path(__file__).resolve().parents[1]
SOURCE_NAMES=('sccc.c','s3c.c','SCAPI.H','SCIO.H','COMPILECLI.inc',
              's3c_lex.inc','s3c_pp.inc','s3c_parse.inc','s3c_emit.inc','s3c_scl.inc',
              'runtime/SCLFMT.H','runtime/scl_format.c')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--data',type=Path,default=ROOT/'build/m10a1/sanddata.img')
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    parser.add_argument('--compiler-build-report',type=Path,default=ROOT/'build/m10a2-native-bootstrap/native-build-01.json')
    args=parser.parse_args()
    build=ROOT/'build/m10a2-bootstrap';native=ROOT/'build/m10a2-native-bootstrap'
    compiler_build=json.loads(args.compiler_build_report.read_text(encoding='utf-8'))
    if compiler_build['exit_code'] or compiler_build['compiler']['sha256']!=sha(native/'fs/bin/sccc.scx'):
        raise ValueError('编译器与指定构建记录不一致')
    symbols=unique_symbols(build/'core.sym')
    prefix='/TMP/SCLN-'+secrets.token_hex(3)
    extras={prefix[1:]+'/SCCC0.SCX':(native/'fs/bin/sccc.scx').read_bytes()}
    source_hashes={}
    for name in SOURCE_NAMES:
        blob=(ROOT/'user'/name).read_bytes()
        extras[prefix[1:]+'/SRC/'+name]=blob
        source_hashes['user/'+name]=hashlib.sha256(blob).hexdigest()
    for name in ('NATIVEBASE.C','NATIVEUPPER.C','NATIVEBASE.json','NATIVEUPPER.json'):
        blob=(ROOT/'tests/m10a2'/name).read_bytes()
        extras[prefix[1:]+'/'+name]=blob
        source_hashes['tests/m10a2/'+name]=hashlib.sha256(blob).hexdigest()
    # 一个旧SCX/一个旧SKM仅比较真实输出字节，保持旧模式的入口和回填。
    legacy_c=b'int v[2]={3,4}; static int plus(int x){return x+2;} int main(void){return plus(v[1]);}\n'
    legacy_skm=b'int v=3; int skm_main(void *api){(void)api;return v;}\n'
    extras[prefix[1:]+'/OLD.C']=legacy_c
    extras[prefix[1:]+'/MOD.C']=legacy_skm
    source_sha=sha(args.data)
    report=dict(status='RUNNING',scope='M10A2_SCCC_NATIVE_SCL_AND_SELF_BOOTSTRAP_ONLY',
                cases=[],prefix=prefix,source_data_sha256=source_sha,
                core_sha256=sha(build/'fs/SYS/CORE/CORE.SKM'),
                host_compiler_sha256=sha(native/'fs/bin/sccc.scx'),
                compiler_build_sha256=sha(args.compiler_build_report),
                input_sha256=source_hashes,guest_libraries=[],screenshots=[],
                execution_context='RING3_FROM_EXTERNAL_SYSTEM_SHELL',
                dynamic_SCX_startup=False,full_M10a2=False)
    def persist():
        (args.out/'native.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with QemuSession(build/'sandcore.img',args.data,args.out,args.accel,True,
                     audio=False,memory=256,data_prepare=lambda path:prepare(path,build,extras)) as vm:
        guest=Guest(vm)
        def run(name,command,code=0,contains=None,timeout=150):
            actual,output,wall,cpu=guest.command(command,timeout=timeout)
            okay=actual==code and (contains is None or contains in output)
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',command=command,
                exit_code=actual,expected_exit=code,output=output.decode('utf-8','replace'),
                wall_seconds=wall,qemu_cpu_seconds=cpu))
            persist();print(name+(': PASS' if okay else ': FAIL'),flush=True)
            if not okay:
                raise AssertionError(name)
            return output
        def check(name,okay,**detail):
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL',**detail))
            persist();print(name+(': PASS' if okay else ': FAIL'),flush=True)
            if not okay:
                raise AssertionError(name)
        def libraries(compiler,generation):
            folder=prefix+'/'+generation
            run(generation+'-directory','mkdir '+folder)
            generated=[]
            for lib,source in (('BASE','NATIVEBASE'),('UPPER','NATIVEUPPER')):
                target=folder+'/'+lib+'.SCL'
                run(generation+'-compile-'+lib,compiler+' '+prefix+'/'+source+'.C '+target+
                    ' --scl '+prefix+'/'+source+'.json',contains=b'SCCC OK SCL NATIVE')
                blob=guest.get_bytes(target,generation+'-'+lib+'.scl')
                parsed=validate(blob)
                item=dict(generation=generation,name=lib+'.SCL',bytes=len(blob),
                          sha256=hashlib.sha256(blob).hexdigest(),guest_compiled=True,
                          segments=parsed['segments'],exports=parsed['exports'],
                          imports=parsed['imports'],relocations=len(parsed['relocations']))
                report['guest_libraries'].append(item);persist()
                generated.append(blob)
                run(generation+'-install-'+lib,'cp '+target+' /HOME/M10A2/'+lib+'.SCL')
            run(generation+'-real-SCL-calls','/HOME/M10A2/SCLPROBE.SCX',
                contains=b'SCL_BOOTSTRAP_PASS')
            return generated
        try:
            wait_first_desktop(guest,symbols,report);guest.connect()
            compiler0=prefix+'/SCCC0.SCX'
            first=libraries(compiler0,'G0')
            for mode,suffix in (('', 'SCX'),(' --skm','SKM')):
                source=prefix+('/OLD.C' if suffix=='SCX' else '/MOD.C')
                old=prefix+'/OLD.'+suffix;new=prefix+'/NEW.'+suffix
                run('existing-compiler-'+suffix,'sccc '+source+' '+old+mode)
                run('new-compiler-'+suffix,compiler0+' '+source+' '+new+mode)
                old_blob=guest.get_bytes(old,'old-'+suffix)
                new_blob=guest.get_bytes(new,'new-'+suffix)
                check('legacy-'+suffix+'-bytes-identical',old_blob==new_blob,
                      bytes=len(new_blob),sha256=hashlib.sha256(new_blob).hexdigest())
            # 编译器本身由客体内运行的SCCC生成，然后由此产物再编两库。
            compiler1=prefix+'/SCCC1.SCX'
            run('compiler-self-build-G1',compiler0+' '+prefix+'/SRC/sccc.c '+compiler1,timeout=240)
            compiler_blob=guest.get_bytes(compiler1,'native-compiler-G1.scx')
            entry,load,bss,stack,flags,base,author=struct.unpack_from('<7I',compiler_blob,8)
            check('native-compiler-container',compiler_blob[:8]==b'SCX1MIO\0' and
                  len(compiler_blob)==36+load and load+bss<=0x3d0000 and base==0x400000,
                  bytes=len(compiler_blob),load=load,bss=bss,stack=stack,
                  sha256=hashlib.sha256(compiler_blob).hexdigest())
            second=libraries(compiler1,'G1')
            check('G0-G1-library-bytes-identical',first==second,
                  hashes=[hashlib.sha256(blob).hexdigest() for blob in second])
            # map路径是实际目录；仅此一个提交故障验证失败保留旧主体。
            faildir=prefix+'/FAIL';run('failure-directory','mkdir '+faildir)
            target=faildir+'/BASE.SCL'
            guest.put_bytes(b'old-library-body',target,'old-body')
            run('map-is-directory','mkdir '+target+'.map')
            run('map-failure',compiler1+' '+prefix+'/NATIVEBASE.C '+target+' --scl '+
                prefix+'/NATIVEBASE.json',code=1,contains=b'cannot write SCL RVA map')
            check('map-failure-preserves-old-body',guest.get_bytes(target,'old-after-map')==b'old-library-body')
            badmanifest=prefix+'/BAD.json'
            guest.put_bytes(b'{"unknown":1}',badmanifest,'bad-manifest')
            run('bad-manifest',compiler1+' '+prefix+'/NATIVEBASE.C '+target+' --scl '+badmanifest,
                code=1,contains=b'unknown top-level field')
            check('bad-manifest-preserves-old-body',guest.get_bytes(target,'old-after-bad')==b'old-library-body')
            alias=prefix+'/BASE.SCL.log'
            guest.put_bytes(legacy_c,alias,'alias-source')
            run('log-input-alias-rejected',compiler1+' '+alias+' '+prefix+'/BASE.SCL --scl '+
                prefix+'/NATIVEBASE.json',code=2,contains=b'overlaps an input')
            check('log-input-alias-preserved',guest.get_bytes(alias,'alias-after')==legacy_c)
            before,_=stable_pages(vm,symbols)
            run('native-library-orphan-exit','/HOME/M10A2/SCLPROBE.SCX orphan',
                contains=b'ORPHAN modules-live-at-exit')
            after,observations=stable_pages(vm,symbols)
            check('native-library-exit-page-recovery',before==after,before=before,after=after,
                  stable_observations=observations)
            vm.hmp('sendkey f1');time.sleep(.3)
            picture=vm.out/'desktop.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status']='DECLARED_NATIVE_SCL_AND_G1_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,
                message=str(error),traceback=traceback.format_exc()))
            try:
                picture=vm.out/'failure.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture:
                report['capture_error']=repr(capture)
            raise
        finally:
            guest.close()
            report['source_disk_unchanged']=sha(args.data)==source_sha
            report['current_inputs_unchanged']=all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest
                                                  for name,digest in source_hashes.items())
            persist()
    print(report['status'],flush=True)

if __name__=='__main__':
    main()
