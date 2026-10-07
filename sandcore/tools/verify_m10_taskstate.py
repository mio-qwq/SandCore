#!/usr/bin/env python3
"""通过公开ABI实测131任务的环境/凭据/异常栈/调试/SIMD与回收。"""
import argparse
import hashlib
import os
from pathlib import Path
import traceback

from m10_failure_snapshot import close_guest
from m10_guest_boot import unique_symbols
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest
from verify_m10_network import Cases, connect_ready, failure_evidence, screenshot
from verify_m10_sessions import desktop_observation, settled_desktop

ROOT = Path(__file__).resolve().parents[1]


def run_disk(boot, data, symbols, out, simd):
    report = dict(status='RUNNING', scope='DECLARED_131_TASK_PRIVATE_STATE_ONLY',
                  cases=[], screenshots=[], source_sha256=sha(data))
    with QemuSession(boot, data, out, 'tcg', True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        cases = Cases(guest, report, symbols)
        try:
            connect_ready(guest, symbols, report)
            source = (ROOT/'tests/m10/M10STATE.C').read_bytes()
            report['native_source_sha256'] = hashlib.sha256(source).hexdigest()
            report['host_simd_scx_sha256'] = sha(simd)
            guest.put_bytes(source, '/TMP/M10STATE.C', 'task-state-source')
            guest.put_bytes(simd.read_bytes(), '/TMP/M10SIMD.SCX', 'host-simd-probe')
            cases.run('native-public-task-state-controller',
                      's3c /TMP/M10STATE.C /TMP/M10STATE.SCX', timeout=360)
            observation = desktop_observation(vm, symbols)
            report['settled_resources'] = settled_desktop(
                guest, symbols, observation['counters']['desktop_io_session'])
            # 冷暖轮各留完整字节和页账。固定131只是实测规模，不能
            # 将此夹具说成无限内存、普通用户授权或整机性能证明。
            for mode in ('environments', 'debug', 'simd'):
                for phase in ('cold', 'warm'):
                    output = cases.run(f'131-{mode}-{phase}', '/TMP/M10STATE.SCX '+mode,
                                       contains=b'state_created=131', timeout=360)
                    assert output.count(b'state_created=131\n') == 1 and b'FAIL ' not in output
                    markers = [b'all state-owned physical pages return',
                               b'131 simultaneous private task identities environments and ready acknowledgements']
                    if mode == 'simd':
                        markers.append(b'all 131 SIMD states preserve XMM7 MXCSR and x87 across real preemption')
                    else:
                        markers.extend([b'all 131 private fault callbacks receive real page faults and retained environments',
                                        b'reused PID has fresh identity environment and no inherited debugger or fault callback'])
                    if mode == 'debug':
                        markers.extend([b'131 real debug targets paused before first instruction with exact 88B contexts',
                                        b'INT3 executes while all 130 sibling targets retain their paused entry and event',
                                        b'real x86 single step resumes exactly one instruction after breakpoint removal'])
                    cases.check(f'{mode}-{phase}-declared-markers', all(m in output for m in markers))
            screenshot(vm, report, 'task-state-desktop')
            report['status'] = 'DECLARED_CASES_PASS'
        except BaseException as error:
            failure_evidence(vm, guest, report, error, symbols)
            raise
        finally:
            try:
                close_guest(guest, report)
            finally:
                report['source_unchanged'] = sha(data) == report['source_sha256']
                checkpoint(vm.out/'taskstate.json', report)
    if not report['source_unchanged']:
        raise AssertionError('只读输入盘改变')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    parser.add_argument('--simd', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) < 2 or len(set(p.resolve(strict=True) for p in args.data)) != len(args.data):
        parser.error('需要Windows Python及两份不同的只读来源盘')
    blob = args.simd.read_bytes()
    if len(blob) < 36 or blob[:8] != b'SCX1MIO\0':
        parser.error('SIMD夹具必须是独立编译的实际SCX')
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='RUNNING', scope='DECLARED_TASK_STATE_NOT_COMPLETE_M10A1', disks=[],
                  remaining=['ordinary permissions and per-session task state reuse',
                             'historical SCX and no-SIMD fallback',
                             'cross-owner window capture and transaction exit/fault ledgers',
                             'real application fairness and HMP input latency tails'])
    checkpoint(args.out/'taskstate-matrix.json', report)
    try:
        symbols = unique_symbols(args.symbols)
        for index, data in enumerate(args.data, 1):
            report['disks'].append(run_disk(args.boot, data, symbols,
                                           args.out/f'disk-{index}', args.simd))
            checkpoint(args.out/'taskstate-matrix.json', report)
        report['status'] = 'DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                      message=str(error), traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'taskstate-matrix.json', report)


if __name__ == '__main__':
    main()
