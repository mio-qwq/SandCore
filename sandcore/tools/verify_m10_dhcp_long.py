#!/usr/bin/env python3
"""长/无限租约真实双来源验证；HMP只读协议timer，不改状态或时钟。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import threading
import time
import traceback

from m10dhcplongpeer import LongLeasePeer
from m10_guest_boot import physical_word,unique_symbols
from m10_failure_snapshot import close_guest,failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession,sha
from verify_m9 import Guest
from verify_m10_network import Cases,connect_ready,failure_evidence,screenshot

ROOT=Path(__file__).resolve().parents[1]
MODES=('maximum-default','long-explicit','infinite-explicit','transition')
OFFERS={'maximum-default':(0xFFFFFFFE,None,None),'long-explicit':(86400,43200,75600),
        'infinite-explicit':(0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF),'transition':(60,20,40)}


def observe(vm,symbols,layout):
    # 实际32位配置编译出的offsetof定位client_data及timer；只暂停
    # 一个自洽样本，立即恢复。不读协议载荷、凭据、熵或任何私钥。
    was_running=vm.qmp('query-status')['running']
    try:
        vm.qmp('stop');pointer=physical_word(vm,symbols['interface']+layout['client_data'])
        if not 0x100000<=pointer<0x10000000:raise ValueError('DHCP对象地址不在本轮256MiB范围')
        reply=vm.hmp(f'xp /16wx 0x{pointer:08x}')
        values=re.findall(r'0x([0-9a-fA-F]{8})',reply)
        if len(values)!=16:raise RuntimeError('只读DHCP字段观察格式改变：'+reply)
        body=struct.pack('<16I',*(int(value,16) for value in values))
        result={'object_address':pointer,'state':body[layout['state']],
                'sc_ticks':physical_word(vm,symbols['sc_ticks'])}
        for name,offset in layout.items():
            if name not in ('timeout_bytes','pointer_bytes','client_data','state'):
                result[name]=struct.unpack_from('<I',body,offset)[0]
        return result
    finally:
        if was_running and vm.process.poll() is None:vm.qmp('cont')


def verify_timers(guest,cases,mode,layout,symbols,report,infinite=False):
    offered=OFFERS[mode] if not infinite else (0xFFFFFFFF,)*3
    lease,t1,t2=offered
    expected=(lease,lease//2 if t1 is None else t1,lease*7//8 if t2 is None else t2)
    timer_expected=tuple(0 if value==0xFFFFFFFF else value or 1 for value in expected)
    samples=[];began=time.monotonic();first=None
    # 无限过渡的清旧计数在正常粗回调内完成；等真实PIT三秒或15秒
    # 宿主预算，不能手动调用计时器/强写零。有限字段不允许宽容截断。
    while True:
        sample=observe(guest.vm,symbols,layout);samples.append(sample)
        if first is None:first=sample['sc_ticks']
        actual=tuple(sample[name] for name in ('t0_timeout','t1_timeout','t2_timeout'))
        actual_offers=tuple(sample[name] for name in ('offered_t0_lease','offered_t1_renew','offered_t2_rebind'))
        okay=sample['state']==10 and actual==timer_expected and actual_offers==expected
        if infinite or mode=='infinite-explicit':
            okay=okay and sample['t1_renew_time']==sample['t2_rebind_time']==sample['lease_used']==0
        if okay or not (infinite or mode=='infinite-explicit'):break
        if (sample['sc_ticks']-first)&0xFFFFFFFF>=300 or time.monotonic()-began>=15:break
        time.sleep(.1)
    report.setdefault('timer_observations',[]).append(dict(mode=mode,transition_infinite=infinite,samples=samples))
    cases.check('real-timer-values-'+mode+('-infinite' if infinite else ''),okay,
                expected_offers=expected,expected_timers=timer_expected,actual=samples[-1],
                scope='READ_ONLY_PROTOCOL_FIELDS_NO_STATE_OR_CLOCK_WRITE')


def run_mode(guest,cases,peer,mode,symbols,layout,report):
    peer.set_lease(*OFFERS[mode]);event_start=len(peer.events);cursor=len(guest.console)
    result={};errors=[]
    timing=dict(mode=mode,before=cases.clock_observation(),host_timeout_seconds=900)
    report['active_long_lease']=timing;checkpoint(guest.vm.out/'dhcp-long.json',report)
    def execute():
        try:
            code,output,wall,cpu=guest.command('/TMP/M10DHL.SCX '+mode,timeout=900)
            result.update(exit_code=code,stdout_hex=output.hex(),wall_seconds=wall,qemu_cpu_seconds=cpu)
        except BaseException as error:errors.append(error)
    worker=threading.Thread(target=execute,name='M10-long-lease-'+mode);worker.start()
    deadline=time.monotonic()+900
    try:
        for label in (('bound','infinite') if mode=='transition' else ('bound',)):
            pattern=b'M10DHL_STAGE '+label.encode()+rb'\r?\n'
            while True:
                try:
                    found=guest.wait(pattern,cursor,min(2,max(.1,deadline-time.monotonic())));break
                except TimeoutError:
                    if not worker.is_alive():raise RuntimeError('长租约探针阶段前退出：'+label+' '+repr(result))
                    if time.monotonic()>=deadline:raise
            cursor=found.end()
            verify_timers(guest,cases,mode,layout,symbols,report,label=='infinite')
            if mode=='transition' and label=='bound':peer.set_lease(0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF)
            guest.client.input(b'\n')
        while worker.is_alive() and time.monotonic()<deadline:worker.join(.2)
        if worker.is_alive():raise TimeoutError('长/无限租约探针未实际退出')
        if errors:raise errors[0]
        timing['after']=cases.clock_observation();report.pop('active_long_lease',None)
        output=bytes.fromhex(result['stdout_hex'])
        cases.check('real-address-hold-'+mode,result['exit_code']==0 and b'dhcp_long_failures=0' in output,
                    **result,clock_observations=timing)
        requests=[e for e in peer.events[event_start:] if e['event']=='long-lease-request']
        replies=[e for e in peer.events[event_start:] if e['event']=='long-lease-reply']
        okay=any(e['phase']=='select' and e['kind']==5 and e['seconds']==OFFERS[mode][0] for e in replies)
        if mode=='transition':
            renewal=[e for e in replies if e['phase']=='renew' and e['kind']==5 and e['seconds']==0xFFFFFFFF]
            okay=okay and len(renewal)==1
            okay=okay and all(e['phase'] not in ('renew','rebind','release') for e in requests
                             if renewal and e['monotonic']>renewal[-1]['monotonic'])
            okay=okay and any(e['phase']=='renew' and not e['requested'] and not e['server_id'] for e in requests)
        else:okay=okay and all(e['phase'] not in ('renew','rebind') for e in requests)
        cases.check('independent-wire-'+mode,okay,requests=requests,replies=replies)
    finally:
        if worker.is_alive():
            failure_snapshot(guest.vm,symbols,report)
            try:
                guest.client.fail(RuntimeError('长租约失败，保留原状态后停止等待'));close_guest(guest,report)
            finally:worker.join(10)


def run_disk(args,data,out,symbols,layout,modes):
    report=dict(status='RUNNING',scope='DECLARED_LONG_AND_INFINITE_LEASE',cases=[],screenshots=[],source_sha256=sha(data))
    with LongLeasePeer(out.with_name(out.name+'-peer')) as peer:
        with QemuSession(args.boot,data,out,'tcg',True,audio=False,network='socket',network_peer=peer.port,
                         network_capture=True,memory=256) as vm:
            guest=Guest(vm);cases=Cases(guest,report,symbols)
            try:
                connect_ready(guest,symbols,report)
                source=(ROOT/'tests/m10/M10DHL.C').read_bytes();report['fixture_sha256']=hashlib.sha256(source).hexdigest()
                guest.put_bytes(source,'/TMP/M10DHL.C','dhcp-long-source')
                cases.run('native-long-lease-fixture','s3c /TMP/M10DHL.C /TMP/M10DHL.SCX',timeout=240)
                for mode in modes:
                    run_mode(guest,cases,peer,mode,symbols,layout,report)
                    cases.run('real-echo-after-'+mode,'ping -c 1 -W 2 10.23.0.1',contains=b'received 1')
                screenshot(vm,report,'dhcp-long-desktop');report['status']='DECLARED_CASES_PASS'
            except BaseException as error:
                failure_evidence(vm,guest,report,error,symbols);raise
            finally:
                try:close_guest(guest,report)
                finally:
                    peer.expect_disconnect();report['source_unchanged']=sha(data)==report['source_sha256']
                    checkpoint(vm.out/'dhcp-long.json',report)
        if not report['source_unchanged']:raise AssertionError('长租约只读来源盘改变')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--layout',type=Path,required=True)
    parser.add_argument('--core-freeze',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--modes',choices=MODES,nargs='+',default=list(MODES))
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(p.resolve(strict=True) for p in args.data))!=len(args.data):
        parser.error('需要Windows Python和至少两个不同只读来源盘')
    layout_report=json.loads(args.layout.read_text(encoding='utf-8'));layout=layout_report['fields']
    frozen=json.loads((args.core_freeze/'manifest.json').read_text(encoding='utf-8'))
    hashes={f['path'].replace('\\','/'):f['sha256'] for f in frozen['source_files']}
    if layout['pointer_bytes']!=4 or layout['timeout_bytes']!=4:raise ValueError('观察布局不是32位秒计数')
    for name,expected in layout_report['headers'].items():
        if hashes.get(name.replace('\\','/'))!=expected:raise ValueError('当前候选实际构建输入与观察布局不同：'+name)
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='DECLARED_LEASE_BOUNDARIES_NOT_COMPLETE_NETWORK',disks=[],modes=args.modes,
                complete_modes=set(args.modes)==set(MODES),layout=layout_report,symbols_sha256=sha(args.symbols),
                remaining=['complete driver/TCP/network/OS performance and compatibility'])
    checkpoint(args.out/'dhcp-long-matrix.json',report)
    try:
        symbols=unique_symbols(args.symbols)
        if 'interface' not in symbols:raise ValueError('需要当前主核唯一IPv4接口符号')
        for index,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args,data,args.out/f'disk-{index}',symbols,layout,args.modes))
            checkpoint(args.out/'dhcp-long-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()));raise
    finally:checkpoint(args.out/'dhcp-long-matrix.json',report)


if __name__=='__main__':main()
