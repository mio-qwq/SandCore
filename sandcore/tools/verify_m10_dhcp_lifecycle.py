#!/usr/bin/env python3
"""双来源真实60秒租约自动续期、丢续期后重绑定、完全断应答后到期恢复。"""
import argparse
import hashlib
import os
from pathlib import Path
import threading
import time
import traceback

from m10dhcppeer import LeasePeer
from m10_guest_boot import unique_symbols
from m10_failure_snapshot import close_guest,failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession,sha
from verify_m9 import Guest
from verify_m10_network import Cases,connect_ready,failure_evidence,screenshot

ROOT=Path(__file__).resolve().parents[1]
MODES=('renew','rebind','expire')


def lifecycle(guest,cases,peer,mode,symbols,report):
    result,errors={},[];start=len(guest.console);cursor=start
    event_start=len(peer.events);peer.set_policy('all')
    timing=dict(name='automatic-lease-'+mode,before=cases.clock_observation(),host_timeout_seconds=480)
    report['active_lifecycle']=timing
    checkpoint(guest.vm.out/'dhcp-lifecycle.json',report)

    def execute():
        try:
            code,output,wall,cpu=guest.command('/TMP/M10DHCP.SCX '+mode,timeout=480)
            result.update(exit_code=code,stdout_hex=output.hex(),wall_seconds=wall,qemu_cpu_seconds=cpu)
        except BaseException as error:errors.append(error)

    worker=threading.Thread(target=execute,name='M10-DHCP-lifecycle-'+mode)
    worker.start();deadline=time.monotonic()+480
    try:
        stages=('bound','expired') if mode=='expire' else ('bound',)
        for label in stages:
            pattern=b'M10DHCP_STAGE '+label.encode()+rb'\r?\n'
            while True:
                try:
                    found=guest.wait(pattern,cursor,min(2,max(.1,deadline-time.monotonic())));break
                except TimeoutError:
                    if not worker.is_alive():raise RuntimeError('租约探针在阶段前退出：'+label+' '+repr(result))
                    if time.monotonic()>=deadline:raise
            cursor=found.end()
            policy='all' if label=='expired' or mode=='renew' else 'drop-renew' if mode=='rebind' else 'blackout'
            peer.set_policy(policy)
            report.setdefault('stages',[]).append(dict(mode=mode,label=label,policy=policy,clock=cases.clock_observation()))
            checkpoint(guest.vm.out/'dhcp-lifecycle.json',report)
            guest.client.input(b'\n')
        while worker.is_alive() and time.monotonic()<deadline:worker.join(.2)
        if worker.is_alive():raise TimeoutError('租约探针没有真实退出')
        if errors:raise errors[0]
        timing['after']=cases.clock_observation()
        output=bytes.fromhex(result['stdout_hex'])
        report.pop('active_lifecycle',None)
        cases.check('automatic-lease-'+mode,result['exit_code']==0 and b'dhcp_lifecycle_failures=0' in output,
                    **result,clock_observations=timing)
        events=peer.events[event_start:]
        requests=[e for e in events if e['event']=='lease-request']
        replies=[e for e in events if e['event']=='lease-reply']
        if mode=='renew':
            okay=any(e['phase']=='renew' for e in requests) and any(e['phase']=='renew' and e['kind']==5 for e in replies)
        elif mode=='rebind':
            okay=any(e['phase']=='renew' and e['policy']=='drop-renew' for e in requests)
            okay=okay and any(e['phase']=='rebind' for e in requests) and any(e['phase']=='rebind' and e['kind']==5 for e in replies)
        else:
            dropped=[e for e in events if e['event']=='lease-reply-dropped']
            okay=all(any(e['phase']==phase and e['policy']=='blackout' for e in dropped) for phase in ('renew','rebind'))
            okay=okay and any(e['phase']=='release' and e['policy']=='blackout' for e in requests)
            blackout=[e['monotonic'] for e in events if e['event']=='lease-policy' and e['policy']=='blackout']
            recovered=[e['monotonic'] for e in events if e['event']=='lease-policy' and e['policy']=='all'
                       and blackout and e['monotonic']>blackout[-1]]
            okay=okay and bool(recovered) and any(e['phase']=='select' and e['policy']=='all'
                                                and e['monotonic']>recovered[-1] for e in requests)
            okay=okay and not any(blackout and recovered and blackout[-1]<e['monotonic']<recovered[-1] for e in replies)
        okay=okay and all(not e['requested'] and not e['server_id'] for e in requests if e['phase'] in ('renew','rebind'))
        cases.check('independent-lease-wire-'+mode,okay,requests=requests,replies=replies)
    finally:
        if worker.is_alive():
            # 原始失败现场先保存，随后停止串口等待。不能额外发送LF
            # 或重新启动租约把最初失败的地址/定时状态覆盖掉。
            failure_snapshot(guest.vm,symbols,report)
            try:
                guest.client.fail(RuntimeError('DHCP生命周期失败，终止宿主等待'))
                close_guest(guest,report)
            finally:worker.join(10)


