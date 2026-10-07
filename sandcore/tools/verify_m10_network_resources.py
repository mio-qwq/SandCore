#!/usr/bin/env python3
"""正式ABI的跨所有者权限/隐藏普通会话收发与真实socket内存耗尽页账。"""
import argparse
import hashlib
import os
from pathlib import Path
import traceback

from m10netpeer import FramePeer
from m10_guest_boot import unique_symbols
from m10_failure_snapshot import close_guest
from m10_verification_report import checkpoint
from scserial import QemuSession,sha
from verify_m9 import Guest
from verify_m10_network import Cases,connect_ready,failure_evidence,screenshot
from verify_m10_sessions import settled_desktop,desktop_observation

ROOT=Path(__file__).resolve().parents[1]


def run_disk(boot,data,symbols,out):
    report=dict(status='RUNNING',scope='DECLARED_NETWORK_PERMISSION_AND_REAL_OOM_ONLY',cases=[],screenshots=[],source_sha256=sha(data))
    with FramePeer(out.with_name(out.name+'-peer')) as peer:
        with QemuSession(boot,data,out,'tcg',True,audio=False,network='socket',network_peer=peer.port,
                         network_capture=True,memory=256) as vm:
            guest=Guest(vm);cases=Cases(guest,report,symbols)
            try:
                connect_ready(guest,symbols,report)
                source=(ROOT/'tests/m10/M10NRES.C').read_bytes()
                report['fixture_sha256']=hashlib.sha256(source).hexdigest()
                guest.put_bytes(source,'/TMP/M10NRES.C','network-resource-source')
                cases.run('native-network-resource-fixture','s3c /TMP/M10NRES.C /TMP/M10NRES.SCX',timeout=240)
                cases.run('native-public-network-diagnostics','s3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX',timeout=240)
                cases.run('static-isolated-interface','ifconfig en0 10.23.0.2 netmask 255.255.255.0 gw 10.23.0.1; ifconfig',contains=b'10.23.0.2')
                observation=desktop_observation(vm,symbols)
                report['settled_resources']=settled_desktop(guest,symbols,observation['counters']['desktop_io_session'])
                cases.run('ordinary-hidden-session-and-cross-owner-permissions','/TMP/M10NRES.SCX permissions',contains=b'network_resource_failures=0',timeout=120)
                exchanges=[e for e in peer.events if e['event']=='udp-echo' and e['bytes']==18]
                cases.check('ordinary-hidden-network-real-wire',len(exchanges)==1 and exchanges[0]['sha256']==hashlib.sha256(b'hidden-mio-network').hexdigest(),events=exchanges)
                cases.run('real-socket-exhaustion-rollback-and-PF-recovery','/TMP/M10NRES.SCX exhaust',contains=b'network_resource_failures=0',timeout=1800)
                cases.run('post-exhaustion-actual-peer-echo','ping -c 1 -W 2 10.23.0.1',contains=b'received 1')
                cases.run('post-exhaustion-zero-logical-sockets','/TMP/M10NET.SCX info',contains=b'sockets=0')
                screenshot(vm,report,'network-resource-desktop');report['status']='DECLARED_CASES_PASS'
            except BaseException as error:
                failure_evidence(vm,guest,report,error,symbols,diagnostics=True);raise
            finally:
                try:close_guest(guest,report)
                finally:
                    peer.expect_disconnect();report['source_unchanged']=sha(data)==report['source_sha256']
                    checkpoint(vm.out/'network-resources.json',report)
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
    report=dict(status='RUNNING',scope='DECLARED_SOCKET_RESOURCES_NOT_COMPLETE_NETWORK',disks=[],
                remaining=['driver reset/link/DMA accounting and parallel TCP accept/exit cases',
                           'short DHCP lease and full congestion/throughput/input latency'])
    checkpoint(args.out/'network-resources-matrix.json',report)
    try:
        symbols=unique_symbols(args.symbols)
        for n,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,symbols,args.out/f'disk-{n}'))
            checkpoint(args.out/'network-resources-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()));raise
    finally:checkpoint(args.out/'network-resources-matrix.json',report)


if __name__=='__main__':main()
