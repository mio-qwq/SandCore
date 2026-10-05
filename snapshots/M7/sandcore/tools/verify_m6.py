#!/usr/bin/env python3
"""mio：Windows QEMU 双盘验收，HMP 输入与截图，QMP 鼠标分沿注入。"""
import json
import socket
import time
import struct
import hashlib
import subprocess
import shutil
from pathlib import Path
from snap import ppm_to_png

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "m6a"
OUT.mkdir(parents=True, exist_ok=True)


def hmp(command):
    with socket.create_connection(("127.0.0.1", 4444), 3) as s:
        s.settimeout(3)
        data = b""
        while b"(qemu)" not in data:
            data += s.recv(65536)
        s.sendall((command + "\n").encode())
        data = b""
        while b"(qemu)" not in data:
            try:
                block = s.recv(65536)
            except ConnectionResetError:
                if command == "quit": break
                raise
            if not block: break
            data += block
        return data.decode(errors="replace")


def key(name):
    hmp(f"sendkey {name} 40")
    time.sleep(0.09)


def text(value):
    mapping = {" ": "spc", "/": "slash", ".": "dot", "-": "minus", "\n": "ret", "\b": "backspace",
               "*": "shift-8", "+": "shift-equal", "=": "equal", "_": "shift-minus",
               ":": "shift-semicolon", ";": "semicolon", "'": "apostrophe", '"': "shift-apostrophe"}
    for ch in value:
        key(mapping.get(ch, "shift-"+ch.lower() if ch.isupper() else ch))
    time.sleep(0.5)


def qmp(events):
    with socket.create_connection(("127.0.0.1", 4445), 3) as s:
        f = s.makefile("rwb")
        json.loads(f.readline())
        for obj in [{"execute": "qmp_capabilities"}, {"execute": "input-send-event", "arguments": {"events": events}}]:
            f.write(json.dumps(obj).encode()+b"\n"); f.flush()
            while True:
                result = json.loads(f.readline())
                if "error" in result: raise RuntimeError(result)
                if "return" in result: break
    time.sleep(0.12)


def move(dx, dy):
    qmp([{"type": "rel", "data": {"axis": "x", "value": dx}},
         {"type": "rel", "data": {"axis": "y", "value": dy}}])


def button(down):
    qmp([{"type": "btn", "data": {"down": down, "button": "left"}}])


def click():
    button(True); button(False)
    time.sleep(0.5)


def shot(name):
    ppm = OUT / (name+".ppm")
    hmp(f'screendump "{ppm.as_posix()}"')
    png = ppm.with_suffix(".png")
    ppm_to_png(ppm, png)
    ppm.unlink()
    print(png, flush=True)


def memory(address, count):
    target = OUT / "memory.bin"
    hmp(f'pmemsave {address:#x} {count} "{target.as_posix()}"')
    return target.read_bytes()


def symbols():
    out = {}
    for row in (ROOT / "build/kernel.sym").read_text().splitlines():
        cols = row.split()
        if len(cols) == 3: out[cols[2]] = int(cols[0], 16)
    return out


def windows():
    sym = symbols()
    count = struct.unpack("<I", memory(sym["nwins"], 4))[0]
    data = memory(sym["wins"], count*80)
    out = []
    for i in range(count):
        raw = data[i*80:(i+1)*80]
        used, owner, handle, x, y, w, h = struct.unpack_from("<7i", raw)
        canvas, cw, ch = struct.unpack_from("<3I", raw, 40)
        out.append(dict(owner=owner, handle=handle, x=x, y=y, w=w, h=h,
                        digest=hashlib.sha256(memory(canvas, cw*ch)).hexdigest()))
    return out


def point(x, y):
    sym = symbols()
    px = struct.unpack("<i", memory(sym["mx"], 4))[0]
    py = struct.unpack("<i", memory(sym["my"], 4))[0]
    move(x-px, y-py)


def icon(target):
    sym=symbols()
    raw=memory(sym["dicons"],4*3048)
    for i in range(4):
        path=raw[i*3048+16:i*3048+48].split(b"\0",1)[0].decode()
        if path==target:
            point(15,15+i*44); return
    raise AssertionError(f"桌面缺少目标图标：{target}")