def run_disk(boot,data,symbols,out,modes):
    report=dict(status='RUNNING',scope='DECLARED_REAL_SHORT_LEASE_LIFECYCLE',cases=[],screenshots=[],source_sha256=sha(data))
    with LeasePeer(out.with_name(out.name+'-peer')) as peer:
        with QemuSession(boot,data,out,'tcg',True,audio=False,network='socket',network_peer=peer.port,
                         network_capture=True,memory=256) as vm:
            guest=Guest(vm);cases=Cases(guest,report,symbols)
            try:
                connect_ready(guest,symbols,report)
                source=(ROOT/'tests/m10/M10DHCP.C').read_bytes();report['fixture_sha256']=hashlib.sha256(source).hexdigest()
                guest.put_bytes(source,'/TMP/M10DHCP.C','dhcp-lifecycle-source')
                cases.run('native-DHCP-lifecycle-fixture','s3c /TMP/M10DHCP.C /TMP/M10DHCP.SCX',timeout=240)
                cases.run('native-network-diagnostics','s3c /SYS/TEST/M10NET.C /TMP/M10NET.SCX',timeout=240)
                for mode in modes:
                    lifecycle(guest,cases,peer,mode,symbols,report)
                    cases.run('real-echo-after-lease-'+mode,'ping -c 1 -W 2 10.23.0.1',contains=b'received 1')
                screenshot(vm,report,'dhcp-lifecycle-desktop');report['status']='DECLARED_CASES_PASS'
            except BaseException as error:
                failure_evidence(vm,guest,report,error,symbols,diagnostics=True);raise
            finally:
                try:close_guest(guest,report)
                finally:
                    peer.expect_disconnect();report['source_unchanged']=sha(data)==report['source_sha256']
                    checkpoint(vm.out/'dhcp-lifecycle.json',report)
        if not report['source_unchanged']:raise AssertionError('只读租约来源盘改变')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--modes',choices=MODES,nargs='+',default=list(MODES))
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(p.resolve(strict=True) for p in args.data))!=len(args.data):
        parser.error('需要Windows Python与两个不同只读来源盘')
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='DECLARED_DHCP_LIFECYCLE_NOT_COMPLETE_NETWORK',disks=[],modes=args.modes,
                complete_modes=set(args.modes)==set(MODES),remaining=['long leases and infinite lease arithmetic','driver/TCP/complete performance and compatibility'])
    checkpoint(args.out/'dhcp-lifecycle-matrix.json',report)
    try:
        symbols=unique_symbols(args.symbols)
        for index,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,symbols,args.out/f'disk-{index}',args.modes))
            checkpoint(args.out/'dhcp-lifecycle-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()));raise
    finally:checkpoint(args.out/'dhcp-lifecycle-matrix.json',report)


if __name__=='__main__':main()
