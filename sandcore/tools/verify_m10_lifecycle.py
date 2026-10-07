#!/usr/bin/env python3
"""统一阶段的公开三环生命周期探针，仅报告本次声明用例。

双来源盘分别客体编译，再实际并发/退出/复用/耗尽。JSON只宣称列出
用例的结果，调试/SIMD/真实GUI/输入长尾等合同仍须独立完成。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm


def run_case(guest, report, name, command, markers=(), timeout=240):
    code, output, transcript, wall, cpu = guest.run(command, timeout)
    okay = code == 0 and b'FAIL ' not in output and all(marker in output for marker in markers)
    # 相同键可来自两次batch。保留有序数列和全部原字节，不把后一次
    # 更好值覆盖前一次失败/低谷，也不由这些PIT数值生成伪造的FPS。
    metrics = {}
    for key, value in re.findall(rb'^([a-z_]+)=(-?[0-9]+)\r?$', output, re.M):
        metrics.setdefault(key.decode('ascii'), []).append(int(value))
    case = dict(name=name, command=command, status='PASS' if okay else 'FAIL',
                exit_code=code, stdout_hex=output.hex(), stdout_sha256=hashlib.sha256(output).hexdigest(),
                metrics=metrics, wall_seconds=wall, qemu_cpu_seconds=cpu,
                transcript=transcript.decode('utf-8', 'replace'))
    report['cases'].append(case)
    print(name+(': PASS' if okay else ': FAIL'), flush=True)
    if not okay:
        raise AssertionError(name)
    return case


def run_disk(boot, data, directory, accel, exhaust):
    report = dict(status='RUNNING', scope='DECLARED_LIFECYCLE_CASES_NOT_WHOLE_M10A1',
                  cases=[], screenshots=[], source_data=str(data.resolve()), source_sha256=sha(data))
    with QemuSession(boot, data, directory, accel, True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        try:
            guest.connect()
            run_case(guest, report, 'native-lifecycle-fixture', 's3c /SYS/TEST/M10LIFE.C /TMP/M10LIFE.SCX', timeout=360)
            run_case(guest, report, 'native-global-resource-fixture', 's3c /SYS/TEST/M10RES.C /TMP/M10RES.SCX', timeout=360)
            run_case(guest, report, 'native-windows-and-capture-pages', '/TMP/M10RES.SCX graphics',
                     (b'windows=18', b'PASS live immutable snapshots exceed eight and 64MiB with exact pixel bytes',
                      b'PASS explicit window capture and heap pages restore exact PF baseline'))
            run_case(guest, report, '12-concurrent-write-transactions', '/TMP/M10RES.SCX transactions',
                     (b'transactions=12', b'PASS alternating commit abort returns exact binary targets'))
            run_case(guest, report, '131-simultaneous-tasks-jobs-private-pipes', '/TMP/M10LIFE.SCX many 131',
                     (b'created=131', b'PASS paged query includes every live child with same generation',
                      b'PASS all owned job tickets remain live simultaneously',
                      b'PASS old CPUINFO exact 64 words and ignored registers',
                      b'PASS owned tasks and task sidecar pages return to baseline'))
            run_case(guest, report, 'create-failure-rollback', '/TMP/M10LIFE.SCX rollback',
                     (b'PASS missing executable and invalid flags roll back without page growth',))
            cycle = run_case(guest, report, '131-reclaim-recreate-and-stale-tickets', '/TMP/M10LIFE.SCX cycle 131',
                             (b'PASS reused PID has a fresh generation', b'PASS reaped job ticket cannot bind to reused task',
                              b'PASS old generation cannot kill new child'))
            if cycle['metrics'].get('created') != [131, 131]:
                cycle['status'] = 'FAIL'
                raise AssertionError('cycle did not create both complete batches')
            run_case(guest, report, '131-compute-workers-PIT-and-dispatches', '/TMP/M10LIFE.SCX fair 131 600',
                     (b'dispatch_zero=0', b'running_sample_zero=0',
                      b'PASS all compute workers received CPU in measured interval'))
            if exhaust:
                run_case(guest, report, 'real-memory-exhaustion-rollback-and-recreate', '/TMP/M10LIFE.SCX exhaust',
                         (b'PASS real child creation failed before fixture budget',
                          b'PASS repeated allocation failures preserve owned objects and physical pages',
                          b'PASS old generation cannot kill new child'), timeout=1800)
            # 真实HMP输入与完整screendump存档。这一组只有运行后的桌面
            # 画面证据，不能冒充负载过程中输入长尾/隐藏零绘制验证。
            vm.hmp('sendkey esc')
            picture = vm.out/'lifecycle-desktop.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status'] = 'DECLARED_CASES_PASS'
        except BaseException as error:
            report['status'] = 'FAIL_OR_INTERRUPTED'
            report['error'] = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
            # 启动/编译/并发失败同样保留客体原画面；不先发Esc改变故障现场。
            # 截图失败只记诊断，不能覆盖原始失败或将中断标为成功。
            try:
                if vm.process.poll() is None:
                    picture = vm.out/'lifecycle-failure.ppm'
                    vm.hmp('screendump "'+picture.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture_error:
                report['failure_screenshot_error'] = str(capture_error)
            raise
        finally:
            if report['status']=='RUNNING':
                report['status']='FAIL_OR_INTERRUPTED'
            try:
                guest.close()
            finally:
                report['source_unchanged'] = sha(data)==report['source_sha256']
                (vm.out/'lifecycle.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if not report['source_unchanged']:
            raise AssertionError('source disk changed')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--boot', required=True, type=Path)
    parser.add_argument('--data', required=True, action='append', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--accel', choices=['tcg', 'whpx'], default='tcg')
    parser.add_argument('--skip-exhaust', action='store_true', help='只用于重测；不能代替资源耗尽验收')
    args = parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(path.resolve(strict=True) for path in args.data))!=len(args.data):
        parser.error('需要Windows Python、至少两个不同来源的数据盘')
    args.out.mkdir(parents=True, exist_ok=False)
    reports = []
    try:
        for index, data in enumerate(args.data, 1):
            reports.append(run_disk(args.boot, data, args.out/f'disk-{index}-lifecycle', args.accel, not args.skip_exhaust))
    finally:
        matrix = dict(scope='DECLARED_LIFECYCLE_CASES_ONLY_NOT_WHOLE_M10A1', disks=reports,
                      status='DECLARED_CASES_PASS' if len(reports)==len(args.data) else 'FAIL_OR_INCOMPLETE',
                      exhaustion_required=not args.skip_exhaust,
                      remaining=['debug/SIMD/fault/credentials/session/permission reuse',
                                 'cross-owner and exit/fault window/capture/transaction reclamation',
                                 'complete physical page ownership and retained cache accounting',
                                 'real applications and load-time HMP input latency tails',
                                 'matching baseline CPU and frame comparisons', 'all other M10a1 contracts'])
        (args.out/'lifecycle-matrix.json').write_text(json.dumps(matrix, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
