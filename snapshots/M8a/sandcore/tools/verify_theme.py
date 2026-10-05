#!/usr/bin/env python3
"""mio：M8命名主题/ARGB控件的Windows双盘QEMU验证。

测试盘在启动前注入已验收M7的G2，实际系统内编译当前主题探针及设置。
HMP输入启动程序，QMP约120ms点击切主题，最后真重启读回配置。读取
用户变量、页表与截图只诊断结果，不注入内核函数或修改客体状态。
"""
import hashlib
import json
import struct
import time
import zipfile
from pathlib import Path
from PIL import Image
import verify_truecolor as t
import verify_s3c as compiler

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-theme'
OUT.mkdir(parents=True,exist_ok=True)
t.OUT=t.q.OUT=compiler.OUT=OUT
q=t.q


def wait(condition,description,seconds=40):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=condition()
        if value:return value
        faults=q.memory(q.symbols()['fault_cards'],8*24)
        assert not any(struct.unpack_from('<I',faults,i*24)[0] for i in range(1,8)), description+'：三环异常'
        time.sleep(.2)
    q.shot('timeout')
    raise AssertionError(description)


def user_word(window,address):
    pd=t.word(q.symbols()['tasks']+window['owner']*168)
    return t.word(t.physical(pd,address))


def window():
    wins=t.windows()
    return wins[-1] if len(wins)==2 else None


def native_symbols(data):
    return {p[1]:int(p[0],16) for row in data.decode().splitlines()[1:]
            if len(p:=row.split())>=3 and p[2]=='OBJECT'}


def collect_probe(address,stage):
    w=wait(window,'探针窗口应真实创建')
    wait(lambda:user_word(w,address)==stage,'探针应完成真实系统调用')
    pd=t.word(q.symbols()['tasks']+w['owner']*168)
    values=struct.unpack('<40I',q.memory(t.physical(pd,address),160))
    assert values[1]==0, ('主题探针失败',values[:8])
    time.sleep(1)
    return values


def reclaim(baseline):
    q.key('esc')
    # 切主题改变照片/图标常驻页数，不是应用泄漏。保存起始缓存页数，
    # 结束按真实当前缓存做差；其余所有窗口/用户页/页表仍须严格归零。
    wait(lambda:len(t.windows())==1 and t.word(q.symbols()['pf_used'])==baseline[0]
         +t.word(q.symbols()['desktop_pages'])-baseline[1],
         '关闭应回收RGB窗口、私有堆和页表',15)


def main():
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'theme',prepare)
    disk=OUT/'sanddata-std-128-theme.img'
    checks=[]
    artifacts={}
    try:
        t.open_shell(True)
        probe=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/themeprobe.c','HOME/TH.SCX',120)
        probe_map=compiler.await_file(disk,'HOME/TH.SCX.map')
        addresses=native_symbols(probe_map)
        (OUT/'themeprobe-native.scx').write_bytes(probe)
        (OUT/'themeprobe-native.scx.map').write_bytes(probe_map)
        wait(lambda:compiler.task_states()[1:].count(1)==1 and 2 not in compiler.task_states()[1:],
             '编译器僵尸必须先安全回收')
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/TH.SCX\n')
        values=collect_probe(addresses['theme_probe'],1)
        assert values[9]==1 and values[8+8+3]==0, '应为Classic且正文黑色'
        q.shot('01-native-theme-protocol')
        reclaim(baseline)
        checks.append('已验收G2实际编译主题探针；非法地址/容量/语法/重复/颜色/UTF8/NUL/路径整体拒绝，失败快照不变')
        settings=compiler.compile_native(disk,'BIN/G2.SCX','SYS/SRC/settings.c','HOME/SET.SCX',180)
        settings_map=compiler.await_file(disk,'HOME/SET.SCX.map')
        settings_symbols=native_symbols(settings_map)
        (OUT/'settings-native.scx').write_bytes(settings)
        (OUT/'settings-native.scx.map').write_bytes(settings_map)
        wait(lambda:compiler.task_states()[1:].count(1)==1 and 2 not in compiler.task_states()[1:],'设置编译器应回收')
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/SET.SCX\n')
        w=wait(window,'原生设置程序应启动')
        wait(lambda:user_word(w,settings_symbols['ui_frames'])>0,'ARGB控件应真实提交')
        assert 0x40000000<=user_word(w,settings_symbols['ui_pixels'])<0x44000000
        def tap(x,y):
            # 基于WININFO实际坐标映射客户区，保持真实鼠标事件，非调用按钮函数。
            t.point(w['x']+1+x,w['y']+24+y);t.click()
        tap(160,85)
        wait(lambda:user_word(w,settings_symbols['tab'])==1,'鼠标应切到Desktop页面')
        time.sleep(1.5);q.shot('02-native-classic-settings')
        tap(112,175)
        wait(lambda:user_word(w,settings_symbols['ui_classic'])==0,'Aurora按钮应实际保存/应用')
        time.sleep(1.5);q.shot('03-native-aurora-settings')
        with Image.open(OUT/'03-native-aurora-settings.png') as pic:
            assert pic.size==(1024,768)
            assert pic.convert('RGB').getpixel((w['x']+1+500,w['y']+24+400)) in ((255,255,255),(251,252,254))
        t.point(30,750);t.click()
        wait(lambda:t.word(q.symbols()['menu_open'])==1,'Aurora开始菜单应展开')
        time.sleep(.8);q.shot('04-aurora-menu')
        q.key('esc');wait(lambda:t.word(q.symbols()['menu_open'])==0,'Esc收起菜单')
        tap(280,175)
        wait(lambda:user_word(w,settings_symbols['ui_classic'])==1,'Classic按钮应实际保存/应用')
        time.sleep(1);q.shot('05-classic-restored')
        reclaim(baseline)
        checks.append('G2实际编译当前NUI/Settings，ARGB后台使用高端私有堆；真实鼠标切页面/切两主题、开始菜单和退出回收通过')
        # -no-reboot用于让意外CPU重启显式失败；正常持久化验收采用
        # 退出本VM再以同一副本盘冷启动，不把QEMU重启策略当内核故障。
        assert proc.poll() is None
        q.hmp('quit');proc.wait(timeout=10)
        proc=t.launch('std',128,'theme',reuse=True)
        t.open_shell(True)
        baseline=(t.word(q.symbols()['pf_used']),t.word(q.symbols()['desktop_pages']))
        q.text('run HOME/TH.SCX inspect\n')
        values=collect_probe(addresses['theme_probe'],2)
        assert values[9]==1 and values[11]==24 and values[12]==32
        q.shot('06-reboot-classic-persisted')
        reclaim(baseline)
        checks.append('退出本测试VM后用同一副本盘真实冷启动，重读SandFS主题配置，Classic样式/布局恢复，三环查询通过')
        assert t.word(q.symbols()['event_overflow'])==t.word(q.symbols()['keyboard_overflow'])==0
        artifacts={name:hashlib.sha256(data).hexdigest() for name,data in
                   [('G2',g2),('themeprobe',probe),('settings',settings)]}
        for relative in ('kernel/theme.c','kernel/wm_native.inc','user/SCAPI.H','user/NUI.inc','user/settings.c','user/themeprobe.c','build/sandcore.img'):
            artifacts[relative]=hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()
        report=dict(author='mio',status='PASS',checks=checks,sha256=artifacts,
                    native='M7已验收G2运行于本轮M8内核，实际生成并运行本轮探针与设置；不是新版编译器三代发布结论')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('THEME PASS',report,flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
