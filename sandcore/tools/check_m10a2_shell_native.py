#!/usr/bin/env python3
"""在隔离P5源码上运行真实SCCC宿主适配器；不冒充客体编译或执行。"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests/m10a2'))
import test_scd_format as backend


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--case', action='append', choices=('SC32API', 'SCUI', 'SCSHELL', 'SCFILEWORK', 'DESKTOP'))
    args = parser.parse_args()
    stage = args.stage.resolve()
    if not stage.is_relative_to((ROOT / 'build').resolve()) or not args.tag.replace('-', '').isalnum():
        parser.error('需要工作区内隔离候选和安全tag')
    proof = json.loads(args.build_report.read_text(encoding='utf-8'))
    if proof.get('exit_code') != 0 or proof.get('changed_source_files') != []:
        raise ValueError('需要成功且源码保持的P5整体构建')
    inputs = proof['inputs_sha256']
    for name, expected in inputs.items():
        if sha(stage / name) != expected:
            raise ValueError('候选源码与构建不一致：' + name)
    out = stage / 'reports' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    backend.ROOT, backend.OUT = stage, out
    compiler = backend.build_sccc('SCCC-shell-check')
    vfs = out / 'VFS'
    for name in inputs:
        source = stage / name
        if not name.startswith('user/') or source.suffix.lower() not in ('.c', '.h', '.inc', '.json'):
            continue
        target = vfs / 'TMP/SHELL/SRC' / name[5:].upper()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        if source.parent in (stage / 'user', stage / 'user/stdlib') and source.suffix.lower() == '.h':
            target = vfs / 'SYS/INC' / source.name.upper()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())
    for name in ('sclib/SCLIB.H', 'runtime/SCLDR.H'):
        (vfs / 'SYS/INC' / Path(name).name).write_bytes((stage / 'user' / name).read_bytes())
    (vfs / 'HOME').mkdir(parents=True)
    report = dict(scope='P5_ACTUAL_SCCC_HOST_ADAPTER', guest_compilation_or_execution=False,
        build_report=str(args.build_report.resolve()), build_report_sha256=sha(args.build_report),
        driver_sha256=sha(Path(__file__)), source_sha256=inputs, cases=[])
    status = 0
    cases = [('SC32API', 'sc32/SC32API', 'SCL'), ('SCUI', 'scui/SCUI', 'SCL'),
        ('SCSHELL', 'scshell/SCSHELL', 'SCL'), ('SCFILEWORK', 'shellworker/SCFILEWORK', 'SCX'),
        ('DESKTOP', 'scshell/desktop/SCSHELL', 'SCX')]
    if args.case:
        cases = [case for case in cases if case[0] in args.case]
    try:
        for name, source, extension in cases:
            target = '/HOME/' + Path(source).name + '.' + extension
            command = [str(compiler), '/', str(vfs), '/TMP/SHELL/SRC/' + source + '.C',
                target, '--scl' if extension == 'SCL' else '--dynamic',
                '/TMP/SHELL/SRC/' + source + '.json', '0']
            started = time.monotonic()
            result = backend.run(command, success=False)
            log = out / (name + '.log')
            log.write_text(result.stdout + result.stderr, encoding='utf-8')
            artifact = vfs / target.lstrip('/')
            item = dict(name=name, exit_code=result.returncode, seconds=time.monotonic() - started,
                command=command, log_sha256=sha(log))
            if artifact.is_file():
                item.update(bytes=artifact.stat().st_size, sha256=sha(artifact))
            report['cases'].append(item)
            print(name, 'EXIT', result.returncode, flush=True)
            if result.returncode:
                print(result.stderr[-2500:], flush=True)
                status = result.returncode
                break
    finally:
        changed = [name for name, value in inputs.items() if sha(stage / name) != value]
        report.update(exit_code=status or bool(changed), changed_source_files=changed)
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
