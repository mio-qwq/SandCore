#!/usr/bin/env python3
"""隔离线缆真实TCP丢包/窗口/乱序/半关闭/RST声明矩阵；不冒充完整网络验收。"""
import argparse
import hashlib
import os
from pathlib import Path
import time
import traceback

from m10tcppeer import TcpFaultPeer, UPSTREAM
from m10_guest_boot import unique_symbols
from m10_failure_snapshot import close_guest
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest
from verify_m10_network import Cases, connect_ready, failure_evidence, screenshot

ROOT=Path(__file__).resolve().parents[1]


def run_disk(boot,data,symbols,out):
    report=dict(status='RUNNING',scope='DECLARED_RAW_TCP_FAULTS_ONLY',source_sha256=sha(data),cases=[],screenshots=[])
    with TcpFaultPeer(out.with_name(out.name+'-peer')) as peer:
        with QemuSession(boot,data,out,'tcg',True,audio=False,network='socket',network_peer=peer.port,
                         network_capture=True,memory=256) as vm:
            guest=Guest(vm);cases=Cases(guest,report,symbols)
            try:
                connect_ready(guest,symbols,report)
                source=(ROOT/'tests/m10/M10TCP.C').read_bytes()
                report['fixture_sha256']=hashlib.sha256(source).hexdigest()
                guest.put_bytes(source,'/TMP/M10TCP.C','tcp-raw-source')
                cases.run('native-TCP-fault-fixture','s3c /TMP/M10TCP.C /TMP/M10TCP.SCX',timeout=240)
                cases.run('native-public-network-diagnostic','s3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX',timeout=240)
                cases.run('isolated-static-interface','ifconfig en0 10.23.0.2 netmask 255.255.255.0 gw 10.23.0.1; ifconfig',contains=b'10.23.0.2')
                cases.run('raw-loss-window-reorder-half-close','/TMP/M10TCP.SCX exchange',contains=b'tcp_fault_failures=0',timeout=90)
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    wire=peer.summary();flow=[c for c in wire['connections'] if c['port']==7010]
                    if len(flow)==1 and flow[0]['final_ack']:break
                    guest.stop.wait(.05)
                if len(flow)!=1:raise AssertionError('missing unique TCP exchange on wire')
                connection=flow[0]
                cases.check('raw-independent-wire-evidence',connection['syns']>=2 and connection['established']
                    and connection['dropped_data_repeated'] and connection['window_reopened']
                    and connection['upstream_bytes']==len(UPSTREAM)
                    and connection['upstream_sha256']==hashlib.sha256(UPSTREAM).hexdigest()
                    and connection['half_closed'] and connection['final_ack']
                    and not connection['unexpected_guest_reset'],connection=connection)
                cases.run('raw-refused-SYN','/TMP/M10TCP.SCX refused',contains=b'tcp_fault_failures=0')
                cases.run('raw-established-reset','/TMP/M10TCP.SCX reset',contains=b'tcp_fault_failures=0')
                cases.run('logical-sockets-after-close','netstat -an',contains=b'Proto')
                report['wire_summary']=peer.summary()
                cases.check('raw-reset-wire-evidence',sum(c['port']==7011 and c['refused'] for c in report['wire_summary']['connections'])==1
                    and sum(c['port']==7012 and c['reset_sent'] for c in report['wire_summary']['connections'])==1)
                screenshot(vm,report,'TCP-raw-faults-desktop')
                report['status']='DECLARED_CASES_PASS'
            except BaseException as error:
                report['wire_summary']=peer.summary()
                failure_evidence(vm,guest,report,error,symbols,diagnostics=True)
                raise
            finally:
                try:close_guest(guest,report)
                finally:
                    peer.expect_disconnect();report['source_unchanged']=sha(data)==report['source_sha256']
                    checkpoint(vm.out/'tcp-faults.json',report)
        if not report['source_unchanged']:raise AssertionError('read-only source changed')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(p.resolve(strict=True) for p in args.data))!=len(args.data):
        parser.error('需要Windows Python及两份不同的只读来源盘')
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='DECLARED_RAW_TCP_NOT_FULL_NETWORK',disks=[],
                remaining=['full congestion/long-tail throughput and persist timing',
                           'socket OOM/exit/cross-owner generations and complete page ledger',
                           'driver reset/link/DMA wrap stress and full historical compatibility'])
    checkpoint(args.out/'tcp-faults-matrix.json',report)
    try:
        symbols=unique_symbols(args.symbols)
        for n,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,symbols,args.out/f'disk-{n}'))
            checkpoint(args.out/'tcp-faults-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:checkpoint(args.out/'tcp-faults-matrix.json',report)


if __name__=='__main__':main()
