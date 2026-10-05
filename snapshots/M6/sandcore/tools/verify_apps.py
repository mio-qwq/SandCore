#!/usr/bin/env python3
"""mio：M6 应用验收。验证沙核内汇编、编辑落盘、目录导航、计算与游戏。

所有程序通过真实桌面/键盘路径启动，不从宿主直接修改来伪造运行结果。
QEMU 使用数据盘副本，测试写入不会覆盖发布盘或用户实际编辑的文件。
pmemsave 检查用户态变量时先走任务私有页表，不能把虚拟地址当物理地址。
"""
import json
import struct
import subprocess
import shutil
import time
from pathlib import Path
import verify_m6 as vm

ROOT=vm.ROOT
OUT=ROOT/"build/m6"
OUT.mkdir(parents=True,exist_ok=True)
vm.OUT=OUT


def read_file(path):
    raw=(OUT/"sanddata-test.img").read_bytes()
    count=struct.unpack_from("<I",raw,12)[0]
    for i in range(count):
        start=512+i*40
        name=raw[start:start+32].split(b"\0",1)[0].decode()
        if name==path:
            lba,size=struct.unpack_from("<2I",raw,start+32)
            return raw[lba*512:lba*512+size]
    raise AssertionError("文件未落盘："+path)


def app_memory(app,name,size=4):
    win=vm.windows()[-1]
    symbols={}
    for row in (ROOT/f"build/user-{app}.sym").read_text().splitlines():
        cols=row.split()
        if len(cols)==3: symbols[cols[2]]=int(cols[0],16)
    address=symbols[name]
    # task_t=40B 原始字段 + 128B 参数，pd 位于任务槽开头。
    pd=struct.unpack("<I",vm.memory(vm.symbols()["tasks"]+win["owner"]*168,4))[0]
    pt=struct.unpack("<I",vm.memory(pd+4,4))[0]&~4095
    output=bytearray()
    while size:
        page=(address-0x400000)//4096
        physical=struct.unpack("<I",vm.memory(pt+page*4,4))[0]&~4095
        take=min(size,4096-address%4096)
        output+=vm.memory(physical+address%4096,take)
        address+=take; size-=take
    return bytes(output)


def launch(command):
    vm.text(command+"\n")
    time.sleep(0.3)
    assert len(vm.windows())>=2,"应用未创建窗口："+command


def close():
    vm.key("esc"); time.sleep(0.2)
    assert len(vm.windows())==1,"应用退出后必须只剩 Shell"


def select_cell(target):
    current=struct.unpack("<i",app_memory("mines","selected"))[0]
    while current//8<target//8: vm.key("down"); current+=8
    while current//8>target//8: vm.key("up"); current-=8
    while current%8<target%8: vm.key("right"); current+=1
    while current%8>target%8: vm.key("left"); current-=1


