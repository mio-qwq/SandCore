#!/usr/bin/env python3
"""mio：M8 真彩色与私有图形堆的实际 CPU/显存验收。

启动本脚本自己的 Windows QEMU，以软盘引导并挂 IDE 副本盘。应用由
开始菜单中的 Shell、HMP sendkey 命令真实启动；不注入函数调用、不改
页表或任务状态。pmemsave 只读结果、实际画布和显存，截图留作证据。
32MB 机器特意制造部分分配后的 OOM；资源计数必须回到原值。
"""
import json
import re
import hashlib
import zipfile
from contextlib import contextmanager
import shutil
import socket
import struct
import subprocess
import time
from pathlib import Path
from PIL import Image
import verify_m6 as q

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/m8-truecolor'
OUT.mkdir(parents=True, exist_ok=True)
q.OUT = OUT
PROBE_ADDRESS = None

def word(address):
    return struct.unpack('<I', q.memory(address, 4))[0]

def point(x, y):
    # 真彩色全屏合成比旧索引模式更重。每个 PS/2 包给主循环完整
    # 观察周期，避免多包积压后用旧坐标反向纠偏，造成来回抖动。
    sym = q.symbols()
    for _ in range(60):
        px, py = word(sym['mx']), word(sym['my'])
        if (px, py) == (x, y):
            return
        q.move(max(-100, min(100, x-px)), max(-100, min(100, y-py)))
        time.sleep(.3)
    raise AssertionError(('mouse not settled', x, y, px, py))

def click():
    # 两个真实边沿仅间隔QMP驱动的约120ms，不能靠长按一秒掩盖丢点击。
    q.button(True)
    q.button(False)
    time.sleep(.3)

def table_symbols(path):
    result = {}
    for row in path.read_text().splitlines():
        columns = row.split()
        if len(columns) == 3:
            result[columns[2]] = int(columns[0], 16)
    return result

def windows():
    # win_t 的稳定 80B 布局不含格式；这里不沿用旧脚本的 1B 像素摘要。
    sym = q.symbols()
    count = word(sym['nwins'])
    raw = q.memory(sym['wins'], count * 80)
    result = []
    for index in range(count):
        owner, handle, x, y, w, h = struct.unpack_from('<6i', raw, index*80+4)
        canvas, cw, ch = struct.unpack_from('<3I', raw, index*80+40)
        result.append(dict(owner=owner, handle=handle, x=x, y=y, w=w, h=h,
                           canvas=canvas, cw=cw, ch=ch))
    return result

def physical(pd, address):
    # 用户堆虚址从 1GB 起，绝不能把该虚址直接交给 pmemsave 当物理地址。
    pde = word(pd + (address >> 22)*4)
    assert pde & 5 == 5, ('private PDE missing', hex(address), hex(pde))
    pte = word((pde & ~4095) + ((address >> 12) & 1023)*4)
    assert pte & 5 == 5, ('private PTE missing', hex(address), hex(pte))
    return (pte & ~4095) + (address & 4095)

def probe_result(win):
    pd = word(q.symbols()['tasks'] + win['owner']*168)
    address = PROBE_ADDRESS or table_symbols(ROOT/'build/user-rgbprobe.sym')['rgb_probe']
    return pd, list(struct.unpack('<24I', q.memory(physical(pd, address), 96)))

@contextmanager
def stable_frame():
    # 后台在下一帧绘制时会暂时只有壁纸。要比较后台与前台必须取得
    # 已结束合成的CPU只读快照；不能把正在覆盖的后台当成混合错误。
    # stop/cont只暂停观测，不写任何客体内存、寄存器、状态或函数。
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        q.hmp('stop')
        if word(q.symbols()['render_hold']) == 0:
            break
        q.hmp('cont')
        time.sleep(.03)
    else:
        raise AssertionError('无法取得合成结束后的只读快照')
    try:
        yield
    finally:
        q.hmp('cont')

