"""同一VM/隐藏空闲host/同一caller的两次冷资源测试；不暂停客体计时。"""
import re
import secrets
import threading
import time

from scserial import sha
from verify_m10a2_ui_gui import sessions
from verify_m9 import process_cpu


RESULT = rb'ICONPROFILE RESULT icons=(\d+) ticks=(\d+) hz=(\d+) pixels_bytes=(\d+) entries=(\d+) misses=(\d+) hits=(\d+) samples=(\d+) dispatches=(\d+)'


def run(vm, guest, harness, report, icon_target, out):
    original_rows=sessions(harness.run('sessions-before-icon-comparison','sessionctl list')['output'].encode())
    previous=next(row['id'] for row in original_rows if row['active'])
    before,observed_before=harness.quiet();resources_before=harness.resources()
    harness.login_cooldown()
    marker='__ICON_AB_'+secrets.token_hex(8)
    inner=icon_target+' control; _ab=$?; echo '+marker+':$_ab; exit $_ab'
    start=len(guest.console);completed={}

    def execute():
        try:
            completed['item']=harness.run('ordinary-same-vm-icon-comparison',
                "printf '%s\\n' '' '"+inner+"' | su mio",
                contains=(b'ICONPROFILE PASS same_vm_cold_resource_comparison',(marker+':0').encode()),
                forbidden=(b'ICONPROFILE FAIL',b'[AUTH] login failed'),timeout=240)
        except BaseException as error:completed['error']=error

    thread=threading.Thread(target=execute,daemon=True);thread.start()
    phases=[]
    try:
        for phase in ('baseline','optimized'):
            begin=guest.wait(rb'ICONPROFILE BEGIN\r?\n',start,120)
            wall=time.monotonic();cpu=process_cpu(vm.process)
            result=guest.wait(RESULT,begin.end(),60)
            metrics=dict(zip(('icons','ticks','hz','pixels_bytes','entries','misses','hits','samples','dispatches'),
                (int(value) for value in result.groups())))
            metrics.update(phase=phase,observed_wall_seconds=time.monotonic()-wall,
                observed_qemu_cpu_seconds=process_cpu(vm.process)-cpu,
                timing_scope='PASSIVE_CONSOLE_BOUNDARIES_INCLUDES_TRANSPORT_AND_ALL_QEMU_WORK')
            phases.append(metrics);start=result.end()
            if phase=='baseline':
                done=guest.wait(rb'ICONPROFILE BASELINE_DONE\r?\n',start,20);start=done.end()
        thread.join(30)
        if thread.is_alive():raise TimeoutError('分项比较退出/领票没有结束')
        if 'error' in completed:raise completed['error']
        item=completed['item']
        identity=re.search(r'ICONPROFILE CONTROLLER sid=(\d+) host=(\d+)/(\d+) epoch=(\d+)',item['output'])
        if not identity:raise ValueError('缺少实际隐藏host身份')
        for key in ('icons','hz','pixels_bytes','entries','misses','hits'):
            if phases[0][key]!=phases[1][key]:raise ValueError('前后资源规模/cache状态不同：'+key)
        report['icon_same_vm_comparison']=dict(scope='COLD_ICON_RESOURCE_COMPONENT_NOT_WHOLE_DESKTOP',
            phases=phases,actual_controller_identity=dict(zip(('session','host_pid','host_generation','host_epoch'),map(int,identity.groups()))),
            compiled_caller_sha256=sha(out/'native-artifacts/ICONPROFILE.SCX'),same_caller_both_phases=True,
            hidden_host_same_loaded_optimized_library=True,no_guest_memory_writes=True,
            production_library_never_modified=True)
        harness.run('restore-desktop-after-icon-comparison','sessionctl show '+str(previous))
        harness.run('logout-icon-comparison-session','sessionctl logout '+identity[1])
        after,observed_after=harness.quiet()
        ledger,okay=harness.ledger(before,after,resources_before,harness.resources(),True)
        harness.append(dict(name='icon-comparison-final-owner-page-account',**ledger,
            before_observations=observed_before,after_observations=observed_after),okay)
    finally:
        # 不留下继续写共享串口/报告的后台线程。失败时让有界客体超时先结束，
        # 超时仍未结束就明确报告；QemuSession finally随后负责关闭本测试VM。
        if thread.is_alive():thread.join(2)
