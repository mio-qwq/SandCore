#!/usr/bin/env python3
"""只用已暂存公开INC编译两个SDK示例；HOST GCC与真实SCCC宿主后端分列。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/m10a2'))
import test_scd_format as backend
sys.path.insert(0, str(ROOT / 'tools'))
import scd_format

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    sdk = args.sdk.resolve(strict=True)
    if not sdk.is_relative_to(ROOT / 'build'):
        raise ValueError('SDK应是本工程build下已暂存的新目录')
    manifest = json.loads((sdk / 'MANIFEST.json').read_text(encoding='utf-8'))
    hashes = json.loads((sdk / 'HASHES.json').read_text(encoding='utf-8'))['files']
    pinned = {}
    for name, row in hashes.items():
        path = (sdk / name).resolve(strict=True)
        if not path.is_relative_to(sdk) or sha(path) != row['sha256'] or path.stat().st_size != row['bytes']:
            raise ValueError('SDK payload/metadata不匹配: ' + name)
        pinned[path] = row['sha256']
    if manifest['profile'] != 'native-preview' or not manifest['inputs_unchanged']:
        raise ValueError('需要来源守恒的native-preview')
    compiler_source = manifest['compiler']['original_generation_source_input_sha256']
    for name, expected in compiler_source.items():
        if name.startswith('user/s3c') or name in ('user/sccc.c','user/COMPILECLI.inc','user/SCAPI.H','user/SCIO.H'):
            path = ROOT / name
            if sha(path) != expected:
                raise ValueError('SCCC当前源码与SDK真G1来源不同: ' + name)
            pinned[path] = expected
    adapter_source = ROOT / 'tests/m10a2/test_scd_format.py'
    pinned[adapter_source] = sha(adapter_source)
    out = ROOT / 'build/sdk-public-compile' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    report = dict(scope='SDK_PUBLIC_HEADERS_AND_TWO_EXAMPLES_COMPILE_ONLY',
        executes_i386=False, guest_compile=False, guest_runtime=False,
        sdk=str(sdk), manifest_sha256=sha(sdk/'MANIFEST.json'), cases=[])
    started = time.monotonic()
    status = 1
    try:
        def command(label, argv):
            begin = time.monotonic()
            result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
            log = out / (label + '.log')
            log.write_text(result.stdout + result.stderr, encoding='utf-8')
            case = dict(name=label, command=argv, exit_code=result.returncode,
                wall_seconds=time.monotonic()-begin, log=str(log), log_sha256=sha(log))
            report['cases'].append(case)
            print(label, result.returncode, flush=True)
            if result.returncode:
                print((result.stdout+result.stderr)[-3500:], flush=True)
                raise RuntimeError(label+' failed')
            return case
        for name in ('EDIT2','UTILITY'):
            source = sdk / 'SYS/SDK/EXAMPLES' / (name+'.C')
            spec = source.with_suffix('.JSON')
            obj = out / (name+'.o')
            artifact = out / 'HOST_GCC' / (name+'.SCX')
            artifact.parent.mkdir(exist_ok=True)
            command(name+'-GCC', ['gcc','-m32','-std=c99','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-builtin','-nostdinc','-fno-pic','-fno-pie',
                '-fno-stack-protector','-fno-asynchronous-unwind-tables','-mno-sse','-mno-mmx',
                '-msoft-float','-mincoming-stack-boundary=2','-mpreferred-stack-boundary=4',
                '-I'+str(sdk/'SYS/INC'),'-x','c','-c',str(source),'-o',str(obj)])
            case = command(name+'-HOST-DYNAMIC', [sys.executable,str(ROOT/'tools/mkdscx.py'),
                str(obj),str(artifact),'--manifest',str(spec)])
            case.update(bytes=artifact.stat().st_size,sha256=sha(artifact),
                format=scd_format.validate_scx(artifact.read_bytes()), source='HOST_GCC_NOT_NATIVE')
        backend.OUT = out
        compiler = backend.build_sccc('SCCC-SDK-adapter')
        report['adapter'] = dict(source_sha256=sha(out/'SCCC-SDK-adapter.c'),
            executable_sha256=sha(compiler), original_adapter_source_sha256=pinned[adapter_source],
            current_compiler_source_sha256={str(p):v for p,v in pinned.items() if p.is_relative_to(ROOT/'user')})
        vfs = out/'VFS'
        # 只把公开INC与独立例子复制到VFS；不给编译器任何应用私有头/原app正文。
        for file in (sdk/'SYS/INC').rglob('*'):
            if file.is_file():
                target = vfs/'SYS/INC'/file.relative_to(sdk/'SYS/INC').as_posix().upper()
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(file.read_bytes())
        for name in ('EDIT2','UTILITY'):
            for suffix in ('.C','.JSON'):
                source = sdk/'SYS/SDK/EXAMPLES'/(name+suffix)
                target = vfs/'SDK'/(name+suffix)
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes())
        (vfs/'HOME').mkdir(parents=True,exist_ok=True)
        for name in ('EDIT2','UTILITY'):
            case = command(name+'-SCCC-HOST-ADAPTER', [str(compiler),'/',str(vfs),
                '/SDK/'+name+'.C','/HOME/'+name+'.SCX','--dynamic','/SDK/'+name+'.JSON','0'])
            artifact = vfs/'HOME'/(name+'.SCX')
            case.update(bytes=artifact.stat().st_size,sha256=sha(artifact),
                format=scd_format.validate_scx(artifact.read_bytes()), source='ACTUAL_SCCC_BACKEND_HOST_ADAPTER_NOT_GUEST')
        status = 0
    except BaseException as error:
        (out/'error.log').write_text(traceback.format_exc(),encoding='utf-8')
        report['error'] = repr(error)
    finally:
        changed = [str(p) for p, expected in pinned.items() if sha(p) != expected]
        if changed:status=1
        report.update(exit_code=status,wall_seconds=time.monotonic()-started,changed_inputs=changed)
        (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return status

if __name__=='__main__':
    raise SystemExit(main())
