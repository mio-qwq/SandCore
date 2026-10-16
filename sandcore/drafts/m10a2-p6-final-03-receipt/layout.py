"""Root显式执行的新i386布局入口；默认只写计划，不调用GCC/WSL/VM。"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from bindings import Binding, HERE, ROOT, HOST, HOST_SHA, LAYOUT, OLD_LAYOUT, LAYOUT_ENTRY, LAYOUT_ENTRY_SHA, load, pin, rel, require, save, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--native-report-sha256')
    parser.add_argument('--refresh-report-sha256')
    parser.add_argument('--out', type=Path, default=LAYOUT.parent)
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / 'build') and out.name.startswith('m10a2-p6-gui-coherent-layout-')
            and out != OLD_LAYOUT.parent.resolve() and not out.exists(), '新layout必须用不存在的新目录，不能复用95旧凭证')
    require(sha(HOST) == HOST_SHA and sha(LAYOUT_ENTRY) == LAYOUT_ENTRY_SHA, '固定HOST09/旧布局提取入口改变')
    root = ROOT.as_posix()
    wsl_root = '/mnt/' + root[0].lower() + '/' + root[3:]
    command = ['wsl.exe', 'sh', '-lc', 'cd ' + shlex.quote(wsl_root)
               + ' && PYTHONDONTWRITEBYTECODE=1 python3 ' + shlex.quote(rel(LAYOUT_ENTRY))
               + ' --out ' + shlex.quote(rel(out))]
    if not args.execute:
        save(HERE / 'LAYOUT-PLAN.json', dict(schema=1, status='PREPARED_NOT_LAYOUT_COMPILED',
            command=command, out=rel(out), host_report=pin(HOST),
            required_before_execution=['Root explicit native04 PASS', 'native04 complete SHA', 'outer refresh04 complete SHA'],
            old_layout_reused=False, build_started=False, VM_started=False))
        print('LAYOUT_PREPARED_NOT_COMPILED')
        return
    bound = Binding(args.native_report_sha256, args.refresh_report_sha256)
    bound.checked.read(LAYOUT_ENTRY, LAYOUT_ENTRY_SHA)
    extractor = ROOT / 'tools/m10a2_shell_inspect.py'
    require(rel(extractor) in bound.recorded, 'HOST09没有固定实际布局提取器')
    bound.checked.read(extractor, bound.recorded[rel(extractor)])
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    started = time.monotonic()
    completed = subprocess.run(command, cwd=ROOT, env=env, capture_output=True)
    out.mkdir(parents=True, exist_ok=True)
    log = out / 'wrapper.log'
    require(not log.exists(), '不覆盖布局wrapper日志')
    log.write_bytes(completed.stdout + completed.stderr)
    execution = dict(command=command, exit_code=completed.returncode,
                     wall_seconds=time.monotonic() - started, log=pin(log),
                     scope='HOST_I386_LAYOUT_COMPILATION_ONLY_NOT_GUEST')
    if completed.returncode:
        save(out / 'wrapper-failure.json', dict(status='FAIL_LAYOUT_COMPILE_NOT_DELIVERABLE', execution=execution))
        raise RuntimeError('实际新layout提取失败；保留输出，不覆盖旧凭证')
    proof_path = out / 'layout-proof.json'
    proof = load(proof_path)
    require(proof['headers'] == {name: row['sha256'] for name, row in bound.roles.items()}, '实际新layout不是native04/HOST09的结构头')
    # 只给这个刚实编的新凭证增加来源绑定，不改95旧proof或它的历史头摘要。
    proof['native_source_binding'] = bound.metadata
    proof['wrapper_execution'] = execution
    save(proof_path, proof)
    bound.layout(proof_path, sha(proof_path))
    require(not bound.checked.unchanged(), '实际新layout生成期间固定输入改变')
    print('ACTUAL_NEW_LAYOUT_READY ' + str(proof_path) + ' SHA256=' + sha(proof_path))


if __name__ == '__main__':
    main()
