#!/usr/bin/env python3
"""统一验证阶段的任务分段/真实HMP键到画面测量，不宣布整机达标。

宿主仅用普通三环窗口和已有公开ABI；串口标记与实际像素分别核对。
两来源、完整原生分辨率、不同时运行其它本工具VM，便于前后对照。
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import threading
import time
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case
from m10_guest_jobs import wait_background


def screen(vm, report, label, rectangle):
    path = vm.out/(label+'.ppm')
    vm.hmp('screendump "'+path.as_posix()+'"')
    data = path.read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not match:
        raise ValueError('HMP没有完整PPM')
    width, height = map(int, match.groups())
    pixels = data[match.end():]
    if len(pixels) != width*height*3:
        raise ValueError('HMP像素字节不完整')
    x, y, rw, rh = rectangle
    if min(x, y, rw, rh) < 0 or not rw or not rh or x+rw > width or y+rh > height:
        raise ValueError('客户区参考区域越界')
    reference = b''.join(pixels[((y+row)*width+x)*3:((y+row)*width+x+rw)*3] for row in range(rh))
    record = png_from_ppm(path)
    record.update(reference_rectangle=rectangle, reference_sha256=hashlib.sha256(reference).hexdigest())
    report['screenshots'].append(record)
    return record


def run_disk(boot, data, directory, workers, ticks, samples):
    report = dict(scope='SCHEDULING_DIAGNOSTICS_AND_SYNTHETIC_INPUT_NOT_ALL_APPLICATIONS', status='RUNNING',
                  cases=[], screenshots=[], inputs=[], workers=workers, interval_ticks=ticks,
                  source_sha256=sha(data), fixture_sources=[])
    with QemuSession(boot, data, directory, 'tcg', True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        workload, workload_result, workload_error = None, {}, []
        try:
            guest.connect()
            for name in ('M10LIFE', 'M10INPUT'):
                content = (Path(__file__).resolve().parents[1]/'tests/m10'/(name+'.C')).read_bytes()
                report['fixture_sources'].append(dict(name=name, sha256=hashlib.sha256(content).hexdigest()))
                guest.put_bytes(content, '/TMP/'+name+'.C', 'source-'+name)
                run_case(guest, report, 'native-'+name, f's3c /TMP/{name}.C /TMP/{name}.SCX', timeout=360)
            code, sessions, _, _, _ = guest.run('sessionctl list')
            match = re.search(rb'^([0-9]+)\s+-1\s+-1\s+SYSTEM\s', sessions, re.M)
            if code or not match:
                raise AssertionError('没有外部管理会话')
            run_case(guest, report, 'explicit-external-SYSTEM-desktop', 'sessionctl show '+match[1].decode())
            # 只在本VM明确外部管理切换之后显示；普通mio桌面没有SYSTEM窗口。
            position = len(guest.console)
            code, output, _, _ = guest.command('/TMP/M10INPUT.SCX & echo M10INPUT_JOB:$!')
            job = re.search(rb'M10INPUT_JOB:([0-9]+)', output)
            if code or not job:
                raise AssertionError('输入窗口作业未创建')
            ready = guest.wait(rb'M10INPUT_CLIENT x=([0-9]+) y=([0-9]+) w=([0-9]+) h=([0-9]+)\r?\n', position, 30)
            x, y, width, height = map(int, ready.groups())
            # 仅比较数字卡片，排除系统时钟/鼠标等与输入无关的像素。
            rectangle = [x+32, y+170, min(520, width-64), 30]
            vm.hmp('mouse_move -2000 -2000')
            baseline = screen(vm, report, 'input-initial', rectangle)
            last_digest = baseline['reference_sha256']
            start = len(guest.console)
            command = f'/TMP/M10LIFE.SCX fair {workers} {ticks}'

            def run_workload():
                try:
                    code, output, wall, cpu = guest.command(command, timeout=1200)
                    metrics = {}
                    for key, value in re.findall(rb'^([a-z_]+)=(-?[0-9]+)\r?$', output, re.M):
                        metrics.setdefault(key.decode(), []).append(int(value))
                    workload_result.update(command=command, exit_code=code, stdout_hex=output.hex(),
                                           metrics=metrics, wall_seconds=wall, qemu_cpu_seconds=cpu)
                except BaseException as error:
                    workload_error.append(error)

            workload = threading.Thread(target=run_workload, name='M10-compute-load')
            workload.start()
            guest.wait(rb'WORKERS_READY\r?\n', start, 900)
            keys = 'abcdefghijklmnoprstuvwxyz'
            for index in range(samples):
                key = keys[index % len(keys)]
                cursor = len(guest.console)
                began = time.monotonic()
                vm.hmp('sendkey '+key)
                acknowledged = guest.wait(rb'M10INPUT count='+str(index+1).encode()+rb' key=([0-9]+) tick=([0-9]+)\r?\n', cursor, 10)
                painted = time.monotonic()
                if int(acknowledged[1]) != ord(key):
                    raise AssertionError('HMP按键被错路由或重复')
                changed = None
                for attempt in range(3):
                    changed = screen(vm, report, f'input-{index+1:02d}-{attempt}', rectangle)
                    if changed['reference_sha256'] != last_digest:
                        break
                seen = time.monotonic()
                if changed['reference_sha256'] == last_digest:
                    raise AssertionError('串口已确认按键，实际数字区域却未改变')
                report['inputs'].append(dict(index=index+1, key=key, guest_tick=int(acknowledged[2]),
                                             host_send_monotonic=began, paint_ack_seconds=painted-began,
                                             visible_snapshot_seconds=seen-began,
                                             reference_sha256=changed['reference_sha256']))
                last_digest = changed['reference_sha256']
            # 生命周期夹具的起始快照包含这个窗口任务。必须等它完成
            # 回收核对再关窗口，否则起始5任务/结束4任务会被误判泄漏。
            # 前后测量都保持同一批活跃任务与相同客户区画面。
            workload.join(1200)
            if workload.is_alive():
                raise TimeoutError('计算负载没有结束')
            if workload_error:
                raise workload_error[0]
            report['workload'] = workload_result
            metrics = workload_result['metrics']
            if workload_result['exit_code'] or b'FAIL ' in bytes.fromhex(workload_result['stdout_hex']) or metrics.get('created') != [workers] or metrics.get('dispatch_zero') != [0] or metrics.get('running_sample_zero') != [0]:
                raise AssertionError('输入负载期间的完整计算批次失败或有任务未得到CPU')
            vm.hmp('sendkey esc')
            guest.wait(rb'M10INPUT_DONE\r?\n', ready.end(), 10)
            code, output, transcript, wall, cpu = wait_background(guest, int(job[1]))
            report['cases'].append(dict(name='input-window-actual-exit', status='PASS' if code==0 and output==b'' else 'FAIL',
                                        exit_code=code, stdout_hex=output.hex(), transcript=transcript.decode('utf-8', 'replace'),
                                        wall_seconds=wall, qemu_cpu_seconds=cpu))
            if code or output:
                raise AssertionError('原交互Shell的窗口后台作业退出失败')
            ordered = sorted(item['paint_ack_seconds'] for item in report['inputs'])
            report['paint_ack_statistics'] = dict(samples=len(ordered), minimum=ordered[0], maximum=ordered[-1],
                                                  p50=ordered[math.ceil(.5*len(ordered))-1],
                                                  p95=ordered[math.ceil(.95*len(ordered))-1],
                                                  scope='This finite sample only; includes host injection/UART observation')
            report['status'] = 'MEASUREMENTS_AND_DECLARED_CORRECTNESS_COLLECTED_NOT_PERFORMANCE_PASS'
        except BaseException as error:
            report['status'] = 'FAIL_OR_INTERRUPTED'
            report['error'] = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
            try:
                if vm.process.poll() is None:
                    path = vm.out/'scheduling-failure.ppm'
                    vm.hmp('screendump "'+path.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(path))
            except Exception as capture_error:
                report['failure_screenshot_error'] = str(capture_error)
            raise
        finally:
            # 控制线程若仍在等客体，先让本Guest的等待明确失败并join，
            # 再停止读者/心跳、最后由VM拥有者关闭管道，避免先关底层
            # pipe使仍读取它的线程掩盖最初失败。不操作其它QEMU。
            if workload is not None and workload.is_alive():
                cancellation = RuntimeError('本次测量已中断')
                guest.errors.append(cancellation)
                guest.client.fail(cancellation)
                with guest.condition:
                    guest.condition.notify_all()
                workload.join(20)
                if workload.is_alive():
                    report['workload_cleanup_error'] = '测量线程未停止'
            original_error = sys.exc_info()[1]
            try:
                try:
                    guest.close()
                except Exception as close_error:
                    report['guest_cleanup_error'] = str(close_error)
                    if original_error is None:
                        raise
                    original_error.add_note('测量会话关闭同时失败：' + str(close_error))
            finally:
                report['source_unchanged'] = sha(data)==report['source_sha256']
                (vm.out/'scheduling.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if not report['source_unchanged']:
            raise AssertionError('原输入盘被改动')
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=131)
    parser.add_argument('--ticks', type=int, default=6000)
    parser.add_argument('--samples', type=int, default=16)
    parser.add_argument('--against', type=Path, help='已有同配置测量的scheduling-matrix.json')
    args = parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(p.resolve(strict=True) for p in args.data))!=len(args.data):
        parser.error('需要Windows与两个不同来源数据盘')
    if not 65<=args.workers<=16384 or not 100<=args.ticks<=60000 or not 1<=args.samples<=128:
        parser.error('超出本次诊断证据预算')
    args.out.mkdir(parents=True, exist_ok=False)
    reports, comparison_error = [], None
    try:
        for index, data in enumerate(args.data, 1):
            reports.append(run_disk(args.boot, data, args.out/f'disk-{index}', args.workers, args.ticks, args.samples))
        if args.against:
            before = json.loads(args.against.read_text(encoding='utf-8'))['disks']
            if len(before)!=len(reports):
                raise AssertionError('前后盘数量不一致')
            for previous, current in zip(before, reports):
                same = all(previous[key]==current[key] for key in ('workers', 'interval_ticks', 'fixture_sources'))
                same = same and [(r['key'],r['reference_sha256']) for r in previous['inputs']]==[(r['key'],r['reference_sha256']) for r in current['inputs']]
                current['matching_fixture_parameters_and_real_pixels'] = same
                if not same:
                    raise AssertionError('前后实际参考像素/参数/夹具不相同')
    except BaseException as error:
        comparison_error = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
        raise
    finally:
        matrix = dict(scope='MEASUREMENTS_NOT_WHOLE_SYSTEM_PERFORMANCE_PASS', disks=reports,
                      status='COLLECTED' if len(reports)==len(args.data) and comparison_error is None else 'FAIL_OR_INCOMPLETE',
                      error=comparison_error,
                      against=str(args.against) if args.against else None,
                      remaining=['real Notes/terminal/audio/network and scale scenarios',
                                 'complete scheduling sidecar and page ownership regressions',
                                 'all other M10a1 requirements'])
        (args.out/'scheduling-matrix.json').write_text(json.dumps(matrix, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