def verify():
    results=[]
    vm.shot("01-boot"); vm.key("ret"); time.sleep(1); vm.shot("02-desktop")
    vm.icon("bin/shell.scx"); vm.click(); vm.shot("03-shell")
    launch("asm home/demo.asm apps/demo.scx")
    vm.shot("04-native-assembler")
    assert struct.unpack("<i",app_memory("assembler","error"))[0]==0
    scx=read_file("apps/demo.scx")
    assert scx[:8]==b"SCX1MIO\0" and len(scx)==36+struct.unpack_from("<I",scx,12)[0]
    close(); launch("run apps/demo.scx"); vm.shot("05-generated-program")
    assert b"Built in SandCore" in scx and b"hello from SandAsm" in scx
    w=vm.windows()[-1]; vm.point(w["x"]+w["w"]-7,w["y"]+7); vm.click()
    assert len(vm.windows())==1
    results.append("ring3 两遍汇编器读取源文件、生成 SCX、实际运行新程序：通过")
    launch("run apps/note.scx home/check.txt")
    vm.text("mio native note\nline two")
    vm.key("f2"); time.sleep(0.3); vm.shot("06-note-saved")
    assert read_file("home/check.txt")==b"mio native note\nline two"
    close(); launch("run apps/note.scx home/check.txt")
    assert app_memory("note","text",24).split(b"\0",1)[0]==b"mio native note\nline two"
    vm.text("X"); vm.key("f3")
    assert app_memory("note","text",24).split(b"\0",1)[0]==b"mio native note\nline two"
    assert struct.unpack("<i",app_memory("note","changed"))[0]==0
    vm.shot("07-note-reloaded")
    close(); results.append("Notes 输入、F2 精确落盘、退出后重读、F3 舍弃未保存修改：通过")
    launch("run apps/note.scx home/bad.asm")
    vm.text("nonsense\n"); vm.key("f2"); time.sleep(0.2); close()
    launch("asm home/bad.asm apps/bad.scx")
    assert struct.unpack("<i",app_memory("assembler","error"))[0]==1
    assert struct.unpack("<i",app_memory("assembler","error_line"))[0]==1
    vm.shot("07b-assembler-error")
    try:
        read_file("apps/bad.scx")
        raise AssertionError("失败的汇编不得写出新 SCX")
    except AssertionError as exc:
        assert "文件未落盘" in str(exc)
    close(); results.append("汇编器非法指令报第 1 行，失败不写输出：通过")
    # 名字超长后寄存器解析仍可能继续，必须保持暂存串 NUL 终止；
    # 用 Notes 真正写源文件，再经 Shell 汇编，不能从宿主直接伪造错误标志。
    launch("run apps/note.scx home/long.asm")
    vm.text("mov "+"x"*32+", 1\n"); vm.key("f2"); time.sleep(0.2); close()
    launch("asm home/long.asm apps/long.scx")
    assert struct.unpack("<i",app_memory("assembler","error"))[0]==1
    assert struct.unpack("<i",app_memory("assembler","error_line"))[0]==1
    vm.shot("07c-assembler-long-symbol")
    try:
        read_file("apps/long.scx")
        raise AssertionError("超长符号不得写出 SCX")
    except AssertionError as exc:
        assert "文件未落盘" in str(exc)
    close()
    # _start 定义在载荷末尾虽然语法成立，却没有可执行入口；汇编器要
    # 在 FSWRITE 之前拒绝，不能显示成功后把坏文件留给加载器才发现。
    launch("run apps/note.scx home/entry.asm")
    vm.text("nop\n_start:\n"); vm.key("f2"); time.sleep(0.2); close()
    launch("asm home/entry.asm apps/entry.scx")
    assert struct.unpack("<i",app_memory("assembler","error"))[0]==1
    vm.shot("07d-assembler-invalid-entry")
    try:
        read_file("apps/entry.scx")
        raise AssertionError("载荷末尾入口不得写出 SCX")
    except AssertionError as exc:
        assert "文件未落盘" in str(exc)
    close(); results.append("汇编器超长符号安全拒绝、载荷末尾入口拒绝、两者均不写输出：通过")
    launch("run apps/files.scx")
    vm.key("4"); vm.shot("08-files-home")
    assert struct.unpack("<i",app_memory("files","directory"))[0]==4
    assert struct.unpack("<i",app_memory("files","count"))[0]>=3
    vm.key("ret"); time.sleep(0.3); vm.shot("09-files-opens-note")
    assert len(vm.windows())==3
    vm.key("esc"); time.sleep(0.2); close()
    results.append("Files 导航 home/、读取新增文件、Enter 打开 Notes：通过")
    launch("run apps/calc.scx")
    vm.text("12*7\n"); vm.shot("10-calculator")
    assert app_memory("calc","answer",32).split(b"\0",1)[0]==b"84"
    vm.key("c"); vm.text("7/0\n"); vm.shot("11-calculator-zero")
    assert app_memory("calc","answer",32).split(b"\0",1)[0]==b"division by zero"
    vm.key("c"); vm.text("2147483647+1\n")
    assert app_memory("calc","answer",32).split(b"\0",1)[0]==b"overflow"
    vm.shot("11b-calculator-overflow")
    vm.key("c"); vm.text("-2147483648/-1\n")
    assert app_memory("calc","answer",32).split(b"\0",1)[0]==b"overflow"
    vm.shot("11c-calculator-idiv-guard")
    close(); results.append("Calculator 整数乘法、除零、i32 加法溢出与 INT_MIN/-1 防异常：通过")
    launch("run apps/mines.scx")
    vm.key("spc"); vm.shot("12-mines-first-safe")
    assert struct.unpack("<i",app_memory("mines","finished"))[0]!=1
    vm.key("right"); assert struct.unpack("<i",app_memory("mines","selected"))[0]==1
    vm.key("r"); vm.shot("13-mines-reset")
    assert struct.unpack("<i",app_memory("mines","started"))[0]==0
    vm.key("f"); vm.key("spc")
    assert app_memory("mines","flag",48)[0]==1
    assert struct.unpack("<i",app_memory("mines","started"))[0]==0
    vm.shot("13a-mines-flag")
    vm.key("f"); assert app_memory("mines","flag",48)[0]==0
    w=vm.windows()[-1]
    vm.point(w["x"]+1+8+2*20+5,w["y"]+13+34+5); vm.click()
    assert struct.unpack("<i",app_memory("mines","selected"))[0]==2
    assert struct.unpack("<i",app_memory("mines","started"))[0]==1
    bombs=app_memory("mines","bomb",48)
    assert sum(bombs)==8
    for cell in range(48):
        if not bombs[cell]: select_cell(cell); vm.key("spc")
    assert struct.unpack("<i",app_memory("mines","finished"))[0]==2
    assert struct.unpack("<i",app_memory("mines","revealed"))[0]==40
    vm.shot("13b-mines-clear")
    vm.key("r"); vm.key("spc")
    bombs=app_memory("mines","bomb",48); mine=bombs.index(1)
    select_cell(mine); vm.key("spc")
    assert struct.unpack("<i",app_memory("mines","finished"))[0]==1
    vm.shot("13c-mines-boom")
    close(); results.append("Mines 首格安全、旗标阻止揭格、鼠标、方向键、重开、40 格胜利与踩雷失败：通过")
    launch("run apps/palette.scx")
    vm.key("2"); vm.shot("14-palette-night")
    assert read_file("sys/wall.cfg")==b"1"
    close(); vm.shot("15-wallpaper-night")
    results.append("Palette 展示正式槽位、夜色壁纸设置落盘：通过")
    vm.hmp("system_reset"); time.sleep(1); vm.key("ret"); time.sleep(1)
    assert struct.unpack("<i",vm.memory(vm.symbols()["wallpaper_mode"],4))[0]==1
    assert not vm.windows()
    vm.shot("16-wallpaper-after-reboot")
    results.append("重新启动 QEMU 客户机，sys/wall.cfg 恢复夜色且没有自动窗口：通过")
    (OUT/"results.json").write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding="utf-8")
    print("PASS",flush=True)


if __name__=="__main__":
    shutil.copy2(ROOT/"build/sanddata.img",OUT/"sanddata-test.img")
    command=[r"C:\Program Files\qemu\qemu-system-i386.exe",
             "-drive","format=raw,if=floppy,file=build/sandcore.img",
             "-drive",f"format=raw,if=ide,file={(OUT/'sanddata-test.img').as_posix()}",
             "-display","none","-monitor","tcp:127.0.0.1:4444,server,nowait",
             "-qmp","tcp:127.0.0.1:4445,server,nowait"]
    (OUT/"qemu-command.json").write_text(json.dumps(command,indent=2),encoding="utf-8")
    proc=subprocess.Popen(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1); assert proc.poll() is None
        verify()
    finally:
        if proc.poll() is None: vm.hmp("quit"); proc.wait(timeout=5)
