#!/usr/bin/env python3
"""依据匹配主核符号等待真实首帧；仅外部HMP读计数与正常按键。"""
import re
import time


def unique_symbols(path):
    addresses = {}
    for line in path.read_text(encoding='ascii').splitlines():
        fields = line.split()
        if len(fields) == 3:
            addresses.setdefault(fields[2], []).append(int(fields[0], 16))
    return {name: values[0] for name, values in addresses.items() if len(values) == 1}


def physical_word(vm, address):
    reply = vm.hmp(f'xp /1wx 0x{address:08x}')
    values = re.findall(r'0x([0-9a-fA-F]{8})', reply)
    if len(values) != 1:
        raise RuntimeError('HMP单字只读观察返回格式改变：'+reply)
    return int(values[0], 16)


def wait_first_desktop(guest, symbols, report, timeout=90):
    required = ('boot_stage', 'sc_ticks', 'wm_frames')
    if any(name not in symbols for name in required):
        raise ValueError('必须提供当前主核唯一的启动/时钟/完成帧符号')
    # 初始化会验签、装字体、绘制原生欢迎页与首次壁纸。协议请求
    # 不能在管理轮尚未启动时开始正常3秒重试/5秒心跳。用真实ESC
    # 通过原任意键入口，再观察wm_compose结束才开始外部HELLO；
    # 不改内核状态、租约、权限或协议超时，也不以固定睡眠冒充就绪。
    began = time.monotonic()
    state = dict(status='WAITING',method='READ_ONLY_HMP_COUNTERS_AND_PHYSICAL_ESC',
                 symbols={name:symbols[name] for name in required},observations=[],input_sent=False)
    report['startup_readiness'] = state
    last = None
    while time.monotonic()-began < timeout:
        if guest.errors:
            raise RuntimeError('等待首帧期间串口读取失败') from guest.errors[0]
        if guest.vm.process.poll() is not None:
            raise RuntimeError('等待首帧期间QEMU退出')
        stage = physical_word(guest.vm,symbols['boot_stage'])
        frames = physical_word(guest.vm,symbols['wm_frames'])
        if (stage,frames) != last:
            state['observations'].append(dict(seconds=time.monotonic()-began,boot_stage=stage,
                                              completed_frames=frames,
                                              ticks=physical_word(guest.vm,symbols['sc_ticks'])))
            last = (stage,frames)
        if stage == 2 and not state['input_sent']:
            guest.vm.hmp('sendkey esc')
            state.update(input_sent=True,input='esc',input_seconds=time.monotonic()-began)
        if stage == 3 and frames > 0:
            state.update(status='ACTUAL_FIRST_DESKTOP_COMPLETED',seconds=time.monotonic()-began)
            return
        guest.stop.wait(.2)
    state.update(status='TIMEOUT',seconds=time.monotonic()-began)
    raise TimeoutError('实际欢迎页/桌面首帧没有在预算内完成')
