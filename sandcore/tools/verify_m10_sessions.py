#!/usr/bin/env python3
"""真实登录会话/NUI与65个同UID桌面；其余输入和内置应用合同另列。"""
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

from m10_guest_boot import physical_word, unique_symbols, wait_first_desktop
from m10_failure_snapshot import close_guest, failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case

ROOT=Path(__file__).resolve().parents[1]
STAGES=(('initial',None),('mio-first','a'),('mio-first-capture',None),
        ('mio-second','b'),('mio-second-capture',None),('root','c'),('root-capture',None),
        ('system','q'),('system-capture',None),('mio-return','d'),('mio-return-capture',None))
CONTROLLER_CHECKS=(
    'session page preserves capacity boundary',
    'external SYSTEM window starts hidden from mio',
    'same UID logins own different desktops and root stays ordinary',
    'ordinary child has no SYSTEM realm and no hidden initial frames',
    'first mio session receives only its real HMP key',
    'hidden NUI submits zero new frames and preserves canvas while background advances',
    'same UID sessions do not share keyboard events',
    'same UID foreign desktop capture is denied',
    'foreign close preserves window and original WINCLOSE return ABI',
    'mio and ordinary root cannot display SYSTEM',
    'ordinary root cannot capture SYSTEM window',
    'session path traversal fails without modifying output',
    'root desktop receives its key without SYSTEM rights',
    'explicit external switch displays SYSTEM and routes only its key',
    'returning desktop redraws and has no leaked old root SYSTEM keys',
    'minimized NUI stops frame generation and background continues',
    'restored window redraws at current visibility generation',
    'logging out one mio session preserves the other session identity',
    'new login cannot inherit retired session ID or PID generation',
    'original mio desktop is restored after cleanup')
EXPECTED_CONTROLLER_COUNTS=Counter({name:3 if name==CONTROLLER_CHECKS[3] else 1 for name in CONTROLLER_CHECKS})


def desktop_observation(vm,symbols):
    # 各公开32位计数逐字读取，不暂停客体。字段可能跨一次服务轮，
    # 因而作为带宿主时间的观察，不能称为原子物理页总账。
    names=('sc_ticks','wm_frames','desktop_io_bytes','desktop_io_batches',
           'desktop_io_ticks_max','desktop_io_pending','desktop_io_session')
    return dict(monotonic=time.monotonic(),counters={name:physical_word(vm,symbols[name])
                                                   for name in names if name in symbols})


def settled_desktop(guest,symbols,session):
    # 新批次只在完整图片交付之后取最终画面，保留原图片分辨率。
    # HMP输入阶段仍立即送真实键，随后实际客户键计数先验证；此等待
    # 不暂停心跳、不放宽ACK/网络超时，也不以固定睡眠猜资源已完成。
    if 'desktop_io_pending' not in symbols:
        return dict(method='LEGACY_SYNCHRONOUS_DESKTOP_NO_PENDING_COUNTER')
    deadline=time.monotonic()+60;observations=[];last=None;stable=0;first_zero_frame=None
    while time.monotonic()<deadline:
        if guest.errors:
            raise RuntimeError('等待完整桌面资源期间串口失败') from guest.errors[0]
        sample=desktop_observation(guest.vm,symbols);counters=sample['counters']
        state=(counters['desktop_io_session'],counters['desktop_io_pending'],counters['desktop_io_bytes'])
        if state!=last or not observations:
            observations.append(sample);last=state
        if state[0]==session and not state[1]:
            if first_zero_frame is None:first_zero_frame=counters['wm_frames']
            stable+=1
            # pending在IO交付时清零，下一次wm_compose可能仍在生成
            # 原尺寸背景采样缓存。必须再观察实际完成帧，不能在swap
            # 之前把上一张程序壁纸当作最终图片；IRQ心跳仍照常推进。
            if stable>=3 and counters['wm_frames']>first_zero_frame:
                observations.append(sample)
                return dict(method='REAL_VISIBLE_PENDING_ZERO_AND_LATER_COMPLETED_FRAME',observations=observations)
        else:stable=0;first_zero_frame=None
        guest.stop.wait(.05)
    raise TimeoutError('可见桌面原始资源没有完整交付')


