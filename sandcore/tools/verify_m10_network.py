#!/usr/bin/env python3
"""统一阶段的M10双来源盘网络用例；结果只代表本次声明覆盖。

QEMU与管理传输复用私有stdio/Windows管道；以太网对端和应用服务
拥有自己的loopback连接。每项保留退出码、精确字节、耗时和CPU。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import time
import traceback

from m10netpeer import FramePeer
from m10netservices import Services
from m10_guest_jobs import wait_background
from m10_guest_boot import physical_word, unique_symbols, wait_first_desktop
from m10_failure_snapshot import close_guest, failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]

NAMES = ('ip ifconfig ifup ifdown route arp arping ping traceroute netstat nslookup '
         'hostname dnsdomainname ipcalc udhcpc nc wget tftp').split()


class Cases:
    def __init__(self, guest, report, symbols):
        self.guest, self.report, self.symbols = guest, report, symbols

    def clock_observation(self):
        # 独立宿主时间/PIT/服务计数揭示实际计时推进。只读公开标量，
        # 不暂停客体、不读取socket正文、凭据或随机种子；逐字段观察
        # 不宣称原子快照。观察失败单列，不能覆盖原命令错误。
        observation=dict(monotonic=time.monotonic(),scope='NONATOMIC_PUBLIC_CLOCK_AND_SERVICE_COUNTERS')
        try:
            names=('sc_ticks','wm_frames','service_batches','last_service_tick',
                   'desktop_io_bytes','desktop_io_batches','desktop_io_pending','desktop_io_ticks_max')
            observation['counters']={name:physical_word(self.guest.vm,self.symbols[name])
                                     for name in names if name in self.symbols}
        except Exception as error:
            observation['read_error']=repr(error)
        return observation

    def check(self, name, okay, reference=None, **details):
        self.report['cases'].append(dict(name=name, reference=reference, status='PASS' if okay else 'FAIL', **details))
        checkpoint(self.guest.vm.out/'network.json',self.report)
        print(name+(': PASS' if okay else ': FAIL'), flush=True)
        if not okay:
            raise AssertionError(name)

    def run(self, name, script, reference=None, expected=None, contains=None, code=0, timeout=90):
        timing=dict(name=name,script=script,host_command_timeout_seconds=timeout,before=self.clock_observation())
        self.report['active_case']=timing
        checkpoint(self.guest.vm.out/'network.json',self.report)
        try:
            actual, output, transcript, wall, cpu = self.guest.run(script, timeout)
        except BaseException as error:
            timing['after']=self.clock_observation()
            timing['operation_wall_seconds']=timing['after']['monotonic']-timing['before']['monotonic']
            self.report['cases'].append(dict(name=name,reference=reference,status='FAIL',script=script,
                                            error=dict(type=type(error).__name__,message=str(error)),clock_observations=timing))
            checkpoint(self.guest.vm.out/'network.json',self.report)
            raise
        timing['after']=self.clock_observation()
        timing['operation_wall_seconds']=timing['after']['monotonic']-timing['before']['monotonic']
        del self.report['active_case']
        okay = actual == code and (expected is None or expected == output) and (contains is None or contains in output)
        self.check(name, okay, reference, exit_code=actual, expected_exit=code,
                   stdout_hex=output.hex(), expected_hex=expected.hex() if expected is not None else None,
                   contains_hex=contains.hex() if contains is not None else None, script=script,
                   wall_seconds=wall, qemu_cpu_seconds=cpu,clock_observations=timing,
                   transcript=transcript.decode('utf-8', 'replace'))
        return output

    def wait_job(self, name, job, expected_code):
        actual, output, transcript, wall, cpu = wait_background(self.guest, job)
        self.check(name, actual==expected_code and output==b'', 'nc', exit_code=actual,
                   expected_exit=expected_code, stdout_hex=output.hex(), owner='ORIGINAL_INTERACTIVE_SHELL',
                   wall_seconds=wall, qemu_cpu_seconds=cpu, transcript=transcript.decode('utf-8', 'replace'))


def screenshot(vm, report, name):
    # 验收必须驱动真正输入；这一项记录网络用例后的输入/桌面截图，
    # 字体/会话/旧应用完整截图归另一个矩阵，不能混成网络PASS。
    vm.hmp('sendkey esc')
    path = vm.out/(name+'.ppm')
    vm.hmp('screendump "'+path.as_posix()+'"')
    report['screenshots'].append(png_from_ppm(path))


def failure_evidence(vm, guest, report, error, symbols, diagnostics=False):
    report['status'] = 'FAIL_OR_INTERRUPTED'
    report['error'] = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
    failure_snapshot(vm,symbols,report)
    # 先存失败现场再做只读诊断，不先按Esc改变原画面。任何附加诊断
    # 失败只记其自身错误，不能覆盖最初的失败或把部分通过标为完成。
    try:
        if vm.process.poll() is None:
            picture = vm.out/'network-failure.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
    except Exception as capture_error:
        report['failure_screenshot_error'] = str(capture_error)
    if diagnostics and vm.process.poll() is None:
        try:
            code, output, transcript, wall, cpu = guest.run('/TMP/M10NET.SCX diagnostics', 15)
            report['failure_network_snapshot'] = dict(exit_code=code, stdout_hex=output.hex(),
                                                     transcript=transcript.decode('utf-8', 'replace'),
                                                     wall_seconds=wall, qemu_cpu_seconds=cpu)
        except Exception as snapshot_error:
            report['failure_snapshot_error'] = str(snapshot_error)
        # 串口仍可用时保存本次nc原错误，不能只把未监听概括成超时。
        # 只取公开测试生成的两个文件，VM停机时保留各自读失败原因。
        for path in ('/TMP/NCERR','/TMP/NCOUT'):
            try:
                output=guest.get_bytes(path,'failure-'+path.rsplit('/',1)[-1])
                report.setdefault('failure_nc_files',{})[path]=dict(bytes=len(output),sha256=hashlib.sha256(output).hexdigest(),
                                                                   stdout_hex=output.hex())
            except Exception as snapshot_error:
                report.setdefault('failure_nc_file_errors',{})[path]=str(snapshot_error)


def connect_ready(guest,symbols,report):
    guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n',timeout=60,debug=True)
    wait_first_desktop(guest,symbols,report)
    guest.connect()


def run_isolated(boot, data, directory, accel, symbols):
    report = dict(status='RUNNING', scope='ISOLATED_IPV4_SUBSETS_NOT_M10_COMPLETE', cases=[], screenshots=[])
    with FramePeer(directory.with_name(directory.name+'-peer')) as peer:
        with QemuSession(boot, data, directory, accel, True, audio=False,
                         network='socket', network_peer=peer.port, network_capture=True, memory=256) as vm:
            guest = Guest(vm)
            cases = Cases(guest, report,symbols)
            try:
                connect_ready(guest,symbols,report)
                cases.run('native-network-fixture', 's3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX', timeout=240)
                port_source=(ROOT/'tests/m10/M10PORT.C').read_bytes()
                report['port_fixture_sha256']=hashlib.sha256(port_source).hexdigest()
                guest.put_bytes(port_source,'/TMP/M10PORT.C','port-options-source')
                cases.run('native-address-reuse-fixture','s3c /TMP/M10PORT.C /TMP/M10PORT.SCX',timeout=240)
                cases.run('address-reuse-and-old-option-boundaries','/TMP/M10PORT.SCX',contains=b'port_failures=0')
                cases.run('network-startup-snapshot', '/TMP/M10NET.SCX diagnostics')
                cases.run('controlled-DHCP', 'udhcpc -n -t 20', 'udhcpc', contains=b'10.23.0.2', timeout=40)
                for name in NAMES:
                    cases.run('help-'+name, name+' --help', name, contains=name.encode()+b': ')
                cases.run('link-info', 'ip link show', 'ip', contains=b'en0')
                cases.run('address-info', 'ifconfig en0', 'ifconfig', contains=b'10.23.0.2')
                cases.run('hostname-set-query', 'hostname m10-net; hostname', 'hostname', expected=b'm10-net\n')
                cases.run('domain-set-query', 'dnsdomainname m10.test; dnsdomainname', 'dnsdomainname', expected=b'm10.test\n')
                cases.run('calculator-/31', 'ipcalc 192.0.2.10/31', 'ipcalc', contains=b'HOSTMIN=192.0.2.10\nHOSTMAX=192.0.2.11\n')
                cases.run('calculator-invalid-mask', 'ipcalc 192.0.2.10 255.0.255.0', 'ipcalc', expected=b'', code=2)
                cases.run('DNS-controlled-A', 'nslookup host.m10.test', 'nslookup', contains=b'Address: 10.23.0.1\n')
                # 立即第二次同名请求应来自缓存；以对端线缆查询数独立判定。
                before = sum(event['event']=='dns-query' and event.get('name')=='host.m10.test' for event in peer.events)
                cases.run('DNS-cached-A', 'nslookup host.m10.test', 'nslookup', contains=b'Address: 10.23.0.1\n')
                after = sum(event['event']=='dns-query' and event.get('name')=='host.m10.test' for event in peer.events)
                cases.check('DNS-cache-wire-count', before == after and before >= 1, 'nslookup', queries_before=before, queries_after=after)
                cases.run('DNS-negative', 'nslookup absent.m10.test', 'nslookup', code=1)
                cases.run('DNS-timeout', 'nslookup -t 2 drop.m10.test', 'nslookup', code=1)
                cases.run('ICMP-controlled-echo', 'ping -c 2 -i 0 -W 2 10.23.0.1', 'ping', contains=b'received 2')
                cases.run('ICMP-fragmented-echo', 'ping -c 1 -W 4 -s 65000 10.23.0.1', 'ping', contains=b'received 1')
                cases.run('TTL-and-quoted-errors', 'traceroute -q 1 -m 3 -w 2 10.23.1.2', 'traceroute', contains=b'10.23.1.2')
                cases.run('ARP-actual-reply', 'arping -c 2 -w 2 10.23.0.1', 'arping', code=0)
                cases.run('ARP-DAD-conflict', 'arping -D -c 1 -w 2 10.23.0.99', 'arping', code=1)
                cases.run('ARP-table', 'arp -n', 'arp', contains=b'10.23.0.1')
                cases.run('ARP-static-insert', 'arp -s 10.23.0.22 52:54:00:11:22:33; arp -n', 'arp', contains=b'10.23.0.22')
                cases.run('ARP-static-remove', 'arp -d 10.23.0.22', 'arp', expected=b'')
                cases.run('route-insert', 'route add 10.24.0.0/16 gw 10.23.0.1; route -n', 'route', contains=b'10.24.0.0')
                cases.run('route-remove', 'route del 10.24.0.0/16', 'route', expected=b'')
                cases.run('netstat-query', 'netstat -an', 'netstat', contains=b'Proto')
                cases.run('dynamic-sockets-and-ABI', '/TMP/M10NET.SCX many', contains=b'PASS socket pagination and buffer boundary')
                # 原对端60秒短租约会在真实coarse timer到期后撤下地址。
                # 时钟取样增加了真实测试时长，60000B发送曾恰在该状态
                # 返回失败，根本没有发出分片。分片矩阵先明确配置静态
                # 地址，避免把租约到期冒充重组错误；短租约续期/到期
                # 仍是独立必验项，不能靠该静态配置宣称DHCP已完整通过。
                cases.run('static-address-before-fragment-matrix',
                          'ifconfig en0 10.23.0.2 netmask 255.255.255.0 gw 10.23.0.1; ifconfig',
                          'ifconfig',contains=b'10.23.0.2')
                for count in (0, 1, 8193, 60000):
                    cases.run('UDP-reversed-fragments-'+str(count), f'/TMP/M10NET.SCX udp 10.23.0.1 {count}', contains=b'PASS UDP reassembly exact original bytes')
                cases.run('network-info-after-close', '/TMP/M10NET.SCX info', contains=b'sockets=0')
                cases.run('interface-down', 'ifdown en0', 'ifdown', expected=b'')
                cases.run('down-send-error', 'ping -c 1 -W 1 10.23.0.1', 'ping', code=1)
                cases.run('interface-up', 'ifup en0', 'ifup', expected=b'')
                cases.run('DHCP-after-up', 'udhcpc -n -t 20', 'udhcpc', contains=b'10.23.0.2', timeout=40)
                cases.run('static-address', 'ifconfig en0 10.23.0.2 netmask 255.255.255.0 gw 10.23.0.1; ifconfig', 'ifconfig', contains=b'10.23.0.2')
                screenshot(vm, report, 'isolated-network-desktop')
                report['status'] = 'DECLARED_CASES_PASS'
            except BaseException as error:
                failure_evidence(vm, guest, report, error, symbols, diagnostics=True)
                raise
            finally:
                if report['status']=='RUNNING':report['status']='FAIL_OR_INTERRUPTED'
                try:
                    close_guest(guest,report)
                finally:
                    report['source_unchanged']=sha(data)==vm.report['sources']['sanddata']['sha256']
                    checkpoint(vm.out/'network.json',report)
                    peer.expect_disconnect()
    return report


def unused_port(protocol):
    # 只选应用转发端口；释放到QEMU绑定之间的竞争若失败，必须报告
    # 新会话失败，不能把别的宿主服务或控制端口当作回退对端。
    with socket.socket(socket.AF_INET, protocol) as handle:
        handle.bind(('127.0.0.1', 0))
        return handle.getsockname()[1]


def exchange_tcp(port, body):
    deadline = time.monotonic()+12
    while True:
        try:
            handle = socket.create_connection(('127.0.0.1', port), 1)
            break
        except ConnectionRefusedError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.05)
    with handle:
        handle.settimeout(10)
        handle.sendall(body)
        handle.shutdown(socket.SHUT_WR)
        output = bytearray()
        while part := handle.recv(4096):
            output.extend(part)
            if len(output) > 1024*1024:
                raise ValueError('网络程序输出超过本用例预算')
        return bytes(output)


def background_nc(guest, command):
    code, output, _, _ = guest.command(command+' > /TMP/NCOUT 2> /TMP/NCERR & echo M10JOB:$!')
    match = re.search(rb'M10JOB:([0-9]+)', output)
    if code or not match:
        raise AssertionError('无法取得后台作业票据')
    return int(match[1])


def wait_listener(guest, port):
    deadline = time.monotonic()+10
    while time.monotonic() < deadline:
        code, output, _, _, _ = guest.run('netstat -an', 15)
        if not code and re.search(rb':'+str(port).encode()+rb'\b', output):
            return
        time.sleep(.05)
    raise TimeoutError('客体实际socket尚未绑定')


def run_services(boot, data, directory, accel, native, symbols):
    report = dict(status='RUNNING', scope='APPLICATION_SUBSETS_NOT_M10_COMPLETE', cases=[], screenshots=[], native=[])
    with Services(directory.with_name(directory.name+'-services')) as services:
        forward = unused_port(socket.SOCK_STREAM)
        udp_forward = unused_port(socket.SOCK_DGRAM)
        with QemuSession(boot, data, directory, accel, True, audio=False, network='user',
                         host_forwards=[f'tcp:{forward}:17010',f'udp:{udp_forward}:17011'], network_capture=True, memory=256) as vm:
            guest = Guest(vm)
            cases = Cases(guest, report,symbols)
            try:
                connect_ready(guest,symbols,report)
                cases.run('user-backend-DHCP', 'udhcpc -n -t 20', 'udhcpc', contains=b'10.0.2.', timeout=40)
                guest.put_bytes(services.payload, '/TMP/NETINPUT', 'network-input')
                guest.put_bytes(services.payload[:257], '/TMP/NETUDP', 'udp-single-datagram')
                ports = services.ports
                if native:
                    for name in NAMES:
                        cases.run('native-compile-'+name, f's3c /SYS/SRC/net/{name}.c /TMP/N-{name}.SCX', timeout=240)
                        cases.run('native-run-'+name, f'/TMP/N-{name}.SCX --help', contains=name.encode()+b': ')
                        report['native'].append(name)
                cases.run('native-e-identity-fixture', 's3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX', timeout=240)
                for udp in (False, True):
                    port = ports['udp' if udp else 'tcp']
                    path='/TMP/NETUDP' if udp else '/TMP/NETINPUT'
                    body=services.payload[:257] if udp else services.payload
                    cases.run('nc-binary-'+('udp' if udp else 'tcp'), f'nc {"-u -q 2" if udp else ""} -w 5 10.0.2.2 {port} < {path}', 'nc', expected=body)
                cases.run('nc-port-scan', f'nc -z -w 3 10.0.2.2 {ports["tcp"]}', 'nc', expected=b'')
                identity_reference = None
                for program, body, expected, exit_code in [
                    ('cat -', services.payload, services.payload, 0),
                    ('sh', b'echo M10-SH; exit 7\n', b'M10-SH\n', 7),
                    ('/TMP/M10NET.SCX identity', b'', None, 0),
                    ('echo M10-early-exit', b'', b'M10-early-exit\n', 0)]:
                    job = background_nc(guest, f'nc -l -p 17010 -w 5 -e "{program}"')
                    wait_listener(guest,17010)
                    output = exchange_tcp(forward, body)
                    okay = output == expected if expected is not None else b'PASS network SYSTEM child is root NORMAL\n' in output and b'FAIL' not in output
                    cases.check('nc-e-'+program, okay, 'nc', received_bytes=len(output), received_sha256=hashlib.sha256(output).hexdigest(), received_hex=output.hex())
                    if program=='/TMP/M10NET.SCX identity':identity_reference=output
                    cases.wait_job('nc-e-exit-'+program, job, exit_code)
                # 真实对端FIN与短程序EOF竞争，连续重新监听同端口；此前
                # 多次复现过坏释放。逐次核对原字节及原Shell退出结果，
                # 不能只因为最后一次仍能连接就声称中间没有停机或丢尾。
                for attempt in range(1,6):
                    job=background_nc(guest,'nc -l -p 17010 -w 5 -e "/TMP/M10NET.SCX identity"')
                    wait_listener(guest,17010)
                    output=exchange_tcp(forward,b'')
                    cases.check('nc-e-rapid-close-'+str(attempt),identity_reference is not None and output==identity_reference,
                                'nc',received_bytes=len(output),received_sha256=hashlib.sha256(output).hexdigest())
                    cases.wait_job('nc-e-rapid-close-exit-'+str(attempt),job,0)
                job = background_nc(guest, 'nc -u -l -p 17011 -w 5 -e sh')
                wait_listener(guest,17011)
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as handle:
                    handle.settimeout(10)
                    handle.sendto(b'echo M10-UDP; exit 9\n',('127.0.0.1',udp_forward))
                    output=bytearray();proper_source=True
                    # Shell的几次stdout写入可能形成几个UDP报文，不把
                    # 流式程序输出的分块边界假设成一次系统调用。
                    while len(output)<len(b'M10-UDP\n'):
                        part,address=handle.recvfrom(65535);output.extend(part)
                        proper_source=proper_source and address==('127.0.0.1',udp_forward)
                    cases.check('nc-UDP-e-shell',output==b'M10-UDP\n' and proper_source,'nc',received_hex=output.hex())
                cases.wait_job('nc-UDP-e-exit',job,9)
                base = f'http://10.0.2.2:{ports["http"]}'
                for path in ('length', 'chunked', 'close', 'redirect'):
                    cases.run('HTTP-'+path, f'wget -q -T 10 -O /TMP/NETGET {base}/{path}', 'wget', expected=b'')
                    output = guest.get_bytes('/TMP/NETGET', 'http-'+path)
                    cases.check('HTTP-bytes-'+path, output == services.payload, 'wget', bytes=len(output), sha256=hashlib.sha256(output).hexdigest())
                for path in ('truncated', 'ambiguous', 'badchunk', 'oversize'):
                    cases.run('HTTP-reject-'+path, f'wget -q -T 5 -O /TMP/NETGET {base}/{path}', 'wget', code=1)
                    output = guest.get_bytes('/TMP/NETGET', 'preserved-'+path)
                    cases.check('HTTP-preserves-old-'+path, output == services.payload, 'wget', sha256=hashlib.sha256(output).hexdigest())
                cases.run('HTTPS-unimplemented', 'wget -q -O /TMP/NETGET https://host.m10.test/file', 'wget', code=1)
                for name in ('normal', 'duplicate', 'lost-final-ack', 'multiple', 'empty'):
                    cases.run('TFTP-get-'+name, f'tftp -g -r {name} -l /TMP/NETTFTP -t 1 -R 8 10.0.2.2 {ports["tftp"]}', 'tftp', timeout=40)
                    output = guest.get_bytes('/TMP/NETTFTP', 'tftp-'+name)
                    expected = b'' if name=='empty' else services.payload[:16384] if name=='multiple' else services.payload
                    cases.check('TFTP-get-bytes-'+name, output == expected, 'tftp', bytes=len(output), sha256=hashlib.sha256(output).hexdigest())
                for name in ('normal', 'lost-data'):
                    cases.run('TFTP-put-'+name, f'tftp -p -r {name} -l /TMP/NETINPUT -t 1 -R 8 10.0.2.2 {ports["tftp"]}', 'tftp', timeout=40)
                    found = any(event['event']=='tftp-upload' and event.get('name')==name and event.get('sha256')==services.digest for event in services.events)
                    cases.check('TFTP-put-host-digest-'+name, found, 'tftp', expected_sha256=services.digest)
                cases.run('TFTP-remote-error', f'tftp -g -r missing -l /TMP/NETTFTP -t 1 -R 2 10.0.2.2 {ports["tftp"]}', 'tftp', code=1)
                screenshot(vm, report, 'application-network-desktop')
                report['status'] = 'DECLARED_CASES_PASS'
            except BaseException as error:
                failure_evidence(vm, guest, report, error, symbols, diagnostics=True)
                raise
            finally:
                if report['status']=='RUNNING':report['status']='FAIL_OR_INTERRUPTED'
                try:
                    close_guest(guest,report)
                finally:
                    report['source_unchanged']=sha(data)==vm.report['sources']['sanddata']['sha256']
                    checkpoint(vm.out/'network.json',report)
    return report


def run_no_nic(boot, data, directory, accel, symbols):
    report = dict(status='RUNNING', scope='NO_NIC_FALLBACK_ONLY', cases=[], screenshots=[])
    with QemuSession(boot, data, directory, accel, True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        cases = Cases(guest, report,symbols)
        try:
            connect_ready(guest,symbols,report)
            cases.run('native-no-device-fixture', 's3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX', timeout=240)
            cases.run('no-NIC-status-and-error', '/TMP/M10NET.SCX none', contains=b'PASS socket fails clearly without NIC')
            cases.run('no-NIC-computation', 'ipcalc 192.0.2.1/32', 'ipcalc', contains=b'HOSTMAX=192.0.2.1')
            cases.run('no-NIC-tool-error', 'nc -z -w 1 10.0.2.2 12345', 'nc', code=1)
            screenshot(vm, report, 'no-nic-desktop')
            report['status']='DECLARED_CASES_PASS'
        except BaseException as error:
            failure_evidence(vm, guest, report, error, symbols, diagnostics=True)
            raise
        finally:
            if report['status']=='RUNNING':report['status']='FAIL_OR_INTERRUPTED'
            try:
                close_guest(guest,report)
            finally:
                report['source_unchanged']=sha(data)==vm.report['sources']['sanddata']['sha256']
                checkpoint(vm.out/'network.json',report)
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--boot', required=True, type=Path)
    parser.add_argument('--data', required=True, action='append', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--symbols',required=True,type=Path,help='与实际主核匹配的符号；只读等待桌面首帧')
    parser.add_argument('--accel', choices=['tcg', 'whpx'], default='tcg')
    parser.add_argument('--skip-native', action='store_true', help='只用于重复网络调试；不能代替原生编译证据')
    parser.add_argument('--profiles', nargs='+', choices=['isolated', 'applications', 'no-nic'],
                        default=['isolated', 'applications', 'no-nic'], help='调试选择；缺其它profile不能当完整矩阵')
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) < 2 or len(set(path.resolve(strict=True) for path in args.data)) != len(args.data):
        parser.error('需要Windows Python、至少两个不同来源的数据盘')
    args.out.mkdir(parents=True, exist_ok=False)
    symbols=unique_symbols(args.symbols)
    reports = []
    matrix = dict(scope='DECLARED_NETWORK_CASES_ONLY_NOT_WHOLE_M10A1',disks=reports,status='RUNNING',
                  native_required=not args.skip_native,profiles=args.profiles,
                  complete_profiles=set(args.profiles)=={'isolated','applications','no-nic'},
                  symbols_sha256=sha(args.symbols),
                  remaining=['TCP raw fault injection/loss/reorder/window and throughput', 'network OOM/latency/exit accounting',
                             'short DHCP lease renewal/rebinding/expiry with independent clock evidence',
                             'ordinary identity and foreign handles', 'TFTP >32MiB sequence wrap',
                             'link watchdog/reset recovery', 'all other M10a1 contracts'])
    checkpoint(args.out/'network-matrix.json',matrix)
    try:
        for index, data in enumerate(args.data, 1):
            profiles = {}
            reports.append(profiles)
            checkpoint(args.out/'network-matrix.json',matrix)
            if 'isolated' in args.profiles:
                profiles['isolated'] = run_isolated(args.boot, data, args.out/f'disk-{index}-isolated', args.accel,symbols)
                checkpoint(args.out/'network-matrix.json',matrix)
            if 'applications' in args.profiles:
                profiles['applications'] = run_services(args.boot, data, args.out/f'disk-{index}-applications', args.accel, not args.skip_native,symbols)
                checkpoint(args.out/'network-matrix.json',matrix)
            if 'no-nic' in args.profiles:
                profiles['no_nic'] = run_no_nic(args.boot, data, args.out/f'disk-{index}-no-nic', args.accel,symbols)
                checkpoint(args.out/'network-matrix.json',matrix)
        if any(not result['source_unchanged'] for disk in reports for result in disk.values()):
            raise AssertionError('只读网络来源盘发生改变')
        matrix['status']='DECLARED_NETWORK_CASES_PASS'
    except BaseException as error:
        matrix.update(status='FAIL_OR_INCOMPLETE',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'network-matrix.json',matrix)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
