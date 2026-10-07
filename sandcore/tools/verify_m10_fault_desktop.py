#!/usr/bin/env python3
"""真实故障/隐藏清理/切回焦点与HMP输入；失败同样收齐双盘观察。"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import threading
import time
import traceback

from m10_failure_snapshot import close_guest
from m10_guest_boot import physical_word, unique_symbols
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_network import Cases, connect_ready, failure_evidence

ROOT = Path(__file__).resolve().parents[1]
STAGES = (('baseline','a'), ('visible-fault',None), ('visible-clean','c'),
          ('hidden-fault',None), ('hidden-clean','b'))
CHECKS = (
    'external SYSTEM test window starts hidden from ordinary desktop',
    'same test window has focus before any child fault',
    'baseline accepts its real HMP key',
    'hidden child actually faults and pauses in its own SYSTEM session',
    'hidden fault cleanup restores focus when its desktop returns',
    'hidden fault cleanup accepts its real HMP key',
    'visible child actually faults and pauses in its own SYSTEM session',
    'visible fault cleanup restores focus on the same desktop',
    'visible fault cleanup accepts its real HMP key',
    'original ordinary desktop is restored after both cleanup paths')
MULTICARD_STAGES = (('a-two-faults',None), ('a-one-fault',None), ('b-fault',None),
                   ('b-after-hidden-a-clean',None), ('a-clean','d'),
                   ('b-still-fault',None), ('b-clean','e'))
MULTICARD_CHECKS = (
    'mio and root fault agents have ordinary identities and separate sessions',
    'two real fault children coexist and block only their desktop',
    'removing the older nonhead card preserves the newer modal fault',
    'another ordinary user has its own real visible modal fault',
    "hidden last card cleanup preserves the other user's visible modal fault",
    'returning to the cleaned ordinary desktop restores its focus',
    'cleaned mio desktop receives only its real HMP d key',
    "switching back preserves the other user's outstanding fault",
    'last visible root fault cleanup restores its own focus',
    'cleaned root desktop receives only its real HMP e key',
    'both temporary sessions retire and the original desktop is restored')


def controller(guest, report, symbols, multicard=False):
    result, errors = {}, []
    began = len(guest.console)
    stem = 'M10FCARD' if multicard else 'M10MODAL'
    stages = MULTICARD_STAGES if multicard else STAGES
    checks = MULTICARD_CHECKS if multicard else CHECKS

    def execute():
        try:
            code, output, wall, cpu = guest.command('/TMP/'+stem+'.SCX controller', timeout=180)
            result.update(exit_code=code, stdout_hex=output.hex(), wall_seconds=wall, qemu_cpu_seconds=cpu)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=execute, name='M10-fault-controller')
    thread.start()
    cursor = began
    try:
        for label, key in stages:
            pattern = (stem.encode()+b'_STAGE '+re.escape(label.encode())
                       +rb' session=([0-9]+) window=([0-9]+) focus=([0-9]+)\r?\n')
            deadline = time.monotonic()+30
            while True:
                try:
                    match = guest.wait(pattern, cursor, min(2, max(.1,deadline-time.monotonic())))
                    break
                except TimeoutError:
                    if not thread.is_alive():
                        raise RuntimeError('控制者提前退出：'+label+' '+repr(result))
                    if time.monotonic() >= deadline:
                        raise
            cursor = match.end()
            stage = dict(label=label, session=int(match[1]), window=int(match[2]),
                         focus=int(match[3]), hmp_key=key)
            report.setdefault('stages', []).append(stage)
            checkpoint(guest.vm.out/'fault-desktop.json', report)
            # 原生画布/故障覆盖层已经请求合成；只等完成帧计数推进，
            # 不改客体状态，不以固定睡眠称截图已反映刚刚的清理。
            first = physical_word(guest.vm, symbols['wm_frames'])
            while physical_word(guest.vm, symbols['wm_frames']) <= first:
                if time.monotonic() >= deadline:
                    raise TimeoutError('真实完成帧未推进：'+label)
                guest.stop.wait(.05)
            picture = guest.vm.out/(label+'.ppm')
            guest.vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(dict(label=label, **png_from_ppm(picture)))
            if key:
                guest.vm.hmp('sendkey '+key+' 20')
            guest.client.input(b'\n')
            checkpoint(guest.vm.out/'fault-desktop.json', report)
        thread.join(30)
        if thread.is_alive():
            raise TimeoutError('故障控制者未收尾')
        if errors:
            raise errors[0]
        output = bytes.fromhex(result['stdout_hex'])
        lines = re.findall(rb'^(PASS|FAIL) (.+)\r?$', output, re.M)
        observed = Counter(text.decode('ascii').rstrip('\r') for _,text in lines)
        if observed != Counter(checks):
            raise AssertionError('缺少完整声明检查：'+repr(observed))
        for status, text in lines:
            report['cases'].append(dict(name=text.decode('ascii').rstrip('\r'), status=status.decode()))
        result['metrics'] = {name.decode():int(value) for name,value in
                             re.findall(rb'^(modal_[a-z_-]+)=(-?[0-9]+)\r?$', output,re.M)}
        result['stdout_sha256'] = hashlib.sha256(output).hexdigest()
        report['controller'] = result
        passed = result['exit_code']==0 and all(status==b'PASS' for status,_ in lines)
        report['status'] = 'DECLARED_CASES_PASS' if passed else 'DECLARED_CASES_FAILED'
        checkpoint(guest.vm.out/'fault-desktop.json', report)
        print('real-hidden-visible-fault-desktop: '+('PASS' if passed else 'FAIL'), flush=True)
    finally:
        if thread.is_alive():
            try:
                guest.client.fail(RuntimeError('结束失败的故障控制器等待'))
            finally:
                thread.join(15)


def run_disk(boot, data, symbols, directory, multicard=False):
    report = dict(status='RUNNING', scope='REAL_SYSTEM_HIDDEN_VISIBLE_FAULT_FOCUS_INPUT_ONLY',
                  cases=[], screenshots=[], source_sha256=sha(data))
    if multicard:
        report['scope'] = 'REAL_TWO_ORDINARY_USERS_THREE_PF_CARDS_CLEANUP_AND_INPUT'
    stem = 'M10FCARD' if multicard else 'M10MODAL'
    with QemuSession(boot,data,directory,'tcg',True,audio=False,network='none',memory=256) as vm:
        guest = Guest(vm)
        try:
            connect_ready(guest,symbols,report)
            source = (ROOT/('tests/m10/'+stem+'.C')).read_bytes()
            report['fixture_sha256'] = hashlib.sha256(source).hexdigest()
            guest.put_bytes(source,'/TMP/'+stem+'.C','fault-desktop-source')
            Cases(guest,report,symbols).run('native-public-fault-desktop-probe',
                                           's3c /TMP/'+stem+'.C /TMP/'+stem+'.SCX',timeout=360)
            controller(guest,report,symbols,multicard)
        except BaseException as error:
            failure_evidence(vm,guest,report,error,symbols)
            raise
        finally:
            try:
                close_guest(guest,report)
            finally:
                report['source_unchanged'] = sha(data)==report['source_sha256']
                checkpoint(vm.out/'fault-desktop.json',report)
    if not report['source_unchanged']:
        raise AssertionError('只读输入盘改变')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--multicard',action='store_true',help='两个普通用户桌面/三个真实PF卡，复用正常公开ABI与HMP阶段')
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(p.resolve(strict=True) for p in args.data))!=len(args.data):
        parser.error('需要Windows Python及两份不同来源盘')
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='FAULT_DESKTOP_ONLY_NOT_WHOLE_M10A1',disks=[])
    checkpoint(args.out/'fault-desktop-matrix.json',report)
    try:
        symbols=unique_symbols(args.symbols)
        for n,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,symbols,args.out/f'disk-{n}',args.multicard))
            checkpoint(args.out/'fault-desktop-matrix.json',report)
        report['status']='DECLARED_CASES_PASS' if all(d['status']=='DECLARED_CASES_PASS' for d in report['disks']) else 'DECLARED_CASES_FAILED'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'fault-desktop-matrix.json',report)
    return 0 if report['status']=='DECLARED_CASES_PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