def controller(guest,report,symbols):
    result,errors={},[]
    began=len(guest.console)

    def execute():
        try:
            code,output,wall,cpu=guest.command('/TMP/M10SESS.SCX controller',timeout=600)
            result.update(exit_code=code,stdout_hex=output.hex(),wall_seconds=wall,qemu_cpu_seconds=cpu)
        except BaseException as error:
            errors.append(error)

    thread=threading.Thread(target=execute,name='M10-session-controller')
    last_input=None
    thread.start()
    cursor=began
    try:
        for label,key in STAGES:
            pattern=rb'M10SESS_STAGE '+re.escape(label.encode())+rb' session=([0-9]+) window=([0-9]+)\r?\n'
            deadline=time.monotonic()+120
            while True:
                try:
                    match=guest.wait(pattern,cursor,min(2,max(.1,deadline-time.monotonic())))
                    break
                except TimeoutError:
                    if not thread.is_alive():
                        report['early_controller_result']=dict(result)
                        raise RuntimeError('控制者在阶段前已经退出：'+label)
                    if time.monotonic()>=deadline:
                        raise
            cursor=match.end()
            stage=dict(label=label,session=int(match[1]),window=int(match[2]),hmp_key=key)
            stage['observation']=desktop_observation(guest.vm,symbols)
            if last_input is not None:
                stage['wall_since_previous_hmp_key']=time.monotonic()-last_input
            if key:
                last_input=time.monotonic()
                guest.vm.hmp('sendkey '+key)
                stage['hmp_send_seconds']=time.monotonic()-last_input
            else:
                # 控制者在这项真实读stdin等待，截图结束才正常发LF。
                # 带capture的阶段已完成客体键检查，不用固定睡眠猜画面。
                stage['settled_desktop']=settled_desktop(guest,symbols,stage['session'])
                path=guest.vm.out/(label+'.ppm')
                guest.vm.hmp('screendump "'+path.as_posix()+'"')
                stage['screenshot']=png_from_ppm(path)
                report['screenshots'].append(stage['screenshot'])
            report.setdefault('stages',[]).append(stage)
            checkpoint(guest.vm.out/'sessions.json',report)
            guest.client.input(b'\n')
        thread.join(90)
        if thread.is_alive():
            raise TimeoutError('会话控制者没有真实退出')
        if errors:
            raise errors[0]
        output=bytes.fromhex(result['stdout_hex'])
        result['checks']=re.findall(rb'^PASS (.*)\r?$',output,re.M)
        result['checks']=[value.decode('utf-8','replace') for value in result['checks']]
        # 三个普通子任务各自检查同一个身份合同，因此这个名称真实
        # 出现三次。按完整声明集合和每项出现次数核对，不能误要求
        # 22个名称全不同，也不能只检查一个宽松的总数量下限。
        okay=result['exit_code']==0 and b'FAIL ' not in output and Counter(result['checks'])==EXPECTED_CONTROLLER_COUNTS
        report['cases'].append(dict(name='real-login-desktops-NUI-input-hide-logout',status='PASS' if okay else 'FAIL',**result))
        checkpoint(guest.vm.out/'sessions.json',report)
        if not okay:
            raise AssertionError('真实会话/NUI检查未全部通过')
        print('real-login-desktops-NUI-input-hide-logout: PASS',flush=True)
    finally:
        if thread.is_alive():
            # 先保存最初失败时的真实现场，再结束宿主等待；不额外送LF
            # 改变客体阶段，也不让随后清理报错盖住最初的阶段错误。
            failure_snapshot(guest.vm,symbols,report)
            try:
                guest.client.fail(RuntimeError('会话用例失败，结束宿主等待'))
                close_guest(guest,report)
            finally:
                thread.join(10)