def wait_probe(stage):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        wins = windows()
        if len(wins) >= 2:
            pd, result = probe_result(wins[-1])
            if result[0] == stage:
                assert result[1] == 0, ('probe failed', result)
                return wins[-1], pd, result
        faults = q.memory(q.symbols()['fault_cards'], 8*24)
        assert not any(struct.unpack_from('<I', faults, index*24)[0] for index in range(8)), 'ring3 fault'
        time.sleep(.2)
    q.shot('timeout')
    raise AssertionError('真实程序未在45秒内完成验收；保留 timeout 截图')

def launch(vga, ram, tag='', prepare=None, reuse=False):
    for port in (4444, 4445):
        try:
            connection = socket.create_connection(('127.0.0.1', port), .2)
        except OSError:
            continue
        connection.close()
        raise RuntimeError(f'{port} 已占用；不操作他人的虚拟机')
    suffix = '-'+tag if tag else ''
    disk = OUT / f'sanddata-{vga}-{ram}{suffix}.img'
    if reuse:
        assert disk.is_file(), '冷启动必须继续使用刚刚保存的同一测试盘'
    else:
        shutil.copy2(ROOT/'build/sanddata.img', disk)
        if prepare:
            prepare(disk)
    command = [r'C:\Program Files\qemu\qemu-system-i386.exe',
               '-drive', 'format=raw,if=floppy,file=build/sandcore.img',
               '-drive', f'format=raw,if=ide,file={disk.as_posix()}',
               '-m', str(ram), '-vga', vga, '-display', 'none',
               '-monitor', 'tcp:127.0.0.1:4444,server,nowait',
               '-qmp', 'tcp:127.0.0.1:4445,server,nowait', '-no-reboot']
    (OUT/f'qemu-{vga}-{ram}{suffix}.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    proc = subprocess.Popen(command, cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW)
    time.sleep(2)
    assert proc.poll() is None, 'QEMU 启动失败'
    return proc

def open_shell(native):
    q.key('ret')
    time.sleep(1)
    assert not windows(), '开机应只有桌面'
    point(30, 750) if native else point(16, 190)
    click()
    deadline = time.monotonic()+10
    while not word(q.symbols()['menu_open']) and time.monotonic()<deadline:
        time.sleep(.1)
    assert word(q.symbols()['menu_open']), '真实点击应展开开始菜单'
    time.sleep(.5)
    q.shot('menu-'+('std' if native else 'cirrus'))
    for _ in range(7):
        q.key('down')
    q.key('ret')
    deadline = time.monotonic()+10
    while not windows() and time.monotonic()<deadline:
        time.sleep(.1)
    assert len(windows()) == 1, '开始菜单必须实际启动 Shell'

def check_pixels(win, pd, result, native):
    assert result[6] == result[8], ('alloc/free did not converge', result)
    assert 0x40000000 <= result[7] < 0x44000000
    assert 0x40000000 <= result[9] < 0x44000000
    assert result[10] == win['cw']*win['ch']*4
    # 应用像素 -> 内核独占画布 -> 合成后台 -> PCI 显存，逐层读实值。
    # 只看 PNG 颜色数不能排除把一幅宿主图片误当成系统输出。
    palette = struct.unpack('<256I', q.memory(q.symbols()['colors'], 1024))
    cyan = int(re.search(r'#define\s+PAL_UI_CYAN\s+(\d+)', (ROOT/'kernel/palette.h').read_text(encoding='utf-8')).group(1))
    blue = palette[cyan+7] & 255
    for x in (win['cw']//3, win['cw']-48):
        y = min(150, win['ch']-50)
        offset = (y*win['cw']+x)*4
        actual = word(physical(pd, result[9]+offset))
        expected = ((x*255//(win['cw']-1)) << 16) | (((y-96)*255//(win['ch']-140)) << 8) | blue
        alpha = 128 if x >= win['cw']-96 else 255
        assert actual == (alpha << 24) | expected, ('user ARGB', hex(actual), hex(expected))
        assert word(win['canvas']+offset) == actual, 'FRAME32 必须完整复制用户 ARGB'
        if native:
            background = result[11]  # 窗口语义表面RGB，不再把旧DAC青槽当新主题
            if alpha == 128:
                mixed = 0
                for shift in (0, 8, 16):
                    value = (((expected >> shift) & 255)*alpha + ((background >> shift) & 255)*(255-alpha)+127)//255
                    mixed |= value << shift
                expected = mixed
            position = ((win['y']+24+y)*1024+win['x']+1+x)*4
            assert word(word(q.symbols()['native_bb'])+position) == expected, '后台 RGB/透明混合不符'
            assert word(word(q.symbols()['framebuffer'])+position) == expected, 'PCI 显存不符'

def run_case(vga, ram, oom=False):
    proc = launch(vga, ram)
    native = vga == 'std'
    checks = []
    try:
        open_shell(native)
        baseline = word(q.symbols()['pf_used'])
        for cycle in range(1 if oom or not native else 3):
            arguments = ' oom' if oom else ' abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ' if cycle==0 else ''
            q.text('run apps/rgbprobe.scx' + arguments + '\n')
            win, pd, result = wait_probe(2 if oom else 1)
            assert result[6] == result[8], '资源计数必须收敛'
            if not oom:
                time.sleep(1.5)
                with stable_frame():
                    check_pixels(win, pd, result, native)
            else:
                assert result[7] == 0, '32MB 机器必须实际走物理页不足回滚'
                # 完成变量只代表提交完成，仍等待真实显存呈现，避免
                # 把刚开窗的黑画布保存成“OOM提示已经显示”的证据。
                position=((win['y']+24+10)*1024+win['x']+11)*4
                deadline=time.monotonic()+10
                while word(word(q.symbols()['framebuffer'])+position)!=0x00FBFCFE and time.monotonic()<deadline:
                    time.sleep(.1)
                assert word(word(q.symbols()['framebuffer'])+position)==0x00FBFCFE
                time.sleep(.5)
            name = f'{vga}-{ram}-' + ('oom' if oom else f'rgb-{cycle+1}')
            q.shot(name)
            with Image.open(OUT/(name+'.png')) as pic:
                assert pic.size == (1024,768) if native else pic.size in ((320,200),(640,400)), pic.size
                assert (word(q.symbols()['gfx_width']),word(q.symbols()['gfx_height'])) == ((1024,768) if native else (320,200))
                if native and not oom:
                    region = pic.convert('RGB').crop((win['x']+2, win['y']+24+100,
                                                     win['x']+win['cw']-100, win['y']+24+win['ch']-48))
                    colors = len(set(region.get_flattened_data()))
                    assert colors > 10000, ('画面似乎被量化到调色板', colors)
                    checks.append(f'{name}: 实际截图连续RGB区 {colors} 色，用户帧/内核画布/后台/PCI显存逐层一致')
            q.key('esc')
            deadline = time.monotonic()+5
            while time.monotonic() < deadline and (len(windows()) != 1 or word(q.symbols()['pf_used']) != baseline):
                time.sleep(.1)
            assert len(windows()) == 1 and word(q.symbols()['pf_used']) == baseline, '退出遗留窗口/图形堆/页表'
        checks.append(f'{vga}/{ram}MB: 高端用户堆清零、跨页表读写、非法释放/重复释放/非法帧拒绝、全部退出回收通过' if not oom
                      else 'std/32MB: 32MB部分分配失败后像素页和新页表完整回滚，退出回收通过')
        assert word(q.symbols()['event_overflow']) == 0, '验收期间输入队列不得溢出'
        assert word(q.symbols()['keyboard_overflow']) == 0, '验收期间键盘队列不得丢字'
        checks.append(f'{vga}/{ram}MB: 开始菜单约120ms真实点击/方向键启动通过，鼠标事件队列零溢出')
        (OUT/f'results-{vga}-{ram}.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
        return checks
    except Exception:
        if proc.poll() is None:
            q.shot(f'failure-{vga}-{ram}')
        raise
    finally:
        if proc.poll() is None:
            q.hmp('quit')
            proc.wait(timeout=10)

def main():
    checks = run_case('std', 128)
    checks += run_case('cirrus', 128)
    checks += run_case('std', 32, oom=True)
    (OUT/'results.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS', checks, flush=True)

def native_case():
    global PROBE_ADDRESS
    import verify_s3c as compiler
    # 复用原生编译协议，但输出目录不能覆盖已验收的M7历史证据。
    q.OUT=compiler.OUT=OUT
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        generation2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(generation2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    proc=launch('std',128,'native',lambda disk: compiler.disk_put(disk,'BIN/G2.SCX',generation2))
    disk=OUT/'sanddata-std-128-native.img'
    try:
        open_shell(True)
        blob=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/rgbprobe.c','HOME/RGBP.SCX',120)
        raw_map=compiler.await_file(disk,'HOME/RGBP.SCX.map')
        entries={}
        for line in raw_map.decode().splitlines()[1:]:
            parts=line.split()
            if len(parts)>=3 and parts[2]=='OBJECT':
                entries[parts[1]]=int(parts[0],16)
        PROBE_ADDRESS=entries['rgb_probe']
        (OUT/'rgbprobe-native.scx').write_bytes(blob)
        (OUT/'rgbprobe-native.scx.map').write_bytes(raw_map)
        # 编译窗口关闭与僵尸页表回收不是同一个瞬间。基线必须在只剩
        # Shell的运行态/暂停态之后读取，不能把待回收的G2页算进去。
        deadline=time.monotonic()+10
        while compiler.task_states()[1:].count(1)!=1 or 2 in compiler.task_states()[1:]:
            assert time.monotonic()<deadline, ('compiler not reclaimed',compiler.task_states())
            time.sleep(.2)
        baseline=word(q.symbols()['pf_used'])
        q.text('run HOME/RGBP.SCX\n')
        win,pd,result=wait_probe(1)
        time.sleep(1.5)
        with stable_frame():
            check_pixels(win,pd,result,True)
        q.shot('native-g2-rgb-heap')
        q.key('esc')
        deadline=time.monotonic()+10
        while (len(windows())!=1 or word(q.symbols()['pf_used'])!=baseline) and time.monotonic()<deadline:
            time.sleep(.1)
        final_windows=windows();final_pages=word(q.symbols()['pf_used'])
        if len(final_windows)!=1 or final_pages!=baseline:
            diagnostic=dict(baseline=baseline,final_pages=final_pages,
                            windows=final_windows,states=compiler.task_states())
            (OUT/'native-exit-failure.json').write_text(json.dumps(diagnostic,indent=2),encoding='utf-8')
            q.shot('native-exit-failure')
            raise AssertionError(('native exit/reclaim',diagnostic))
        report=dict(author='mio',status='PASS',
                    compiler='已验收M7包中的实际G2；本轮没有宣称新版编译器三代重新收敛',
                    compiler_sha256=hashlib.sha256(generation2).hexdigest(),
                    source_sha256=hashlib.sha256((ROOT/'user/rgbprobe.c').read_bytes()).hexdigest(),
                    api_sha256=hashlib.sha256((ROOT/'user/SCAPI.H').read_bytes()).hexdigest(),
                    scx_sha256=hashlib.sha256(blob).hexdigest(),
                    checks=['系统内G2编译当前SCAPI.H/RGB探针','真实生成SCX运行',
                            '高端堆/格式/owner/地址断言全部通过','RGB/alpha/PCI显存逐层一致','退出资源归零'])
        (OUT/'native-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print('NATIVE PASS',report,flush=True)
    finally:
        PROBE_ADDRESS=None
        if proc.poll() is None:
            q.hmp('quit');proc.wait(timeout=10)

if __name__ == '__main__':
    import sys
    native_case() if '--native' in sys.argv else main()