def regression():
    results = []
    start_pages=struct.unpack("<I",memory(symbols()["pf_used"],4))[0]
    assert len(windows()) == 1, "hello 退出后应只剩 Shell"
    text("clear\n")
    before = windows()[0]["digest"]
    text("abc\b\b\b")
    assert windows()[0]["digest"] == before, "退格必须完全擦除，提示符不能有残迹"
    for _ in range(4): key("backspace")
    assert windows()[0]["digest"] == before, "空行退格不得删除提示符"
    shot("08-backspace")
    results.append("退格整格擦除、空行提示符守卫：通过")
    for cycle in range(10):
        text("run apps/hello.scx\n")
        both = windows()
        assert len(both) == 2 and both[0]["owner"] != both[1]["owner"]
        assert "CR0=80000011" in hmp("info registers"), "分页必须启用"
        if cycle == 0:
            hello = both[-1]
            point(hello["x"]+25, hello["y"]+5); button(True); move(25,15); button(False)
            assert windows()[-1]["digest"] == hello["digest"], "拖拽不能损坏画布"
            point(15,20); click()
            top = windows()[-1]
            assert top["owner"] == both[0]["owner"] and top["handle"] == both[0]["handle"]
            text("echo owner\n")
            assert len(windows()) == 2, "后台 hello 不得偷走 Shell 的按键"
            # Shell 置顶后再启动同一程序，必须保留原 hello 的任务与画布。
            text("run apps/hello.scx\n")
            assert len(windows()) == 3
            shot("09-two-hello")
            key("ret"); time.sleep(0.25)
            assert len(windows()) == 2
            text("exit\n")
            assert len(windows()) == 1
            key("ret"); time.sleep(0.25)
            assert len(windows()) == 0
            icon("bin/shell.scx"); click(); time.sleep(0.25)
            assert len(windows()) == 1
            results.append("拖拽画布、稳定句柄、输入 owner、同程序两份并发：通过")
        else:
            key("ret"); time.sleep(0.25)
            assert len(windows()) == 1
    shot("10-repeated-launch")
    assert struct.unpack("<I",memory(symbols()["pf_used"],4))[0]==start_pages,"10 轮退出后不得泄漏物理页"
    results.append("连续启动/退出 10 轮，超出任务表容量仍可复用：通过")
    (OUT/"results.json").write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding="utf-8")
    print("PASS", results, flush=True)


def all_tests():
    global OUT
    current=(ROOT/'build/sanddata.img').read_bytes()
    v4=struct.unpack_from('<I',current,20)[0]==4
    if v4: OUT=ROOT/'build/m6a-m7'; OUT.mkdir(parents=True,exist_ok=True)
    data = OUT/"sanddata-test.img"
    shutil.copy2(ROOT/"build/sanddata.img", data)
    command = [r"C:\Program Files\qemu\qemu-system-i386.exe",
               "-drive", "format=raw,if=floppy,file=build/sandcore.img",
               "-drive", f"format=raw,if=ide,file={data.as_posix()}",
               "-display", "none", "-monitor", "tcp:127.0.0.1:4444,server,nowait",
               "-qmp", "tcp:127.0.0.1:4445,server,nowait", "-no-reboot"]
    (OUT/"qemu-command.json").write_text(json.dumps(command,indent=2),encoding="utf-8")
    proc = subprocess.Popen(command, cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        assert proc.poll() is None, "QEMU 启动失败（检查监视器端口是否占用）"
        shot("01-boot"); key("ret"); time.sleep(1); shot("02-desktop")
        sym = symbols()
        assert struct.unpack("<I",memory(sym["zh16_active_n"],4))[0] == (36 if v4 else 27), "必须实际加载盘上的字体"
        assert not windows(), "开机不应自动弹 Shell"
        icon("bin/shell.scx"); click(); shot("03-shell")
        text("help\n"); shot("04-help")
        text("echo mio\n"); text("ls\n"); shot("05-files")
        text("run apps/hello.scx\n"); shot("06-concurrent")
        key("ret"); time.sleep(0.25); shot("07-exit-reclaimed")
        regression()
        w = windows()[-1]
        point(w["x"]+w["w"]-7,w["y"]+7); click()
        assert not windows(), "红叉应终止 GUI 任务并回收窗口"
        icon("bin/shell.scx"); click()
        text("run apps/panic.scx\n"); shot("11-panic")
        if v4:
            from verify_m7_base import task_states
            assert task_states().count(3)==1,'M7 三环除零应暂停，而非内核红屏'
            point(284,47);click();assert len(windows())==1
        else:
            assert memory(0xA0000+100*320+100,1)==bytes([40]),'M6 历史除零红屏'

        checks = json.loads((OUT/"results.json").read_text(encoding="utf-8"))
        checks += ["双盘启动、磁盘字体、无自动 Shell、文件图标：通过",
                   "红叉回收与真实 ring3 除零（M7 暂停卡片/M6 历史红屏）：通过"]
        (OUT/"results.json").write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding="utf-8")
    finally:
        if proc.poll() is None:
            hmp("quit"); proc.wait(timeout=5)


if __name__ == "__main__":
    import sys
    if sys.argv[1] == "boot":
        shot("01-boot"); key("ret"); time.sleep(1); shot("02-desktop")
    elif sys.argv[1] == "shell":
        move(-145, -85); click(); shot("03-shell")
        text("help\n"); shot("04-help")
        text("echo mio\n"); text("ls\n"); shot("05-files")
        text("run apps/hello.scx\n"); shot("06-concurrent")
    elif sys.argv[1] == "regression":
        regression()
    elif sys.argv[1] == "all":
        all_tests()