def run_disk(boot,data,directory,symbols,resource_cycles=False,controller_only=False):
    report=dict(status='RUNNING',scope='DECLARED_REAL_SESSION_AND_NUI_CASES_ONLY',cases=[],screenshots=[],
                source=str(data.resolve()),source_sha256=sha(data))
    with QemuSession(boot,data,directory,'tcg',True,audio=False,network='none',memory=256) as vm:
        guest=Guest(vm)
        try:
            guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n',timeout=60,debug=True)
            wait_first_desktop(guest,symbols,report)
            guest.connect()
            # NUI是应用私有源码，SCCC只默认搜索当前目录与SYS/INC。
            # 原样放在探针旁，不把私有实现挪入公共头或省掉真实界面。
            report['private_gui_dependencies']=[]
            for name in ('NUI.inc','SCMEM.inc','GLYPHS.inc'):
                dependency=(ROOT/'user'/name).read_bytes()
                guest.put_bytes(dependency,'/TMP/'+name,'gui-dependency-'+name)
                report['private_gui_dependencies'].append(dict(name=name,sha256=hashlib.sha256(dependency).hexdigest()))
            source=(ROOT/'tests/m10/M10SESS.C').read_bytes()
            report['fixture_source_sha256']=hashlib.sha256(source).hexdigest()
            guest.put_bytes(source,'/TMP/M10SESS.C','session-probe-source')
            run_case(guest,report,'native-session-NUI-probe','s3c /TMP/M10SESS.C /TMP/M10SESS.SCX',timeout=360)
            controller(guest,report,symbols)
            if not controller_only:
                run_case(guest,report,'65-real-ordinary-login-sessions','/TMP/M10SESS.SCX '+('many-cycle' if resource_cycles else 'many'),
                         (b'many_created=65',b'many_ready=65',
                          b'PASS paged session query includes all 65 ordinary hidden desktops',
                          b'PASS all 65 retired session IDs are invalid after logout',
                          b'PASS all 65 task owners and dynamic side pages return to baseline',
                          b'PASS physical page delta equals only retained committed PID history pages'),timeout=600)
            if resource_cycles and not controller_only:
                output=bytes.fromhex(report['cases'][-1]['stdout_hex'])
                names=('many_created','many_ready','many_free_before','many_free_after',
                       'many_record_pages_before','many_record_pages_after','many_side_pages_before','many_side_pages_after')
                values={name:[int(value) for value in re.findall(rb'^'+name.encode()+rb'=([0-9]+)\r?$',output,re.M)] for name in names}
                report['resource_cycles']=values
                if any(len(items)!=2 for items in values.values()) or values['many_created']!=[65,65] or values['many_ready']!=[65,65]:
                    raise AssertionError('没有取得两轮真实65桌面资源样本')
                if values['many_free_before'][1]!=values['many_free_after'][1] or values['many_record_pages_before'][1]!=values['many_record_pages_after'][1]:
                    raise AssertionError('暖重复仍增加物理页或任务历史页')
                print('warm-65-session-physical-pages-and-history-ledger: PASS',flush=True)
            picture=vm.out/'restored-original-mio.ppm'
            vm.hmp('sendkey esc')
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status']='DECLARED_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
            failure_snapshot(vm,symbols,report)
            try:
                if vm.process.poll() is None:
                    path=vm.out/'session-failure.ppm'
                    vm.hmp('screendump "'+path.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(path))
            except Exception as capture_error:
                report['capture_error']=str(capture_error)
            raise
        finally:
            try:
                close_guest(guest,report)
            finally:
                report['source_unchanged']=sha(data)==report['source_sha256']
                checkpoint(vm.out/'sessions.json',report)
    if not report['source_unchanged']:
        raise AssertionError('只读原输入盘改变')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--resource-cycles',action='store_true',help='同进程重复两轮65个真实桌面，并核冷暖PF/记录/旁表页账')
    parser.add_argument('--controller-only',action='store_true',help='只重测11阶段/22项与完整桌面截图；不能代替65会话资源矩阵')
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(path.resolve(strict=True) for path in args.data))!=len(args.data):
        parser.error('需要Windows Python与两不同只读来源盘')
    if args.controller_only and args.resource_cycles:
        parser.error('controller-only与resource-cycles不能同时选择')
    symbols=unique_symbols(args.symbols)
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='DECLARED_CONTROLLER_ONLY_NOT_FULL_SESSION_MATRIX' if args.controller_only else 'DECLARED_SESSION_AND_NUI_CASES_NOT_WHOLE_M10A1',
                complete_profiles=not args.controller_only,disks=[],
                remaining=['held keys mouse dragging relative capture and fault cards',
                           'normal password/F12 locked desktop UI and theme snapshots',
                           'all built-in GUI hidden generation and audio/network progress',
                           'legacy deferred drawing and task fault/SIMD/credential lifecycle',
                           'full physical-page and cache ownership ledger'])
    checkpoint(args.out/'session-matrix.json',report)
    try:
        for index,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,args.out/f'disk-{index}',symbols,args.resource_cycles,args.controller_only))
            checkpoint(args.out/'session-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'session-matrix.json',report)


if __name__=='__main__':
    main()
