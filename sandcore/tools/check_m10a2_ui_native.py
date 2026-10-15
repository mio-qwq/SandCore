#!/usr/bin/env python3
"""用既有真实SCCC宿主适配器预检本批完整UI语法，避免以反复客体启动定位语法缺口。"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/m10a2'))
import test_scd_format as backend


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-report', required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--scui-only', action='store_true', help='只预检本次受影响SCUI，不重复既有Apps/ABI输入')
    args = parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.tag):
        parser.error('invalid tag')
    proof_path = (ROOT / args.build_report).resolve()
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    if proof['exit_code'] or proof['source_files_modified_during_build']:
        raise ValueError('需要成功且输入未变的统一构建')
    pinned = {ROOT / item['path']: item['sha256'] for item in proof['inputs']}
    for path, expected in pinned.items():
        if sha(path) != expected:
            raise ValueError('当前输入不匹配构建：' + str(path))
    out = ROOT / 'build/m10a2-ui-native-host' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    report = dict(scope='ACTUAL_SCCC_HOST_ADAPTER_UI_SYNTAX', executes_i386=False,
        executes_generated_compiler=False, guest_compilation_or_execution=False,
        build_report=str(proof_path), build_report_sha256=sha(proof_path),
        adapter_source_sha256=sha(ROOT / 'tests/m10a2/test_scd_format.py'),
        driver_sha256=sha(Path(__file__)), requested_scope='SCUI_ONLY' if args.scui_only else 'FULL_UI_BATCH', cases=[])
    started = time.monotonic()
    status = 1
    try:
        backend.OUT = out
        # 只观察真实前端已有计数；fatal也留数值，下一步只扩实际撞线的表。
        declaration = 'static int type_count,symbol_count,param_count,node_count,fix_count;\n'
        observe = ('fprintf(stderr,"SCCC_HOST_CAPACITY types=%d symbols=%d params=%d nodes=%d fixes=%d\\n",'
                   'type_count,symbol_count,param_count,node_count,fix_count);')
        old_exit = 'static void sc_exit(int n){exit(n);}'
        if old_exit not in backend.ADAPTER or backend.WRAPPER.count('return result;') != 1:
            raise ValueError('真实宿主适配器观察锚点已改变')
        backend.ADAPTER = backend.ADAPTER.replace(old_exit,
            declaration + 'static void sc_exit(int n){' + observe + 'exit(n);}')
        backend.WRAPPER = backend.WRAPPER.replace('return result;', observe + 'return result;')
        compiler = backend.build_sccc('SCCC-current-ui')
        report['emitted_adapter_sha256'] = sha(out / 'SCCC-current-ui.c')
        vfs = out / 'VFS'

        def copy(source, destination):
            target = vfs / destination.upper()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())

        for path in pinned:
            if path.is_relative_to(ROOT / 'user'):
                name = path.relative_to(ROOT / 'user').as_posix()
                copy(path, 'TMP/UICHECK/SRC/' + name)
                if path.parent == ROOT / 'user' and path.suffix == '.H':
                    copy(path, 'SYS/INC/' + name)
                if path.parent == ROOT / 'user/stdlib' and path.suffix == '.h':
                    copy(path, 'SYS/INC/' + path.name)
        for name in ('sclib/SCLIB.H', 'runtime/SCLDR.H'):
            copy(ROOT / 'user' / name, 'SYS/INC/' + Path(name).name)
        for name in ('SCUIDEMO', 'PLATFORMPROBE', 'SCUITEXT', 'AGGCALLEE', 'AGGPROBE'):
            for suffix in ('.C', '.json'):
                copy(ROOT / 'tests/m10a2' / (name + suffix), 'TMP/UICHECK/' + name + suffix)
        copy(ROOT / 'tests/m10a2/AGGABI.H', 'TMP/UICHECK/AGGABI.H')
        for suffix in ('.C','.json'):
            copy(ROOT/'tests/m10a2'/('DEFAULTAPI'+suffix),'TMP/UICHECK/DEFAULTAPI'+suffix)
        (vfs / 'HOME').mkdir(parents=True)
        cases = [('AGGCALLEE', '/TMP/UICHECK/AGGCALLEE.C', '--scl',
                  '/TMP/UICHECK/AGGCALLEE.json', 'SCL'),
                 ('AGGPROBE', '/TMP/UICHECK/AGGPROBE.C', '--dynamic',
                  '/TMP/UICHECK/AGGPROBE.json', 'SCX'),
                 ('SCUI', '/TMP/UICHECK/SRC/scui/SCUI.C', '--scl',
                  '/TMP/UICHECK/SRC/scui/SCUI.json', 'SCL')]
        cases.extend((name, '/TMP/UICHECK/' + name + '.C', '--dynamic',
                      '/TMP/UICHECK/' + name + '.json', 'SCX')
                     for name in ('SCUIDEMO', 'PLATFORMPROBE', 'SCUITEXT'))
        cases.append(('DEFAULTAPI','/TMP/UICHECK/DEFAULTAPI.C','--dynamic',
            '/TMP/UICHECK/DEFAULTAPI.json','SCX'))
        if args.scui_only:
            cases=[case for case in cases if case[0]=='SCUI']
        for name, source, mode, manifest, extension in cases:
            target = '/HOME/' + name + '.' + extension
            command = [str(compiler), '/', str(vfs), source, target, mode, manifest, '0']
            begin = time.monotonic()
            result = backend.run(command, success=False)
            log = out / (name + '.log')
            log.write_text(result.stdout + result.stderr, encoding='utf-8')
            item = dict(name=name, command=command, exit_code=result.returncode,
                wall_seconds=time.monotonic() - begin, log=str(log), log_sha256=sha(log))
            metrics = re.search(r'SCCC_HOST_CAPACITY types=(\d+) symbols=(\d+) params=(\d+) nodes=(\d+) fixes=(\d+)', result.stderr)
            if metrics:
                item['actual_frontend_counts'] = dict(zip(
                    ('types', 'symbols', 'params', 'last_function_nodes', 'fixes'), map(int, metrics.groups())))
            artifact = vfs / target.lstrip('/').upper()
            if artifact.is_file():
                item.update(bytes=artifact.stat().st_size, sha256=sha(artifact))
            report['cases'].append(item)
            print(name, result.returncode, flush=True)
            if result.returncode:
                print(result.stderr[-2500:], flush=True)
                break
        else:
            status = 0
    except BaseException as error:
        diagnostic = traceback.format_exc()
        error_path = out / 'error.log'
        error_path.write_text(diagnostic, encoding='utf-8')
        report['error'] = dict(type=type(error).__name__, message=str(error),
            log=str(error_path), log_sha256=sha(error_path))
        raise
    finally:
        changed = [p.relative_to(ROOT).as_posix() for p, value in pinned.items() if sha(p) != value]
        if changed:
            status = 1
        report.update(exit_code=status, wall_seconds=time.monotonic() - started,
            changed_input_paths=changed)
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
