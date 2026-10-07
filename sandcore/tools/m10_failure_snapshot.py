#!/usr/bin/env python3
"""只读保存失败现场的公开计数；不依赖已失联的管理串口。"""
import sys
import time


def failure_snapshot(vm, symbols, report):
    if 'failure_kernel_snapshot' in report or vm.process.poll() is not None:
        return
    snapshot = dict(scope='READ_ONLY_PUBLIC_COUNTERS_NO_CREDENTIALS_OR_UART_PAYLOAD', samples=[])
    report['failure_kernel_snapshot'] = snapshot
    running = False
    try:
        snapshot['qmp_status'] = vm.qmp('query-status')
        running = snapshot['qmp_status']['running']
        # 暂停只保证同一次样本自洽；恢复后再采样，区分停滞与仍在推进。
        # 只读唯一符号的标量和UART队列头，不读凭据、随机种子或收发正文。
        for index in range(2 if running else 1):
            vm.qmp('stop')
            sample = dict(index=index, registers=vm.hmp('info registers'), counters={})
            snapshot['samples'].append(sample)
            for name in ('boot_stage','sc_ticks','wm_frames','fs_faulted','current',
                         'kernel_pending','ready_head','ready_tail','wake_head','wake_tail',
                         'deferred_head','deferred_tail','cleanup_head','cleanup_tail',
                         'wait_count','render_hold','render_copy_ticks','idle_dispatches',
                         'desktop_io_bytes','desktop_io_batches','desktop_io_ticks_max',
                         'desktop_io_pending','desktop_io_session',
                         'last_sequence','last_rx','tx_size','tx_position','received',
                         'console_head','console_tail','cached_size'):
                if name in symbols:
                    sample['counters'][name] = vm.hmp(f'xp /1wx 0x{symbols[name]:08x}')
            if 'ata_diagnostic' in symbols:
                sample['counters']['ata_diagnostic'] = vm.hmp(f'xp /16wx 0x{symbols["ata_diagnostic"]:08x}')
            if 'sc_net_free_fault' in symbols:
                sample['counters']['sc_net_free_fault'] = vm.hmp(f'xp /12wx 0x{symbols["sc_net_free_fault"]:08x}')
            if 'serial_ports' in symbols:
                for port, offset in (('debug',0),('management',8232)):
                    sample['counters']['uart_'+port] = vm.hmp(f'xp /10wx 0x{symbols["serial_ports"]+offset:08x}')
            if running:
                vm.qmp('cont')
                if not index:
                    time.sleep(.2)
    except Exception as error:
        snapshot['diagnostic_error'] = repr(error)
    finally:
        if running and vm.process.poll() is None:
            try:
                if not vm.qmp('query-status')['running']:
                    vm.qmp('cont')
            except Exception as error:
                snapshot['resume_error'] = repr(error)


def close_guest(guest, report):
    """清理错误单列；原用例失败不能被心跳线程的后续错误覆盖。"""
    original = sys.exc_info()[1]
    try:
        guest.close()
    except Exception as error:
        report.setdefault('guest_cleanup_errors',[]).append(repr(error))
        if original is None:
            report['status'] = 'FAIL_OR_INTERRUPTED'
            raise
        original.add_note('管理连接清理同时失败：'+str(error))
